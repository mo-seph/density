// SK6812MINI chain on GP13: PIO state machine fed by DMA, so showLeds() costs the CPU a few
// microseconds instead of the ~600 us the 20-LED frame takes on the wire.
#pragma once
#include <cstdint>
#include "density/types.h"
#include "board_table.h"

namespace density {
namespace proto {

// ---- LED power budget (NOTES.md: 20 LEDs at full white draw 0.72-0.81 A on an unfused
// USB 5 V rail; cap at about 350-400 mA). The estimate is deliberately simple:
constexpr uint32_t kLedCurrentCapMa = 350;    // total LED current cap
constexpr uint32_t kLedMaPerChannel = 12;     // one colour channel at full PWM
constexpr uint32_t kLedIdleMa = 1;            // per LED, even when dark
constexpr float kLedGamma = 2.2f;             // applied to the core's linear RGB

class LedChain {
public:
    bool begin();
    void set(PadId p, Rgb c);         // linear RGB; applied at the next show()
    void show();                      // gamma, current cap, chain order, start DMA
    void service();                   // call often: starts a frame that show() had to defer

    // Diagnostics for the last frame sent.
    uint32_t lastEstimateMa() const { return lastEstimateMa_; }   // before the cap
    uint32_t lastCappedMa() const { return lastCappedMa_; }       // after the cap
    bool ok() const { return ok_; }

private:
    bool startFrame();

    Rgb linear_[kNumPads] = {};
    uint32_t words_[kNumPads] = {};   // GRB, left-justified, in chain order (DMA source)
    uint8_t gamma_[256] = {};
    bool pending_ = false;
    bool ok_ = false;
    uint32_t lastStartUs_ = 0;
    uint32_t lastEstimateMa_ = 0;
    uint32_t lastCappedMa_ = 0;
    int dma_ = -1;
    void* pio_ = nullptr;
    unsigned sm_ = 0;
};

}  // namespace proto
}  // namespace density
