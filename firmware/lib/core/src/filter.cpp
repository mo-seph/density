#include "filter.h"

namespace density {

namespace {
inline float absf(float x) { return x < 0 ? -x : x; }
}

void Filter::configure(const FilterParams& p, const AnalogCal& cal, bool invert) {
    params_ = p;
    cal_ = cal;
    invert_ = invert;
    float s = p.smoothing;
    if (s < 0.01f) s = 0.01f;
    // smoothing = share of each new sample at the nominal 1 ms step:
    // alpha(dt) = dt / (tau + dt), so alpha(1 ms) = smoothing  =>  tau = (1/s - 1) ms.
    tauStillUs_ = (s >= 1.0f) ? 0.0f : (1.0f / s - 1.0f) * 1000.0f;
    started_ = false;
}

void Filter::setCalibration(const AnalogCal& cal) {
    cal_ = cal;
    if (!started_) return;
    s_ = calibrate(median());
    if (s_ < kEndSnap) s_ = 0;
    if (s_ > 1.0f - kEndSnap) s_ = 1;
    out_ = anchor_ = s_;
    moving_ = false;
}

void Filter::setInvert(bool invert) {
    if (invert == invert_) return;
    invert_ = invert;
    if (started_) setCalibration(cal_);
}

float Filter::calibrate(uint16_t raw) const {
    float x;
    if (cal_.rawMax > cal_.rawMin) {
        if (raw <= cal_.rawMin) x = 0;
        else if (raw >= cal_.rawMax) x = 1;
        else x = float(raw - cal_.rawMin) / float(cal_.rawMax - cal_.rawMin);
    } else if (cal_.rawMax < cal_.rawMin) {   // reversed end stops: treat as inverted travel
        if (raw >= cal_.rawMin) x = 0;
        else if (raw <= cal_.rawMax) x = 1;
        else x = float(cal_.rawMin - raw) / float(cal_.rawMin - cal_.rawMax);
    } else {
        x = raw > cal_.rawMin ? 1.0f : 0.0f;
    }
    return invert_ ? 1.0f - x : x;
}

uint16_t Filter::median() const {
    uint16_t v[5];
    for (int i = 0; i < 5; ++i) v[i] = hist_[i];
    for (int i = 1; i < 5; ++i) {   // insertion sort, 5 items
        uint16_t k = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; --j; }
        v[j + 1] = k;
    }
    return v[2];
}

float Filter::target() const {
    if (!started_) return out_;
    float x = calibrate(median());
    if (x < kEndSnap) x = 0;
    if (x > 1.0f - kEndSnap) x = 1;
    return x;
}

void Filter::reset(uint16_t raw, uint32_t nowUs) {
    for (auto& h : hist_) h = raw;
    histPos_ = 0;
    lastRaw_ = raw;
    lastUs_ = nowUs;
    started_ = true;
    s_ = calibrate(raw);
    if (s_ < kEndSnap) s_ = 0;
    if (s_ > 1.0f - kEndSnap) s_ = 1;
    out_ = anchor_ = s_;
    moving_ = false;
    lastMoveUs_ = nowUs;
}

float Filter::update(uint16_t raw, uint32_t nowUs) {
    if (!started_) {
        reset(raw, nowUs);
        return out_;
    }
    lastRaw_ = raw;
    hist_[histPos_] = raw;
    histPos_ = uint8_t((histPos_ + 1) % 5);

    uint32_t dtUs = nowUs - lastUs_;   // wraps correctly
    lastUs_ = nowUs;

    const float x = calibrate(median());
    const float tau = moving_ ? (kMoveTauUs < tauStillUs_ ? kMoveTauUs : tauStillUs_) : tauStillUs_;
    const float dt = float(dtUs);
    const float alpha = (tau <= 0.0f || dt >= 1e6f) ? 1.0f : dt / (tau + dt);
    s_ += alpha * (x - s_);
    if (s_ < kEndSnap) s_ = 0;
    if (s_ > 1.0f - kEndSnap) s_ = 1;

    const float db = params_.deadband;
    if (!moving_) {
        const bool beyondDeadband = absf(s_ - out_) > db;
        const bool ontoEndStop = (x == s_) && (s_ == 0.0f || s_ == 1.0f) && absf(s_ - out_) > kEndTouch;
        if (beyondDeadband || ontoEndStop) {
            moving_ = true;
            anchor_ = s_;
            lastMoveUs_ = nowUs;
            out_ = s_;
        }
    } else {
        out_ = s_;
        if (absf(s_ - anchor_) > db * 0.5f || ((s_ == 0.0f || s_ == 1.0f) && s_ != anchor_)) {
            anchor_ = s_;
            lastMoveUs_ = nowUs;
        } else if (uint32_t(nowUs - lastMoveUs_) >= uint32_t(params_.settleMs) * 1000u) {
            moving_ = false;   // out_ stays frozen at its current value
        }
    }
    return out_;
}

}  // namespace density
