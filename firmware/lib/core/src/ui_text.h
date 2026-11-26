// Small text and hashing helpers for the UI. No heap: everything writes into caller buffers.
#pragma once
#include "density/hal.h"
#include <cstddef>
#include <cstdint>

namespace density {
namespace ui {

// Copy `in` into `out` so that it is at most `maxChars` characters. A name that is too long
// is cut and ends in '.', with spaces before the cut removed ("Synthesizers" -> "Synthesiz.").
void fitText(char* out, size_t cap, const char* in, size_t maxChars);

// Number of characters of `in` to put on a line of `maxChars`: breaks at the last space
// that fits, or hard-cuts a long word. Returns strlen(in) when it all fits.
size_t wrapLength(const char* in, size_t maxChars);

// "Knob 7", "Fader" (or "Fader 2" when there are several), "Pedal 1". 1-based for people.
void controlName(const HardwareInfo& hw, AnalogId a, char* out, size_t cap);

// 0..1 -> 0..127, clamped and rounded.
uint8_t to7(float v);

// FNV-1a, used for the per-display "has the content changed" keys.
constexpr uint32_t kHashSeed = 2166136261u;
inline uint32_t hashAdd(uint32_t h, uint32_t v) {
    for (int i = 0; i < 4; ++i) { h ^= (v >> (8 * i)) & 0xFF; h *= 16777619u; }
    return h;
}
uint32_t hashStr(uint32_t h, const char* s);

}  // namespace ui
}  // namespace density
