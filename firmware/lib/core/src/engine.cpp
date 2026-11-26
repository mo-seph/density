#include "engine.h"

namespace density {

namespace {
inline float absf(float x) { return x < 0 ? -x : x; }
inline uint8_t round7(float pos) {
    float v = pos * 127.0f + 0.5f;
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    return uint8_t(v);
}
inline bool matches(const MidiAddress& a, const MidiMsg& m) {
    return a.type != MidiType::None && a.type == m.type && a.channel == m.channel && a.number == m.number;
}
}  // namespace

Engine::Engine(Hal& hal, const Config& cfg) : hal_(hal), cfg_(cfg) {
    const HardwareInfo& hw = hal_.info();
    nAnalog_ = hw.numAnalog() < kMaxAnalog ? hw.numAnalog() : kMaxAnalog;
    nPads_ = hw.numPads < kMaxPads ? hw.numPads : kMaxPads;
    nBanks_ = cfg_.numBanks < 1 ? 1 : (cfg_.numBanks > kMaxBanks ? kMaxBanks : cfg_.numBanks);
    tol_ = cfg_.pickupTolerance >= kMinPickupTolerance ? cfg_.pickupTolerance : kMinPickupTolerance;
    for (AnalogId a = 0; a < nAnalog_; ++a) {
        const FilterParams& fp = cfg_.filter[uint8_t(hw.kindOf(a))];
        filter_[a].configure(fp, cfg_.cal[a], cfg_.analog[a][0].invert);
        pickedUp_[a] = true;
    }
}

uint8_t Engine::slotOf(AnalogId a, BankId b) const { return cfg_.analog[a][b].shared ? 0 : b; }
uint8_t Engine::padSlotOf(PadId p, BankId b) const { return cfg_.pads[p][b].shared ? 0 : b; }

uint8_t Engine::quantize(float pos, uint8_t current) {
    const float v = pos * 127.0f;
    const float c = float(current);
    if (v > c + 0.5f + kHysteresis || v < c - 0.5f - kHysteresis) return round7(pos);
    return current;
}

void Engine::send(const MidiAddress& addr, uint8_t value) {
    if (addr.type == MidiType::None) return;
    MidiMsg m;
    m.type = addr.type;
    m.channel = addr.channel;
    m.number = addr.number;
    m.value = value > 127 ? 127 : value;
    hal_.midiSend(m);
}

void Engine::startBoot(uint32_t nowUs) {
    // The filters reset themselves from their first reading (Filter::update).
    for (AnalogId a = 0; a < nAnalog_; ++a) filter_[a].setInvert(abind(a).invert);
    bootStartUs_ = nowUs;
    bootStarted_ = true;
}

void Engine::finishBoot(uint32_t nowUs) {
    for (AnalogId a = 0; a < nAnalog_; ++a) {
        Filter& f = filter_[a];
        f.setInvert(abind(a).invert);   // the bank may have been set during the window
        f.resetFromMedian(nowUs);
        const float p = f.position();
        const uint8_t p7 = round7(p);
        for (uint8_t b = 0; b < kMaxBanks; ++b) {
            stored_[a][b] = p;
            stored7_[a][b] = p7;
        }
        q_[a] = p7;
        pickedUp_[a] = true;
        prevPos_[a] = p;
        awaitMove_[a] = false;
    }
    booted_ = true;
}

void Engine::update() {
    const uint32_t now = hal_.micros();
    nowUs_ = now;
    if (!booted_) {
        if (!bootStarted_) startBoot(now);
        for (AnalogId a = 0; a < nAnalog_; ++a) filter_[a].update(hal_.analogRaw(a), now);
        if (uint32_t(now - bootStartUs_) < kBootUs) return;
        finishBoot(now);
    }

    MidiMsg m;
    for (int i = 0; i < kMaxMidiPerUpdate && hal_.midiReceive(m); ++i) handleMidi(m);

    for (PadId p = 0; p < nPads_; ++p) updatePad(p);

    for (AnalogId a = 0; a < nAnalog_; ++a) updateAnalog(a, now);
}

bool Engine::reachedStored(float pos, float stored) const {
    if (absf(pos - stored) <= tol_) return true;
    if (pos <= 2.0f * tol_ && stored <= pos) return true;          // resting near the bottom
    if (pos >= 1.0f - 2.0f * tol_ && stored >= pos) return true;   // resting near the top
    return false;
}

void Engine::updateAnalog(AnalogId a, uint32_t nowUs) {
    Filter& f = filter_[a];
    const float p = f.update(hal_.analogRaw(a), nowUs);
    const uint8_t slot = slotOf(a, bank_);

    // After a suspend: nothing until a new movement beyond the dead band.
    if (awaitMove_[a] && absf(p - resumePos_[a]) > f.deadband()) awaitMove_[a] = false;

    if (f.moving() && !suspended_[a] && !awaitMove_[a]) {
        if (!pickedUp_[a]) {
            const float s = stored_[a][slot];
            const float prev = prevPos_[a];
            const bool crossed = (prev - s) * (p - s) <= 0.0f;
            if (reachedStored(p, s) || crossed) {
                pickedUp_[a] = true;
                q_[a] = stored7_[a][slot];
            }
        }
        if (pickedUp_[a]) {
            stored_[a][slot] = p;
            const uint8_t nq = quantize(p, q_[a]);
            if (nq != q_[a]) {
                q_[a] = nq;
                stored7_[a][slot] = nq;
                send(abind(a).addr, nq);
                if (abind(a).addr.type != MidiType::None) {
                    hasSent_[a] = true;
                    lastSent7_[a] = nq;
                    lastSentUs_[a] = nowUs;
                }
            }
        }
    }
    prevPos_[a] = p;
}

void Engine::evaluatePickup(AnalogId a) {
    const uint8_t slot = slotOf(a, bank_);
    const float p = filter_[a].position();
    q_[a] = stored7_[a][slot];
    prevPos_[a] = p;
    if (abind(a).pickup == PickupMode::Jump)
        pickedUp_[a] = true;
    else
        pickedUp_[a] = reachedStored(p, stored_[a][slot]);
}

void Engine::updatePad(PadId p) {
    const bool down = hal_.padPressed(p);
    if (down == pressed_[p]) return;
    pressed_[p] = down;
    // A reference to the binding of the bank this pad was pressed in: it stays that binding
    // even when the press changes the bank.
    const PadBinding& b = pbind(p);
    if (down) {
        switch (b.action) {
            case PadAction::None: break;
            case PadAction::BankSelect:
                if (b.arg < nBanks_) {
                    setBank(b.arg);
                    send(b.addr, b.onValue);
                }
                break;
            case PadAction::BankNext:
                setBank(BankId((bank_ + 1) % nBanks_));
                send(b.addr, b.onValue);
                break;
            case PadAction::BankPrev:
                setBank(BankId((bank_ + nBanks_ - 1) % nBanks_));
                send(b.addr, b.onValue);
                break;
            case PadAction::Momentary:
                heldAddr_[p] = b.addr;
                heldOff_[p] = b.offValue;
                send(b.addr, b.onValue);
                break;
            case PadAction::Toggle: {
                bool& t = toggle_[p][padSlotOf(p, bank_)];
                t = !t;
                send(b.addr, t ? b.onValue : b.offValue);
                break;
            }
        }
    } else if (heldAddr_[p].type != MidiType::None) {
        send(heldAddr_[p], heldOff_[p]);
        heldAddr_[p] = MidiAddress{};
    }
}

void Engine::handleMidi(const MidiMsg& m) {
    if (m.type == MidiType::None) return;
    const uint8_t v = m.value > 127 ? 127 : m.value;

    for (AnalogId a = 0; a < nAnalog_; ++a) {
        const uint8_t cur = slotOf(a, bank_);
        // The hand wins only over our own echo, or while the hand is still turning.
        const bool handActive = booted_ && filter_[a].moving() && pickedUp_[a] && !suspended_[a] && !awaitMove_[a];
        const int diff = int(v) - int(lastSent7_[a]);
        const bool handWins = handActive && hasSent_[a] &&
                              ((diff <= kEchoSteps && diff >= -int(kEchoSteps)) ||
                               uint32_t(nowUs_ - lastSentUs_[a]) < kHandWinsUs);
        bool hitCurrent = false;
        for (BankId b = 0; b < nBanks_; ++b) {
            if (!matches(cfg_.analog[a][b].addr, m)) continue;
            const uint8_t slot = slotOf(a, b);
            if (slot == cur && handWins) continue;
            stored_[a][slot] = float(v) / 127.0f;
            stored7_[a][slot] = v;
            if (booted_ && slot == cur) hitCurrent = true;
        }
        if (hitCurrent) evaluatePickup(a);
    }

    for (PadId p = 0; p < nPads_; ++p) {
        for (BankId b = 0; b < nBanks_; ++b) {
            const PadBinding& pb = cfg_.pads[p][b];
            if (!matches(pb.addr, m)) continue;
            const uint8_t slot = padSlotOf(p, b);
            midiValue_[p][slot] = float(v) / 127.0f;
            if (pb.action == PadAction::Toggle) toggle_[p][slot] = (v != pb.offValue);
        }
    }
}

void Engine::setBank(BankId b) {
    if (b >= nBanks_ || b == bank_) return;
    bank_ = b;
    if (!booted_) return;
    for (AnalogId a = 0; a < nAnalog_; ++a) {
        filter_[a].setInvert(abind(a).invert);
        evaluatePickup(a);
    }
}

void Engine::setCalibration(AnalogId a, AnalogCal cal) {
    if (a >= nAnalog_) return;
    filter_[a].setCalibration(cal);
    if (booted_) evaluatePickup(a);
}

void Engine::suspend(AnalogId a, bool s) {
    if (a >= nAnalog_) return;
    const bool resume = suspended_[a] && !s;
    suspended_[a] = s;
    if (resume && booted_) {
        evaluatePickup(a);
        // Ending a suspend is not a touch: wait for a new movement, measured from where the
        // control is now (the filter's target, so its catching up is not a movement).
        awaitMove_[a] = pickedUp_[a];
        resumePos_[a] = filter_[a].target();
    }
}

AnalogCal Engine::calibration(AnalogId a) const {
    return a < nAnalog_ ? filter_[a].calibration() : AnalogCal{};
}

uint8_t Engine::value7(AnalogId a) const {
    return a < nAnalog_ ? stored7_[a][slotOf(a, bank_)] : 0;
}

AnalogState Engine::analog(AnalogId a) const {
    AnalogState st;
    if (a >= nAnalog_) return st;
    const Filter& f = filter_[a];
    st.raw = f.raw();
    st.rawSmooth = f.rawSmooth();
    st.position = f.position();
    st.stored = stored_[a][slotOf(a, bank_)];
    st.pickedUp = pickedUp_[a];
    st.direction = st.pickedUp ? 0 : (st.stored > st.position ? 1 : -1);
    st.moving = f.moving();
    return st;
}

PadState Engine::pad(PadId p) const {
    PadState st;
    if (p >= nPads_) return st;
    const PadBinding& b = cfg_.pads[p][bank_];
    const uint8_t slot = padSlotOf(p, bank_);
    st.pressed = pressed_[p];
    switch (b.action) {
        case PadAction::Toggle: st.on = toggle_[p][slot]; break;
        case PadAction::BankSelect: st.on = (b.arg < nBanks_ && b.arg == bank_); break;
        default: st.on = pressed_[p]; break;
    }
    st.midiValue = midiValue_[p][slot];
    return st;
}

}  // namespace density
