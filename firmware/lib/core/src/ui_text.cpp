#include "ui_text.h"
#include <cstdio>
#include <cstring>

namespace density {
namespace ui {

void fitText(char* out, size_t cap, const char* in, size_t maxChars) {
    if (cap == 0) return;
    if (!in) in = "";
    if (maxChars >= cap) maxChars = cap - 1;
    size_t n = strlen(in);
    if (n <= maxChars) {
        memcpy(out, in, n);
        out[n] = 0;
        return;
    }
    if (maxChars == 0) { out[0] = 0; return; }
    size_t keep = maxChars - 1;
    while (keep > 0 && in[keep - 1] == ' ') --keep;
    memcpy(out, in, keep);
    out[keep] = '.';
    out[keep + 1] = 0;
}

size_t wrapLength(const char* in, size_t maxChars) {
    size_t n = strlen(in);
    if (n <= maxChars) return n;
    for (size_t i = maxChars; i > 0; --i)
        if (in[i] == ' ') return i;
    return maxChars;
}

void controlName(const HardwareInfo& hw, AnalogId a, char* out, size_t cap) {
    unsigned idx = hw.indexInKind(a) + 1u;
    switch (hw.kindOf(a)) {
        case AnalogKind::Knob: snprintf(out, cap, "Knob %u", idx); break;
        case AnalogKind::Fader:
            if (hw.numFaders == 1) snprintf(out, cap, "Fader");
            else snprintf(out, cap, "Fader %u", idx);
            break;
        case AnalogKind::Pedal: snprintf(out, cap, "Pedal %u", idx); break;
    }
}

uint8_t to7(float v) {
    if (!(v > 0)) return 0;   // also catches NaN
    if (v >= 1) return 127;
    return static_cast<uint8_t>(v * 127.0f + 0.5f);
}

uint32_t hashStr(uint32_t h, const char* s) {
    if (!s) return h;
    for (; *s; ++s) { h ^= static_cast<uint8_t>(*s); h *= 16777619u; }
    return hashAdd(h, 0);
}

}  // namespace ui
}  // namespace density
