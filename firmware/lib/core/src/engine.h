// The mapping engine: analog readings and pad presses -> MIDI, with banks, stored values
// and pickup; MIDI from the host -> stored values and pad state.
//
// Usage, once per main-loop pass:   hal.poll(); engine.update();
//
// Boot: for the first kBootUs (30 ms) after the first update() the engine only feeds the
// filters: it reads no MIDI, no pads, and sends nothing. At the end of that window every
// filter is reset from its median (so one bad first conversion does not count), the stored
// value of every bank (and the shared value) is set to the control's physical position, and
// every control is picked up. Normal operation starts in that same update(). Pads held
// during the window fire then (a pad held at boot fires once). Calibration and bank set
// before or during the window are honoured. Nothing is sent until a control is actually
// moved. Toggle pads start off.
//
// Analog controls
//   - Position comes from a Filter (filter.h): frozen while still, so untouched controls
//     never send.
//   - Output is 7-bit with hysteresis: the sent value changes only when position*127 moves
//     more than kHysteresis (a quarter step) past the half-way point to the next value.
//   - Stored value per (control, bank), or one per control when the binding is `shared`.
//     While a picked-up control moves, its stored value follows its position.
//   - Pickup mode: after a bank change, a host change of the stored value, a calibration
//     change or the end of a suspend, the control is picked up only if its position is
//     within the tolerance (Config::pickupTolerance, at least kMinPickupTolerance) of the
//     stored value. Otherwise it sends nothing until it reaches (within tolerance) or
//     crosses the stored value; then it sends its own value and tracks normally.
//     End stops: a control within 2 x tolerance of an end (position <= 2*tol, or
//     >= 1 - 2*tol) has reached any stored value at or beyond that end (stored <= position,
//     or stored >= position), so a pot that cannot quite reach its calibrated end can
//     still pick up 0 or 127.
//   - Jump mode: always reported as picked up. The 7-bit state is reset to the stored value
//     on a bank change, so the first touch that moves beyond the dead band sends the
//     physical value; a bank change alone sends nothing.
//   - End of a suspend: a control that is picked up then (always, in jump mode) sends
//     nothing until it moves beyond its filter's dead band from where it was when the
//     suspend ended, even if the filter still reports it as moving. A control that is not
//     picked up follows the pickup rules.
//   - Addr type None: never sends, but stored value and state are kept as usual.
//
// Pads (the binding of the current bank is used)
//   - Momentary: onValue on press, offValue on release, on the address that was pressed
//     (even if the bank changed while held).
//   - Toggle: press flips the state of (pad, bank) or (pad) if shared, sends on/offValue.
//   - BankSelect: press selects bank `arg` (ignored if arg >= numBanks).
//   - BankNext / BankPrev: press moves one bank, wrapping.
//   - Bank pads (BankSelect, BankNext, BankPrev) whose addr type is not None send onValue
//     on press, after changing the bank, on the address configured for the pad in the bank
//     where it was pressed; nothing on release. (An ignored BankSelect sends nothing.)
//   - PadState.on: Toggle = toggle state; BankSelect = its bank is current; other actions =
//     pressed.
//
// MIDI in
//   - At most kMaxMidiPerUpdate messages are handled per update(); the rest wait.
//   - CC or Note whose (type, channel, number) equals an analog binding's address in any
//     bank sets that stored value (value/127), and pickup is re-evaluated if that is the
//     current bank. Exception, "the hand wins": for the current bank, if the control is
//     moving, picked up and not suspended, AND the value is plausibly our own echo or the
//     hand is still turning (within kEchoSteps of the last 7-bit value this control sent,
//     or this control sent within the last kHandWinsUs), the message is ignored for the
//     current bank and the control keeps tracking the hand.
//   - Matching a pad's address sets its midiValue; for Toggle pads, state = value != offValue.
//   - Nothing received is echoed back.
//
// update() does no heap allocation.
#pragma once
#include "density/engine_view.h"
#include "density/hal.h"
#include "filter.h"

namespace density {

class Engine : public EngineView {
public:
    static constexpr float kHysteresis = 0.25f;   // in 7-bit steps
    static constexpr uint32_t kBootUs = 30000;     // boot window: filters only, nothing sent
    static constexpr float kMinPickupTolerance = 0.012f;
    static constexpr uint8_t kEchoSteps = 2;       // "hand wins": host value this close to ours
    static constexpr uint32_t kHandWinsUs = 50000; // "hand wins": or we sent this recently
    static constexpr int kMaxMidiPerUpdate = 64;

    Engine(Hal& hal, const Config& cfg);

    void update();

    // EngineView
    const Config& config() const override { return cfg_; }
    BankId bank() const override { return bank_; }
    AnalogState analog(AnalogId a) const override;
    PadState pad(PadId p) const override;
    // Current end stops (Config::cal holds only the values at start-up).
    AnalogCal calibration(AnalogId a) const override;
    void setBank(BankId b) override;
    void setCalibration(AnalogId a, AnalogCal cal) override;
    void suspend(AnalogId a, bool suspended) override;

    // Extras (not in EngineView).
    // True once the boot window has ended.
    bool booted() const { return booted_; }
    float pickupTolerance() const { return tol_; }
    // Last 7-bit value sent/received for a control in the current bank.
    uint8_t value7(AnalogId a) const;
    bool suspended(AnalogId a) const { return a < kMaxAnalog && suspended_[a]; }
    uint8_t numAnalog() const { return nAnalog_; }
    uint8_t numPads() const { return nPads_; }

private:
    uint8_t slotOf(AnalogId a, BankId b) const;
    uint8_t padSlotOf(PadId p, BankId b) const;
    const AnalogBinding& abind(AnalogId a) const { return cfg_.analog[a][bank_]; }
    const PadBinding& pbind(PadId p) const { return cfg_.pads[p][bank_]; }

    void startBoot(uint32_t nowUs);
    void finishBoot(uint32_t nowUs);
    bool reachedStored(float pos, float stored) const;
    void updateAnalog(AnalogId a, uint32_t nowUs);
    void updatePad(PadId p);
    void handleMidi(const MidiMsg& m);
    // Re-derive picked-up state for the current bank from position vs stored.
    void evaluatePickup(AnalogId a);
    void send(const MidiAddress& addr, uint8_t value);
    static uint8_t quantize(float pos, uint8_t current);

    Hal& hal_;
    const Config& cfg_;
    uint8_t nAnalog_ = 0, nPads_ = 0, nBanks_ = 1;
    BankId bank_ = 0;
    float tol_ = 0.03f;
    bool bootStarted_ = false;
    bool booted_ = false;
    uint32_t bootStartUs_ = 0;
    uint32_t nowUs_ = 0;

    // Analog
    Filter filter_[kMaxAnalog];
    float stored_[kMaxAnalog][kMaxBanks] = {};
    uint8_t stored7_[kMaxAnalog][kMaxBanks] = {};
    uint8_t q_[kMaxAnalog] = {};          // 7-bit output state with hysteresis (current bank)
    bool pickedUp_[kMaxAnalog] = {};
    bool suspended_[kMaxAnalog] = {};
    float prevPos_[kMaxAnalog] = {};
    bool hasSent_[kMaxAnalog] = {};
    uint8_t lastSent7_[kMaxAnalog] = {};   // last 7-bit value this control sent (any bank)
    uint32_t lastSentUs_[kMaxAnalog] = {};
    bool awaitMove_[kMaxAnalog] = {};      // after a suspend: wait for a new movement
    float resumePos_[kMaxAnalog] = {};

    // Pads
    bool pressed_[kMaxPads] = {};
    bool toggle_[kMaxPads][kMaxBanks] = {};
    float midiValue_[kMaxPads][kMaxBanks] = {};
    MidiAddress heldAddr_[kMaxPads] = {};
    uint8_t heldOff_[kMaxPads] = {};
};

}  // namespace density
