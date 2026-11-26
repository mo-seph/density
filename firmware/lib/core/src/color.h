// HSV to RGB for the pad LEDs. Portable: no Arduino headers.
//
// Output is plain linear 8-bit RGB. The board driver applies gamma and the total
// current cap, so nothing here does.
#pragma once
#include "density/types.h"

namespace density {

// h in degrees (any value; wrapped into 0..360), s and v clamped to 0..1.
// brightness is the global LED scale (Config::ledBrightness) and multiplies v.
Rgb hsvToRgb(const Hsv& c, float brightness = 1.0f);

// Named rather than operator== so it cannot clash with one added to types.h later.
inline bool sameRgb(const Rgb& a, const Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

}  // namespace density
