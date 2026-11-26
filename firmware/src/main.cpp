// Board glue: build the Hal, bring the hardware up, hand over to the application.
// Core 0 runs setup()/loop(); core 1 runs setup1()/loop1(), which only push display
// framebuffers over I2C1 (see board/density_proto/oled.h).
#include <Arduino.h>
#include "app.h"
#include "board/board.h"
#include "hw_test/hw_test.h"

static BoardHal gHal;
BoardHal& boardHal() { return gHal; }

void setup() {
    gHal.begin();   // also mounts LittleFS
#ifdef DENSITY_HW_TEST
    hwTestSetup();
#else
    appSetup(gHal);
#endif
}

void loop() {
#ifdef DENSITY_HW_TEST
    hwTestLoop();
#else
    appLoop(gHal);
#endif
}

void setup1() { density::proto::oled_core1::setup1(); }
void loop1() { density::proto::oled_core1::loop1(); }
