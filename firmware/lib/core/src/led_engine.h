// Pad LED colours from the config and the engine's state. Portable: no Arduino headers.
//
//   LedEngine leds(hal, engine);
//   loop: leds.update(hal.millis());
//
// Per pad, in priority order:
//   1. mode Off                     -> dark (nothing below applies)
//   2. pad held, action not None    -> press flash: full brightness, half saturation (whitish)
//   3. linked control not picked up -> pickup indicator (see below)
//   4. the LedSpec's mode, then its effect (blink / fade) on top
// Global Config::ledBrightness scales the result. No heap allocation in update().
//
// Pickup indicator: the pad blinks (period kPickupBlinkMs, all pads in step), its
// brightness showing the stored value it is waiting for (never darker than kPickupMinV
// so a stored 0 is still visible). Colour gives the direction at a glance:
//   red  (kPickupDownHue) = turn the control DOWN to reach the stored value
//   blue (kPickupUpHue)   = turn the control UP
// These are fixed hues, not shifted from the bank hue, so they read the same in every bank.
// The linked control is LedSpec::source if set, else the control the pad sits under.
#pragma once
#include "density/engine_view.h"
#include "density/hal.h"

namespace density {

class LedEngine {
public:
    static constexpr uint16_t kPickupBlinkMs = 300;   // full on+off period of the pickup indicator
    static constexpr float kPickupDownHue = 0;        // red: turn down
    static constexpr float kPickupUpHue = 215;        // blue: turn up
    static constexpr float kPickupMinV = 0.15f;       // indicator brightness at stored value 0
    static constexpr float kPressSat = 0.5f;          // press flash: saturation multiplier
    static constexpr uint16_t kResendMs = 200;        // re-send unchanged frames this often

    LedEngine(Hal& hal, const EngineView& view);

    // Call once per loop pass. Sets every pad, then showLeds() if anything changed
    // or kResendMs has passed since the last show.
    void update(uint32_t nowMs);

    // The colour pad p should show now, before ledBrightness. Exposed for tests and the UI.
    Hsv padColor(PadId p, uint32_t nowMs) const;

    // Forget what was sent, so the next update() shows a full frame.
    void invalidate() { primed_ = false; }

private:
    Hal& hal_;
    const EngineView& view_;
    Rgb last_[kMaxPads] = {};
    bool primed_ = false;
    uint32_t lastShowMs_ = 0;

    AnalogId controlOver(PadId p) const;
};

}  // namespace density
