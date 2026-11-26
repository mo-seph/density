#include "oled.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <pico/mutex.h>
#include <cstring>

namespace density {
namespace proto {

namespace {
constexpr size_t kFbBytes = size_t(kDisplayWidth) * kDisplayHeight / 8;   // 1024

auto_init_mutex(gShadowMutex);
uint8_t gShadow[kNumDisplays][kFbBytes];
volatile bool gPending[kNumDisplays] = {};
volatile bool gStart = false;
volatile bool gReady = false;
volatile bool gPresent[kNumDisplays] = {};
volatile uint32_t gFrames[kNumDisplays] = {};
Adafruit_SSD1306* gSsd[kNumDisplays] = {};   // core 1 only

// GFXcanvas1 layout (row-major, MSB = leftmost pixel) -> SSD1306 layout (8-pixel pages,
// LSB = top row of the page).
void canvasToSsd(const uint8_t* src, uint8_t* dst) {
    constexpr int kRowBytes = kDisplayWidth / 8;
    for (int page = 0; page < kDisplayHeight / 8; ++page) {
        for (int x = 0; x < kDisplayWidth; ++x) {
            const uint8_t mask = 0x80 >> (x & 7);
            const uint8_t* col = src + (page * 8) * kRowBytes + (x >> 3);
            uint8_t b = 0;
            for (int bit = 0; bit < 8; ++bit)
                if (col[bit * kRowBytes] & mask) b |= uint8_t(1u << bit);
            dst[page * kDisplayWidth + x] = b;
        }
    }
}
}  // namespace

// ---------------------------------------------------------------- core 0: drawing
bool OledDisplay::begin() {
    if (!canvas_) canvas_ = new GFXcanvas1(kDisplayWidth, kDisplayHeight);
    if (!canvas_ || !canvas_->getBuffer()) return false;
    canvas_->setTextWrap(false);
    canvas_->fillScreen(0);
    return true;
}

void OledDisplay::clear() {
    if (present_ && canvas_) canvas_->fillScreen(0);
}

void OledDisplay::text(int16_t x, int16_t y, const char* s, uint8_t size, bool inverted) {
    if (!present_ || !canvas_ || !s) return;
    canvas_->setTextSize(size ? size : 1);   // size 1 = 6x8 cells, 2 = 12x16 (GFX classic font)
    canvas_->setTextColor(inverted ? 0 : 1, inverted ? 1 : 0);   // with a background: solid cells
    canvas_->setCursor(x, y);
    canvas_->print(s);
}

void OledDisplay::rect(int16_t x, int16_t y, int16_t w, int16_t h, bool filled) {
    if (!present_ || !canvas_) return;
    if (filled) canvas_->fillRect(x, y, w, h, 1);
    else canvas_->drawRect(x, y, w, h, 1);
}

void OledDisplay::hline(int16_t x, int16_t y, int16_t w) {
    if (present_ && canvas_) canvas_->drawFastHLine(x, y, w, 1);
}

void OledDisplay::vline(int16_t x, int16_t y, int16_t h) {
    if (present_ && canvas_) canvas_->drawFastVLine(x, y, h, 1);
}

void OledDisplay::flush() {
    if (!present_ || !canvas_) return;
    mutex_enter_blocking(&gShadowMutex);   // core 1 holds it only for a 1 KB memcpy
    std::memcpy(gShadow[index_], canvas_->getBuffer(), kFbBytes);
    gPending[index_] = true;
    mutex_exit(&gShadowMutex);
}

// ---------------------------------------------------------------- core 1: I2C
namespace oled_core1 {

void start() { gStart = true; }

bool waitReady(uint32_t timeoutMs) {
    const uint32_t t0 = millis();
    while (!gReady && millis() - t0 < timeoutMs) delay(1);
    return gReady;
}

bool present(uint8_t i) { return i < kNumDisplays && gPresent[i]; }
uint32_t framesPushed(uint8_t i) { return i < kNumDisplays ? gFrames[i] : 0; }

void setup1() {
    while (!gStart) delay(1);
    Wire1.setSDA(kPinSda);
    Wire1.setSCL(kPinScl);
    Wire1.setClock(kI2cHz);
    Wire1.begin();
    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        Wire1.beginTransmission(kDisplayAddr[i]);
        const bool ack = Wire1.endTransmission() == 0;
        if (!ack) continue;
        auto* ssd = new Adafruit_SSD1306(kDisplayWidth, kDisplayHeight, &Wire1, -1, kI2cHz, kI2cHz);
        // reset = false (no reset pin), periphBegin = false (Wire1 already started).
        if (ssd && ssd->begin(SSD1306_SWITCHCAPVCC, kDisplayAddr[i], false, false)) {
            ssd->clearDisplay();
            ssd->display();
            gSsd[i] = ssd;
            gPresent[i] = true;
        }
        // On failure the object is left allocated (Adafruit_SSD1306 has no virtual
        // destructor); it happens at most once per display, at boot.
    }
    gReady = true;
}

void loop1() {
    static uint8_t local[kFbBytes];
    bool any = false;
    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        if (!gPending[i] || !gSsd[i]) continue;
        mutex_enter_blocking(&gShadowMutex);
        std::memcpy(local, gShadow[i], kFbBytes);
        gPending[i] = false;
        mutex_exit(&gShadowMutex);
        canvasToSsd(local, gSsd[i]->getBuffer());
        gSsd[i]->display();   // ~25 ms at 400 kHz; only core 1 waits
        gFrames[i] = gFrames[i] + 1;
        any = true;
    }
    if (!any) delay(1);
}

}  // namespace oled_core1

}  // namespace proto
}  // namespace density
