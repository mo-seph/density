#include "density_app.h"
#include "config_loader.h"

namespace density {

void App::setup() {
    const HardwareInfo& hw = hal_.info();
    std::string json;
    std::string logLine;
    if (!hal_.readFile(kConfigPath, json)) {
        defaultConfig(hw, cfg_);
        message_ = "No config.json: using defaults";
        logLine = message_;
    } else {
        std::string err;
        if (!loadConfig(json, hw, cfg_, err)) {
            message_ = err;   // the screen has little room: no prefix there
            logLine = "config.json: " + err;
        } else {
            for (const std::string& w : configWarnings(cfg_, hw)) {
                hal_.log(("config warning: " + w).c_str());
            }
        }
    }
    json.clear();
    json.shrink_to_fit();

    engine_.reset(new Engine(hal_, cfg_));
    leds_.reset(new LedEngine(hal_, *engine_));
    ui_.reset(new Ui(hal_, *engine_));
    ui_->loadCalibration();

    if (!message_.empty()) {
        hal_.log(logLine.c_str());
        ui_->setMessage(message_.c_str());
    } else {
        hal_.log(("config loaded: " + cfg_.name).c_str());
    }
}

void App::loop() {
    hal_.poll();
    engine_->update();
    const uint32_t now = hal_.millis();
    leds_->update(now);
    ui_->update(now);
}

}  // namespace density
