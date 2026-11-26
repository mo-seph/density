// Two SSD1306 128x64 on I2C1. Drawing happens on core 0 into a 1 KB framebuffer
// (Adafruit GFX canvas). flushDisplay() copies that buffer (about 1 us) and hands it to
// core 1, which owns I2C1 and pushes it with Adafruit_SSD1306 (~25 ms per frame at
// 400 kHz). The main loop never waits for I2C.
#pragma once
#include <cstdint>
#include "density/hal.h"
#include "board_table.h"

class GFXcanvas1;

namespace density {
namespace proto {

class OledDisplay : public Display {
public:
    explicit OledDisplay(uint8_t index) : index_(index) {}
    bool begin();   // allocates the framebuffer
    void setPresent(bool p) { present_ = p; }
    bool present() const { return present_; }

    uint8_t width() const override { return kDisplayWidth; }
    uint8_t height() const override { return kDisplayHeight; }
    void clear() override;
    void text(int16_t x, int16_t y, const char* s, uint8_t size = 1, bool inverted = false) override;
    void rect(int16_t x, int16_t y, int16_t w, int16_t h, bool filled) override;
    void hline(int16_t x, int16_t y, int16_t w) override;
    void vline(int16_t x, int16_t y, int16_t h) override;

    // Copies the framebuffer for core 1 to push. Never blocks on I2C.
    void flush();

private:
    uint8_t index_;
    bool present_ = false;
    GFXcanvas1* canvas_ = nullptr;
};

// Core 1 side. start() is called from core 0's Hal::begin() and returns once core 1
// has probed both addresses (or after a timeout); then present(i) is valid.
namespace oled_core1 {
void start();                       // release core 1 to initialise I2C and probe
bool waitReady(uint32_t timeoutMs);
bool present(uint8_t i);
uint32_t framesPushed(uint8_t i);   // diagnostics
void setup1();                      // run on core 1 (from ::setup1)
void loop1();                       // run on core 1 (from ::loop1)
}  // namespace oled_core1

}  // namespace proto
}  // namespace density
