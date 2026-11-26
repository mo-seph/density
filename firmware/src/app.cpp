// The real application: forwards the board hooks to the portable density::App.
// Left out in the hardware-test build, where the weak stubs in app_stub.cpp take over.
#ifndef DENSITY_HW_TEST
#include "app.h"
#include "density_app.h"

static density::App* app = nullptr;

void appSetup(density::Hal& hal) {
    app = new density::App(hal);
    app->setup();
}

void appLoop(density::Hal&) { app->loop(); }
#endif
