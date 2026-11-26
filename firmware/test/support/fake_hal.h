// A Hal for native tests. Header-only, no dependencies beyond the standard library.
//
// Everything is public and settable: tests poke inputs (time, raw analog, pads,
// encoders, received MIDI) and read outputs (sent MIDI, LEDs, displays, files, log).
//
//   density::FakeHal hal;              // prototype layout by default
//   hal.setAnalog(3, 2048);
//   hal.advance(1);                     // +1 ms
//   engine.update();
//   TEST_ASSERT_EQUAL(1, hal.sent.size());
#pragma once
#include "density/hal.h"
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace density {

// Records drawing since the last clear(). Text items keep their position and style.
class FakeDisplay : public Display {
public:
    struct TextItem {
        int16_t x, y;
        std::string s;
        uint8_t size;
        bool inverted;
    };
    struct RectItem {
        int16_t x, y, w, h;
        bool filled;
    };

    uint8_t w = 128, h = 64;
    std::vector<TextItem> textItems;
    std::vector<RectItem> rects;
    int hlines = 0, vlines = 0;
    int clears = 0;
    int flushes = 0;   // incremented by FakeHal::flushDisplay

    uint8_t width() const override { return w; }
    uint8_t height() const override { return h; }
    void clear() override {
        textItems.clear();
        rects.clear();
        hlines = vlines = 0;
        ++clears;
    }
    void text(int16_t x, int16_t y, const char* s, uint8_t size = 1, bool inverted = false) override {
        textItems.push_back({x, y, s ? s : "", size, inverted});
    }
    void rect(int16_t x, int16_t y, int16_t rw, int16_t rh, bool filled) override {
        rects.push_back({x, y, rw, rh, filled});
    }
    void hline(int16_t, int16_t, int16_t) override { ++hlines; }
    void vline(int16_t, int16_t, int16_t) override { ++vlines; }

    // Just the strings, in drawing order.
    std::vector<std::string> texts() const {
        std::vector<std::string> out;
        for (const auto& t : textItems) out.push_back(t.s);
        return out;
    }
    // True if any drawn string contains `needle`.
    bool shows(const char* needle) const {
        for (const auto& t : textItems)
            if (t.s.find(needle) != std::string::npos) return true;
        return false;
    }
    // All strings joined with '\n' (handy in assertion messages).
    std::string allText() const {
        std::string out;
        for (const auto& t : textItems) {
            if (!out.empty()) out += '\n';
            out += t.s;
        }
        return out;
    }
};

class FakeHal : public Hal {
public:
    HardwareInfo hw;
    uint64_t timeUs = 0;

    uint16_t raw[kMaxAnalog] = {};
    bool pads[kMaxPads] = {};
    int8_t encSteps[kMaxEncoders] = {};
    bool encPress[kMaxEncoders] = {};

    Rgb leds[kMaxPads] = {};
    int showCount = 0;
    int pollCount = 0;

    FakeDisplay displays[kMaxDisplays];

    std::vector<MidiMsg> sent;
    std::deque<MidiMsg> incoming;

    std::map<std::string, std::string> files;
    std::vector<std::string> logs;
    bool echoLog = false;   // also print log() to stdout

    FakeHal() { setPrototype(); }

    // 16 knobs, 1 fader, 2 pedals, 20 pads, 1 encoder, 2 displays; knob i over pad i.
    void setPrototype() {
        hw = HardwareInfo{};
        hw.name = "fake_proto";
        hw.numKnobs = 16;
        hw.numFaders = 1;
        hw.numPedals = 2;
        hw.numPads = 20;
        hw.numEncoders = 1;
        hw.numDisplays = 2;
        for (uint8_t a = 0; a < kMaxAnalog; ++a) hw.padUnder[a] = (a < 16) ? a : kNone;
    }

    // ---- test-side controls ----
    void setTimeMs(uint32_t ms) { timeUs = uint64_t(ms) * 1000; }
    void setTimeUs(uint64_t us) { timeUs = us; }
    void advance(uint32_t ms) { timeUs += uint64_t(ms) * 1000; }
    void advanceUs(uint32_t us) { timeUs += us; }
    void setAnalog(AnalogId a, uint16_t v) { if (a < kMaxAnalog) raw[a] = v; }
    void setAllAnalog(uint16_t v) { for (auto& r : raw) r = v; }
    void setPad(PadId p, bool down) { if (p < kMaxPads) pads[p] = down; }
    void turn(uint8_t enc, int8_t steps) { if (enc < kMaxEncoders) encSteps[enc] += steps; }
    void setEncoderPressed(uint8_t enc, bool down) { if (enc < kMaxEncoders) encPress[enc] = down; }
    void inject(const MidiMsg& m) { incoming.push_back(m); }
    void injectCC(uint8_t ch, uint8_t num, uint8_t val) { inject({MidiType::CC, ch, num, val}); }
    void injectNote(uint8_t ch, uint8_t num, uint8_t vel) { inject({MidiType::Note, ch, num, vel}); }
    void clearSent() { sent.clear(); }
    FakeDisplay& fakeDisplay(uint8_t i) { return displays[i < kMaxDisplays ? i : 0]; }

    // ---- Hal ----
    const HardwareInfo& info() const override { return hw; }
    uint32_t micros() override { return uint32_t(timeUs); }
    uint32_t millis() override { return uint32_t(timeUs / 1000); }
    void poll() override { ++pollCount; }
    uint16_t analogRaw(AnalogId a) override { return a < kMaxAnalog ? raw[a] : 0; }
    bool padPressed(PadId p) override { return p < kMaxPads ? pads[p] : false; }
    int8_t encoderSteps(uint8_t enc) override {
        if (enc >= kMaxEncoders) return 0;
        int8_t s = encSteps[enc];
        encSteps[enc] = 0;
        return s;
    }
    bool encoderPressed(uint8_t enc) override { return enc < kMaxEncoders ? encPress[enc] : false; }
    void setLed(PadId p, Rgb c) override { if (p < kMaxPads) leds[p] = c; }
    void showLeds() override { ++showCount; }
    Display& display(uint8_t i) override { return displays[i < kMaxDisplays ? i : 0]; }
    void flushDisplay(uint8_t i) override { displays[i < kMaxDisplays ? i : 0].flushes++; }
    void midiSend(const MidiMsg& m) override { sent.push_back(m); }
    bool midiReceive(MidiMsg& out) override {
        if (incoming.empty()) return false;
        out = incoming.front();
        incoming.pop_front();
        return true;
    }
    bool readFile(const char* path, std::string& out) override {
        auto it = files.find(path);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
    bool writeFile(const char* path, const std::string& data) override {
        files[path] = data;
        return true;
    }
    void log(const char* msg) override {
        logs.push_back(msg ? msg : "");
        if (echoLog) std::printf("[log] %s\n", msg ? msg : "");
    }
};

}  // namespace density
