// LED engine and colour tests. Run: pio test -e native_config
#include <unity.h>

#include "density/engine_view.h"
#include "../../lib/core/src/color.h"
#include "../../lib/core/src/led_engine.h"
#include "fake_hal.h"

using namespace density;

// A settable EngineView.
class FakeView : public EngineView {
public:
    Config cfg;
    BankId b = 0;
    AnalogState as[kMaxAnalog];
    PadState ps[kMaxPads];

    const Config& config() const override { return cfg; }
    BankId bank() const override { return b; }
    AnalogState analog(AnalogId a) const override { return as[a]; }
    PadState pad(PadId p) const override { return ps[p]; }
    void setBank(BankId nb) override { b = nb; }
    AnalogCal calibration(AnalogId a) const override { return cfg.cal[a]; }
    void setCalibration(AnalogId, AnalogCal) override {}
    void suspend(AnalogId, bool) override {}
};

static FakeHal* hal;
static FakeView* view;
static LedEngine* leds;

void setUp() {
    hal = new FakeHal();
    view = new FakeView();
    defaultConfig(hal->hw, view->cfg);
    view->cfg.ledBrightness = 1.0f;   // most tests check unscaled colours
    leds = new LedEngine(*hal, *view);
}
void tearDown() {
    delete leds;
    delete view;
    delete hal;
}

static LedSpec& spec(PadId p, BankId b = 0) { return view->cfg.leds[p][b]; }

static void assertRgb(int r, int g, int b, Rgb c, const char* msg = nullptr) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s expected (%d,%d,%d) got (%d,%d,%d)", msg ? msg : "", r, g, b, c.r, c.g, c.b);
    TEST_ASSERT_TRUE_MESSAGE(c.r == r && c.g == g && c.b == b, buf);
}

// Colour of pad p after one update at time t.
static Rgb shown(PadId p, uint32_t t) {
    leds->update(t);
    return hal->leds[p];
}

// ---------------------------------------------------------------- colour

void test_hsv_known_points() {
    assertRgb(255, 0, 0, hsvToRgb({0, 1, 1}), "red");
    assertRgb(255, 255, 0, hsvToRgb({60, 1, 1}), "yellow");
    assertRgb(0, 255, 0, hsvToRgb({120, 1, 1}), "green");
    assertRgb(0, 255, 255, hsvToRgb({180, 1, 1}), "cyan");
    assertRgb(0, 0, 255, hsvToRgb({240, 1, 1}), "blue");
    assertRgb(255, 0, 255, hsvToRgb({300, 1, 1}), "magenta");
    assertRgb(255, 128, 0, hsvToRgb({30, 1, 1}), "orange");
    assertRgb(255, 255, 255, hsvToRgb({77, 0, 1}), "white");
    assertRgb(0, 0, 0, hsvToRgb({77, 1, 0}), "black");
    assertRgb(128, 128, 128, hsvToRgb({0, 0, 0.5f}), "grey");
    assertRgb(255, 128, 128, hsvToRgb({0, 0.5f, 1}), "pink");
    assertRgb(255, 0, 0, hsvToRgb({360, 1, 1}), "360 wraps");
    assertRgb(0, 0, 255, hsvToRgb({-120, 1, 1}), "negative wraps");
    assertRgb(255, 0, 0, hsvToRgb({0, 2, 3}), "clamped");
}

void test_hsv_brightness_scales_v() {
    assertRgb(128, 0, 0, hsvToRgb({0, 1, 1}, 0.5f));
    assertRgb(64, 0, 0, hsvToRgb({0, 1, 0.5f}, 0.5f));
    assertRgb(0, 0, 0, hsvToRgb({0, 1, 1}, 0));
}

// ---------------------------------------------------------------- modes

void test_mode_off_and_static() {
    spec(16) = LedSpec{};   // Off
    TEST_ASSERT_EQUAL_INT(int(LedMode::Off), int(spec(16).mode));
    spec(17).mode = LedMode::Static;
    spec(17).color = {240, 1, 1};
    assertRgb(0, 0, 0, shown(16, 0), "off");
    assertRgb(0, 0, 255, shown(17, 0), "static");
    spec(17).color.v = 0.5f;
    assertRgb(0, 0, 128, shown(17, 1), "static val");
}

void test_mode_level_follows_stored_value() {
    // Default config: pad 0 Level, under knob 0.
    LedSpec& s = spec(0);
    s.color = {0, 1, 1};
    s.floorV = 0.1f;
    view->as[0].stored = 0;
    assertRgb(26, 0, 0, shown(0, 0), "floor");
    view->as[0].stored = 1;
    assertRgb(255, 0, 0, shown(0, 1), "full");
    view->as[0].stored = 0.5f;
    assertRgb(140, 0, 0, shown(0, 2), "half: 0.1 + 0.9 * 0.5");
}

void test_mode_level_explicit_source_and_unlinked() {
    LedSpec& s = spec(17);   // pad 18: no control above it
    s.mode = LedMode::Level;
    s.color = {0, 1, 1};
    s.floorV = 0.2f;
    view->as[16].stored = 1;   // fader
    assertRgb(51, 0, 0, shown(17, 0), "no source: static at floor");
    s.source = 16;
    assertRgb(255, 0, 0, shown(17, 1), "source fader1");
}

void test_mode_state() {
    LedSpec& s = spec(16);   // default: State, momentary note
    s.color = {120, 1, 1};
    s.floorV = 0.2f;
    view->cfg.pads[16][0].action = PadAction::None;   // keep the press flash out of this test
    assertRgb(0, 51, 0, shown(16, 0), "off -> floor");
    view->ps[16].on = true;
    assertRgb(0, 255, 0, shown(16, 1), "on");
    view->ps[16].on = false;
    view->ps[16].pressed = true;
    assertRgb(0, 255, 0, shown(16, 2), "held");
}

void test_mode_midi() {
    LedSpec& s = spec(18);
    s.mode = LedMode::Midi;
    s.color = {240, 1, 1};
    view->ps[18].midiValue = 0;
    assertRgb(0, 0, 0, shown(18, 0), "0");
    view->ps[18].midiValue = 0.5f;
    assertRgb(0, 0, 128, shown(18, 1), "half");
}

void test_bank_hue() {
    view->cfg.numBanks = 3;
    view->cfg.banks[0].hue = 0;
    view->cfg.banks[1].hue = 120;
    view->cfg.banks[2].hue = 240;
    for (int b = 0; b < 3; ++b) {
        spec(17, b).mode = LedMode::Static;
        spec(17, b).hueFromBank = true;
        spec(17, b).color = {60, 1, 1};
    }
    assertRgb(255, 0, 0, shown(17, 0), "bank 1 hue");
    view->b = 1;
    assertRgb(0, 255, 0, shown(17, 1), "bank 2 hue");

    // A bank-select pad shows the hue of the bank it selects, whatever the current bank.
    for (int b = 0; b < 3; ++b) {
        view->cfg.pads[18][b].action = PadAction::BankSelect;
        view->cfg.pads[18][b].arg = 2;
        spec(18, b) = spec(17, b);
    }
    assertRgb(0, 0, 255, shown(18, 2), "bank-select pad: target bank hue");
}

// ---------------------------------------------------------------- effects

void test_blink_in_step() {
    for (PadId p : {PadId(16), PadId(17)}) {
        spec(p).mode = LedMode::Static;
        spec(p).color = {0, 1, 1};
        spec(p).effect = LedEffect::Blink;
        spec(p).periodMs = 400;
    }
    leds->update(1000);   // 1000 % 400 = 200: second half, dark
    assertRgb(0, 0, 0, hal->leds[16]);
    assertRgb(0, 0, 0, hal->leds[17]);
    leds->update(1210);   // 10 ms into a period: lit
    assertRgb(255, 0, 0, hal->leds[16]);
    assertRgb(255, 0, 0, hal->leds[17]);
    assertRgb(255, 0, 0, shown(16, 1399), "199 ms in: still lit");
    assertRgb(0, 0, 0, shown(16, 1400), "200 ms in: dark");
}

void test_fade_is_smooth_pulse() {
    spec(17).mode = LedMode::Static;
    spec(17).color = {0, 1, 1};
    spec(17).effect = LedEffect::Fade;
    spec(17).periodMs = 1000;
    assertRgb(0, 0, 0, shown(17, 0), "start: dark");
    TEST_ASSERT_INT_WITHIN(1, 128, shown(17, 250).r);   // quarter: half
    assertRgb(255, 0, 0, shown(17, 500), "half: full");
    TEST_ASSERT_INT_WITHIN(1, 128, shown(17, 750).r);   // three quarters: half
    assertRgb(0, 0, 0, shown(17, 1000), "end: dark again");
    // Monotonic rise over the first half.
    int prev = -1;
    for (uint32_t t = 0; t <= 500; t += 25) {
        int r = shown(17, 2000 + t).r;
        TEST_ASSERT_TRUE(r >= prev);
        prev = r;
    }
}

void test_effect_on_level() {
    spec(0).color = {0, 1, 1};
    spec(0).floorV = 0;
    spec(0).effect = LedEffect::Blink;
    spec(0).periodMs = 100;
    view->as[0].stored = 0.5f;
    assertRgb(128, 0, 0, shown(0, 0), "lit at level");
    assertRgb(0, 0, 0, shown(0, 60), "dark");
}

// ---------------------------------------------------------------- pickup indicator

void test_pickup_indicator_turn_down() {
    spec(3).mode = LedMode::Static;   // normal display would be static green
    spec(3).color = {120, 1, 1};
    view->as[3].pickedUp = false;
    view->as[3].direction = -1;
    view->as[3].stored = 0.5f;
    Hsv c = leds->padColor(3, 0);
    TEST_ASSERT_EQUAL_FLOAT(LedEngine::kPickupDownHue, c.h);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, LedEngine::kPickupMinV + (1 - LedEngine::kPickupMinV) * 0.5f, c.v);
    assertRgb(147, 0, 0, shown(3, 0), "red, stored brightness");
    assertRgb(0, 0, 0, shown(3, LedEngine::kPickupBlinkMs / 2), "blink off phase");
    assertRgb(147, 0, 0, shown(3, LedEngine::kPickupBlinkMs), "blink on again");
    // Picked up: back to normal.
    view->as[3].pickedUp = true;
    assertRgb(0, 255, 0, shown(3, 0));
}

void test_pickup_indicator_turn_up_and_brightness() {
    view->as[5].pickedUp = false;
    view->as[5].direction = +1;
    view->as[5].stored = 1.0f;
    Hsv c = leds->padColor(5, 10);
    TEST_ASSERT_EQUAL_FLOAT(LedEngine::kPickupUpHue, c.h);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.s);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.v);
    Rgb up = shown(5, 10);
    TEST_ASSERT_TRUE(up.b == 255 && up.r == 0);   // blue
    view->as[5].stored = 0;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, LedEngine::kPickupMinV, leds->padColor(5, 10).v);   // still visible at 0
    // Two pads in opposite directions differ clearly.
    view->as[6].pickedUp = false;
    view->as[6].direction = -1;
    view->as[6].stored = 1.0f;
    view->as[5].stored = 1.0f;
    leds->update(20);
    TEST_ASSERT_TRUE(hal->leds[5].b > 200 && hal->leds[5].r < 50);
    TEST_ASSERT_TRUE(hal->leds[6].r > 200 && hal->leds[6].b < 50);
}

void test_pickup_indicator_uses_source_and_respects_off() {
    spec(17).mode = LedMode::Level;
    spec(17).source = 16;    // fader
    view->as[16].pickedUp = false;
    view->as[16].direction = -1;
    view->as[16].stored = 1;
    TEST_ASSERT_EQUAL_FLOAT(LedEngine::kPickupDownHue, leds->padColor(17, 0).h);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, leds->padColor(17, 0).v);
    spec(17).mode = LedMode::Off;
    assertRgb(0, 0, 0, shown(17, 0), "Off pads stay dark");
}

// ---------------------------------------------------------------- press feedback

void test_press_feedback() {
    // Default pad 17: momentary, State. Make it static so the press is the only change.
    spec(16).mode = LedMode::Static;
    spec(16).color = {0, 1, 0.5f};
    assertRgb(128, 0, 0, shown(16, 0), "idle");
    view->ps[16].pressed = true;
    assertRgb(255, 128, 128, shown(16, 1), "held: full, whitish");
    // Even over the pickup indicator.
    view->cfg.pads[0][0].action = PadAction::Toggle;
    view->as[0].pickedUp = false;
    view->as[0].direction = 1;
    view->ps[0].pressed = true;
    spec(0).color = {0, 1, 1};
    assertRgb(255, 128, 128, shown(0, 2), "press beats pickup");
    // Action None: no flash.
    view->cfg.pads[0][0].action = PadAction::None;
    view->as[0].pickedUp = true;
    view->as[0].stored = 1;
    assertRgb(255, 0, 0, shown(0, 3), "no action, no flash");
    // Mode Off: no flash.
    spec(16).mode = LedMode::Off;
    assertRgb(0, 0, 0, shown(16, 4), "off stays off");
}

// ---------------------------------------------------------------- brightness and sending

void test_global_brightness() {
    spec(17).mode = LedMode::Static;
    spec(17).color = {0, 1, 1};
    view->cfg.ledBrightness = 0.25f;
    assertRgb(64, 0, 0, shown(17, 0));
    view->cfg.ledBrightness = 0.5f;
    assertRgb(128, 0, 0, shown(17, 1));
}

void test_unchanged_frame_not_resent_every_pass() {
    leds->update(0);
    TEST_ASSERT_EQUAL_INT(1, hal->showCount);   // first frame always shown
    for (uint32_t t = 1; t < LedEngine::kResendMs; ++t) leds->update(t);
    TEST_ASSERT_EQUAL_INT(1, hal->showCount);   // nothing changed
    leds->update(LedEngine::kResendMs);
    TEST_ASSERT_EQUAL_INT(2, hal->showCount);   // periodic refresh
    // A change is shown at once.
    view->as[0].stored = 1.0f;
    leds->update(LedEngine::kResendMs + 1);
    TEST_ASSERT_EQUAL_INT(3, hal->showCount);
    leds->update(LedEngine::kResendMs + 2);
    TEST_ASSERT_EQUAL_INT(3, hal->showCount);
    // invalidate() forces a frame.
    leds->invalidate();
    leds->update(LedEngine::kResendMs + 3);
    TEST_ASSERT_EQUAL_INT(4, hal->showCount);
    // Re-sent at least a few times a second over a long quiet stretch.
    int before = hal->showCount;
    for (uint32_t t = 1000; t < 2000; ++t) leds->update(t);
    TEST_ASSERT_TRUE(hal->showCount - before >= 4);
    TEST_ASSERT_TRUE(hal->showCount - before <= 6);
}

void test_update_sets_every_pad_on_the_board() {
    for (PadId p = 0; p < 20; ++p) {
        spec(p).mode = LedMode::Static;
        spec(p).color = {120, 1, 1};
    }
    leds->update(0);
    for (PadId p = 0; p < 20; ++p) assertRgb(0, 255, 0, hal->leds[p]);
    assertRgb(0, 0, 0, hal->leds[20], "beyond numPads untouched");
}

void test_example_config_bank_change() {
    // Bank buttons set up as the example config does: each shows its own bank hue, the current one bright.
    view->cfg.numBanks = 4;
    float hues[4] = {120, 180, 280, 30};
    for (int b = 0; b < 4; ++b) {
        view->cfg.banks[b].hue = hues[b];
        for (int i = 0; i < 4; ++i) {
            view->cfg.pads[16 + i][b].action = PadAction::BankSelect;
            view->cfg.pads[16 + i][b].arg = uint8_t(i);
            spec(16 + i, b).mode = LedMode::State;
            spec(16 + i, b).hueFromBank = true;
        }
    }
    view->b = 2;
    view->ps[18].on = true;   // pad 19 selects bank 3, the current one
    leds->update(0);
    Hsv on = leds->padColor(18, 0), off = leds->padColor(16, 0);
    TEST_ASSERT_EQUAL_FLOAT(280, on.h);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, on.v);
    TEST_ASSERT_EQUAL_FLOAT(120, off.h);
    TEST_ASSERT_EQUAL_FLOAT(0.04f, off.v);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hsv_known_points);
    RUN_TEST(test_hsv_brightness_scales_v);
    RUN_TEST(test_mode_off_and_static);
    RUN_TEST(test_mode_level_follows_stored_value);
    RUN_TEST(test_mode_level_explicit_source_and_unlinked);
    RUN_TEST(test_mode_state);
    RUN_TEST(test_mode_midi);
    RUN_TEST(test_bank_hue);
    RUN_TEST(test_blink_in_step);
    RUN_TEST(test_fade_is_smooth_pulse);
    RUN_TEST(test_effect_on_level);
    RUN_TEST(test_pickup_indicator_turn_down);
    RUN_TEST(test_pickup_indicator_turn_up_and_brightness);
    RUN_TEST(test_pickup_indicator_uses_source_and_respects_off);
    RUN_TEST(test_press_feedback);
    RUN_TEST(test_global_brightness);
    RUN_TEST(test_unchanged_frame_not_resent_every_pass);
    RUN_TEST(test_update_sets_every_pad_on_the_board);
    RUN_TEST(test_example_config_bank_change);
    return UNITY_END();
}
