// Compile-time board table for the 125 x 130 mm prototype (density_proto).
// Source of every fact here: ../proto-125x130/NOTES.md ("Brain pins", "Analogue inputs",
// "Shift register bits", "LED chain order"). Pad numbers in NOTES.md are 1-based
// (BTN1 = PadId 0); everything in this file is 0-based.
#pragma once
#include <cstdint>
#include "density/hal.h"

namespace density {
namespace proto {

// ---- Counts --------------------------------------------------------------------------
constexpr uint8_t kNumKnobs = 16;
constexpr uint8_t kNumFaders = 1;
constexpr uint8_t kNumPedals = 2;
constexpr uint8_t kNumAnalog = kNumKnobs + kNumFaders + kNumPedals;   // 19
constexpr uint8_t kNumPads = 20;
constexpr uint8_t kNumEncoders = 1;
constexpr uint8_t kNumDisplays = 2;

// ---- Pins (RP2350-Zero GPIO numbers) ------------------------------------------------
// MCP3208 on SPI0. CS is a plain GPIO so it stays low for the whole 24-clock frame.
constexpr uint8_t kPinAdcSck = 2;
constexpr uint8_t kPinAdcMosi = 3;
constexpr uint8_t kPinAdcMiso = 4;
constexpr uint8_t kPinAdcCs = 5;
constexpr uint32_t kAdcSpiHz = 1000000;   // MCP3208 at 3.3 V: keep <= 1 MHz

// CD74HC4067 address S0..S3 on four consecutive pins, written in one go.
constexpr uint8_t kPinMuxS0 = 6;          // S0 = GP6, S1 = GP7, S2 = GP8, S3 = GP9
constexpr uint32_t kMuxMask = 0xFu << kPinMuxS0;

// Three chained 74HC165. GP11 (load) must stay a plain GPIO.
constexpr uint8_t kPinSrClk = 10;
constexpr uint8_t kPinSrLoad = 11;
constexpr uint8_t kPinSrData = 12;
constexpr uint8_t kSrBits = 24;

// SK6812MINI chain (through a 5 V level shifter).
constexpr uint8_t kPinLedData = 13;

// Two SSD1306 128x64 on I2C1.
constexpr uint8_t kPinSda = 14;
constexpr uint8_t kPinScl = 15;
constexpr uint32_t kI2cHz = 400000;
constexpr uint8_t kDisplayAddr[kNumDisplays] = {0x3C, 0x3D};
constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 64;

// ---- Analog sources -----------------------------------------------------------------
// AnalogId 0..15 = knobs RV1..RV16 (mux address = AnalogId, ADC CH0)
// AnalogId 16    = fader RV17 (ADC CH1)
// AnalogId 17/18 = pedal 1 / pedal 2 (ADC CH2 / CH3)
constexpr uint8_t kAdcChMux = 0;
constexpr uint8_t kAdcChDirect[kNumFaders + kNumPedals] = {1, 2, 3};

// ---- Shift register bit map ---------------------------------------------------------
// The 24 bits are shifted MSB-first into a uint32_t, so the first bit out (U5 D7) ends up
// at bit 23. Then U5 = bits 23..16 (Dn at bit 16+n), U6 = bits 15..8, U7 = bits 7..0.
// Inputs are pulled up: a pressed button reads 0.
//   U5 D0..D7 = BTN1..BTN8   -> pads 0..7   at bits 16..23
//   U6 D0..D7 = BTN9..BTN16  -> pads 8..15  at bits 8..15
//   U7 D0..D3 = BTN17..BTN20 -> pads 16..19 at bits 0..3
//   U7 D4 = ENC_A, D5 = ENC_B, D6 = ENC_SW, D7 = spare (reads 1)
constexpr uint8_t kPadBit[kNumPads] = {
    16, 17, 18, 19, 20, 21, 22, 23,   // BTN1..BTN8
    8, 9, 10, 11, 12, 13, 14, 15,     // BTN9..BTN16
    0, 1, 2, 3,                       // BTN17..BTN20
};
constexpr uint8_t kBitEncA = 4;
constexpr uint8_t kBitEncB = 5;
constexpr uint8_t kBitEncSw = 6;
constexpr uint8_t kBitSpare = 7;      // should always read 1: a 0 here means the chain is broken

// ---- LED chain order ----------------------------------------------------------------
// kChainToPad[chain position] = pad beside that LED. The chain starts at the bottom right
// of the pot block and snakes upward (NOTES.md "LED chain order").
constexpr uint8_t kChainToPad[kNumPads] = {
    15, 14, 13, 12,   // chain 1-4   : BTN16, BTN15, BTN14, BTN13
    8, 9, 10, 11,     // chain 5-8   : BTN9 .. BTN12
    7, 6, 5, 4,       // chain 9-12  : BTN8 .. BTN5
    0, 1, 2, 3,       // chain 13-16 : BTN1 .. BTN4
    16, 17, 18, 19,   // chain 17-20 : BTN17 .. BTN20
};

struct PadToChain {
    uint8_t v[kNumPads] = {};
    constexpr PadToChain() {
        for (uint8_t c = 0; c < kNumPads; ++c) v[kChainToPad[c]] = c;
    }
};
constexpr PadToChain kPadToChain{};

// Sanity: the chain table must be a permutation of 0..19.
constexpr bool chainIsPermutation() {
    bool seen[kNumPads] = {};
    for (uint8_t c = 0; c < kNumPads; ++c) {
        if (kChainToPad[c] >= kNumPads || seen[kChainToPad[c]]) return false;
        seen[kChainToPad[c]] = true;
    }
    return true;
}
static_assert(chainIsPermutation(), "LED chain table must be a permutation of the pads");
static_assert(kNumAnalog <= kMaxAnalog && kNumPads <= kMaxPads, "board exceeds core limits");
static_assert(kNumEncoders <= kMaxEncoders && kNumDisplays <= kMaxDisplays, "board exceeds core limits");

// ---- Encoder ------------------------------------------------------------------------
// Alps EC11E15244B2: 30 detents, 15 pulses per turn, i.e. two quadrature edges per detent.
constexpr uint8_t kEncoderEdgesPerDetent = 2;
// Not yet verified on the real board: if turning clockwise shows a negative count in the
// hardware test (display 0 header "last-", log "counts down"), set kEncoderReverse = true.
constexpr bool kEncoderReverse = false;

// ---- The table the core sees --------------------------------------------------------
inline HardwareInfo makeHardwareInfo() {
    HardwareInfo hw;
    hw.name = "density_proto";
    hw.numKnobs = kNumKnobs;
    hw.numFaders = kNumFaders;
    hw.numPedals = kNumPedals;
    hw.numPads = kNumPads;
    hw.numEncoders = kNumEncoders;
    hw.numDisplays = kNumDisplays;
    // HardwareInfo's constructor sets every padUnder entry to kNone; only the knobs have pads.
    for (uint8_t k = 0; k < kNumKnobs; ++k) hw.padUnder[k] = k;   // knob i sits over pad i
    return hw;
}

}  // namespace proto
}  // namespace density
