// The UI's pages. Each is small and owns only fixed-size state (no heap in steady state).
#pragma once
#include "ui_page.h"
#include "density/engine_view.h"

namespace density {

// Current bank name large, all banks listed with the current one highlighted.
// Inside: turn changes bank (EngineView::setBank); press or long press leaves.
class BanksPage : public Page {
public:
    BanksPage(EngineView& e) : e_(e) {}
    const char* title() const override { return "Banks"; }
    bool adjustable() const override { return e_.config().numBanks > 1; }
    const char* editHint() const override { return "turn:bank push:done"; }
    void onTurn(int steps) override;
    uint32_t contentKey() const override;
    void draw(Display& d, int16_t height) const override;

private:
    EngineView& e_;
};

// Bar graph of every analog control's stored value in the current bank. A control that is
// not picked up is drawn hollow, with a tick at its physical position. The control touched
// most recently is named at the top with its value 0-127.
class LevelsPage : public Page {
public:
    LevelsPage(Hal& h, EngineView& e) : hal_(h), e_(e) {}
    const char* title() const override { return "Levels"; }
    void tick(uint32_t nowMs) override;
    uint32_t contentKey() const override;
    void draw(Display& d, int16_t height) const override;
    AnalogId lastMoved() const { return last_; }

private:
    uint8_t count() const;
    Hal& hal_;
    EngineView& e_;
    AnalogId last_ = kNone;
};

// Pedal calibration. Inside: turn picks a pedal, press starts. While running, the pedal is
// suspended (no MIDI) and its raw min / max are tracked; press saves (refused if the range is
// too small), long press cancels. Saved calibrations go to /cal.json.
class CalibrationPage : public Page {
public:
    CalibrationPage(Hal& h, EngineView& e) : hal_(h), e_(e) {}
    const char* title() const override { return "Pedal cal"; }
    bool adjustable() const override { return hal_.info().numPedals > 0; }
    const char* editHint() const override;
    void onEnter() override;
    void onLeave() override;
    void onTurn(int steps) override;
    PageResult onPress() override;
    PageResult onLongPress() override;
    void tick(uint32_t nowMs) override;
    uint32_t contentKey() const override;
    void draw(Display& d, int16_t height) const override;

    // Read /cal.json and apply it. False if there is no file or it cannot be parsed.
    bool load();

    bool running() const { return running_; }
    uint16_t trackedMin() const { return min_; }
    uint16_t trackedMax() const { return max_; }
    const char* note() const { return noteOn_ ? note_ : ""; }

private:
    AnalogId analogId(uint8_t pedal) const;
    void stop();
    bool save();
    void setNote(const char* s);

    Hal& hal_;
    EngineView& e_;
    uint8_t sel_ = 0;          // pedal index
    bool running_ = false;
    uint16_t raw_ = 0, min_ = 0xFFFF, max_ = 0;
    AnalogCal cal_[kMaxAnalog];
    bool calSet_[kMaxAnalog] = {};
    char note_[24] = {};
    bool noteOn_ = false;
    uint32_t now_ = 0, noteUntil_ = 0;
};

// Config name, bank count, board name and counts, and the last message from Ui::setMessage.
// A message (up to kMaxMessage characters) comes straight after the name, wrapped at word
// boundaries over as many lines as it needs; the board lines fill whatever room is left.
// If the message still does not fit, its END is shown (the detail is usually at the end).
class AboutPage : public Page {
public:
    AboutPage(Hal& h, EngineView& e) : hal_(h), e_(e) {}
    const char* title() const override { return "About"; }
    uint32_t contentKey() const override;
    void draw(Display& d, int16_t height) const override;
    void setMessage(const char* s);
    const char* message() const { return msg_; }
    static constexpr size_t kMaxMessage = 160;

private:
    Hal& hal_;
    EngineView& e_;
    char msg_[kMaxMessage + 1] = {};
    uint32_t msgVersion_ = 0;
};

// Not in the page list: the current bank name, large, for display 1.
class BankNameView : public Page {
public:
    BankNameView(EngineView& e) : e_(e) {}
    const char* title() const override { return "Bank"; }
    uint32_t contentKey() const override;
    void draw(Display& d, int16_t height) const override;

private:
    EngineView& e_;
};

}  // namespace density
