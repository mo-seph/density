// Buttons and encoder: three chained 74HC165 sampled from a repeating timer interrupt on
// core 0, so a slow main-loop pass does not delay sampling.
//
// It is NOT independent of flash writes: arduino-pico's LittleFS turns interrupts off and
// idles core 1 for every flash erase and program, so during Hal::writeFile (saving
// calibration, uploading a config) this timer, USB and core 1 all pause, for some tens of
// milliseconds per file. Afterwards the timer fires the missed ticks back to back; the
// sampler ignores those and resynchronises (see SampleGate in input_logic.h). A press or
// encoder step made entirely inside the pause is lost.
#pragma once
#include <cstdint>

namespace density {
namespace proto {

// Sample rate of the shift registers. NOTES.md asks for 2 kHz or faster.
constexpr uint32_t kInputSampleHz = 4000;
// Button debounce: a change must hold this long before it counts (about 5 ms).
constexpr uint32_t kDebounceMs = 5;

struct InputSnapshot {
    uint32_t padsPressed = 0;     // debounced, bit p = pad p pressed
    bool encPressed = false;      // debounced encoder switch
    int32_t encCount = 0;         // cumulative detent steps since boot (+ = clockwise)
    uint32_t raw = 0;             // last raw 24-bit shift register word (for diagnostics)
    uint32_t samples = 0;         // number of samples taken since boot
    uint32_t resyncs = 0;         // times sampling restarted after a gap (flash writes)
};

// Result of InputSampler::chainCheck().
struct ChainCheck {
    uint32_t raw = 0;        // the 24 data bits, as in InputSnapshot::raw
    bool extraBit = true;    // 25th bit: U7's serial input is tied to GND, so it must be 0
    bool spareBit = false;   // U7 D7 is pulled up and unused, so it must be 1
    bool ok() const { return !extraBit && spareBit; }
};

class InputSampler {
public:
    // Configures the pins, takes one sample to initialise the debouncer and starts the timer.
    bool begin();
    // Consistent copy of the state gathered by the interrupt.
    InputSnapshot snapshot() const;
    // Reads 25 bits with interrupts off (about 8 us, so the timer cannot interleave) and
    // checks both fixed bits. For start-up and the hardware test, not for every sample.
    static ChainCheck chainCheck();
    // One raw read of `bits` bits (default: the 24 data bits). Call only from the timer, or
    // with interrupts off, or before begin() starts the timer.
    static uint32_t readChain(uint8_t bits);
};

}  // namespace proto
}  // namespace density
