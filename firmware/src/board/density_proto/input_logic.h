// Portable input logic used by the shift-register sampler: button debounce, quadrature
// decoding and the rule for samples that arrive late. No Arduino or SDK headers, so it can be
// checked natively.
#pragma once
#include <cstdint>

namespace density {
namespace proto {

// Debounces up to 32 inputs held in a bitmask. A bit's debounced state changes only after
// the raw input has disagreed with it for `stableSamples` consecutive samples.
class Debouncer32 {
public:
    explicit Debouncer32(uint8_t stableSamples = 20, uint32_t initial = 0)
        : stable_(stableSamples), state_(initial) {}

    void reset(uint32_t state) {
        state_ = state;
        for (auto& c : count_) c = 0;
    }

    // raw: 1 = active (pressed). mask: which bits to process. Returns the debounced state.
    uint32_t update(uint32_t raw, uint32_t mask) {
        uint32_t diff = (raw ^ state_) & mask;
        for (uint8_t i = 0; i < 32; ++i) {
            const uint32_t bit = 1u << i;
            if (!(mask & bit)) continue;
            if (diff & bit) {
                if (++count_[i] >= stable_) {
                    state_ ^= bit;
                    count_[i] = 0;
                }
            } else {
                count_[i] = 0;
            }
        }
        return state_;
    }

    // Keep the debounced state but forget every change in progress, so the next change must
    // again hold for `stableSamples` samples from now.
    void restart() {
        for (auto& c : count_) c = 0;
    }

    uint32_t state() const { return state_; }

private:
    uint8_t stable_;
    uint32_t state_;
    uint8_t count_[32] = {};
};

// Quadrature decoder with a transition table. Invalid (double) transitions count 0.
// Emits one step per `edgesPerDetent` valid edges, and re-aligns at the rest states
// 00 and 11 (this encoder has a detent at both), so contact bounce cannot drift it.
class QuadratureDecoder {
public:
    explicit QuadratureDecoder(uint8_t edgesPerDetent = 2) : edges_(edgesPerDetent) {}

    void reset(uint8_t ab) {
        prev_ = ab & 3;
        acc_ = 0;
    }

    // ab = (A << 1) | B, as read (levels, not "pressed"). Returns -1, 0 or +1 detent steps.
    int8_t update(uint8_t ab) {
        // Index = prev * 4 + cur. 00 -> 01 -> 11 -> 10 -> 00 counts +1 per edge.
        static constexpr int8_t kTable[16] = {
            0, +1, -1, 0,
            -1, 0, 0, +1,
            +1, 0, 0, -1,
            0, -1, +1, 0,
        };
        ab &= 3;
        acc_ += kTable[(prev_ << 2) | ab];
        prev_ = ab;
        int8_t step = 0;
        if (acc_ >= edges_) {
            step = +1;
            acc_ -= edges_;
        } else if (acc_ <= -edges_) {
            step = -1;
            acc_ += edges_;
        }
        if (edges_ == 2 && (ab == 0 || ab == 3)) acc_ = 0;   // at a detent: re-align
        return step;
    }

private:
    int8_t edges_;
    uint8_t prev_ = 0;
    int8_t acc_ = 0;
};

// Decides what to do with each timer tick of the sampler.
//
// The repeating timer is scheduled on a fixed grid (each target = previous target + period).
// When interrupts have been off for a while (arduino-pico's LittleFS turns them off for every
// flash erase/program), the timer fires all the missed ticks back to back once interrupts
// return. Those catch-up ticks are microseconds apart and must not count as debounce samples,
// or a bouncing contact could pass the debounce in a fraction of the debounce time.
//
//   Normal : the usual case. Read the chain, debounce, decode.
//   Skip   : do nothing, do not even read the chain. Used for
//              - the first tick after a gap of more than `latePeriods` periods,
//              - every catch-up tick that follows it (less than half a period after the
//                previous tick),
//              - any tick less than half a period after the last sample used.
//   Resync : the first tick at least half a period after the previous tick once a gap was
//            seen, i.e. the first one back on the grid. Read the chain, restart debounce
//            counting, and set the quadrature decoder to the current A/B levels without
//            counting a step (edges during the gap are unknown: a jump of two edges looks
//            like one edge the other way).
// Ordinary interrupt latency (one tick delayed by tens of microseconds) stays Normal.
class SampleGate {
public:
    enum class Kind : uint8_t { Normal, Skip, Resync };

    SampleGate(uint32_t periodUs, uint32_t latePeriods)
        : minUs_(periodUs / 2), lateUs_(periodUs * latePeriods) {}

    void reset(uint32_t nowUs) {
        lastTick_ = lastUsed_ = nowUs;
        inGap_ = false;
    }

    // nowUs: free-running microsecond clock (wraps; differences are taken unsigned).
    Kind classify(uint32_t nowUs) {
        const uint32_t sinceTick = nowUs - lastTick_;
        lastTick_ = nowUs;
        if (sinceTick > lateUs_) {
            inGap_ = true;
            return Kind::Skip;
        }
        if (inGap_) {
            if (sinceTick < minUs_) return Kind::Skip;   // still catching up
            inGap_ = false;
            lastUsed_ = nowUs;
            return Kind::Resync;
        }
        if (nowUs - lastUsed_ < minUs_) return Kind::Skip;
        lastUsed_ = nowUs;
        return Kind::Normal;
    }

private:
    uint32_t minUs_;
    uint32_t lateUs_;
    uint32_t lastTick_ = 0;
    uint32_t lastUsed_ = 0;
    bool inGap_ = false;
};

}  // namespace proto
}  // namespace density
