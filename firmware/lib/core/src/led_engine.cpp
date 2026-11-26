#include "led_engine.h"
#include "color.h"
#include <cmath>

namespace density {

namespace {
constexpr float kTwoPi = 6.2831853f;
float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
}  // namespace

LedEngine::LedEngine(Hal& hal, const EngineView& view) : hal_(hal), view_(view) {}

AnalogId LedEngine::controlOver(PadId p) const {
    const HardwareInfo& hw = hal_.info();
    const uint8_t n = hw.numAnalog() < kMaxAnalog ? hw.numAnalog() : kMaxAnalog;
    for (uint8_t a = 0; a < n; ++a)
        if (hw.padUnder[a] == p) return a;
    return kNone;
}

Hsv LedEngine::padColor(PadId p, uint32_t nowMs) const {
    const Config& cfg = view_.config();
    BankId bank = view_.bank();
    if (bank >= kMaxBanks) bank = 0;
    const LedSpec& s = cfg.leds[p][bank];
    if (s.mode == LedMode::Off) return Hsv{0, 0, 0};

    const PadBinding& pb = cfg.pads[p][bank];
    const PadState ps = view_.pad(p);

    // Hue: from the spec, or the bank's. A bank-select pad with "bank" hue takes the hue of
    // the bank it selects, so a row of bank buttons shows every bank's colour.
    float hue = s.color.h;
    if (s.hueFromBank) {
        BankId hb = bank;
        if (pb.action == PadAction::BankSelect && pb.arg < cfg.numBanks) hb = pb.arg;
        hue = cfg.banks[hb].hue;
    }
    const float vmax = clamp01(s.color.v);
    const float floorV = clamp01(s.floorV) < vmax ? clamp01(s.floorV) : vmax;

    // 2. Press flash.
    if (ps.pressed && pb.action != PadAction::None) return Hsv{hue, clamp01(s.color.s) * kPressSat, 1.0f};

    // 3. Pickup indicator.
    const AnalogId linked = s.source != kNone ? s.source : controlOver(p);
    if (linked != kNone && linked < hal_.info().numAnalog()) {
        const AnalogState as = view_.analog(linked);
        if (!as.pickedUp) {
            const bool lit = (nowMs % kPickupBlinkMs) < kPickupBlinkMs / 2;
            const float v = lit ? kPickupMinV + (1.0f - kPickupMinV) * clamp01(as.stored) : 0.0f;
            const float h = as.direction < 0 ? kPickupDownHue : as.direction > 0 ? kPickupUpHue : hue;
            return Hsv{h, 1.0f, v};
        }
    }

    // 4. Mode.
    float v = 0;
    switch (s.mode) {
        case LedMode::Off: v = 0; break;
        case LedMode::Static: v = vmax; break;
        case LedMode::Level:
            if (linked != kNone && linked < hal_.info().numAnalog())
                v = floorV + (vmax - floorV) * clamp01(view_.analog(linked).stored);
            else
                v = floorV;   // nothing to follow: static at floor
            break;
        case LedMode::State: v = (ps.on || ps.pressed) ? vmax : floorV; break;
        case LedMode::Midi: v = vmax * clamp01(ps.midiValue); break;
    }

    // Effects, phase from the clock so every pad with the same period is in step.
    if (s.effect != LedEffect::None && s.periodMs > 0) {
        const uint32_t t = nowMs % s.periodMs;
        if (s.effect == LedEffect::Blink) {
            if (t >= s.periodMs / 2u) v = 0;
        } else {   // Fade: raised cosine, 0 at the start of each period, full at half way
            v *= 0.5f - 0.5f * std::cos(kTwoPi * float(t) / float(s.periodMs));
        }
    }
    return Hsv{hue, clamp01(s.color.s), v};
}

void LedEngine::update(uint32_t nowMs) {
    const Config& cfg = view_.config();
    uint8_t n = hal_.info().numPads;
    if (n > kMaxPads) n = kMaxPads;

    bool changed = !primed_;
    for (PadId p = 0; p < n; ++p) {
        const Rgb c = hsvToRgb(padColor(p, nowMs), cfg.ledBrightness);
        if (!sameRgb(c, last_[p])) changed = true;
        last_[p] = c;
        hal_.setLed(p, c);
    }
    if (changed || uint32_t(nowMs - lastShowMs_) >= kResendMs) {
        hal_.showLeds();
        lastShowMs_ = nowMs;
        primed_ = true;
    }
}

}  // namespace density
