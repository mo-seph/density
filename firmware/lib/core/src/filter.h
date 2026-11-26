// Per-control analog filter: raw 12-bit reading -> stable position 0..1.
//
// Pipeline, per sample:
//   1. median of the last 5 raw readings (removes one- and two-sample spikes)
//   2. calibrate: (raw - rawMin) / (rawMax - rawMin), clamped to 0..1, optional invert
//   3. low-pass (one-pole, time-based): time constant from FilterParams::smoothing
//      while still, a fast fixed time constant while moving
//   4. snap within kEndSnap of 0 or 1 to exactly 0 or 1
//   5. still/moving state:
//        still:  the reported position is frozen. It becomes "moving" (touched) once the
//                smoothed value differs from the frozen one by more than `deadband`,
//                or the control is pushed onto an end stop it was not quite at.
//        moving: the reported position follows the smoothed value. Every time it moves more
//                than deadband/2 from the last anchor, the settle timer restarts. After
//                settleMs without such movement the control is still again and the
//                position is frozen where it is.
//
// Because a still control reports a constant position, an untouched control can never
// produce a stream of messages, whatever the noise. Time comes from the caller (Hal::micros)
// so behaviour does not depend on the loop rate; tuning assumes about 1 kHz. The median
// window is counted in samples (5 samples = about 4 ms at 1 kHz).
#pragma once
#include "density/model.h"
#include <cstdint>

namespace density {

class Filter {
public:
    // Fast time constant used while the control is moving.
    static constexpr float kMoveTauUs = 1500.0f;
    // Snap the smoothed value to an end stop when this close (about 4 LSB of full travel).
    static constexpr float kEndSnap = 0.001f;
    // A still control pushed onto an end stop counts as touched if its frozen position is
    // further than this from the stop. Smaller than the 7-bit hysteresis margin, so a control
    // resting a hair short of a stop can always be pushed to send 0 or 127.
    static constexpr float kEndTouch = 0.0015f;

    void configure(const FilterParams& p, const AnalogCal& cal, bool invert);
    // New end stops: position is re-derived from the current readings at once, the control
    // is left still (a recalibration is not a touch).
    void setCalibration(const AnalogCal& cal);
    void setInvert(bool invert);
    const AnalogCal& calibration() const { return cal_; }

    // Forget history and start still at this reading.
    void reset(uint16_t raw, uint32_t nowUs);
    // Forget history and start still at the median of the readings seen so far
    // (used at the end of the engine's boot window).
    void resetFromMedian(uint32_t nowUs) { reset(median(), nowUs); }

    // Feed one reading. Returns position(). The first call after construction resets.
    float update(uint16_t raw, uint32_t nowUs);

    float position() const { return out_; }   // reported position 0..1
    float smoothed() const { return s_; }     // internal smoothed value (diagnostics)
    bool moving() const { return moving_; }
    uint16_t raw() const { return lastRaw_; }
    // Raw with one- and two-sample spikes removed (median of the last 5 readings).
    uint16_t rawSmooth() const { return started_ ? median() : lastRaw_; }
    float deadband() const { return params_.deadband; }
    // Where the smoothed value is heading: the calibrated median, snapped to the end stops.
    float target() const;
    bool started() const { return started_; }

private:
    float calibrate(uint16_t raw) const;
    uint16_t median() const;

    FilterParams params_{};
    AnalogCal cal_{};
    bool invert_ = false;
    float tauStillUs_ = 4000.0f;

    bool started_ = false;
    uint16_t hist_[5] = {};
    uint8_t histPos_ = 0;
    uint16_t lastRaw_ = 0;
    uint32_t lastUs_ = 0;

    float s_ = 0;          // smoothed
    float out_ = 0;        // reported
    float anchor_ = 0;     // while moving: where the settle timer was last restarted
    bool moving_ = false;
    uint32_t lastMoveUs_ = 0;
};

}  // namespace density
