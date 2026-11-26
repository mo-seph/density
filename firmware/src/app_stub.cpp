// Placeholder application: until the core is integrated, the firmware runs the hardware
// test. These are weak, so any strong appSetup/appLoop elsewhere replaces them.
#include "app.h"
#include "hw_test/hw_test.h"

__attribute__((weak)) void appSetup(density::Hal&) { hwTestSetup(); }
__attribute__((weak)) void appLoop(density::Hal&) { hwTestLoop(); }
