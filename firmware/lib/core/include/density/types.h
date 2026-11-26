// Shared small types. Portable: no Arduino headers.
#pragma once
#include <cstdint>
#include <cstddef>

namespace density {

using AnalogId = uint8_t;   // knobs, then faders, then pedals
using PadId = uint8_t;      // button i and LED i are pad i
using BankId = uint8_t;
constexpr uint8_t kNone = 0xFF;

constexpr uint8_t kMaxAnalog = 32;
constexpr uint8_t kMaxPads = 32;
constexpr uint8_t kMaxBanks = 8;
constexpr uint8_t kMaxEncoders = 2;
constexpr uint8_t kMaxDisplays = 2;

enum class AnalogKind : uint8_t { Knob, Fader, Pedal };

// h: 0..360 degrees, s and v: 0..1
struct Hsv { float h = 0, s = 0, v = 0; };
struct Rgb { uint8_t r = 0, g = 0, b = 0; };

enum class MidiType : uint8_t { None, CC, Note };

// A channel-voice message. channel is 1..16. For Note, value 0 means note off.
struct MidiMsg {
    MidiType type = MidiType::None;
    uint8_t channel = 1;
    uint8_t number = 0;   // CC number or note number
    uint8_t value = 0;    // 0..127
};

inline bool operator==(const MidiMsg& a, const MidiMsg& b) {
    return a.type == b.type && a.channel == b.channel && a.number == b.number && a.value == b.value;
}

}  // namespace density
