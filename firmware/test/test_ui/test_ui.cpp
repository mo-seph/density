// Tests for the screen-and-encoder UI (lib/core/src/ui.*).
#include <unity.h>
#include "fake_hal.h"
#include "density/engine_view.h"
#include "../../lib/core/src/ui.h"
#include "../../lib/core/src/ui_calibration.h"
#include "../../lib/core/src/ui_text.h"
#include <cstring>
#include <vector>

using namespace density;

// ---------------------------------------------------------------- fake engine

struct FakeEngine : EngineView {
    Config cfg;
    BankId cur = 0;
    AnalogState states[kMaxAnalog];
    PadState pads[kMaxPads];
    std::vector<BankId> setBankCalls;
    struct CalCall { AnalogId a; AnalogCal cal; };
    std::vector<CalCall> calCalls;
    bool suspended[kMaxAnalog] = {};
    int suspendCalls = 0;

    FakeEngine() {
        cfg.name = "Test config";
        cfg.numBanks = 4;
        const char* names[] = {"Drums", "Bass", "Synthesizers", "FX"};
        for (int i = 0; i < 4; ++i) cfg.banks[i].name = names[i];
    }
    const Config& config() const override { return cfg; }
    BankId bank() const override { return cur; }
    AnalogState analog(AnalogId a) const override { return states[a]; }
    PadState pad(PadId p) const override { return pads[p]; }
    void setBank(BankId b) override { setBankCalls.push_back(b); cur = b; }
    AnalogCal calibration(AnalogId a) const override {
        for (auto it = calCalls.rbegin(); it != calCalls.rend(); ++it)
            if (it->a == a) return it->cal;
        return cfg.cal[a];
    }
    void setCalibration(AnalogId a, AnalogCal c) override { calCalls.push_back({a, c}); }
    void suspend(AnalogId a, bool s) override { suspended[a] = s; ++suspendCalls; }
};

// ---------------------------------------------------------------- fixture

static FakeHal* hal;
static FakeEngine* eng;
static Ui* ui_;

static const AnalogId kPedal1 = 17, kPedal2 = 18;

void setUp() {
    hal = new FakeHal();
    eng = new FakeEngine();
    ui_ = new Ui(*hal, *eng);
}
void tearDown() {
    delete ui_;
    delete eng;
    delete hal;
}

static void tick(uint32_t ms = 1) {
    for (uint32_t i = 0; i < ms; ++i) {
        hal->advance(1);
        ui_->update(hal->millis());
    }
}
// Long enough for both displays to catch up with any change.
static void settle() { tick(120); }

static void turn(int steps) { hal->turn(0, static_cast<int8_t>(steps)); tick(); }
static void press() {
    hal->setEncoderPressed(0, true);
    tick(50);
    hal->setEncoderPressed(0, false);
    tick();
}
static void longPress() {
    hal->setEncoderPressed(0, true);
    tick(Ui::kLongPressMs + 20);
    hal->setEncoderPressed(0, false);
    tick();
}
static FakeDisplay& d0() { return hal->fakeDisplay(0); }
static FakeDisplay& d1() { return hal->fakeDisplay(1); }
static int totalFlushes() { return d0().flushes + d1().flushes; }

// The calibration page reads rawSmooth (spikes removed); the fake has no filter, so both agree.
static void setRaw(AnalogId a, uint16_t v) {
    eng->states[a].raw = v;
    eng->states[a].rawSmooth = v;
}

static void gotoPage(uint8_t p) {
    while (ui_->pageIndex() != p) turn(1);
}

// ---------------------------------------------------------------- navigation

void test_starts_on_banks_browsing() {
    settle();
    TEST_ASSERT_EQUAL(Ui::kBanks, ui_->pageIndex());
    TEST_ASSERT_FALSE(ui_->editing());
    TEST_ASSERT_TRUE_MESSAGE(d0().shows("<Banks>"), d0().allText().c_str());
    TEST_ASSERT_TRUE(d0().shows("push:open"));
}

void test_turn_moves_between_pages_and_wraps() {
    turn(1);
    TEST_ASSERT_EQUAL(Ui::kLevels, ui_->pageIndex());
    turn(2);
    TEST_ASSERT_EQUAL(Ui::kAbout, ui_->pageIndex());
    turn(1);
    TEST_ASSERT_EQUAL(Ui::kBanks, ui_->pageIndex());
    turn(-1);
    TEST_ASSERT_EQUAL(Ui::kAbout, ui_->pageIndex());
    settle();
    TEST_ASSERT_TRUE(d0().shows("<About>"));
    TEST_ASSERT_TRUE(d0().shows("turn:page"));
}

void test_press_enters_only_adjustable_pages() {
    gotoPage(Ui::kLevels);
    press();
    TEST_ASSERT_FALSE(ui_->editing());
    gotoPage(Ui::kBanks);
    press();
    TEST_ASSERT_TRUE(ui_->editing());
    settle();
    // edit hint, inverted, on the bottom line
    bool found = false;
    for (auto& t : d0().textItems)
        if (t.s.find("turn:bank") != std::string::npos && t.inverted && t.y == 56) found = true;
    TEST_ASSERT_TRUE_MESSAGE(found, d0().allText().c_str());
    // while inside, turning changes bank, not page
    turn(1);
    TEST_ASSERT_EQUAL(Ui::kBanks, ui_->pageIndex());
}

void test_long_press_leaves_and_release_is_not_a_press() {
    press();
    TEST_ASSERT_TRUE(ui_->editing());
    longPress();
    TEST_ASSERT_FALSE(ui_->editing());   // left, and the release did not re-enter
    // long press while browsing does nothing
    longPress();
    TEST_ASSERT_FALSE(ui_->editing());
    TEST_ASSERT_EQUAL(Ui::kBanks, ui_->pageIndex());
}

void test_long_press_fires_while_held() {
    press();
    hal->setEncoderPressed(0, true);
    tick(Ui::kLongPressMs - 50);
    TEST_ASSERT_TRUE(ui_->editing());
    tick(60);
    TEST_ASSERT_FALSE(ui_->editing());
}

void test_short_press_leaves_banks() {
    press();
    TEST_ASSERT_TRUE(ui_->editing());
    press();
    TEST_ASSERT_FALSE(ui_->editing());
}

// ---------------------------------------------------------------- banks

void test_bank_page_turn_calls_setBank_and_clamps() {
    press();
    turn(2);
    TEST_ASSERT_EQUAL(1, eng->setBankCalls.size());
    TEST_ASSERT_EQUAL(2, eng->setBankCalls.back());
    turn(5);
    TEST_ASSERT_EQUAL(3, eng->bank());
    size_t calls = eng->setBankCalls.size();
    turn(1);   // already at the last bank
    TEST_ASSERT_EQUAL(calls, eng->setBankCalls.size());
    settle();
    TEST_ASSERT_TRUE(d0().shows("FX"));
    // current bank highlighted in the list
    bool hl = false;
    for (auto& t : d0().textItems)
        if (t.s.rfind("4 FX", 0) == 0 && t.inverted) hl = true;
    TEST_ASSERT_TRUE_MESSAGE(hl, d0().allText().c_str());
}

void test_bank_page_follows_external_bank_change() {
    settle();
    TEST_ASSERT_EQUAL_STRING("Drums", d0().textItems[0].s.c_str());
    eng->cur = 1;   // e.g. a pad's bank_select
    settle();
    TEST_ASSERT_EQUAL_STRING("Bass", d0().textItems[0].s.c_str());
    TEST_ASSERT_EQUAL(0, eng->setBankCalls.size());
}

void test_long_bank_name_is_truncated() {
    eng->cur = 2;   // "Synthesizers": 12 chars, 10 fit at size 2
    settle();
    const auto& big = d0().textItems[0];
    TEST_ASSERT_EQUAL(2, big.size);
    TEST_ASSERT_EQUAL_STRING("Synthesiz.", big.s.c_str());
    for (auto& t : d0().textItems)
        TEST_ASSERT_TRUE_MESSAGE(t.s.size() * 6 * t.size <= 128, t.s.c_str());
}

void test_fit_text_helper() {
    char buf[32];
    ui::fitText(buf, sizeof buf, "Short", 10);
    TEST_ASSERT_EQUAL_STRING("Short", buf);
    ui::fitText(buf, sizeof buf, "Big Room Leads", 9);
    TEST_ASSERT_EQUAL_STRING("Big Room.", buf);
    ui::fitText(buf, sizeof buf, "Big   Leads", 7);   // spaces before the cut are dropped
    TEST_ASSERT_EQUAL_STRING("Big.", buf);
}

// ---------------------------------------------------------------- levels

void test_levels_shows_moving_control_name_and_value() {
    gotoPage(Ui::kLevels);
    eng->states[6].stored = 0.5f;
    eng->states[6].moving = true;
    settle();
    TEST_ASSERT_TRUE_MESSAGE(d0().shows("Knob 7"), d0().allText().c_str());
    TEST_ASSERT_TRUE(d0().shows(" 64"));
    // stays named after it stops moving; next mover takes over
    eng->states[6].moving = false;
    eng->states[16].stored = 1.0f;
    eng->states[16].moving = true;
    settle();
    TEST_ASSERT_TRUE(d0().shows("Fader"));
    TEST_ASSERT_TRUE(d0().shows("127"));
    eng->states[16].moving = false;
    eng->states[18].moving = true;
    settle();
    TEST_ASSERT_TRUE(d0().shows("Pedal 2"));
}

void test_levels_marks_not_picked_up() {
    gotoPage(Ui::kLevels);
    for (int a = 0; a < 19; ++a) eng->states[a].stored = 0.5f;
    settle();
    int filled = 0, hollow = 0;
    for (auto& r : d0().rects) (r.filled ? filled : hollow)++;
    TEST_ASSERT_EQUAL(19, filled);
    TEST_ASSERT_EQUAL(0, hollow);
    int hl = d0().hlines;
    eng->states[3].pickedUp = false;
    eng->states[3].position = 0.1f;
    settle();
    filled = hollow = 0;
    for (auto& r : d0().rects) (r.filled ? filled : hollow)++;
    TEST_ASSERT_EQUAL(18, filled);
    TEST_ASSERT_EQUAL(1, hollow);
    TEST_ASSERT_EQUAL(hl + 1, d0().hlines);   // the tick at the physical position
    // bars stay on screen
    for (auto& r : d0().rects) {
        TEST_ASSERT_TRUE(r.x >= 0 && r.x + r.w <= 128);
        TEST_ASSERT_TRUE(r.y >= 17 && r.y + r.h <= 56);
    }
}

void test_display1_policy() {
    eng->states[2].stored = 0.25f;
    eng->states[2].moving = true;
    settle();
    // Banks on display 0 -> Levels on display 1
    TEST_ASSERT_TRUE_MESSAGE(d1().shows("Knob 3"), d1().allText().c_str());
    TEST_ASSERT_TRUE(d1().shows(" 32"));
    TEST_ASSERT_EQUAL(1, (int)d1().rects.size());   // only knob 3 is non-zero
    // anything else -> bank name large
    gotoPage(Ui::kLevels);
    settle();
    TEST_ASSERT_TRUE(d1().shows("Bank 1/4"));
    TEST_ASSERT_TRUE(d1().shows("Drums"));
    eng->cur = 2;
    settle();
    TEST_ASSERT_TRUE(d1().shows("Synthesize"));   // long word: cut, rest on line 2
    TEST_ASSERT_TRUE(d1().shows("rs"));
    for (auto& t : d1().textItems)
        TEST_ASSERT_TRUE_MESSAGE(t.s.size() * 6 * t.size <= 128, t.s.c_str());
}

// ---------------------------------------------------------------- calibration

static void enterCalibration() {
    setRaw(kPedal1, 2000);
    setRaw(kPedal2, 2000);
    gotoPage(Ui::kCalibration);
    press();
    TEST_ASSERT_TRUE(ui_->editing());
    TEST_ASSERT_FALSE(ui_->calibrationPage().running());
}

static void sweep(AnalogId a, uint16_t from, uint16_t to) {
    for (int i = 0; i <= 10; ++i) {
        setRaw(a, static_cast<uint16_t>(from + (to - from) * i / 10));
        tick(5);
    }
}

void test_calibration_full_flow_saves() {
    enterCalibration();
    setRaw(kPedal1, 2000);
    press();   // start pedal 1
    const CalibrationPage& cp = ui_->calibrationPage();
    TEST_ASSERT_TRUE(cp.running());
    TEST_ASSERT_TRUE(eng->suspended[kPedal1]);
    settle();
    TEST_ASSERT_TRUE_MESSAGE(d0().shows("Calibrate Pedal 1"), d0().allText().c_str());
    TEST_ASSERT_TRUE(d0().shows("push:save hold:cancel"));
    sweep(kPedal1, 2000, 300);
    sweep(kPedal1, 300, 3800);
    setRaw(kPedal1, 2500);
    settle();
    TEST_ASSERT_EQUAL(300, cp.trackedMin());
    TEST_ASSERT_EQUAL(3800, cp.trackedMax());
    TEST_ASSERT_TRUE(d0().shows("2500"));   // live raw number
    TEST_ASSERT_TRUE(d0().shows("min 300"));
    TEST_ASSERT_TRUE(d0().shows("max 3800"));
    TEST_ASSERT_TRUE(d0().rects.size() >= 2);   // bar outline + fill
    press();   // save
    TEST_ASSERT_FALSE(cp.running());
    TEST_ASSERT_FALSE(eng->suspended[kPedal1]);
    TEST_ASSERT_TRUE(ui_->editing());   // back at the pedal list
    TEST_ASSERT_EQUAL(1, eng->calCalls.size());
    TEST_ASSERT_EQUAL(kPedal1, eng->calCalls[0].a);
    TEST_ASSERT_EQUAL(370, eng->calCalls[0].cal.rawMin);    // 2% margin of 3500
    TEST_ASSERT_EQUAL(3730, eng->calCalls[0].cal.rawMax);
    TEST_ASSERT_EQUAL(1, hal->files.count("/cal.json"));
    TEST_ASSERT_EQUAL_STRING("{\"pedals\":[[370,3730],[0,0]]}\n", hal->files["/cal.json"].c_str());
    settle();
    TEST_ASSERT_TRUE(d0().shows("Pedal 1 saved"));
    TEST_ASSERT_TRUE(d0().shows("370-3730"));
    tick(3000);
    TEST_ASSERT_FALSE(d0().shows("saved"));   // note times out
}

void test_calibration_second_pedal_keeps_first() {
    enterCalibration();
    press();
    sweep(kPedal1, 100, 4000);
    press();
    turn(1);   // pick pedal 2
    press();
    TEST_ASSERT_TRUE(eng->suspended[kPedal2]);
    TEST_ASSERT_FALSE(eng->suspended[kPedal1]);
    sweep(kPedal2, 1000, 3000);
    press();
    TEST_ASSERT_FALSE(eng->suspended[kPedal2]);
    TEST_ASSERT_EQUAL(2, eng->calCalls.size());
    TEST_ASSERT_EQUAL(kPedal2, eng->calCalls[1].a);
    TEST_ASSERT_EQUAL_STRING("{\"pedals\":[[178,3922],[1040,2960]]}\n", hal->files["/cal.json"].c_str());
}

void test_calibration_cancel_restores() {
    enterCalibration();
    press();
    TEST_ASSERT_TRUE(eng->suspended[kPedal1]);
    sweep(kPedal1, 100, 4000);
    longPress();   // cancel
    TEST_ASSERT_FALSE(ui_->calibrationPage().running());
    TEST_ASSERT_FALSE(eng->suspended[kPedal1]);
    TEST_ASSERT_TRUE(ui_->editing());   // still on the page, at the pedal list
    TEST_ASSERT_EQUAL(0, eng->calCalls.size());
    TEST_ASSERT_EQUAL(0, hal->files.count("/cal.json"));
    settle();
    TEST_ASSERT_TRUE(d0().shows("Cancelled"));
    longPress();   // leave the page
    TEST_ASSERT_FALSE(ui_->editing());
    TEST_ASSERT_EQUAL(Ui::kCalibration, ui_->pageIndex());
}

void test_calibration_refuses_small_range() {
    enterCalibration();
    setRaw(kPedal1, 1000);
    press();
    sweep(kPedal1, 1000, 1200);
    press();   // refused
    TEST_ASSERT_TRUE(ui_->calibrationPage().running());
    TEST_ASSERT_TRUE(eng->suspended[kPedal1]);
    TEST_ASSERT_EQUAL(0, eng->calCalls.size());
    TEST_ASSERT_EQUAL(0, hal->files.count("/cal.json"));
    settle();
    TEST_ASSERT_TRUE_MESSAGE(d0().shows("Too small"), d0().allText().c_str());
    // keep going and it then saves
    sweep(kPedal1, 1200, 3500);
    press();
    TEST_ASSERT_FALSE(ui_->calibrationPage().running());
    TEST_ASSERT_EQUAL(1, eng->calCalls.size());
}

void test_calibration_refuses_unplugged() {
    enterCalibration();
    setRaw(kPedal1, 0);
    press();
    sweep(kPedal1, 0, 20);
    press();
    TEST_ASSERT_TRUE(ui_->calibrationPage().running());
    TEST_ASSERT_EQUAL(0, eng->calCalls.size());
    settle();
    TEST_ASSERT_TRUE_MESSAGE(d0().shows("No signal"), d0().allText().c_str());
    longPress();
    TEST_ASSERT_FALSE(eng->suspended[kPedal1]);
}

void test_load_calibration_round_trip() {
    enterCalibration();
    press();
    sweep(kPedal1, 300, 3800);
    press();
    turn(1);
    press();
    sweep(kPedal2, 1000, 3000);
    press();
    // a fresh start with the same flash
    FakeEngine e2;
    Ui u2(*hal, e2);
    TEST_ASSERT_TRUE(u2.loadCalibration());
    TEST_ASSERT_EQUAL(2, e2.calCalls.size());
    TEST_ASSERT_EQUAL(kPedal1, e2.calCalls[0].a);
    TEST_ASSERT_EQUAL(370, e2.calCalls[0].cal.rawMin);
    TEST_ASSERT_EQUAL(3730, e2.calCalls[0].cal.rawMax);
    TEST_ASSERT_EQUAL(kPedal2, e2.calCalls[1].a);
    TEST_ASSERT_EQUAL(1040, e2.calCalls[1].cal.rawMin);
    TEST_ASSERT_EQUAL(2960, e2.calCalls[1].cal.rawMax);
}

void test_load_calibration_missing_or_bad() {
    TEST_ASSERT_FALSE(ui_->loadCalibration());
    hal->files["/cal.json"] = "not json";
    TEST_ASSERT_FALSE(ui_->loadCalibration());
    hal->files["/cal.json"] = "{\"pedals\":[[0,0], [500,3000], [9,9]]}";   // pedal 1 unset, extra ignored
    TEST_ASSERT_TRUE(ui_->loadCalibration());
    TEST_ASSERT_EQUAL(1, eng->calCalls.size());
    TEST_ASSERT_EQUAL(kPedal2, eng->calCalls[0].a);
    eng->calCalls.clear();
    hal->files["/cal.json"] = "{ \"pedals\" : [ [ 100 , 150 ] ] }";   // too small: skipped
    TEST_ASSERT_TRUE(ui_->loadCalibration());
    TEST_ASSERT_EQUAL(0, eng->calCalls.size());
}

void test_calibration_page_leave_unsuspends() {
    // Leaving through any path must not leave a pedal muted. (Only long press leaves,
    // and while running it cancels first; check the double long press too.)
    enterCalibration();
    press();
    longPress();
    longPress();
    TEST_ASSERT_FALSE(ui_->editing());
    TEST_ASSERT_FALSE(eng->suspended[kPedal1]);
}

void test_calibration_tracks_spike_free_raw() {
    // A one-sample glitch in `raw` must not become an end stop: the page uses rawSmooth.
    enterCalibration();
    setRaw(kPedal1, 500);
    press();
    sweep(kPedal1, 500, 3500);
    eng->states[kPedal1].raw = 4095;   // glitch: the filter's median has already removed it
    tick(5);
    setRaw(kPedal1, 3500);
    sweep(kPedal1, 3500, 500);
    TEST_ASSERT_EQUAL(500, ui_->calibrationPage().trackedMin());
    TEST_ASSERT_EQUAL(3500, ui_->calibrationPage().trackedMax());
}

// ---------------------------------------------------------------- about

// Every string drawn on display 0, joined with spaces (wrapped lines read as one text).
static std::string d0Joined() {
    std::string out;
    for (const auto& t : d0().textItems) { if (!out.empty()) out += ' '; out += t.s; }
    return out;
}

void test_about_shows_whole_config_error() {
    const char* msg = "group 2 (\"knob buttons\"): send numbers run past 127 (pad16 in bank 4 would be 163)";
    ui_->setMessage(msg);
    settle();
    TEST_ASSERT_TRUE_MESSAGE(d0Joined().find(msg) != std::string::npos, d0().allText().c_str());
    for (auto& t : d0().textItems) TEST_ASSERT_TRUE_MESSAGE(t.s.size() <= 21, t.s.c_str());
}

void test_about_too_long_message_shows_its_end() {
    std::string msg = "group 12 (\"a long group name here\"): selector 'pads 1 - 99' in the list of selectors given: "
                      "this board has 20 pads, so pad21 to pad99 do not exist (pad99 is the last)";
    TEST_ASSERT_TRUE(msg.size() > 140);
    ui_->setMessage(msg.c_str());
    settle();
    const std::string shown = d0Joined();
    TEST_ASSERT_TRUE_MESSAGE(shown.find("(pad99 is the last)") != std::string::npos, d0().allText().c_str());
    TEST_ASSERT_TRUE(d0().shows("Test config"));   // the name line stays
    for (auto& t : d0().textItems) TEST_ASSERT_TRUE_MESSAGE(t.s.size() <= 21, t.s.c_str());
    // Lines start at word boundaries, so nothing on screen is a word cut in half at the front.
    TEST_ASSERT_TRUE(shown.find(" do not exist") != std::string::npos);
}

void test_about_shows_info_and_message() {
    gotoPage(Ui::kAbout);
    settle();
    TEST_ASSERT_TRUE_MESSAGE(d0().shows("Test config"), d0().allText().c_str());
    TEST_ASSERT_TRUE(d0().shows("Banks: 4"));
    TEST_ASSERT_TRUE(d0().shows("fake_proto"));
    TEST_ASSERT_TRUE(d0().shows("K16 F1 P2 Pad20"));
    TEST_ASSERT_TRUE(d0().shows("No messages"));
    gotoPage(Ui::kBanks);
    ui_->setMessage("config.json line 12: expected a number for 'channel'");
    TEST_ASSERT_EQUAL(Ui::kAbout, ui_->pageIndex());   // jumps there so it is seen
    settle();
    TEST_ASSERT_TRUE(d0().shows("config.json line 12:"));
    TEST_ASSERT_TRUE(d0().shows("expected a number"));
    for (auto& t : d0().textItems) TEST_ASSERT_TRUE_MESSAGE(t.s.size() <= 21, t.s.c_str());
}

// ---------------------------------------------------------------- performance contract

void test_at_most_one_flush_per_update() {
    for (int i = 0; i < 400; ++i) {
        // change something visible on both displays every pass
        eng->states[i % 19].stored = (i % 100) / 100.0f;
        eng->states[i % 19].moving = true;
        if (i % 50 == 0) eng->cur = static_cast<BankId>((eng->cur + 1) % 4);
        if (i % 37 == 0) hal->turn(0, 1);
        int before = totalFlushes();
        hal->advance(1);
        ui_->update(hal->millis());
        TEST_ASSERT_TRUE(totalFlushes() - before <= 1);
    }
}

void test_redraw_rate_limited() {
    // Constant change for one second: each display at most ~25 flushes.
    for (int i = 0; i < 1000; ++i) {
        eng->states[0].stored = (i % 128) / 127.0f;
        eng->states[0].moving = true;
        tick();
    }
    TEST_ASSERT_TRUE(d0().flushes <= 26);
    TEST_ASSERT_TRUE(d1().flushes <= 26);
    TEST_ASSERT_TRUE(d1().flushes >= 20);   // Levels on display 1 keeps up
}

void test_no_flush_when_nothing_changed() {
    settle();
    TEST_ASSERT_EQUAL(1, d0().flushes);
    TEST_ASSERT_EQUAL(1, d1().flushes);
    int f = totalFlushes(), c = d0().clears + d1().clears;
    tick(1000);
    TEST_ASSERT_EQUAL(f, totalFlushes());
    TEST_ASSERT_EQUAL(c, d0().clears + d1().clears);   // not even redrawn into the buffer
    // a change redraws only the display it affects
    turn(1);   // Banks -> Levels: both displays change
    settle();
    TEST_ASSERT_EQUAL(2, d0().flushes);
    TEST_ASSERT_EQUAL(2, d1().flushes);
    eng->states[5].stored = 0.7f;   // Levels on display 0 only now
    settle();
    TEST_ASSERT_EQUAL(3, d0().flushes);
    TEST_ASSERT_EQUAL(2, d1().flushes);
}

void test_single_display_board() {
    hal->hw.numDisplays = 1;
    settle();
    turn(1);
    settle();
    TEST_ASSERT_EQUAL(2, d0().flushes);
    TEST_ASSERT_EQUAL(0, d1().flushes);
    TEST_ASSERT_EQUAL(0, d1().clears);
}

void test_no_encoder_board_does_not_read_it() {
    hal->hw.numEncoders = 0;
    hal->turn(0, 3);
    settle();
    TEST_ASSERT_EQUAL(Ui::kBanks, ui_->pageIndex());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_on_banks_browsing);
    RUN_TEST(test_turn_moves_between_pages_and_wraps);
    RUN_TEST(test_press_enters_only_adjustable_pages);
    RUN_TEST(test_long_press_leaves_and_release_is_not_a_press);
    RUN_TEST(test_long_press_fires_while_held);
    RUN_TEST(test_short_press_leaves_banks);
    RUN_TEST(test_bank_page_turn_calls_setBank_and_clamps);
    RUN_TEST(test_bank_page_follows_external_bank_change);
    RUN_TEST(test_long_bank_name_is_truncated);
    RUN_TEST(test_fit_text_helper);
    RUN_TEST(test_levels_shows_moving_control_name_and_value);
    RUN_TEST(test_levels_marks_not_picked_up);
    RUN_TEST(test_display1_policy);
    RUN_TEST(test_calibration_full_flow_saves);
    RUN_TEST(test_calibration_second_pedal_keeps_first);
    RUN_TEST(test_calibration_cancel_restores);
    RUN_TEST(test_calibration_refuses_small_range);
    RUN_TEST(test_calibration_refuses_unplugged);
    RUN_TEST(test_load_calibration_round_trip);
    RUN_TEST(test_load_calibration_missing_or_bad);
    RUN_TEST(test_calibration_page_leave_unsuspends);
    RUN_TEST(test_calibration_tracks_spike_free_raw);
    RUN_TEST(test_about_shows_info_and_message);
    RUN_TEST(test_about_shows_whole_config_error);
    RUN_TEST(test_about_too_long_message_shows_its_end);
    RUN_TEST(test_at_most_one_flush_per_update);
    RUN_TEST(test_redraw_rate_limited);
    RUN_TEST(test_no_flush_when_nothing_changed);
    RUN_TEST(test_single_display_board);
    RUN_TEST(test_no_encoder_board_does_not_read_it);
    return UNITY_END();
}
