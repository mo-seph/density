// The screen-and-encoder user interface.
//
//   Ui ui(hal, engine);
//   ui.loadCalibration();          // once at start-up, after the engine has its config
//   ui.setMessage(err.c_str());    // e.g. when the config failed to load
//   loop: ui.update(hal.millis()); // once per main-loop pass
//
// Encoder 0 drives it (see ui_page.h for the interaction). Display 0 shows the current page
// with a hint line at the bottom; display 1 shows a second view chosen by secondaryView().
//
// Cost per update(): reads the encoder, ticks the pages, hashes what each display would show,
// and redraws + flushes at most ONE display, only if its content changed and it was last
// flushed at least kMinRedrawMs ago. No heap allocation in steady state.
#pragma once
#include "density/engine_view.h"
#include "density/hal.h"
#include "ui_page.h"
#include "ui_pages.h"

namespace density {

class Ui {
public:
    static constexpr uint32_t kLongPressMs = 600;
    static constexpr uint32_t kMinRedrawMs = 40;   // at most 25 redraws a second per display

    Ui(Hal& hal, EngineView& engine);

    void update(uint32_t nowMs);

    // A line for the About page (e.g. why the config did not load). Showing a non-empty
    // message also switches display 0 to the About page, unless a page is being adjusted.
    void setMessage(const char* msg);

    // Reads /cal.json and applies the pedal calibrations it holds. False if there is no file
    // or it is not readable.
    bool loadCalibration() { return cal_.load(); }

    // For tests and integration code.
    uint8_t pageCount() const { return kNumPages; }
    uint8_t pageIndex() const { return cur_; }
    const Page& page() const { return *pages_[cur_]; }
    bool editing() const { return editing_; }
    const CalibrationPage& calibrationPage() const { return cal_; }
    const LevelsPage& levelsPage() const { return levels_; }
    const AboutPage& aboutPage() const { return about_; }

    // Page indices, in browsing order.
    enum : uint8_t { kBanks = 0, kLevels, kCalibration, kAbout, kNumPages };

private:
    // The one place that decides what display 1 shows.
    Page& secondaryView();

    void handleInput(uint32_t nowMs);
    void turn(int steps);
    void press();
    void longPress();
    void leavePage();
    void drawHint(Display& d) const;
    uint32_t hintKey() const;
    void render(uint8_t i);

    Hal& hal_;

    BanksPage banks_;
    LevelsPage levels_;
    CalibrationPage cal_;
    AboutPage about_;
    BankNameView bankName_;
    Page* pages_[kNumPages];

    uint8_t cur_ = kBanks;
    bool editing_ = false;

    // Encoder switch.
    bool down_ = false, longFired_ = false;
    uint32_t downAt_ = 0;

    // Per display: what was last drawn, and when it was last flushed.
    uint32_t key_[kMaxDisplays] = {};
    bool dirty_[kMaxDisplays] = {true, true};
    bool everFlushed_[kMaxDisplays] = {};
    uint32_t lastFlush_[kMaxDisplays] = {};
    Page* shown_[kMaxDisplays] = {};
    uint8_t nextDisplay_ = 0;
};

}  // namespace density
