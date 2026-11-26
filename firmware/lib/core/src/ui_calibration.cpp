#include "ui_calibration.h"
#include <cstdio>
#include <cstring>

namespace density {

bool calibrationValid(const AnalogCal& c) {
    return c.rawMax <= 4095 && c.rawMin < c.rawMax && c.rawMax - c.rawMin >= kMinCalRange;
}

std::string formatCalibration(const AnalogCal* pedals, const bool* set, uint8_t count) {
    std::string s = "{\"pedals\":[";
    char buf[24];
    for (uint8_t i = 0; i < count; ++i) {
        unsigned lo = set[i] ? pedals[i].rawMin : 0, hi = set[i] ? pedals[i].rawMax : 0;
        snprintf(buf, sizeof buf, "%s[%u,%u]", i ? "," : "", lo, hi);
        s += buf;
    }
    s += "]}\n";
    return s;
}

namespace {

struct Reader {
    const char* p;
    const char* end;
    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    bool eat(char c) { ws(); if (p < end && *p == c) { ++p; return true; } return false; }
    bool peek(char c) { ws(); return p < end && *p == c; }
    bool number(long& out) {
        ws();
        if (p >= end || *p < '0' || *p > '9') return false;
        long v = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            if (v > 100000) return false;
            ++p;
        }
        out = v;
        return true;
    }
};

}  // namespace

bool parseCalibration(const std::string& text, AnalogCal* pedals, bool* set, uint8_t maxCount) {
    for (uint8_t i = 0; i < maxCount; ++i) set[i] = false;
    Reader r{text.data(), text.data() + text.size()};
    const char key[] = "\"pedals\"";
    if (!r.eat('{')) return false;
    r.ws();
    size_t klen = sizeof key - 1;
    if (static_cast<size_t>(r.end - r.p) < klen || memcmp(r.p, key, klen) != 0) return false;
    r.p += klen;
    if (!r.eat(':') || !r.eat('[')) return false;
    unsigned i = 0;
    if (!r.peek(']')) {
        do {
            long lo, hi;
            if (!r.eat('[') || !r.number(lo) || !r.eat(',') || !r.number(hi) || !r.eat(']'))
                return false;
            if (i < maxCount && lo <= 4095 && hi <= 4095) {
                AnalogCal c;
                c.rawMin = static_cast<uint16_t>(lo);
                c.rawMax = static_cast<uint16_t>(hi);
                if (calibrationValid(c)) { pedals[i] = c; set[i] = true; }
            }
            ++i;
        } while (r.eat(','));
    }
    return r.eat(']') && r.eat('}');
}

}  // namespace density
