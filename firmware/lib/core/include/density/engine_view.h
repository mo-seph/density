// What the LED engine and the UI may ask of, and tell, the mapping engine.
// The concrete class (lib/core/src/engine.h) implements this; tests can fake it.
#pragma once
#include "types.h"
#include "model.h"

namespace density {

struct AnalogState {
    uint16_t raw = 0;         // latest raw reading, unfiltered
    uint16_t rawSmooth = 0;   // raw with spikes removed (median); use this to learn end stops
    float position = 0;       // filtered physical position, 0..1 (after calibration and invert)
    float stored = 0;         // stored value in the current bank, 0..1
    bool pickedUp = true;     // false = waiting to reach `stored`, sending nothing
    int8_t direction = 0;     // when not picked up: +1 = turn up to reach stored, -1 = turn down
    bool moving = false;      // touched within the filter's settle time
};

struct PadState {
    bool pressed = false;
    bool on = false;          // toggle state / this pad's bank is the current one
    float midiValue = 0;      // last value received from the host on this pad's addr, 0..1
};

class EngineView {
public:
    virtual ~EngineView() = default;

    virtual const Config& config() const = 0;
    virtual BankId bank() const = 0;
    virtual AnalogState analog(AnalogId a) const = 0;
    virtual PadState pad(PadId p) const = 0;
    // End stops in use now (config value, or as replaced by setCalibration).
    virtual AnalogCal calibration(AnalogId a) const = 0;

    // Commands (used by the UI).
    virtual void setBank(BankId b) = 0;
    // Replace a control's end stops (pedal calibration). Takes effect at once.
    virtual void setCalibration(AnalogId a, AnalogCal cal) = 0;
    // While suspended, a control is still read and filtered but sends no MIDI
    // (used while a pedal is being calibrated).
    virtual void suspend(AnalogId a, bool suspended) = 0;
};

}  // namespace density
