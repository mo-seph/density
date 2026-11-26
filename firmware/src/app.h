// Integration hook between the board glue (main.cpp) and the application (the core:
// engine, LEDs, config loader, UI).
//
// main.cpp calls, in order:
//   hal.begin()        USB names + MIDI, LittleFS mounted, ADC, inputs, LEDs, displays
//   appSetup(hal)      once
//   appLoop(hal)       forever, from Arduino loop()
//
// appLoop owns the loop pass: it must call hal.poll() exactly once per pass (main.cpp does
// not), then run the engine, call hal.showLeds() and flush at most one display.
//
// src/app_stub.cpp holds weak placeholder definitions that run the hardware test. Replace
// them during integration by defining these two functions (strong symbols) in another file,
// or by replacing app_stub.cpp.
#pragma once
#include "density/hal.h"

void appSetup(density::Hal& hal);
void appLoop(density::Hal& hal);
