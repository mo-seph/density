#include "inputs.h"

#include <hardware/gpio.h>
#include <hardware/sync.h>
#include <pico/time.h>
#include <pico/platform.h>

#include "board_table.h"
#include "input_logic.h"

namespace density {
namespace proto {

namespace {

// 74HC165 at 3.3 V: clock pulse width and load pulse width are well under 100 ns, so
// ~15 cycles (100 ns at 150 MHz) per half period leaves margin.
constexpr uint32_t kHalfPeriodCycles = 15;

constexpr uint32_t kStableSamples = (kDebounceMs * kInputSampleHz + 999) / 1000;   // 20 at 4 kHz
static_assert(kStableSamples < 255, "debounce counter is 8 bit");

// Debounced bits use our own layout: bit p = pad p (0..19), bit 20 = encoder switch.
constexpr uint8_t kEncSwSlot = kNumPads;
constexpr uint32_t kDebounceMask = (1u << (kNumPads + 1)) - 1;

constexpr uint32_t kSamplePeriodUs = 1000000 / kInputSampleHz;   // 250 us
// A gap longer than this many periods means the sampler was stopped (interrupts off).
constexpr uint32_t kLatePeriods = 4;

repeating_timer_t gTimer;
Debouncer32 gDebounce(kStableSamples);
QuadratureDecoder gQuad(kEncoderEdgesPerDetent);
SampleGate gGate(kSamplePeriodUs, kLatePeriods);

volatile uint32_t gPads = 0;
volatile bool gEncSw = false;
volatile int32_t gEncCount = 0;
volatile uint32_t gRaw = 0xFFFFFF;
volatile uint32_t gSamples = 0;
volatile uint32_t gResyncs = 0;

// Active-low raw word -> our "pressed" bit layout.
uint32_t pressedBits(uint32_t raw) {
    uint32_t out = 0;
    for (uint8_t p = 0; p < kNumPads; ++p)
        if (!((raw >> kPadBit[p]) & 1u)) out |= 1u << p;
    if (!((raw >> kBitEncSw) & 1u)) out |= 1u << kEncSwSlot;
    return out;
}

uint8_t encoderAB(uint32_t raw) {
    uint8_t a = (raw >> kBitEncA) & 1u;
    uint8_t b = (raw >> kBitEncB) & 1u;
    return kEncoderReverse ? uint8_t((b << 1) | a) : uint8_t((a << 1) | b);
}

void sampleOnce() {
    const SampleGate::Kind kind = gGate.classify(time_us_32());
    if (kind == SampleGate::Kind::Skip) return;   // catch-up tick after a gap
    const uint32_t raw = InputSampler::readChain(kSrBits);
    int8_t step = 0;
    if (kind == SampleGate::Kind::Resync) {
        gDebounce.restart();
        gQuad.reset(encoderAB(raw));   // no step: edges during the gap are unknown
        gResyncs = gResyncs + 1;
    } else {
        step = gQuad.update(encoderAB(raw));
    }
    const uint32_t deb = gDebounce.update(pressedBits(raw), kDebounceMask);
    gRaw = raw;
    gPads = deb & ((1u << kNumPads) - 1);
    gEncSw = (deb >> kEncSwSlot) & 1u;
    if (step) gEncCount = gEncCount + step;
    gSamples = gSamples + 1;
}

bool timerCallback(repeating_timer_t*) {
    sampleOnce();
    return true;   // keep repeating
}

}  // namespace

uint32_t InputSampler::readChain(uint8_t bits) {
    // Parallel load: SH/LD low, then high. After that, the serial output already shows the
    // first bit (U5 D7); each rising clock edge shifts the next one out. After 24 clocks the
    // output shows what entered U7's serial input, which is tied to GND.
    gpio_put(kPinSrLoad, 0);
    busy_wait_at_least_cycles(kHalfPeriodCycles);
    gpio_put(kPinSrLoad, 1);
    busy_wait_at_least_cycles(kHalfPeriodCycles);
    uint32_t v = 0;
    for (uint8_t i = 0; i < bits; ++i) {
        v = (v << 1) | (gpio_get(kPinSrData) ? 1u : 0u);
        gpio_put(kPinSrClk, 1);
        busy_wait_at_least_cycles(kHalfPeriodCycles);
        gpio_put(kPinSrClk, 0);
        busy_wait_at_least_cycles(kHalfPeriodCycles);
    }
    return v;
}

bool InputSampler::begin() {
    gpio_init(kPinSrClk);
    gpio_set_dir(kPinSrClk, GPIO_OUT);
    gpio_put(kPinSrClk, 0);
    gpio_init(kPinSrLoad);
    gpio_set_dir(kPinSrLoad, GPIO_OUT);
    gpio_put(kPinSrLoad, 1);
    gpio_init(kPinSrData);
    gpio_set_dir(kPinSrData, GPIO_IN);
    // The board has pull-ups on the inputs; SR_DATA is driven by U5. A weak pull-up here
    // makes a missing chain read all ones (nothing pressed) instead of floating.
    gpio_pull_up(kPinSrData);

    // Start from what the switches show now, so nothing "presses" at boot.
    const uint32_t raw = readChain(kSrBits);
    gDebounce.reset(pressedBits(raw));
    gQuad.reset(encoderAB(raw));
    gRaw = raw;
    gPads = gDebounce.state() & ((1u << kNumPads) - 1);
    gEncSw = (gDebounce.state() >> kEncSwSlot) & 1u;

    // Negative interval: period measured start to start, so the rate does not drift.
    gGate.reset(time_us_32());
    return add_repeating_timer_us(-int64_t(kSamplePeriodUs), timerCallback, nullptr, &gTimer);
}

ChainCheck InputSampler::chainCheck() {
    const uint32_t irq = save_and_disable_interrupts();
    const uint32_t v = readChain(kSrBits + 1);
    restore_interrupts(irq);
    ChainCheck c;
    c.raw = (v >> 1) & ((1u << kSrBits) - 1);
    c.extraBit = v & 1u;
    c.spareBit = (c.raw >> kBitSpare) & 1u;
    return c;
}

InputSnapshot InputSampler::snapshot() const {
    InputSnapshot s;
    const uint32_t irq = save_and_disable_interrupts();
    s.padsPressed = gPads;
    s.encPressed = gEncSw;
    s.encCount = gEncCount;
    s.raw = gRaw;
    s.samples = gSamples;
    s.resyncs = gResyncs;
    restore_interrupts(irq);
    return s;
}

}  // namespace proto
}  // namespace density
