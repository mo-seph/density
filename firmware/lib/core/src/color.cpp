#include "color.h"
#include <cmath>

namespace density {

namespace {
float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
uint8_t to8(float x) { return uint8_t(clamp01(x) * 255.0f + 0.5f); }
}  // namespace

Rgb hsvToRgb(const Hsv& c, float brightness) {
    float h = std::isfinite(c.h) ? std::fmod(c.h, 360.0f) : 0.0f;
    if (h < 0) h += 360.0f;
    const float s = clamp01(c.s);
    const float v = clamp01(c.v) * clamp01(brightness);

    // Standard hexcone: chroma, then position within the 60-degree sector.
    const float chroma = v * s;
    const float hp = h / 60.0f;
    const float x = chroma * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    switch (int(hp) % 6) {
        case 0: r = chroma; g = x; break;
        case 1: r = x; g = chroma; break;
        case 2: g = chroma; b = x; break;
        case 3: g = x; b = chroma; break;
        case 4: r = x; b = chroma; break;
        default: r = chroma; b = x; break;
    }
    const float m = v - chroma;
    return Rgb{to8(r + m), to8(g + m), to8(b + m)};
}

}  // namespace density
