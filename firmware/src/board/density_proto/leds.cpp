#include "leds.h"

#include <cmath>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/pio.h>
#include <hardware/timer.h>

#include "sk6812.pio.h"

namespace density {
namespace proto {

namespace {
constexpr uint32_t kBitHz = 800000;
constexpr uint32_t kCyclesPerBit = sk6812_T1 + sk6812_T2 + sk6812_T3;   // 10
// One frame on the wire is 20 x 24 bits x 1.25 us = 600 us, then >= 80 us low to latch.
constexpr uint32_t kFrameUs = kNumPads * 24 * 1000000ull / kBitHz;
constexpr uint32_t kMinFrameIntervalUs = kFrameUs + 100;
}  // namespace

bool LedChain::begin() {
    for (int i = 0; i < 256; ++i) {
        float v = std::pow(i / 255.0f, kLedGamma) * 255.0f + 0.5f;
        uint8_t g = v > 255.0f ? 255 : uint8_t(v);
        if (i > 0 && g == 0) g = 1;   // keep dim-but-on values (pad floors) visible
        gamma_[i] = g;
    }

    PIO pio;
    uint sm, offset;
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(&sk6812_program, &pio, &sm, &offset,
                                                          kPinLedData, 1, true)) {
        return ok_ = false;
    }
    pio_ = pio;
    sm_ = sm;

    pio_gpio_init(pio, kPinLedData);
    pio_sm_set_consecutive_pindirs(pio, sm, kPinLedData, 1, true);
    pio_sm_config c = sk6812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, kPinLedData);
    sm_config_set_out_shift(&c, false /* MSB first */, true /* autopull */, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, float(clock_get_hz(clk_sys)) / float(kBitHz * kCyclesPerBit));
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    dma_ = dma_claim_unused_channel(false);
    if (dma_ < 0) return ok_ = false;
    dma_channel_config d = dma_channel_get_default_config(dma_);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, true);
    channel_config_set_write_increment(&d, false);
    channel_config_set_dreq(&d, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma_, &d, &pio->txf[sm], words_, kNumPads, false);

    ok_ = true;
    // All off at boot (the LEDs power up in an undefined state on some batches).
    for (auto& l : linear_) l = Rgb{};
    lastStartUs_ = time_us_32() - kMinFrameIntervalUs;
    show();
    return true;
}

void LedChain::set(PadId p, Rgb c) {
    if (p < kNumPads) linear_[p] = c;
}

void LedChain::show() {
    pending_ = true;
    service();
}

void LedChain::service() {
    if (pending_ && startFrame()) pending_ = false;
}

bool LedChain::startFrame() {
    if (!ok_) return true;   // nothing to drive: drop the frame
    if (dma_channel_is_busy(dma_)) return false;
    const uint32_t now = time_us_32();
    if (now - lastStartUs_ < kMinFrameIntervalUs) return false;   // previous frame still on the wire

    // Gamma, in chain order.
    uint8_t ch[kNumPads][3];
    uint32_t sum = 0;
    for (uint8_t c = 0; c < kNumPads; ++c) {
        const Rgb& l = linear_[kChainToPad[c]];
        ch[c][0] = gamma_[l.g];
        ch[c][1] = gamma_[l.r];
        ch[c][2] = gamma_[l.b];
        sum += ch[c][0] + ch[c][1] + ch[c][2];
    }

    // Current estimate and cap: est = idle * N + sum(channel / 255) * per-channel mA.
    constexpr uint32_t kIdle = kLedIdleMa * kNumPads;
    static_assert(kLedCurrentCapMa > kIdle, "cap must exceed idle current");
    const uint32_t est = kIdle + (sum * kLedMaPerChannel + 254) / 255;
    lastEstimateMa_ = est;
    uint32_t num = 1, den = 1;
    if (est > kLedCurrentCapMa) {
        // Scale every channel by (cap - idle) / (est - idle), using the exact sum.
        num = (kLedCurrentCapMa - kIdle) * 255;
        den = sum * kLedMaPerChannel;
    }
    uint32_t sumAfter = 0;
    for (uint8_t c = 0; c < kNumPads; ++c) {
        uint32_t g = ch[c][0] * num / den, r = ch[c][1] * num / den, b = ch[c][2] * num / den;
        sumAfter += g + r + b;
        words_[c] = (g << 24) | (r << 16) | (b << 8);   // GRB, left-justified for a 24-bit autopull
    }
    lastCappedMa_ = kIdle + (sumAfter * kLedMaPerChannel + 254) / 255;

    dma_channel_set_read_addr(dma_, words_, false);
    dma_channel_set_trans_count(dma_, kNumPads, true);   // starts the transfer
    lastStartUs_ = now;
    return true;
}

}  // namespace proto
}  // namespace density
