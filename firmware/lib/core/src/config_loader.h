// Config loader extras beyond the contract in density/model.h.
// defaultConfig() and loadConfig() themselves are declared in model.h.
#pragma once
#include "density/model.h"
#include <string>
#include <vector>

namespace density {

// Things that load fine but are probably mistakes. At present: two bindings that
// send on the same MIDI address (type, channel, number) in the same bank.
// One line per problem, using the names printed on the board ("knob3", "pad17", "bank 2").
std::vector<std::string> configWarnings(const Config& cfg, const HardwareInfo& hw);

// Expand one selector such as "knob1-16", "fader1", "pedal1-2", "pad17-20" into 0-based ids,
// appended to `out`. pads = true accepts only pad selectors (ids are PadIds); false accepts
// only knob/fader/pedal selectors (ids are AnalogIds, knobs then faders then pedals).
// On failure returns false with a one-line reason in `err` (out may be partly filled).
bool parseSelector(const std::string& sel, const HardwareInfo& hw, bool pads,
                   std::vector<uint8_t>& out, std::string& err);

// Board names, 1-based: "knob3", "fader1", "pedal2", "pad17".
std::string analogName(const HardwareInfo& hw, AnalogId a);
std::string padName(PadId p);

}  // namespace density
