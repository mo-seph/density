// End-to-end: the real config file, loader, engine, LED engine and UI together on FakeHal.
#include <unity.h>
#include <fstream>
#include <sstream>
#include "../../lib/core/src/density_app.h"
#include "fake_hal.h"

using namespace density;

static std::string readExample() {
    for (const char* p : {"data/config.json", "../data/config.json", "../../data/config.json"}) {
        std::ifstream f(p);
        if (f) { std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
    }
    return "";
}

static void run(FakeHal& hal, App& app, int ms) {
    for (int i = 0; i < ms; ++i) { hal.advance(1); app.loop(); }
}

// Move a control smoothly to a raw value, the way a hand would.
static void sweep(FakeHal& hal, App& app, AnalogId a, int to, int stepsMs = 200) {
    int from = hal.raw[a];
    for (int i = 1; i <= stepsMs; ++i) {
        hal.setAnalog(a, uint16_t(from + (to - from) * i / stepsMs));
        hal.advance(1);
        app.loop();
    }
    run(hal, app, 50);
}

static const MidiMsg* lastSent(FakeHal& hal, MidiType t, uint8_t number) {
    for (auto it = hal.sent.rbegin(); it != hal.sent.rend(); ++it)
        if (it->type == t && it->number == number) return &*it;
    return nullptr;
}

void setUp() {}
void tearDown() {}

static void test_missing_config_uses_defaults_and_says_so() {
    FakeHal hal;
    App app(hal);
    app.setup();
    TEST_ASSERT_FALSE(app.message().empty());
    run(hal, app, 200);
    TEST_ASSERT_EQUAL(0, (int)hal.sent.size());
    sweep(hal, app, 0, 4055);
    const MidiMsg* m = lastSent(hal, MidiType::CC, 16);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL(127, m->value);
}

// Every string drawn on a display, joined with spaces (wrapped lines read as one text).
static std::string joined(FakeDisplay& d) {
    std::string out;
    for (const auto& t : d.textItems) { if (!out.empty()) out += ' '; out += t.s; }
    return out;
}

static void test_bad_config_reports_error() {
    FakeHal hal;
    hal.files["/config.json"] = "{ \"banks\": [ { \"name\": 3 ";
    App app(hal);
    app.setup();
    // The screen message is the loader's text without a "config.json: " prefix ...
    TEST_ASSERT_TRUE_MESSAGE(app.message().find("not valid JSON") != std::string::npos, app.message().c_str());
    TEST_ASSERT_TRUE_MESSAGE(app.message().find("never closed") != std::string::npos, app.message().c_str());
    TEST_ASSERT_TRUE(app.message().rfind("config.json", 0) != 0);
    // ... the log line keeps it.
    bool logged = false;
    for (auto& l : hal.logs) logged = logged || l == "config.json: " + app.message();
    TEST_ASSERT_TRUE(logged);
    run(hal, app, 100);
    TEST_ASSERT_EQUAL(Ui::kAbout, app.ui().pageIndex());
    TEST_ASSERT_TRUE_MESSAGE(joined(hal.fakeDisplay(0)).find(app.message()) != std::string::npos,
                             hal.fakeDisplay(0).allText().c_str());
}

static void test_config_range_error_detail_visible() {
    FakeHal hal;
    hal.files["/config.json"] = R"({"banks":[{},{},{},{}],
        "groups":[{"name":"knob buttons","pads":"pad1-16","action":"toggle","send":{"type":"note","first":100,"bank_offset":16}}]})";
    App app(hal);
    app.setup();
    run(hal, app, 200);
    TEST_ASSERT_TRUE_MESSAGE(joined(hal.fakeDisplay(0)).find("pad16 in bank 4 would be 163") != std::string::npos,
                             hal.fakeDisplay(0).allText().c_str());
}

static void encPress(FakeHal& hal, App& app) {
    hal.setEncoderPressed(0, true);  run(hal, app, 20);
    hal.setEncoderPressed(0, false); run(hal, app, 20);
}
// A move without the trailing pause of sweep().
static void glide(FakeHal& hal, App& app, AnalogId a, int to, int ms) {
    int from = hal.raw[a];
    for (int i = 1; i <= ms; ++i) {
        hal.setAnalog(a, uint16_t(from + (to - from) * i / ms));
        hal.advance(1);
        app.loop();
    }
}
static int count(FakeHal& hal, MidiType t, uint8_t number) {
    int n = 0;
    for (auto& m : hal.sent) n += (m.type == t && m.number == number);
    return n;
}

static void test_pedal_calibration_ignores_spike() {
    FakeHal hal;
    hal.files["/config.json"] = "{}";
    hal.setAllAnalog(2000);
    hal.setAnalog(17, 500);
    App app(hal);
    app.setup();
    run(hal, app, 50);
    hal.turn(0, 2); run(hal, app, 20);   // -> Pedal cal page
    TEST_ASSERT_EQUAL(Ui::kCalibration, app.ui().pageIndex());
    encPress(hal, app);                  // enter page
    encPress(hal, app);                  // start pedal 1
    TEST_ASSERT_TRUE(app.ui().calibrationPage().running());
    glide(hal, app, 17, 3500, 300);
    hal.setAnalog(17, 4095); run(hal, app, 1);   // one-sample glitch
    hal.setAnalog(17, 3500); run(hal, app, 50);
    glide(hal, app, 17, 500, 300);
    encPress(hal, app);                  // save
    AnalogCal c = app.engine().calibration(17);
    TEST_ASSERT_TRUE(c.rawMax < 3500);
    hal.clearSent();
    sweep(hal, app, 17, 3500, 300);
    run(hal, app, 400);
    const MidiMsg* m = lastSent(hal, MidiType::CC, 33);   // default: pedal 1 = CC 33
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_MESSAGE(127, m->value, "pedal at its real full travel does not reach 127");
}

static void test_pedal_calibration_cancel_sends_nothing() {
    FakeHal hal;
    hal.files["/config.json"] = "{}";   // pedals in jump mode
    hal.setAllAnalog(2000);
    App app(hal);
    app.setup();
    run(hal, app, 50);
    hal.turn(0, 2); run(hal, app, 20);
    encPress(hal, app);
    encPress(hal, app);
    TEST_ASSERT_TRUE(app.ui().calibrationPage().running());
    glide(hal, app, 17, 3500, 200);
    // Hold the encoder while still moving the pedal: the long press (cancel) fires 50 ms
    // after the pedal stops, inside the filter's settle time.
    hal.setEncoderPressed(0, true);
    glide(hal, app, 17, 3000, int(Ui::kLongPressMs) - 50);
    TEST_ASSERT_TRUE(app.ui().calibrationPage().running());
    TEST_ASSERT_TRUE(app.engine().analog(17).moving);
    run(hal, app, 100);
    TEST_ASSERT_FALSE(app.ui().calibrationPage().running());
    TEST_ASSERT_TRUE(app.engine().analog(17).moving);
    hal.setEncoderPressed(0, false);
    run(hal, app, 1000);
    TEST_ASSERT_EQUAL(0, (int)hal.sent.size());
}

static void test_cal_file_before_boot_sends_nothing() {
    FakeHal hal;
    hal.files["/config.json"] = R"({"groups":[{"controls":"pedal1-2","pickup":"pickup"}]})";
    hal.files["/cal.json"] = "{\"pedals\":[[500,3000],[600,3500]]}";
    hal.setAllAnalog(2000);
    App app(hal);
    app.setup();
    run(hal, app, 1000);
    TEST_ASSERT_EQUAL(0, (int)hal.sent.size());
    TEST_ASSERT_TRUE(app.engine().analog(17).pickedUp);
    TEST_ASSERT_TRUE(app.engine().analog(18).pickedUp);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 1500 / 2500.0f, app.engine().analog(17).stored);
}

static void test_bank_select_pad_with_send_tells_host() {
    FakeHal hal;
    hal.files["/config.json"] = R"({"banks":[{"name":"A"},{"name":"B"}],
        "groups":[{"pads":"pad17-18","action":"bank_select","send":{"type":"cc","first":100}}]})";
    hal.setAllAnalog(2000);
    App app(hal);
    app.setup();
    TEST_ASSERT_EQUAL_STRING("", app.message().c_str());
    run(hal, app, 50);
    hal.setPad(17, true);  run(hal, app, 20);
    hal.setPad(17, false); run(hal, app, 20);
    TEST_ASSERT_EQUAL(1, app.engine().bank());
    TEST_ASSERT_EQUAL(1, count(hal, MidiType::CC, 101));
    TEST_ASSERT_EQUAL(127, lastSent(hal, MidiType::CC, 101)->value);
    TEST_ASSERT_EQUAL(1, (int)hal.sent.size());
}

static void test_bank_change_while_knob_moving_waits_for_pickup() {
    FakeHal hal;
    hal.files["/config.json"] = R"({"banks":[{},{}],"groups":[
        {"controls":"knob1","pickup":"pickup","send":{"type":"cc","first":16,"bank_offset":16}},
        {"pads":"pad17-18","action":"bank_select"}]})";
    hal.setAllAnalog(2000);
    App app(hal);
    app.setup();
    TEST_ASSERT_EQUAL_STRING("", app.message().c_str());
    run(hal, app, 50);
    // Bank 2's stored value for knob 1 is its boot value, ~0.49. Knob to the bottom in bank 1,
    glide(hal, app, 0, 100, 200);
    // then up, switching bank half way.
    glide(hal, app, 0, 800, 100);
    hal.setPad(17, true);
    glide(hal, app, 0, 1500, 100);
    hal.setPad(17, false);
    TEST_ASSERT_EQUAL(1, app.engine().bank());
    TEST_ASSERT_EQUAL_MESSAGE(0, count(hal, MidiType::CC, 32), "sent on bank 2 before pickup");
    glide(hal, app, 0, 2600, 100);
    TEST_ASSERT_TRUE(count(hal, MidiType::CC, 32) > 0);
    const MidiMsg* first = nullptr;
    for (auto& m : hal.sent) if (m.number == 32) { first = &m; break; }
    TEST_ASSERT_INT_WITHIN(5, 63, first->value);
}

static void test_example_config_end_to_end() {
    std::string json = readExample();
    TEST_ASSERT_FALSE_MESSAGE(json.empty(), "data/config.json not found");
    FakeHal hal;
    hal.files["/config.json"] = json;
    hal.setAllAnalog(40);            // everything at minimum
    hal.setAnalog(17, 0); hal.setAnalog(18, 0);   // pedals unplugged
    App app(hal);
    app.setup();
    TEST_ASSERT_EQUAL_STRING("", app.message().c_str());
    TEST_ASSERT_EQUAL(4, app.config().numBanks);

    // Idle: nothing is sent.
    run(hal, app, 500);
    TEST_ASSERT_EQUAL(0, (int)hal.sent.size());

    // Bank 1: knob 3 (id 2) sends CC 18, up to full.
    sweep(hal, app, 2, 4055);
    const MidiMsg* m = lastSent(hal, MidiType::CC, 18);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL(127, m->value);
    TEST_ASSERT_EQUAL(1, m->channel);

    // Its pad's LED (pad 3, id 2) is lit in bank 1's hue (120 = green).
    Rgb c = hal.leds[2];
    TEST_ASSERT_TRUE(c.g > 60);
    TEST_ASSERT_TRUE(c.g > c.r && c.g > c.b);
    // An untouched knob's pad is much dimmer.
    TEST_ASSERT_TRUE(hal.leds[5].g < c.g / 2);

    // Press pad 18 (id 17): selects bank 2.
    hal.clearSent();
    hal.setPad(17, true);  run(hal, app, 30);
    hal.setPad(17, false); run(hal, app, 30);
    TEST_ASSERT_EQUAL(1, app.engine().bank());
    TEST_ASSERT_EQUAL(0, (int)hal.sent.size());   // a bank change alone sends nothing

    // In bank 2 knob 3's stored value is still its boot value (0); the knob is at full,
    // so it is not picked up: sends nothing while moving until it reaches the stored value.
    AnalogState st = app.engine().analog(2);
    TEST_ASSERT_FALSE(st.pickedUp);
    TEST_ASSERT_EQUAL(-1, st.direction);
    sweep(hal, app, 2, 2000);
    TEST_ASSERT_EQUAL(0, (int)hal.sent.size());
    sweep(hal, app, 2, 40);                        // reaches stored 0: picks up
    TEST_ASSERT_TRUE(app.engine().analog(2).pickedUp);
    sweep(hal, app, 2, 2048);
    m = lastSent(hal, MidiType::CC, 34);           // 16 + 2 + bank_offset 16
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_INT_WITHIN(2, 63, m->value);

    // Back to bank 1: stored value there is still full.
    hal.setPad(16, true);  run(hal, app, 30);
    hal.setPad(16, false); run(hal, app, 30);
    TEST_ASSERT_EQUAL(0, app.engine().bank());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, app.engine().analog(2).stored);
    TEST_ASSERT_FALSE(app.engine().analog(2).pickedUp);

    // Host changes knob 5 (id 4, CC 20) in bank 1: stored follows, knob needs picking up.
    hal.injectCC(1, 20, 100);
    run(hal, app, 20);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100 / 127.0f, app.engine().analog(4).stored);
    TEST_ASSERT_FALSE(app.engine().analog(4).pickedUp);

    // The fader is shared: sends CC 7 in any bank, no pickup needed after a bank change.
    hal.clearSent();
    sweep(hal, app, 16, 3000);
    TEST_ASSERT_NOT_NULL(lastSent(hal, MidiType::CC, 7));

    // Displays were drawn and the LEDs were shown.
    TEST_ASSERT_TRUE(hal.fakeDisplay(0).flushes > 0);
    TEST_ASSERT_TRUE(hal.showCount > 0);
    // One poll per loop pass.
    TEST_ASSERT_TRUE(hal.pollCount > 1000);
}

static void test_calibration_file_is_applied_at_start() {
    FakeHal hal;
    hal.files["/cal.json"] = "{\"pedals\":[[500,3000],[0,0]]}";
    App app(hal);
    app.setup();
    AnalogCal c = app.engine().calibration(17);
    TEST_ASSERT_EQUAL(500, c.rawMin);
    TEST_ASSERT_EQUAL(3000, c.rawMax);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_missing_config_uses_defaults_and_says_so);
    RUN_TEST(test_bad_config_reports_error);
    RUN_TEST(test_config_range_error_detail_visible);
    RUN_TEST(test_pedal_calibration_ignores_spike);
    RUN_TEST(test_pedal_calibration_cancel_sends_nothing);
    RUN_TEST(test_cal_file_before_boot_sends_nothing);
    RUN_TEST(test_bank_select_pad_with_send_tells_host);
    RUN_TEST(test_bank_change_while_knob_moving_waits_for_pickup);
    RUN_TEST(test_example_config_end_to_end);
    RUN_TEST(test_calibration_file_is_applied_at_start);
    return UNITY_END();
}
