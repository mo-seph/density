// The configuration as the firmware uses it. The JSON loader fills this in;
// the engine, LED engine and UI only ever read it.
//
// Everything that can differ per bank is indexed [control][bank]. A binding that is
// the same in every bank is simply repeated, with shared = true when the banks also
// share one stored value.
#pragma once
#include "types.h"
#include "hal.h"
#include <string>

namespace density {

struct MidiAddress {
    MidiType type = MidiType::None;   // None = sends nothing
    uint8_t channel = 1;              // 1..16
    uint8_t number = 0;
};

enum class PickupMode : uint8_t { Jump, Pickup };

struct AnalogBinding {
    MidiAddress addr;
    PickupMode pickup = PickupMode::Pickup;
    bool shared = false;     // one stored value across all banks (addr is then the same in each)
    bool invert = false;
};

enum class PadAction : uint8_t {
    None,
    BankSelect,   // arg = bank
    BankNext,
    BankPrev,
    Momentary,    // sends onValue on press, offValue on release
    Toggle,       // press flips a stored on/off state and sends it
};

struct PadBinding {
    PadAction action = PadAction::None;
    uint8_t arg = 0;
    MidiAddress addr;
    uint8_t onValue = 127, offValue = 0;
    bool shared = false;     // toggle state shared across banks
};

enum class LedMode : uint8_t {
    Off,
    Static,   // always color
    Level,    // v follows the stored value of analog control `source` in the current bank
    State,    // on when the pad's toggle is on / its bank is selected / it is held; dim otherwise
    Midi,     // v follows the last value received from the host on the pad's addr
};

enum class LedEffect : uint8_t { None, Blink, Fade };

struct LedSpec {
    LedMode mode = LedMode::Off;
    Hsv color{0, 1, 1};        // v here is the maximum brightness for this LED
    bool hueFromBank = false;  // use the current bank's hue instead of color.h; on a BankSelect pad, the hue of the bank it selects
    float floorV = 0.04f;      // Level/State: brightness at value 0 / off, so the pad stays visible (Midi mode has no floor)
    AnalogId source = kNone;   // Level: which control; kNone = the control this pad sits under
    LedEffect effect = LedEffect::None;
    uint16_t periodMs = 500;
};

struct BankDef {
    std::string name;
    float hue = 0;             // 0..360
};

// Raw end stops for one analog control. Defaults suit the buffered pots.
struct AnalogCal {
    uint16_t rawMin = 40;
    uint16_t rawMax = 4055;
};

struct FilterParams {
    float smoothing = 0.2f;    // 0..1; share of each new sample taken when the control is still
    float deadband = 0.006f;   // fraction of full travel a control must move before it counts as touched
    uint16_t settleMs = 300;   // after this long without movement the control is still again
};

struct Config {
    std::string name = "Density";
    uint8_t numBanks = 1;
    BankDef banks[kMaxBanks];
    AnalogBinding analog[kMaxAnalog][kMaxBanks];
    PadBinding pads[kMaxPads][kMaxBanks];
    LedSpec leds[kMaxPads][kMaxBanks];
    AnalogCal cal[kMaxAnalog];
    FilterParams filter[3];          // indexed by AnalogKind
    float ledBrightness = 0.5f;      // global scale, 0..1
    float pickupTolerance = 0.03f;   // how close physical must come to stored to pick up
};

// Implemented in lib/core/src/config_loader.cpp.
// loadConfig starts from defaultConfig, then applies the JSON. On failure it returns
// false, leaves `out` as the default config, and puts a one-line reason in `err`.
void defaultConfig(const HardwareInfo& hw, Config& out);
bool loadConfig(const std::string& json, const HardwareInfo& hw, Config& out, std::string& err);

}  // namespace density
