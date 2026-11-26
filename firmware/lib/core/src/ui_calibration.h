// Pedal calibration file: /cal.json on the board's flash.
//
// Format (one pair of raw end stops per pedal, in pedal order: pedal 1 first):
//     {"pedals":[[370,3730],[40,4055]]}
// A pair of [0,0] means "never calibrated, keep the config's default" and is skipped on load.
#pragma once
#include "density/model.h"
#include <string>

namespace density {

constexpr const char* kCalibrationPath = "/cal.json";

// Smallest raw range a calibration may have (about 10% of travel). Anything less means the
// pedal was not moved, or is not plugged in.
constexpr uint16_t kMinCalRange = 400;

// Is this a usable pair of end stops?
bool calibrationValid(const AnalogCal& c);

// pedals[i] for i < count. `set[i]` false writes [0,0].
std::string formatCalibration(const AnalogCal* pedals, const bool* set, uint8_t count);

// Parses the format above. Fills pedals[i] and set[i] for i < maxCount (set[i] = false for
// missing or invalid entries). Returns false if the text is not in this format.
bool parseCalibration(const std::string& text, AnalogCal* pedals, bool* set, uint8_t maxCount);

}  // namespace density
