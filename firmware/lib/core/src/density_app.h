// The whole application, portable: loads the configuration, then runs the engine,
// the LEDs and the UI once per loop pass. The board glue (src/app.cpp) only forwards to this,
// so the same code runs in native tests against FakeHal.
#pragma once
#include "density/hal.h"
#include "density/model.h"
#include "engine.h"
#include "led_engine.h"
#include "ui.h"
#include <memory>
#include <string>

namespace density {

class App {
public:
    explicit App(Hal& hal) : hal_(hal) {}

    // Reads /config.json (built-in defaults if it is missing or invalid; the reason is
    // logged and shown on the About page), then /cal.json.
    void setup();
    // One loop pass: poll hardware, run engine, LEDs, UI.
    void loop();

    Engine& engine() { return *engine_; }
    Ui& ui() { return *ui_; }
    const Config& config() const { return cfg_; }
    const std::string& message() const { return message_; }

    static constexpr const char* kConfigPath = "/config.json";

private:
    Hal& hal_;
    Config cfg_;
    std::string message_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<LedEngine> leds_;
    std::unique_ptr<Ui> ui_;
};

}  // namespace density
