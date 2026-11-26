// Config loader tests. Run: pio test -e native_config
//
// The example data/config.json is read from disk: first relative to the working directory
// (PlatformIO runs tests from the project root), then relative to this source file.
#include <unity.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "density/model.h"
#include "../../lib/core/src/config_loader.h"
#include "fake_hal.h"

using namespace density;

static HardwareInfo proto() {
    FakeHal h;   // prototype layout: 16 knobs, 1 fader, 2 pedals, 20 pads, knob i over pad i
    return h.hw;
}

static std::string readExample() {
    std::string here = __FILE__;
    std::string dir = here.substr(0, here.find_last_of('/') + 1);
    for (const std::string& path : {std::string("data/config.json"), dir + "../../data/config.json"}) {
        std::ifstream f(path);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }
    return "";
}

static Config cfg;   // ~11 kB: keep it off the stack
static std::string err;
static HardwareInfo hw;

static bool load(const std::string& json) { return loadConfig(json, hw, cfg, err); }

// Loading must fail and the message must contain every needle.
static void expectError(const std::string& json, std::vector<const char*> needles) {
    bool ok = load(json);
    std::string msg = "json: " + json + "\nerr: " + err;
    TEST_ASSERT_FALSE_MESSAGE(ok, msg.c_str());
    for (const char* n : needles) TEST_ASSERT_TRUE_MESSAGE(err.find(n) != std::string::npos, msg.c_str());
    TEST_ASSERT_TRUE_MESSAGE(err.find('\n') == std::string::npos, "error should be one line");
}

// A group wrapped in a minimal config with 4 banks.
static std::string withGroup(const std::string& group) {
    return R"({ "banks": [ {"name":"A","hue":0}, {"name":"B","hue":90}, {"name":"C","hue":180}, {"name":"D","hue":270} ],
               "groups": [ )" + group + " ] }";
}

static void assertAddr(const MidiAddress& a, MidiType t, int ch, int num) {
    TEST_ASSERT_EQUAL_INT(int(t), int(a.type));
    TEST_ASSERT_EQUAL_INT(ch, a.channel);
    TEST_ASSERT_EQUAL_INT(num, a.number);
}

void setUp() { hw = proto(); err.clear(); }
void tearDown() {}

// ---------------------------------------------------------------- the example file

static void loadExampleOrFail() {
    std::string json = readExample();
    TEST_ASSERT_FALSE_MESSAGE(json.empty(), "could not read data/config.json");
    bool ok = load(json);
    TEST_ASSERT_TRUE_MESSAGE(ok, err.c_str());
}

void test_example_top_level() {
    loadExampleOrFail();
    TEST_ASSERT_EQUAL_STRING("Density proto", cfg.name.c_str());
    TEST_ASSERT_EQUAL_INT(4, cfg.numBanks);
    TEST_ASSERT_EQUAL_STRING("Mix", cfg.banks[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Perf", cfg.banks[3].name.c_str());
    TEST_ASSERT_EQUAL_FLOAT(180, cfg.banks[1].hue);
    TEST_ASSERT_EQUAL_FLOAT(30, cfg.banks[3].hue);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, cfg.ledBrightness);
    TEST_ASSERT_EQUAL_FLOAT(0.03f, cfg.pickupTolerance);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, cfg.filter[int(AnalogKind::Pedal)].smoothing);
    TEST_ASSERT_EQUAL_FLOAT(0.01f, cfg.filter[int(AnalogKind::Pedal)].deadband);
    TEST_ASSERT_EQUAL_FLOAT(0.2f, cfg.filter[int(AnalogKind::Knob)].smoothing);
    TEST_ASSERT_EQUAL_INT(300, cfg.filter[int(AnalogKind::Fader)].settleMs);
}

void test_example_knobs_with_bank_offset() {
    loadExampleOrFail();
    assertAddr(cfg.analog[0][0].addr, MidiType::CC, 1, 16);    // knob1, bank 1
    assertAddr(cfg.analog[15][0].addr, MidiType::CC, 1, 31);   // knob16, bank 1
    assertAddr(cfg.analog[0][1].addr, MidiType::CC, 1, 32);    // knob1, bank 2
    assertAddr(cfg.analog[2][2].addr, MidiType::CC, 1, 50);    // knob3, bank 3: 16 + 2 + 32
    assertAddr(cfg.analog[15][3].addr, MidiType::CC, 1, 79);   // knob16, bank 4
    TEST_ASSERT_EQUAL_INT(int(PickupMode::Pickup), int(cfg.analog[5][2].pickup));
    TEST_ASSERT_FALSE(cfg.analog[5][2].shared);
    TEST_ASSERT_FALSE(cfg.analog[5][2].invert);
}

void test_example_shared_fader_and_pedals() {
    loadExampleOrFail();
    for (int b = 0; b < 4; ++b) {
        assertAddr(cfg.analog[16][b].addr, MidiType::CC, 1, 7);    // fader1
        TEST_ASSERT_TRUE(cfg.analog[16][b].shared);
        assertAddr(cfg.analog[17][b].addr, MidiType::CC, 1, 11);   // pedal1
        assertAddr(cfg.analog[18][b].addr, MidiType::CC, 1, 12);   // pedal2
        TEST_ASSERT_TRUE(cfg.analog[18][b].shared);
        TEST_ASSERT_EQUAL_INT(int(PickupMode::Jump), int(cfg.analog[18][b].pickup));
    }
}

void test_example_toggle_pads() {
    loadExampleOrFail();
    TEST_ASSERT_EQUAL_INT(int(PadAction::Toggle), int(cfg.pads[0][0].action));
    assertAddr(cfg.pads[0][0].addr, MidiType::Note, 1, 36);
    assertAddr(cfg.pads[15][0].addr, MidiType::Note, 1, 51);
    assertAddr(cfg.pads[0][2].addr, MidiType::Note, 1, 68);    // pad1, bank 3
    assertAddr(cfg.pads[15][3].addr, MidiType::Note, 1, 99);   // pad16, bank 4
    TEST_ASSERT_EQUAL_INT(127, cfg.pads[3][1].onValue);
    TEST_ASSERT_EQUAL_INT(0, cfg.pads[3][1].offValue);
}

void test_example_bank_buttons() {
    loadExampleOrFail();
    for (int b = 0; b < 4; ++b)
        for (int i = 0; i < 4; ++i) {
            const PadBinding& pb = cfg.pads[16 + i][b];
            TEST_ASSERT_EQUAL_INT(int(PadAction::BankSelect), int(pb.action));
            TEST_ASSERT_EQUAL_INT(i, pb.arg);                         // first_bank 1 -> bank 0
            TEST_ASSERT_EQUAL_INT(int(MidiType::None), int(pb.addr.type));   // default note cleared
            const LedSpec& ls = cfg.leds[16 + i][b];
            TEST_ASSERT_EQUAL_INT(int(LedMode::State), int(ls.mode));
            TEST_ASSERT_TRUE(ls.hueFromBank);
        }
}

void test_example_knob_leds_link_to_pads_under() {
    loadExampleOrFail();
    for (int b = 0; b < 3; ++b)
        for (int p = 0; p < 16; ++p) {
            const LedSpec& ls = cfg.leds[p][b];
            TEST_ASSERT_EQUAL_INT(int(LedMode::Level), int(ls.mode));
            TEST_ASSERT_TRUE(ls.hueFromBank);
            TEST_ASSERT_EQUAL_INT(p, ls.source);   // padUnder[p] == p on the prototype
        }
}

void test_example_per_bank_override() {
    loadExampleOrFail();
    for (int p = 0; p < 4; ++p) {
        const LedSpec& ls = cfg.leds[p][3];   // bank 4 only
        TEST_ASSERT_EQUAL_INT(int(LedMode::Static), int(ls.mode));
        TEST_ASSERT_FALSE(ls.hueFromBank);
        TEST_ASSERT_EQUAL_FLOAT(300, ls.color.h);
        TEST_ASSERT_EQUAL_FLOAT(1.0f, ls.color.s);
        TEST_ASSERT_EQUAL_FLOAT(0.8f, ls.color.v);
        TEST_ASSERT_EQUAL_INT(int(LedEffect::Fade), int(ls.effect));
        TEST_ASSERT_EQUAL_INT(1500, ls.periodMs);
        // Fields the override did not set are kept from the earlier group.
        TEST_ASSERT_EQUAL_INT(p, ls.source);
        // Other banks untouched.
        TEST_ASSERT_EQUAL_INT(int(LedMode::Level), int(cfg.leds[p][2].mode));
        TEST_ASSERT_EQUAL_INT(int(LedEffect::None), int(cfg.leds[p][2].effect));
    }
    TEST_ASSERT_EQUAL_INT(int(LedMode::Level), int(cfg.leds[4][3].mode));   // pad5 not in the override
    // The override does not touch the pad's button binding.
    TEST_ASSERT_EQUAL_INT(int(PadAction::Toggle), int(cfg.pads[0][3].action));
}

void test_example_has_no_warnings() {
    loadExampleOrFail();
    std::vector<std::string> w = configWarnings(cfg, hw);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, int(w.size()), w.empty() ? "" : w[0].c_str());
}

// ---------------------------------------------------------------- defaults

void test_default_config() {
    defaultConfig(hw, cfg);
    TEST_ASSERT_EQUAL_INT(1, cfg.numBanks);
    assertAddr(cfg.analog[0][0].addr, MidiType::CC, 1, 16);    // knob1
    assertAddr(cfg.analog[15][0].addr, MidiType::CC, 1, 31);   // knob16
    assertAddr(cfg.analog[16][0].addr, MidiType::CC, 1, 32);   // fader1
    assertAddr(cfg.analog[18][0].addr, MidiType::CC, 1, 34);   // pedal2
    TEST_ASSERT_EQUAL_INT(int(MidiType::None), int(cfg.analog[19][0].addr.type));   // not on this board
    TEST_ASSERT_EQUAL_INT(int(PickupMode::Jump), int(cfg.analog[3][0].pickup));
    for (int p = 0; p < 16; ++p) {
        TEST_ASSERT_EQUAL_INT(int(LedMode::Level), int(cfg.leds[p][0].mode));
        TEST_ASSERT_EQUAL_INT(int(PadAction::None), int(cfg.pads[p][0].action));
    }
    for (int p = 16; p < 20; ++p) {
        TEST_ASSERT_EQUAL_INT(int(PadAction::Momentary), int(cfg.pads[p][0].action));
        assertAddr(cfg.pads[p][0].addr, MidiType::Note, 1, 36 + (p - 16));
        TEST_ASSERT_EQUAL_INT(int(LedMode::State), int(cfg.leds[p][0].mode));
    }
    TEST_ASSERT_EQUAL_INT(int(LedMode::Off), int(cfg.leds[20][0].mode));
    TEST_ASSERT_EQUAL_INT(0, int(configWarnings(cfg, hw).size()));
}

void test_empty_object_is_default() {
    TEST_ASSERT_TRUE_MESSAGE(load("{}"), err.c_str());
    assertAddr(cfg.analog[16][0].addr, MidiType::CC, 1, 32);
    TEST_ASSERT_EQUAL_INT(1, cfg.numBanks);
}

void test_top_channel_applies_to_defaults_and_groups() {
    TEST_ASSERT_TRUE_MESSAGE(load(R"({ "midi_channel": 5, "groups": [
        { "controls": "knob1", "send": { "type": "cc", "first": 1 } },
        { "controls": "knob2", "send": { "type": "cc", "first": 2, "channel": 9 } } ] })"), err.c_str());
    assertAddr(cfg.analog[0][0].addr, MidiType::CC, 5, 1);
    assertAddr(cfg.analog[1][0].addr, MidiType::CC, 9, 2);
    assertAddr(cfg.analog[2][0].addr, MidiType::CC, 5, 18);    // still default number
    assertAddr(cfg.pads[16][0].addr, MidiType::Note, 5, 36);
}

void test_failure_leaves_default() {
    TEST_ASSERT_FALSE(load(R"({ "name": "x", "banks": [{"name":"a"},{"name":"b"}], "midi_channel": 17 })"));
    TEST_ASSERT_EQUAL_INT(1, cfg.numBanks);
    TEST_ASSERT_EQUAL_STRING("Default", cfg.name.c_str());
    TEST_ASSERT_FALSE(load(withGroup(R"({ "controls": "knob1", "send": {"type":"cc","first":1} }, { "controls": "knob99" })")));
    assertAddr(cfg.analog[0][0].addr, MidiType::CC, 1, 16);
}

// ---------------------------------------------------------------- selectors and numbering

void test_parse_selector() {
    std::vector<uint8_t> ids;
    std::string e;
    TEST_ASSERT_TRUE(parseSelector("knob1-16", hw, false, ids, e));
    TEST_ASSERT_EQUAL_INT(16, int(ids.size()));
    TEST_ASSERT_EQUAL_INT(0, ids[0]);
    TEST_ASSERT_EQUAL_INT(15, ids[15]);
    ids.clear();
    TEST_ASSERT_TRUE(parseSelector("knob3", hw, false, ids, e));
    TEST_ASSERT_EQUAL_INT(1, int(ids.size()));
    TEST_ASSERT_EQUAL_INT(2, ids[0]);
    ids.clear();
    TEST_ASSERT_TRUE(parseSelector("fader1", hw, false, ids, e));
    TEST_ASSERT_EQUAL_INT(16, ids[0]);
    ids.clear();
    TEST_ASSERT_TRUE(parseSelector("pedal1-2", hw, false, ids, e));
    TEST_ASSERT_EQUAL_INT(2, int(ids.size()));
    TEST_ASSERT_EQUAL_INT(17, ids[0]);
    TEST_ASSERT_EQUAL_INT(18, ids[1]);
    ids.clear();
    TEST_ASSERT_TRUE(parseSelector("pad17-20", hw, true, ids, e));
    TEST_ASSERT_EQUAL_INT(4, int(ids.size()));
    TEST_ASSERT_EQUAL_INT(16, ids[0]);
    ids.clear();
    TEST_ASSERT_TRUE(parseSelector(" Knob 2 - 3 ", hw, false, ids, e));   // forgiving about case and spaces
    TEST_ASSERT_EQUAL_INT(2, int(ids.size()));
    TEST_ASSERT_EQUAL_INT(1, ids[0]);

    const char* bad[] = {"knob17", "knob0", "knob5-2", "slider1", "knob", "knob1-", "knob1x", "fader2", "pedal3", "", "pad1"};
    for (const char* b : bad) {
        ids.clear();
        e.clear();
        TEST_ASSERT_FALSE_MESSAGE(parseSelector(b, hw, false, ids, e), b);
        TEST_ASSERT_FALSE(e.empty());
    }
    ids.clear();
    TEST_ASSERT_FALSE(parseSelector("pad21", hw, true, ids, e));
    TEST_ASSERT_TRUE(e.find("20 pads") != std::string::npos);
    TEST_ASSERT_FALSE(parseSelector("knob1", hw, true, ids, e));
}

void test_selector_array_in_group() {
    TEST_ASSERT_TRUE_MESSAGE(load(withGroup(R"({ "controls": ["knob1-4", "knob9-12"], "send": {"type":"cc","first":100} })")), err.c_str());
    assertAddr(cfg.analog[0][0].addr, MidiType::CC, 1, 100);
    assertAddr(cfg.analog[3][0].addr, MidiType::CC, 1, 103);
    assertAddr(cfg.analog[8][0].addr, MidiType::CC, 1, 104);    // members count on across the list
    assertAddr(cfg.analog[11][0].addr, MidiType::CC, 1, 107);
    assertAddr(cfg.analog[4][0].addr, MidiType::CC, 1, 20);     // knob5 untouched: default
}

void test_one_based_banks_and_first_bank() {
    TEST_ASSERT_TRUE_MESSAGE(load(withGroup(R"(
        { "controls": "knob3", "banks": [2, 4], "send": {"type":"note","first":60} },
        { "pads": "pad1-3", "action": "bank_select", "first_bank": 2 })")), err.c_str());
    assertAddr(cfg.analog[2][1].addr, MidiType::Note, 1, 60);   // bank 2 -> index 1
    assertAddr(cfg.analog[2][3].addr, MidiType::Note, 1, 60);
    assertAddr(cfg.analog[2][0].addr, MidiType::CC, 1, 18);     // bank 1 untouched
    assertAddr(cfg.analog[2][2].addr, MidiType::CC, 1, 18);
    TEST_ASSERT_EQUAL_INT(1, cfg.pads[0][0].arg);
    TEST_ASSERT_EQUAL_INT(3, cfg.pads[2][0].arg);
}

void test_bank_offset_only_with_banks_and_shared_ignores_it() {
    TEST_ASSERT_TRUE_MESSAGE(load(withGroup(R"(
        { "controls": "knob1-2", "send": {"type":"cc","first":20,"bank_offset":10} },
        { "controls": "knob3", "shared": true, "send": {"type":"cc","first":90,"bank_offset":10} })")), err.c_str());
    assertAddr(cfg.analog[1][0].addr, MidiType::CC, 1, 21);
    assertAddr(cfg.analog[1][3].addr, MidiType::CC, 1, 51);
    for (int b = 0; b < 4; ++b) {
        assertAddr(cfg.analog[2][b].addr, MidiType::CC, 1, 90);
        TEST_ASSERT_TRUE(cfg.analog[2][b].shared);
    }
}

void test_group_with_controls_and_pads() {
    TEST_ASSERT_TRUE_MESSAGE(load(withGroup(R"(
        { "controls": "knob1-2", "pads": "pad17-18", "action": "momentary", "on_value": 100, "off_value": 5,
          "send": {"type":"cc","first":40}, "led": { "mode": "static", "hue": 10 } })")), err.c_str());
    assertAddr(cfg.analog[1][0].addr, MidiType::CC, 1, 41);
    assertAddr(cfg.pads[16][0].addr, MidiType::CC, 1, 42);    // pads continue after the controls
    assertAddr(cfg.pads[17][0].addr, MidiType::CC, 1, 43);
    TEST_ASSERT_EQUAL_INT(100, cfg.pads[16][0].onValue);
    TEST_ASSERT_EQUAL_INT(5, cfg.pads[16][0].offValue);
    // led goes to pads under knob1-2 and to pad17-18
    TEST_ASSERT_EQUAL_INT(int(LedMode::Static), int(cfg.leds[0][0].mode));
    TEST_ASSERT_EQUAL_INT(0, cfg.leds[0][0].source);
    TEST_ASSERT_EQUAL_INT(int(LedMode::Static), int(cfg.leds[17][0].mode));
    TEST_ASSERT_EQUAL_FLOAT(10, cfg.leds[17][0].color.h);
    TEST_ASSERT_EQUAL_INT(int(LedMode::Level), int(cfg.leds[2][0].mode));   // pad3 untouched
}

void test_led_fields_and_source() {
    TEST_ASSERT_TRUE_MESSAGE(load(withGroup(R"(
        { "pads": "pad20", "led": { "mode": "level", "source": "fader1", "hue": 45, "sat": 0.5, "val": 0.7,
                                    "floor": 0.1, "effect": "blink", "period_ms": 250 } },
        { "pads": "pad19", "led": { "mode": "midi" } },
        { "pads": "pad18", "led": { "mode": "off" } })")), err.c_str());
    const LedSpec& ls = cfg.leds[19][2];
    TEST_ASSERT_EQUAL_INT(int(LedMode::Level), int(ls.mode));
    TEST_ASSERT_EQUAL_INT(16, ls.source);
    TEST_ASSERT_EQUAL_FLOAT(45, ls.color.h);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, ls.color.s);
    TEST_ASSERT_EQUAL_FLOAT(0.7f, ls.color.v);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, ls.floorV);
    TEST_ASSERT_EQUAL_INT(int(LedEffect::Blink), int(ls.effect));
    TEST_ASSERT_EQUAL_INT(250, ls.periodMs);
    TEST_ASSERT_EQUAL_INT(int(LedMode::Midi), int(cfg.leds[18][0].mode));
    TEST_ASSERT_EQUAL_INT(int(LedMode::Off), int(cfg.leds[17][0].mode));
}

void test_pickup_invert_calibration_filter() {
    TEST_ASSERT_TRUE_MESSAGE(load(R"({
        "groups": [ { "controls": "knob1", "pickup": "pickup", "invert": true } ],
        "calibration": { "pedal1": [100, 3900], "knob2": [0, 4095] },
        "filter": { "fader": { "settle_ms": 120 } } })"), err.c_str());
    TEST_ASSERT_EQUAL_INT(int(PickupMode::Pickup), int(cfg.analog[0][0].pickup));
    TEST_ASSERT_TRUE(cfg.analog[0][0].invert);
    assertAddr(cfg.analog[0][0].addr, MidiType::CC, 1, 16);    // send not given: default kept
    TEST_ASSERT_EQUAL_INT(100, cfg.cal[17].rawMin);
    TEST_ASSERT_EQUAL_INT(3900, cfg.cal[17].rawMax);
    TEST_ASSERT_EQUAL_INT(0, cfg.cal[1].rawMin);
    TEST_ASSERT_EQUAL_INT(40, cfg.cal[0].rawMin);              // untouched default
    TEST_ASSERT_EQUAL_INT(120, cfg.filter[int(AnalogKind::Fader)].settleMs);
    TEST_ASSERT_EQUAL_FLOAT(0.2f, cfg.filter[int(AnalogKind::Fader)].smoothing);
}

void test_comments_and_note_keys_allowed() {
    TEST_ASSERT_TRUE_MESSAGE(load(R"({
        // a line comment
        "_note": "ignored", "comment": "also ignored",
        /* block */ "groups": [ { "controls": "knob1", "_why": "x", "send": {"type":"cc","first":1} } ] })"), err.c_str());
    assertAddr(cfg.analog[0][0].addr, MidiType::CC, 1, 1);
}

// ---------------------------------------------------------------- errors

void test_error_malformed_json() {
    expectError(R"({ "name": "x", )", {"not valid JSON", "IncompleteInput"});
    expectError(R"({ "name": "x" "banks": [] })", {"not valid JSON", "InvalidInput"});
    expectError("[1,2]", {"JSON object"});
}

void test_error_unknown_selector_kind_names_group() {
    expectError(withGroup(R"({ "name": "sliders", "controls": "slider1-4" })"), {"group 1", "\"sliders\"", "slider", "unknown kind"});
}

void test_error_index_beyond_board() {
    expectError(withGroup(R"({ "name": "k", "controls": "knob1-17" })"), {"\"k\"", "16 knobs"});
    expectError(withGroup(R"({ "pads": "pad21" })"), {"group 1", "20 pads"});
    expectError(withGroup(R"({ "controls": "fader1" }, { "controls": "fader2" })"), {"group 2", "1 fader"});
}

void test_error_midi_range() {
    expectError(withGroup(R"({ "controls": "knob1", "send": {"type":"cc","first":128} })"), {"first", "128", "0 to 127"});
    expectError(withGroup(R"({ "controls": "knob1-16", "send": {"type":"cc","first":120} })"), {"past 127", "knob16"});
    expectError(withGroup(R"({ "controls": "knob1-16", "send": {"type":"cc","first":16,"bank_offset":33} })"),
                {"past 127", "bank 4", "130"});
    expectError(withGroup(R"({ "controls": "knob1", "send": {"type":"cc","first":1,"channel":0} })"), {"channel", "1 to 16"});
    expectError(R"({ "midi_channel": 17 })", {"midi_channel", "17"});
    expectError(withGroup(R"({ "pads": "pad1", "action": "toggle", "on_value": 200 })"), {"on_value"});
    expectError(withGroup(R"({ "controls": "knob1", "send": {"type":"cc","first":1.5} })"), {"whole number"});
    expectError(withGroup(R"({ "controls": "knob1", "send": {"type":"sysex","first":1} })"), {"send type", "sysex"});
    expectError(withGroup(R"({ "controls": "knob1", "send": {"first":1} })"), {"\"type\" and \"first\""});
}

void test_error_banks() {
    std::string nine = R"({ "banks": [)";
    for (int i = 0; i < 9; ++i) nine += std::string(i ? "," : "") + R"({"name":"b"})";
    nine += "] }";
    expectError(nine, {"9 banks", "at most 8"});
    expectError(withGroup(R"({ "name": "late", "controls": "knob1", "banks": [5] })"), {"\"late\"", "bank 5", "4 banks"});
    expectError(withGroup(R"({ "controls": "knob1", "banks": [0] })"), {"bank 0"});
    expectError(withGroup(R"({ "pads": "pad1-5", "action": "bank_select" })"), {"pad5", "bank 5"});
    expectError(R"({ "groups": [ { "pads": "pad17-18", "action": "bank_select" } ] })", {"pad18", "bank 2", "1 bank is"});
    expectError(R"({ "banks": [ {"name":"a","hue":400} ] })", {"bank 1", "hue", "0 to 360"});
}

void test_error_unknown_strings() {
    expectError(withGroup(R"({ "pads": "pad1", "led": { "mode": "rainbow" } })"), {"led mode", "rainbow"});
    expectError(withGroup(R"({ "pads": "pad1", "action": "launch" })"), {"action", "launch"});
    expectError(withGroup(R"({ "pads": "pad1", "led": { "effect": "strobe" } })"), {"effect", "strobe"});
    expectError(withGroup(R"({ "controls": "knob1", "pickup": "catch" })"), {"pickup", "catch"});
    expectError(withGroup(R"({ "pads": "pad1", "led": { "hue": "red" } })"), {"hue", "red"});
    expectError(withGroup(R"({ "controls": "knob1", "pikcup": "jump" })"), {"unknown group setting", "pikcup"});
    expectError(R"({ "brightness": 1 })", {"unknown top-level setting", "brightness"});
    expectError(withGroup(R"({ "pads": "pad1", "led": { "colour": 3 } })"), {"unknown led setting", "colour"});
}

void test_error_misplaced_fields() {
    expectError(withGroup(R"({ "controls": "pad1" })"), {"pads go under"});
    expectError(withGroup(R"({ "pads": "knob1" })"), {"not a pad"});
    expectError(withGroup(R"({ "controls": "knob1", "action": "toggle" })"), {"'action' applies to \"pads\""});
    expectError(withGroup(R"({ "pads": "pad1", "invert": true })"), {"'invert' applies to \"controls\""});
    expectError(withGroup(R"({ "name": "nothing" })"), {"\"nothing\"", "needs \"controls\" or \"pads\""});
    expectError(withGroup(R"({ "controls": "fader1", "led": { "mode": "level" } })"), {"none of these controls has a pad"});
    expectError(withGroup(R"({ "pads": "pad1", "led": { "source": "knob1-2" } })"), {"one control"});
    expectError(withGroup(R"({ "controls": ["knob1-4", "knob3"] })"), {"knob3 is listed twice"});
    expectError(withGroup(R"({ "pads": "pad17", "action": "bank_next" }, { "pads": "pad17", "action": "toggle" })"),
                {"group 2", "pad17", "needs a 'send'"});
    expectError(R"({ "calibration": { "pedal1": [3000, 100] } })", {"pedal1", "less than"});
    expectError(R"({ "calibration": { "pedal1": 100 } })", {"pair"});
}

// ---------------------------------------------------------------- warnings

void test_duplicate_address_warning() {
    TEST_ASSERT_TRUE_MESSAGE(load(withGroup(R"(
        { "controls": "knob1", "send": {"type":"cc","first":7} },
        { "controls": "fader1", "shared": true, "send": {"type":"cc","first":7} })")), err.c_str());
    std::vector<std::string> w = configWarnings(cfg, hw);
    TEST_ASSERT_EQUAL_INT(4, int(w.size()));   // knob1 is banked, so one per bank
    TEST_ASSERT_TRUE_MESSAGE(w[0].find("knob1 and fader1") != std::string::npos, w[0].c_str());
    TEST_ASSERT_TRUE_MESSAGE(w[0].find("CC 7 on channel 1") != std::string::npos, w[0].c_str());
    TEST_ASSERT_TRUE_MESSAGE(w[0].find("bank 1 (A)") != std::string::npos, w[0].c_str());

    // Different type or channel is not a clash.
    TEST_ASSERT_TRUE(load(withGroup(R"(
        { "controls": "knob1", "send": {"type":"note","first":7} },
        { "controls": "knob2", "send": {"type":"cc","first":7,"channel":2} },
        { "controls": "fader1", "send": {"type":"cc","first":7} })")));
    TEST_ASSERT_EQUAL_INT(0, int(configWarnings(cfg, hw).size()));

    // Two shared bindings clash in every bank: reported once.
    TEST_ASSERT_TRUE(load(withGroup(R"({ "controls": ["pedal1", "pedal2"], "shared": true, "send": {"type":"cc","first":1} },
                                       { "controls": "pedal2", "send": {"type":"cc","first":1} })")));
    w = configWarnings(cfg, hw);
    TEST_ASSERT_EQUAL_INT(1, int(w.size()));
    TEST_ASSERT_TRUE(w[0].find("all banks") != std::string::npos);
}

// ---- JSON syntax: where the mistake is ----

void test_json_error_trailing_comma_names_line() {
    expectError("{\n  \"name\": \"x\",\n  \"groups\": [\n    {\"controls\":\"knob1\",\"send\":{\"type\":\"cc\",\"first\":1}},\n  ]\n}",
                {"not valid JSON", "line 4", "trailing comma"});
    expectError("{ \"name\": \"x\", }", {"line 1", "trailing comma"});
}

void test_json_error_unbalanced_and_quotes_name_line() {
    expectError("{\n  \"groups\": [\n    { \"controls\": \"knob1\" }\n}\n", {"line 4", "'}'", "'['", "line 2"});
    expectError("{\n  \"name\": \"x\n}\n", {"line 2", "quotes", "not closed"});
    expectError("{\n  \"banks\": [\n", {"'[' opened on line 2", "never closed"});
    expectError("{\n  \"name\": \"x\"\n  \"midi_channel\": 2\n}", {"line 3", "missing comma"});
}

void test_json_error_without_location_has_hint() {
    // Nothing the pre-scan recognises: the message still says what to look for.
    expectError("{ \"name\": tru }", {"not valid JSON", "trailing comma or a missing quote"});
}

void test_utf8_bom_is_skipped() {
    TEST_ASSERT_TRUE_MESSAGE(load("\xEF\xBB\xBF{\"name\":\"x\"}"), err.c_str());
    TEST_ASSERT_EQUAL_STRING("x", cfg.name.c_str());
}

// ---- range check uses the effective shared flag ----

void test_range_check_uses_shared_from_earlier_group() {
    // fader1 is made shared first; a later send with bank_offset applies no offset to it,
    // so 120 is fine (and is what is applied in every bank).
    const char* json = R"({ "banks": [{},{},{},{}], "groups": [
        { "controls": "fader1", "shared": true },
        { "controls": "fader1", "send": { "type": "cc", "first": 120, "bank_offset": 16 } } ] })";
    TEST_ASSERT_TRUE_MESSAGE(load(json), err.c_str());
    for (uint8_t b = 0; b < 4; ++b) assertAddr(cfg.analog[16][b].addr, MidiType::CC, 1, 120);
    // And the other way round: banked from before, the error names the member and bank.
    expectError(R"({ "banks": [{},{},{},{}], "groups": [
        { "name": "knob buttons", "pads": "pad1-16", "action": "toggle",
          "send": { "type": "note", "first": 100, "bank_offset": 16 } } ] })",
                {"\"knob buttons\"", "pad16 in bank 4 would be 163"});
}

// ---- level LED with nothing to follow ----

void test_error_level_led_without_source_on_unlinked_pad() {
    expectError(R"({ "groups": [ { "pads": "pad17", "led": { "mode": "level" } } ] })",
                {"pad17", "level", "source"});
    // With a source it loads; a pad under a knob needs none.
    TEST_ASSERT_TRUE_MESSAGE(load(R"({ "groups": [ { "pads": "pad17", "led": { "mode": "level", "source": "fader1" } } ] })"),
                             err.c_str());
    TEST_ASSERT_TRUE_MESSAGE(load(R"({ "groups": [ { "pads": "pad3", "led": { "mode": "level" } } ] })"), err.c_str());
}

// ---- lower limits ----

void test_error_values_too_small() {
    expectError(R"({ "pickup_tolerance": 0 })", {"pickup_tolerance", "0.012"});
    expectError(R"({ "pickup_tolerance": 0.01 })", {"pickup_tolerance", "at least 0.012"});
    TEST_ASSERT_TRUE_MESSAGE(load(R"({ "pickup_tolerance": 0.012 })"), err.c_str());
    expectError(R"({ "filter": { "knob": { "deadband": 0 } } })", {"filter.knob", "deadband", "0.001"});
    TEST_ASSERT_TRUE_MESSAGE(load(R"({ "filter": { "knob": { "deadband": 0.001 } } })"), err.c_str());
    expectError(R"({ "filter": { "pedal": { "settle_ms": 5 } } })", {"filter.pedal", "settle_ms", "at least 20"});
    TEST_ASSERT_TRUE_MESSAGE(load(R"({ "filter": { "pedal": { "settle_ms": 20 } } })"), err.c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_example_top_level);
    RUN_TEST(test_example_knobs_with_bank_offset);
    RUN_TEST(test_example_shared_fader_and_pedals);
    RUN_TEST(test_example_toggle_pads);
    RUN_TEST(test_example_bank_buttons);
    RUN_TEST(test_example_knob_leds_link_to_pads_under);
    RUN_TEST(test_example_per_bank_override);
    RUN_TEST(test_example_has_no_warnings);
    RUN_TEST(test_default_config);
    RUN_TEST(test_empty_object_is_default);
    RUN_TEST(test_top_channel_applies_to_defaults_and_groups);
    RUN_TEST(test_failure_leaves_default);
    RUN_TEST(test_parse_selector);
    RUN_TEST(test_selector_array_in_group);
    RUN_TEST(test_one_based_banks_and_first_bank);
    RUN_TEST(test_bank_offset_only_with_banks_and_shared_ignores_it);
    RUN_TEST(test_group_with_controls_and_pads);
    RUN_TEST(test_led_fields_and_source);
    RUN_TEST(test_pickup_invert_calibration_filter);
    RUN_TEST(test_comments_and_note_keys_allowed);
    RUN_TEST(test_error_malformed_json);
    RUN_TEST(test_error_unknown_selector_kind_names_group);
    RUN_TEST(test_error_index_beyond_board);
    RUN_TEST(test_error_midi_range);
    RUN_TEST(test_error_banks);
    RUN_TEST(test_error_unknown_strings);
    RUN_TEST(test_error_misplaced_fields);
    RUN_TEST(test_duplicate_address_warning);
    RUN_TEST(test_json_error_trailing_comma_names_line);
    RUN_TEST(test_json_error_unbalanced_and_quotes_name_line);
    RUN_TEST(test_json_error_without_location_has_hint);
    RUN_TEST(test_utf8_bom_is_skipped);
    RUN_TEST(test_range_check_uses_shared_from_earlier_group);
    RUN_TEST(test_error_level_led_without_source_on_unlinked_pad);
    RUN_TEST(test_error_values_too_small);
    return UNITY_END();
}
