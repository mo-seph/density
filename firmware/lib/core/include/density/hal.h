// The hardware interface. Core code sees a board only through this.
// Implemented per board in src/board/, and by test/support/fake_hal.h for native tests.
#pragma once
#include "types.h"
#include <string>

namespace density {

// What the board has. Fixed at compile time by the board's table.
struct HardwareInfo {
    const char* name = "";
    uint8_t numKnobs = 0, numFaders = 0, numPedals = 0;
    uint8_t numPads = 0, numEncoders = 0, numDisplays = 0;
    // padUnder[a] = pad physically paired with analog control a, or kNone.
    uint8_t padUnder[kMaxAnalog];

    HardwareInfo() { for (auto& p : padUnder) p = kNone; }

    uint8_t numAnalog() const { return numKnobs + numFaders + numPedals; }
    AnalogKind kindOf(AnalogId a) const {
        if (a < numKnobs) return AnalogKind::Knob;
        if (a < numKnobs + numFaders) return AnalogKind::Fader;
        return AnalogKind::Pedal;
    }
    // Index within its own kind: fader 0, pedal 1, ...
    uint8_t indexInKind(AnalogId a) const {
        if (a < numKnobs) return a;
        if (a < numKnobs + numFaders) return a - numKnobs;
        return a - numKnobs - numFaders;
    }
};

// A monochrome framebuffer display. (0,0) is top left. Drawing only changes the buffer;
// the board decides when to push it to the glass (see Hal::flushDisplay).
class Display {
public:
    virtual ~Display() = default;
    virtual uint8_t width() const = 0;
    virtual uint8_t height() const = 0;
    virtual void clear() = 0;
    // size 1 = 6x8 pixel cells, size 2 = 12x16. Text is clipped, not wrapped.
    virtual void text(int16_t x, int16_t y, const char* s, uint8_t size = 1, bool inverted = false) = 0;
    virtual void rect(int16_t x, int16_t y, int16_t w, int16_t h, bool filled) = 0;
    virtual void hline(int16_t x, int16_t y, int16_t w) = 0;
    virtual void vline(int16_t x, int16_t y, int16_t h) = 0;
};

class Hal {
public:
    virtual ~Hal() = default;
    virtual const HardwareInfo& info() const = 0;

    // Time since boot.
    virtual uint32_t micros() = 0;
    virtual uint32_t millis() = 0;

    // Called exactly once per main-loop pass by the application (density::App::loop). Scans analog inputs, latches button and encoder
    // state gathered since the last call, services USB.
    virtual void poll() = 0;

    // Latest raw reading, 0..4095, unfiltered. Unplugged pedals read about 0.
    virtual uint16_t analogRaw(AnalogId a) = 0;

    // Debounced state, true = pressed.
    virtual bool padPressed(PadId p) = 0;

    // Detent steps since the previous call for this encoder (+ = clockwise). Clears on read.
    virtual int8_t encoderSteps(uint8_t enc) = 0;
    virtual bool encoderPressed(uint8_t enc) = 0;   // debounced

    // LEDs: set any number, then showLeds() once per pass. The driver applies gamma
    // and the total-current cap; core code passes plain linear RGB.
    virtual void setLed(PadId p, Rgb c) = 0;
    virtual void showLeds() = 0;

    // Displays. display(i) is never null for i < numDisplays, even if the module is absent.
    virtual Display& display(uint8_t i) = 0;
    virtual void flushDisplay(uint8_t i) = 0;   // may block for ms, or return at once and push in the background

    // MIDI. midiReceive returns false when nothing is waiting.
    virtual void midiSend(const MidiMsg& m) = 0;
    virtual bool midiReceive(MidiMsg& out) = 0;

    // Small files on the board's flash. Paths start with '/'.
    virtual bool readFile(const char* path, std::string& out) = 0;
    virtual bool writeFile(const char* path, const std::string& data) = 0;

    // Debug text (USB serial on a board, stdout in tests).
    virtual void log(const char* msg) = 0;
};

}  // namespace density
