// Hardware test mode. Exercises every input and output through density::Hal plus a few
// board diagnostics, so a fresh board can be checked without the core.
//
// LEDs   : knob i sets the brightness of pad i (each pad its own hue, so a wrong chain map
//          shows up as colours in the wrong place). Pads 16/17/18 follow the fader and the two
//          pedals; pad 19 stays dim. A pressed pad is solid white. The encoder moves a
//          blinking white highlight. The encoder switch cycles the LED mode:
//            0 normal, 1 pad walk (pads light one at a time in pad order, BTN1 first; display
//              0 names the lit pad), 2 colour order (all LEDs pure red, green, blue, a second
//              each; display 0 names the colour that should be seen), 3 current-cap test (all
//              20 full white; the driver must scale it to the cap).
// Display 0: raw values of all 16 knobs, fader, pedals, encoder count and the direction of
//          the last step (clockwise should be +), loop rate, LED mA.
// Display 1: chain check (spare bit 1 and 25th bit 0), pad grid in board layout (filled =
//          pressed), raw shift-register word, MIDI state, MIDI messages dropped, last message in.
// MIDI   : pad press/release -> note 36+pad on/off, channel 1, velocity 127.
//          analog a moving more than kRawThreshold -> CC 20+a, channel 1, value raw/32.
//          encoder detent -> CC 50, value 65 (clockwise) or 63 (anticlockwise), relative.
// Serial : one line per pad press/release, analog change, encoder step, MIDI in, and a
//          status line every 5 s.
#include "hw_test.h"

#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "board/board.h"

using namespace density;
using namespace density::proto;

namespace {

constexpr uint16_t kRawThreshold = 48;      // ~1.2 % of travel before a change is reported
constexpr uint8_t kPadNoteBase = 36;
constexpr uint8_t kAnalogCcBase = 20;
constexpr uint8_t kEncoderCc = 50;
constexpr uint32_t kDisplayPeriodMs = 40;   // one display flushed every 40 ms (each ~12 Hz)
constexpr uint32_t kStatusPeriodMs = 5000;
constexpr uint32_t kWalkStepMs = 400;
constexpr uint32_t kColourStepMs = 1000;
// Colour-order test level (linear; gamma makes it about 150 of 255). Even at 255 one channel
// on all 20 LEDs estimates 20 + 20 x 12 = 260 mA, under the cap, so this is never scaled.
constexpr uint8_t kColourLevel = 200;
static_assert(kLedIdleMa * kNumPads + kNumPads * kLedMaPerChannel <= kLedCurrentCapMa,
              "one full channel on every LED must stay under the LED current cap");

enum Mode : uint8_t { kModeNormal, kModeWalk, kModeColour, kModeCap, kNumModes };
const char* const kModeNames[kNumModes] = {"0 normal", "1 pad walk", "2 colour order", "3 cap test"};
const char* const kColourNames[3] = {"RED", "GREEN", "BLUE"};

uint16_t gLastRaw[kNumAnalog];
uint8_t gLastCc[kNumAnalog];
bool gLastPad[kNumPads];
bool gLastEncSw = false;
int32_t gEncPos = 0;
uint8_t gMode = 0;
int8_t gLastDir = 0;        // sign of the last encoder step
char gLastIn[24] = "-";
ChainCheck gChain;
int8_t gLastColour = -1;
uint32_t gLastDisplayMs = 0;
uint8_t gNextDisplay = 0;
uint32_t gLastStatusMs = 0;
uint32_t gLoops = 0;
uint32_t gLoopHz = 0;
uint32_t gRateStartMs = 0;
uint32_t gSamplesAtRate = 0;
uint32_t gSrHz = 0;

Rgb hsv(float h, float s, float v) {
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;
    const float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    auto u = [](float f) { return uint8_t(std::lround(std::fmin(std::fmax(f, 0.0f), 1.0f) * 255)); };
    return Rgb{u(r + m), u(g + m), u(b + m)};
}

const char* analogName(AnalogId a, char* buf, size_t n) {
    if (a < kNumKnobs) snprintf(buf, n, "knob %u (RV%u)", a, a + 1);
    else if (a == kNumKnobs) snprintf(buf, n, "fader");
    else snprintf(buf, n, "pedal %u", a - kNumKnobs);
    return buf;
}

PadId walkPad(uint32_t now) { return PadId((now / kWalkStepMs) % kNumPads); }
uint8_t colourPhase(uint32_t now) { return uint8_t((now / kColourStepMs) % 3); }

void updateLeds(BoardHal& hal, uint32_t now) {
    const bool blink = (now / 150) & 1;
    const uint8_t highlight = uint8_t(((gEncPos % kNumPads) + kNumPads) % kNumPads);
    for (PadId p = 0; p < kNumPads; ++p) {
        Rgb c;
        if (gMode == kModeCap) {
            c = Rgb{255, 255, 255};
        } else if (gMode == kModeColour) {
            const uint8_t ph = colourPhase(now);
            c = Rgb{uint8_t(ph == 0 ? kColourLevel : 0), uint8_t(ph == 1 ? kColourLevel : 0),
                    uint8_t(ph == 2 ? kColourLevel : 0)};
        } else if (gMode == kModeWalk) {
            c = (p == walkPad(now)) ? hsv(p * 18.0f, 1, 1) : Rgb{};
        } else {
            float level = 0.1f;
            if (p < kNumKnobs) level = hal.analogRaw(p) / 4095.0f;
            else if (p < kNumKnobs + kNumFaders + kNumPedals) level = hal.analogRaw(p) / 4095.0f;
            c = hsv(p * 18.0f, 1, level);
            if (p == highlight && blink) c = Rgb{200, 200, 200};
        }
        if (hal.padPressed(p) && gMode != kModeColour) c = Rgb{255, 255, 255};
        hal.setLed(p, c);
    }
    hal.showLeds();
}

// Header: encoder position, sign of the last step ("last+" / "last-"), and the expectation.
void drawEncoderHeader(BoardHal& hal, Display& d) {
    char s[32];
    snprintf(s, sizeof s, "enc%+5ld last%c CW=+ %c", (long)gEncPos,
             gLastDir > 0 ? '+' : gLastDir < 0 ? '-' : '0', hal.encoderPressed(0) ? 'S' : ' ');
    d.text(0, 0, s, 1, true);
}

// Pad walk and colour order: say in large text what should be lit.
void drawBigScreen(BoardHal& hal, uint32_t now) {
    Display& d = hal.display(0);
    char s[32];
    d.clear();
    drawEncoderHeader(hal, d);
    if (gMode == kModeWalk) {
        const PadId p = walkPad(now);
        d.text(0, 10, "PAD WALK, lit:");
        snprintf(s, sizeof s, "BTN %u", p + 1);
        d.text(0, 22, s, 3);
        snprintf(s, sizeof s, "chain pos %u", kPadToChain.v[p] + 1);
        d.text(0, 49, s);
    } else {
        d.text(0, 10, "ALL LEDs should be:");
        d.text(0, 22, kColourNames[colourPhase(now)], 3);
        d.text(0, 49, "else: not GRB order");
    }
    snprintf(s, sizeof s, "LED %3lu>%3lumA m%u", (unsigned long)hal.leds().lastEstimateMa(),
             (unsigned long)hal.leds().lastCappedMa(), gMode);
    d.text(0, 57, s);
}

void drawDisplay0(BoardHal& hal, uint32_t now) {
    if (gMode == kModeWalk || gMode == kModeColour) return drawBigScreen(hal, now);
    Display& d = hal.display(0);
    char s[32];
    d.clear();
    drawEncoderHeader(hal, d);
    for (uint8_t row = 0; row < 4; ++row) {
        snprintf(s, sizeof s, "%4u %4u %4u %4u", hal.analogRaw(row * 4), hal.analogRaw(row * 4 + 1),
                 hal.analogRaw(row * 4 + 2), hal.analogRaw(row * 4 + 3));
        d.text(0, 9 + row * 8, s);
    }
    snprintf(s, sizeof s, "F %4u P1 %4u P2 %4u", hal.analogRaw(16), hal.analogRaw(17), hal.analogRaw(18));
    d.text(0, 41, s);
    snprintf(s, sizeof s, "%4luHz poll%4luus m%u", (unsigned long)gLoopHz,
             (unsigned long)hal.stats().pollUs, gMode);
    d.text(0, 49, s);
    snprintf(s, sizeof s, "LED %3lu>%3lumA SR%4lu", (unsigned long)hal.leds().lastEstimateMa(),
             (unsigned long)hal.leds().lastCappedMa(), (unsigned long)gSrHz);
    d.text(0, 57, s);
}

void drawDisplay1(BoardHal& hal) {
    Display& d = hal.display(1);
    char s[32];
    d.clear();
    gChain = InputSampler::chainCheck();   // ~8 us with interrupts off, ~12 times a second
    snprintf(s, sizeof s, " BTN chain %-5s D0%s", gChain.ok() ? "OK" : "FAULT", hal.displayPresent(0) ? "ok" : "--");
    d.text(0, 0, s, 1, true);
    // BTN1..16: 4 x 4, left to right, top to bottom (as the pots). BTN17..20: column of four.
    for (PadId p = 0; p < kNumPads; ++p) {
        int x, y;
        if (p < 16) { x = (p % 4) * 13; y = 10 + (p / 4) * 11; }
        else { x = 58; y = 10 + (p - 16) * 11; }
        d.rect(int16_t(x), int16_t(y), 11, 9, hal.padPressed(p));
    }
    const uint32_t raw = hal.inputs().raw;
    snprintf(s, sizeof s, "%06lX", (unsigned long)raw);
    d.text(74, 10, s);
    // Spare bit (want 1) and 25th bit (want 0) from the chain check.
    snprintf(s, sizeof s, "sp%d x%d%s", gChain.spareBit ? 1 : 0, gChain.extraBit ? 1 : 0, gChain.ok() ? "" : "!");
    d.text(74, 19, s);
    snprintf(s, sizeof s, "A%luB%luS%lu", (unsigned long)((raw >> kBitEncA) & 1u),
             (unsigned long)((raw >> kBitEncB) & 1u), (unsigned long)((raw >> kBitEncSw) & 1u));
    d.text(74, 28, s);
    d.text(74, 37, hal.midiMounted() ? "MIDI ok" : "MIDI --");
    snprintf(s, sizeof s, "drop%lu", (unsigned long)hal.stats().midiDropped);
    d.text(74, 46, s);
    snprintf(s, sizeof s, "in %s", gLastIn);
    d.text(0, 56, s);
}

}  // namespace

void hwTestSetup() {
    BoardHal& hal = boardHal();
    hal.poll();
    for (AnalogId a = 0; a < kNumAnalog; ++a) {
        gLastRaw[a] = hal.analogRaw(a);
        gLastCc[a] = uint8_t(gLastRaw[a] >> 5);
    }
    for (PadId p = 0; p < kNumPads; ++p) gLastPad[p] = hal.padPressed(p);
    gLastEncSw = hal.encoderPressed(0);
    gRateStartMs = gLastStatusMs = millis();
    gSamplesAtRate = hal.inputs().samples;

    char line[128];
    hal.log("Hardware test mode. Encoder switch cycles LED mode: 0 normal, 1 pad walk, 2 colour order, 3 cap test");
    gChain = InputSampler::chainCheck();
    snprintf(line, sizeof line, "chain check: spare bit %d (want 1), 25th bit %d (want 0): %s",
             gChain.spareBit ? 1 : 0, gChain.extraBit ? 1 : 0, gChain.ok() ? "chain OK" : "chain FAULT");
    hal.log(line);
    for (AnalogId a = 0; a < kNumAnalog; ++a) {
        char name[20];
        snprintf(line, sizeof line, "boot %s raw %u", analogName(a, name, sizeof name), gLastRaw[a]);
        hal.log(line);
    }
}

void hwTestLoop() {
    BoardHal& hal = boardHal();
    hal.poll();
    const uint32_t now = hal.millis();
    char line[176];
    char name[20];

    // Pads.
    for (PadId p = 0; p < kNumPads; ++p) {
        const bool down = hal.padPressed(p);
        if (down == gLastPad[p]) continue;
        gLastPad[p] = down;
        hal.midiSend(MidiMsg{MidiType::Note, 1, uint8_t(kPadNoteBase + p), uint8_t(down ? 127 : 0)});
        snprintf(line, sizeof line, "pad %u (BTN%u) %s -> note %u %s", p, p + 1, down ? "down" : "up",
                 kPadNoteBase + p, down ? "on" : "off");
        hal.log(line);
    }

    // Analog.
    for (AnalogId a = 0; a < kNumAnalog; ++a) {
        const uint16_t r = hal.analogRaw(a);
        if (std::abs(int(r) - int(gLastRaw[a])) < kRawThreshold) continue;
        gLastRaw[a] = r;
        const uint8_t v = uint8_t(r >> 5);
        if (v != gLastCc[a]) {
            gLastCc[a] = v;
            hal.midiSend(MidiMsg{MidiType::CC, 1, uint8_t(kAnalogCcBase + a), v});
        }
        snprintf(line, sizeof line, "%s raw %4u -> CC %u = %u", analogName(a, name, sizeof name), r,
                 kAnalogCcBase + a, v);
        hal.log(line);
    }

    // Encoder.
    const int8_t steps = hal.encoderSteps(0);
    if (steps) {
        gEncPos += steps;
        gLastDir = steps > 0 ? 1 : -1;
        const int n = steps > 0 ? steps : -steps;
        for (int i = 0; i < n; ++i)
            hal.midiSend(MidiMsg{MidiType::CC, 1, kEncoderCc, uint8_t(steps > 0 ? 65 : 63)});
        snprintf(line, sizeof line, "encoder %+d -> position %ld (%s)", steps, (long)gEncPos,
                 steps > 0 ? "counts up: expected for clockwise"
                           : "counts down: if this was clockwise, set kEncoderReverse = true");
        hal.log(line);
    }
    const bool sw = hal.encoderPressed(0);
    if (sw != gLastEncSw) {
        gLastEncSw = sw;
        if (sw) {
            gMode = uint8_t((gMode + 1) % kNumModes);
            gLastColour = -1;
        }
        snprintf(line, sizeof line, "encoder switch %s%s%s", sw ? "down" : "up", sw ? ", LED mode " : "",
                 sw ? kModeNames[gMode] : "");
        hal.log(line);
    }

    // MIDI in.
    MidiMsg in;
    while (hal.midiReceive(in)) {
        snprintf(gLastIn, sizeof gLastIn, "%s%u #%u=%u", in.type == MidiType::CC ? "CC" : "N", in.channel,
                 in.number, in.value);
        snprintf(line, sizeof line, "midi in %s ch %u #%u = %u", in.type == MidiType::CC ? "CC" : "note",
                 in.channel, in.number, in.value);
        hal.log(line);
    }

    if (gMode == kModeColour && colourPhase(now) != gLastColour) {
        gLastColour = int8_t(colourPhase(now));
        snprintf(line, sizeof line, "colour order: every LED should now be pure %s", kColourNames[gLastColour]);
        hal.log(line);
    }

    updateLeds(hal, now);

    // Displays: draw and flush one per period, alternating.
    if (now - gLastDisplayMs >= kDisplayPeriodMs) {
        gLastDisplayMs = now;
        if (gNextDisplay == 0) drawDisplay0(hal, now);
        else drawDisplay1(hal);
        hal.flushDisplay(gNextDisplay);
        gNextDisplay ^= 1;
    }

    // Rates and status.
    ++gLoops;
    if (now - gRateStartMs >= 1000) {
        const uint32_t dt = now - gRateStartMs;
        gLoopHz = gLoops * 1000 / dt;
        gSrHz = (hal.inputs().samples - gSamplesAtRate) * 1000 / dt;
        gSamplesAtRate = hal.inputs().samples;
        gLoops = 0;
        gRateStartMs = now;
    }
    if (now - gLastStatusMs >= kStatusPeriodMs) {
        gLastStatusMs = now;
        const ProtoStats& st = hal.stats();
        snprintf(line, sizeof line,
                 "status: loop %lu Hz, poll %lu us (max %lu, scan %lu), SR %lu Hz, LED %lu>%lu mA",
                 (unsigned long)gLoopHz, (unsigned long)st.pollUs, (unsigned long)st.pollUsMax,
                 (unsigned long)st.scanUs, (unsigned long)gSrHz, (unsigned long)hal.leds().lastEstimateMa(),
                 (unsigned long)hal.leds().lastCappedMa());
        hal.log(line);
        snprintf(line, sizeof line,
                 "status: MIDI %s, dropped midi %lu (superseded CC %lu) log %lu, frames D0 %lu D1 %lu, input resyncs %lu",
                 hal.midiMounted() ? "mounted" : "not mounted", (unsigned long)st.midiDropped,
                 (unsigned long)st.midiSuperseded, (unsigned long)st.logDropped,
                 (unsigned long)oled_core1::framesPushed(0), (unsigned long)oled_core1::framesPushed(1),
                 (unsigned long)hal.inputs().resyncs);
        hal.log(line);
        hal.resetStats();
    }
}
