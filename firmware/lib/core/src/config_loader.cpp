// JSON configuration -> density::Config. See CONFIG.md for the format.
//
// The JSON is written by hand, so the loader is strict (unknown keys and out-of-range
// values are errors, with a message naming the group) and speaks the board's 1-based
// numbering in messages. The JsonDocument lives only inside loadConfig().

// Allow // and /* */ comments in the file. ArduinoJson puts its settings into its
// inline namespace, so this cannot clash with other translation units.
#define ARDUINOJSON_ENABLE_COMMENTS 1
#include <ArduinoJson.h>

#include "config_loader.h"
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace density {

namespace {

constexpr float kDefaultHue = 200;        // a calm blue for the built-in config
constexpr uint8_t kDefaultFirstCC = 16;   // first general-purpose CC
constexpr uint8_t kDefaultFirstNote = 36; // C1, the usual first drum pad
constexpr float kMinPickupTolerance = 0.012f;   // below this a pot may never pick up (engine.h)
constexpr float kMinDeadband = 0.001f;          // below this ADC noise counts as touching
constexpr long kMinSettleMs = 20;

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

std::string numStr(float x) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", double(x));
    return buf;
}

bool isMidiAction(PadAction a) { return a == PadAction::Momentary || a == PadAction::Toggle; }

const char* typeName(MidiType t) { return t == MidiType::CC ? "CC" : t == MidiType::Note ? "note" : "nothing"; }

// ---------------------------------------------------------------------------------------
// The loader. Every check that can fail sets err (prefixed with `where`) and returns false.
class Loader {
public:
    Loader(const HardwareInfo& hw, Config& cfg, std::string& err) : hw_(hw), cfg_(cfg), err_(err) {}

    bool load(JsonObjectConst root);

private:
    const HardwareInfo& hw_;
    Config& cfg_;
    std::string& err_;
    std::string where_;          // "group 2 (\"knobs\")", "banks", ...
    uint8_t channel_ = 1;        // top-level midi_channel

    bool fail(const std::string& msg) {
        err_ = where_.empty() ? msg : where_ + ": " + msg;
        return false;
    }

    // ---- small typed readers ----
    bool number(JsonVariantConst v, const char* key, float lo, float hi, float& out) {
        if (!v.is<float>()) return fail(fmt("'%s' must be a number", key));
        float x = v.as<float>();
        if (!(x >= lo && x <= hi))
            return fail(fmt("'%s' is %s, must be %s to %s", key, numStr(x).c_str(), numStr(lo).c_str(), numStr(hi).c_str()));
        out = x;
        return true;
    }
    bool integer(JsonVariantConst v, const char* key, long lo, long hi, long& out) {
        if (!v.is<long>()) return fail(fmt("'%s' must be a whole number", key));
        long x = v.as<long>();
        if (x < lo || x > hi) return fail(fmt("'%s' is %ld, must be %ld to %ld", key, x, lo, hi));
        out = x;
        return true;
    }
    bool boolean(JsonVariantConst v, const char* key, bool& out) {
        if (!v.is<bool>()) return fail(fmt("'%s' must be true or false", key));
        out = v.as<bool>();
        return true;
    }
    bool str(JsonVariantConst v, const char* key, const char*& out) {
        if (!v.is<const char*>()) return fail(fmt("'%s' must be a string in quotes", key));
        out = v.as<const char*>();
        return true;
    }
    // Keys starting with '_' and the key "comment" are ignored, so notes can be left in the file.
    bool onlyKeys(JsonObjectConst obj, const char* const* allowed, const char* what) {
        for (JsonPairConst kv : obj) {
            const char* k = kv.key().c_str();
            if (k[0] == '_' || std::strcmp(k, "comment") == 0) continue;
            bool ok = false;
            for (const char* const* a = allowed; *a; ++a)
                if (std::strcmp(k, *a) == 0) { ok = true; break; }
            if (!ok) return fail(fmt("unknown %s '%s'", what, k));
        }
        return true;
    }

    bool selectors(JsonVariantConst v, const char* key, bool pads, std::vector<uint8_t>& out);
    bool padHasControl(uint8_t p) const {
        for (uint8_t a = 0; a < hw_.numAnalog() && a < kMaxAnalog; ++a)
            if (hw_.padUnder[a] == p) return true;
        return false;
    }

    bool topLevel(JsonObjectConst root);
    bool banks(JsonVariantConst v);
    bool filters(JsonVariantConst v);
    bool calibration(JsonVariantConst v);
    bool group(JsonObjectConst g, size_t index);
};

bool Loader::selectors(JsonVariantConst v, const char* key, bool pads, std::vector<uint8_t>& out) {
    std::string e;
    auto one = [&](JsonVariantConst s) {
        if (!s.is<const char*>()) return fail(fmt("'%s' must be a selector string such as \"%s\", or a list of them", key, pads ? "pad1-16" : "knob1-16"));
        if (!parseSelector(s.as<const char*>(), hw_, pads, out, e)) return fail(e);
        return true;
    };
    if (v.is<JsonArrayConst>()) {
        if (v.as<JsonArrayConst>().size() == 0) return fail(fmt("'%s' is an empty list", key));
        for (JsonVariantConst s : v.as<JsonArrayConst>())
            if (!one(s)) return false;
    } else if (!one(v)) {
        return false;
    }
    for (size_t i = 0; i < out.size(); ++i)
        for (size_t j = i + 1; j < out.size(); ++j)
            if (out[i] == out[j])
                return fail(fmt("%s is listed twice in '%s'", (pads ? padName(out[i]) : analogName(hw_, out[i])).c_str(), key));
    return true;
}

bool Loader::load(JsonObjectConst root) {
    if (!topLevel(root)) return false;

    JsonVariantConst groups = root["groups"];
    if (groups.isNull()) return true;
    where_ = "groups";
    if (!groups.is<JsonArrayConst>()) return fail("must be a list [ ... ] of groups");
    size_t i = 0;
    for (JsonVariantConst g : groups.as<JsonArrayConst>()) {
        ++i;
        where_ = fmt("group %u", unsigned(i));
        if (!g.is<JsonObjectConst>()) return fail("must be an object { ... }");
        if (!group(g.as<JsonObjectConst>(), i)) return false;
    }
    where_.clear();
    return true;
}

bool Loader::topLevel(JsonObjectConst root) {
    static const char* const keys[] = {"name", "midi_channel", "led_brightness", "pickup_tolerance",
                                       "banks", "filter", "calibration", "groups", nullptr};
    where_.clear();
    if (!onlyKeys(root, keys, "top-level setting")) return false;

    const char* s;
    float f;
    long n;
    if (!root["name"].isNull()) {
        if (!str(root["name"], "name", s)) return false;
        cfg_.name = s;
    }
    if (!root["midi_channel"].isNull()) {
        if (!integer(root["midi_channel"], "midi_channel", 1, 16, n)) return false;
        channel_ = uint8_t(n);
        // The built-in bindings follow the top-level channel too.
        for (auto& row : cfg_.analog)
            for (auto& b : row) b.addr.channel = channel_;
        for (auto& row : cfg_.pads)
            for (auto& b : row) b.addr.channel = channel_;
    }
    if (!root["led_brightness"].isNull()) {
        if (!number(root["led_brightness"], "led_brightness", 0, 1, f)) return false;
        cfg_.ledBrightness = f;
    }
    if (!root["pickup_tolerance"].isNull()) {
        if (!number(root["pickup_tolerance"], "pickup_tolerance", 0, 0.5f, f)) return false;
        if (f < kMinPickupTolerance)
            return fail(fmt("'pickup_tolerance' is %s, too small: must be at least %s, or a control may never pick up",
                            numStr(f).c_str(), numStr(kMinPickupTolerance).c_str()));
        cfg_.pickupTolerance = f;
    }
    if (!root["banks"].isNull() && !banks(root["banks"])) return false;
    if (!root["filter"].isNull() && !filters(root["filter"])) return false;
    if (!root["calibration"].isNull() && !calibration(root["calibration"])) return false;
    where_.clear();
    return true;
}

bool Loader::banks(JsonVariantConst v) {
    where_ = "banks";
    if (!v.is<JsonArrayConst>()) return fail("must be a list [ ... ] of { \"name\": ..., \"hue\": ... }");
    JsonArrayConst arr = v.as<JsonArrayConst>();
    if (arr.size() == 0) return fail("needs at least one bank");
    if (arr.size() > kMaxBanks) return fail(fmt("%u banks given, at most %u allowed", unsigned(arr.size()), unsigned(kMaxBanks)));
    static const char* const keys[] = {"name", "hue", nullptr};
    uint8_t i = 0;
    for (JsonVariantConst b : arr) {
        where_ = fmt("bank %u", unsigned(i + 1));
        if (!b.is<JsonObjectConst>()) return fail("must be an object { \"name\": ..., \"hue\": ... }");
        JsonObjectConst o = b.as<JsonObjectConst>();
        if (!onlyKeys(o, keys, "bank setting")) return false;
        const char* s;
        float f;
        if (!o["name"].isNull()) {
            if (!str(o["name"], "name", s)) return false;
            cfg_.banks[i].name = s;
        }
        if (!o["hue"].isNull()) {
            if (!number(o["hue"], "hue", 0, 360, f)) return false;
            cfg_.banks[i].hue = f;
        }
        ++i;
    }
    cfg_.numBanks = i;
    return true;
}

bool Loader::filters(JsonVariantConst v) {
    where_ = "filter";
    if (!v.is<JsonObjectConst>()) return fail("must be an object with \"knob\", \"fader\" and/or \"pedal\"");
    static const char* const kinds[] = {"knob", "fader", "pedal", nullptr};
    if (!onlyKeys(v.as<JsonObjectConst>(), kinds, "control kind")) return false;
    static const char* const keys[] = {"smoothing", "deadband", "settle_ms", nullptr};
    for (int k = 0; k < 3; ++k) {
        JsonVariantConst fo = v[kinds[k]];
        if (fo.isNull()) continue;
        where_ = fmt("filter.%s", kinds[k]);
        if (!fo.is<JsonObjectConst>()) return fail("must be an object { \"smoothing\": ..., ... }");
        if (!onlyKeys(fo.as<JsonObjectConst>(), keys, "filter setting")) return false;
        FilterParams& p = cfg_.filter[k];
        float f;
        long n;
        if (!fo["smoothing"].isNull()) {
            if (!number(fo["smoothing"], "smoothing", 0, 1, f)) return false;
            p.smoothing = f;
        }
        if (!fo["deadband"].isNull()) {
            if (!number(fo["deadband"], "deadband", 0, 0.5f, f)) return false;
            if (f < kMinDeadband)
                return fail(fmt("'deadband' is %s, too small: must be at least %s, or noise makes untouched controls send",
                                numStr(f).c_str(), numStr(kMinDeadband).c_str()));
            p.deadband = f;
        }
        if (!fo["settle_ms"].isNull()) {
            if (!integer(fo["settle_ms"], "settle_ms", 0, 60000, n)) return false;
            if (n < kMinSettleMs)
                return fail(fmt("'settle_ms' is %ld, too small: must be at least %ld", n, kMinSettleMs));
            p.settleMs = uint16_t(n);
        }
    }
    return true;
}

bool Loader::calibration(JsonVariantConst v) {
    where_ = "calibration";
    if (!v.is<JsonObjectConst>()) return fail("must be an object such as { \"pedal1\": [40, 4055] }");
    for (JsonPairConst kv : v.as<JsonObjectConst>()) {
        const char* key = kv.key().c_str();
        if (key[0] == '_' || std::strcmp(key, "comment") == 0) continue;
        where_ = fmt("calibration \"%s\"", key);
        std::vector<uint8_t> ids;
        std::string e;
        if (!parseSelector(key, hw_, false, ids, e)) return fail(e);
        JsonVariantConst val = kv.value();
        if (!val.is<JsonArrayConst>() || val.as<JsonArrayConst>().size() != 2)
            return fail("must be a pair [rawMin, rawMax]");
        long lo, hi;
        if (!integer(val[0], "rawMin", 0, 4095, lo) || !integer(val[1], "rawMax", 0, 4095, hi)) return false;
        if (lo >= hi) return fail(fmt("rawMin (%ld) must be less than rawMax (%ld)", lo, hi));
        for (uint8_t a : ids) cfg_.cal[a] = AnalogCal{uint16_t(lo), uint16_t(hi)};
    }
    return true;
}

// Parsed "led" object: each field is applied only if it was given.
struct LedPatch {
    bool hasMode = false, hasHue = false, hasSat = false, hasVal = false, hasFloor = false;
    bool hasEffect = false, hasPeriod = false, hasSource = false;
    LedMode mode = LedMode::Off;
    bool hueFromBank = false;
    float hue = 0, sat = 1, val = 1, floorV = 0;
    LedEffect effect = LedEffect::None;
    uint16_t period = 500;
    AnalogId source = kNone;

    void applyTo(LedSpec& s) const {
        if (hasMode) s.mode = mode;
        if (hasHue) { s.hueFromBank = hueFromBank; if (!hueFromBank) s.color.h = hue; }
        if (hasSat) s.color.s = sat;
        if (hasVal) s.color.v = val;
        if (hasFloor) s.floorV = floorV;
        if (hasEffect) s.effect = effect;
        if (hasPeriod) s.periodMs = period;
        if (hasSource) s.source = source;
    }
};

bool Loader::group(JsonObjectConst g, size_t index) {
    static const char* const keys[] = {"name", "controls", "pads", "banks", "send", "shared", "pickup", "invert",
                                       "led", "action", "first_bank", "on_value", "off_value", nullptr};
    const char* gname = nullptr;
    if (!g["name"].isNull()) {
        if (!str(g["name"], "name", gname)) return false;
        where_ = fmt("group %u (\"%s\")", unsigned(index), gname);
    }
    if (!onlyKeys(g, keys, "group setting")) return false;

    // ---- members ----
    std::vector<uint8_t> controls, pads;
    if (!g["controls"].isNull() && !selectors(g["controls"], "controls", false, controls)) return false;
    if (!g["pads"].isNull() && !selectors(g["pads"], "pads", true, pads)) return false;
    if (controls.empty() && pads.empty()) return fail("needs \"controls\" or \"pads\" (or both)");

    // Settings that only make sense for one kind of member.
    static const char* const controlOnly[] = {"pickup", "invert", nullptr};
    static const char* const padOnly[] = {"action", "first_bank", "on_value", "off_value", nullptr};
    for (const char* const* k = controlOnly; *k; ++k)
        if (!g[*k].isNull() && controls.empty()) return fail(fmt("'%s' applies to \"controls\", but this group has only pads", *k));
    for (const char* const* k = padOnly; *k; ++k)
        if (!g[*k].isNull() && pads.empty()) return fail(fmt("'%s' applies to \"pads\", but this group has only controls", *k));

    // ---- banks ----
    bool inBank[kMaxBanks] = {};
    uint8_t lastBank = 0;
    JsonVariantConst bv = g["banks"];
    if (bv.isNull()) {
        for (uint8_t b = 0; b < cfg_.numBanks; ++b) inBank[b] = true;
        lastBank = cfg_.numBanks - 1;
    } else {
        auto addBank = [&](JsonVariantConst x) {
            if (!x.is<long>()) return fail("'banks' must list bank numbers, such as [1, 3]");
            long n = x.as<long>();
            if (n < 1 || n > cfg_.numBanks)
                return fail(fmt("bank %ld does not exist (%u bank%s declared)", n, unsigned(cfg_.numBanks), cfg_.numBanks == 1 ? "" : "s"));
            inBank[n - 1] = true;
            if (n - 1 > lastBank) lastBank = uint8_t(n - 1);
            return true;
        };
        if (bv.is<JsonArrayConst>()) {
            if (bv.as<JsonArrayConst>().size() == 0) return fail("'banks' is an empty list");
            for (JsonVariantConst x : bv.as<JsonArrayConst>())
                if (!addBank(x)) return false;
        } else if (!addBank(bv)) {
            return false;
        }
    }

    // ---- simple fields ----
    bool hasShared = !g["shared"].isNull(), shared = false;
    if (hasShared && !boolean(g["shared"], "shared", shared)) return false;

    bool hasPickup = !g["pickup"].isNull();
    PickupMode pickup = PickupMode::Pickup;
    if (hasPickup) {
        const char* s;
        if (!str(g["pickup"], "pickup", s)) return false;
        if (!std::strcmp(s, "pickup")) pickup = PickupMode::Pickup;
        else if (!std::strcmp(s, "jump")) pickup = PickupMode::Jump;
        else return fail(fmt("unknown pickup '%s' (use \"pickup\" or \"jump\")", s));
    }
    bool hasInvert = !g["invert"].isNull(), invert = false;
    if (hasInvert && !boolean(g["invert"], "invert", invert)) return false;

    bool hasAction = !g["action"].isNull();
    PadAction action = PadAction::None;
    if (hasAction) {
        const char* s;
        if (!str(g["action"], "action", s)) return false;
        if (!std::strcmp(s, "none")) action = PadAction::None;
        else if (!std::strcmp(s, "bank_select")) action = PadAction::BankSelect;
        else if (!std::strcmp(s, "bank_next")) action = PadAction::BankNext;
        else if (!std::strcmp(s, "bank_prev")) action = PadAction::BankPrev;
        else if (!std::strcmp(s, "momentary")) action = PadAction::Momentary;
        else if (!std::strcmp(s, "toggle")) action = PadAction::Toggle;
        else return fail(fmt("unknown action '%s' (use none, bank_select, bank_next, bank_prev, momentary or toggle)", s));
    }
    long firstBank = 1;
    if (!g["first_bank"].isNull()) {
        if (!hasAction || action != PadAction::BankSelect) return fail("'first_bank' only goes with \"action\": \"bank_select\"");
        if (!integer(g["first_bank"], "first_bank", 1, kMaxBanks, firstBank)) return false;
    }
    if (hasAction && action == PadAction::BankSelect) {
        for (size_t i = 0; i < pads.size(); ++i) {
            long bank = firstBank + long(i);
            if (bank > cfg_.numBanks)
                return fail(fmt("bank_select: %s would select bank %ld, but only %u bank%s declared",
                                padName(pads[i]).c_str(), bank, unsigned(cfg_.numBanks),
                                cfg_.numBanks == 1 ? " is" : "s are"));
        }
    }
    long onValue = -1, offValue = -1;
    if (!g["on_value"].isNull() && !integer(g["on_value"], "on_value", 0, 127, onValue)) return false;
    if (!g["off_value"].isNull() && !integer(g["off_value"], "off_value", 0, 127, offValue)) return false;

    // ---- send ----
    JsonVariantConst sv = g["send"];
    bool hasSend = !sv.isNull();
    MidiType sendType = MidiType::None;
    long first = 0, bankOffset = 0, sendChannel = channel_;
    if (hasSend) {
        if (!sv.is<JsonObjectConst>()) return fail("'send' must be an object { \"type\": ..., \"first\": ... }");
        JsonObjectConst so = sv.as<JsonObjectConst>();
        static const char* const sendKeys[] = {"type", "first", "bank_offset", "channel", nullptr};
        if (!onlyKeys(so, sendKeys, "send setting")) return false;
        if (so["type"].isNull() || so["first"].isNull()) return fail("'send' needs both \"type\" and \"first\"");
        const char* s;
        if (!str(so["type"], "type", s)) return false;
        if (!std::strcmp(s, "cc")) sendType = MidiType::CC;
        else if (!std::strcmp(s, "note")) sendType = MidiType::Note;
        else if (!std::strcmp(s, "none")) sendType = MidiType::None;
        else return fail(fmt("unknown send type '%s' (use \"cc\" or \"note\")", s));
        if (!integer(so["first"], "first", 0, 127, first)) return false;
        if (!so["bank_offset"].isNull() && !integer(so["bank_offset"], "bank_offset", 0, 127, bankOffset)) return false;
        if (!so["channel"].isNull() && !integer(so["channel"], "channel", 1, 16, sendChannel)) return false;
    }

    // ---- led ----
    JsonVariantConst lv = g["led"];
    LedPatch led;
    bool hasLed = !lv.isNull();
    if (hasLed) {
        if (!lv.is<JsonObjectConst>()) return fail("'led' must be an object { \"mode\": ..., ... }");
        JsonObjectConst lo = lv.as<JsonObjectConst>();
        static const char* const ledKeys[] = {"mode", "hue", "sat", "val", "floor", "effect", "period_ms", "source", nullptr};
        if (!onlyKeys(lo, ledKeys, "led setting")) return false;
        const char* s;
        float f;
        long n;
        if (!lo["mode"].isNull()) {
            if (!str(lo["mode"], "mode", s)) return false;
            led.hasMode = true;
            if (!std::strcmp(s, "off")) led.mode = LedMode::Off;
            else if (!std::strcmp(s, "static")) led.mode = LedMode::Static;
            else if (!std::strcmp(s, "level")) led.mode = LedMode::Level;
            else if (!std::strcmp(s, "state")) led.mode = LedMode::State;
            else if (!std::strcmp(s, "midi")) led.mode = LedMode::Midi;
            else return fail(fmt("unknown led mode '%s' (use off, static, level, state or midi)", s));
        }
        if (!lo["hue"].isNull()) {
            led.hasHue = true;
            if (lo["hue"].is<const char*>()) {
                if (std::strcmp(lo["hue"].as<const char*>(), "bank") != 0)
                    return fail(fmt("led 'hue' is \"%s\": use a number 0 to 360, or \"bank\"", lo["hue"].as<const char*>()));
                led.hueFromBank = true;
            } else if (!number(lo["hue"], "hue", 0, 360, led.hue)) {
                return false;
            }
        }
        if (!lo["sat"].isNull()) { if (!number(lo["sat"], "sat", 0, 1, f)) return false; led.hasSat = true; led.sat = f; }
        if (!lo["val"].isNull()) { if (!number(lo["val"], "val", 0, 1, f)) return false; led.hasVal = true; led.val = f; }
        if (!lo["floor"].isNull()) { if (!number(lo["floor"], "floor", 0, 1, f)) return false; led.hasFloor = true; led.floorV = f; }
        if (!lo["effect"].isNull()) {
            if (!str(lo["effect"], "effect", s)) return false;
            led.hasEffect = true;
            if (!std::strcmp(s, "none")) led.effect = LedEffect::None;
            else if (!std::strcmp(s, "blink")) led.effect = LedEffect::Blink;
            else if (!std::strcmp(s, "fade")) led.effect = LedEffect::Fade;
            else return fail(fmt("unknown led effect '%s' (use none, blink or fade)", s));
        }
        if (!lo["period_ms"].isNull()) {
            if (!integer(lo["period_ms"], "period_ms", 20, 60000, n)) return false;
            led.hasPeriod = true;
            led.period = uint16_t(n);
        }
        if (!lo["source"].isNull()) {
            if (!str(lo["source"], "source", s)) return false;
            std::vector<uint8_t> ids;
            std::string e;
            if (!parseSelector(s, hw_, false, ids, e)) return fail("led 'source': " + e);
            if (ids.size() != 1) return fail(fmt("led 'source' must name one control, such as \"knob3\", not \"%s\"", s));
            led.hasSource = true;
            led.source = ids[0];
        }
        if (!controls.empty() && pads.empty()) {
            bool any = false;
            for (uint8_t a : controls) any = any || hw_.padUnder[a] < hw_.numPads;
            if (!any) return fail("'led' is set, but none of these controls has a pad under it");
        }
    }

    // ---- range check for sends: the highest number any member reaches ----
    // Uses each binding's effective `shared` flag (set by this group, or by an earlier one),
    // exactly as the numbers are applied below.
    if (hasSend && sendType != MidiType::None) {
        long top = -1;
        std::string who;
        uint8_t topBank = 0;
        bool topBanked = false;
        for (uint8_t b = 0; b < kMaxBanks; ++b) {
            if (!inBank[b]) continue;
            for (size_t i = 0; i < controls.size() + pads.size(); ++i) {
                const bool isCtl = i < controls.size();
                const bool sh = hasShared ? shared
                                          : (isCtl ? cfg_.analog[controls[i]][b].shared
                                                   : cfg_.pads[pads[i - controls.size()]][b].shared);
                const long num = first + long(i) + (sh ? 0 : bankOffset * b);
                if (num >= top) {
                    top = num;
                    who = isCtl ? analogName(hw_, controls[i]) : padName(pads[i - controls.size()]);
                    topBank = b;
                    topBanked = !sh && bankOffset > 0 && b > 0;
                }
            }
        }
        if (top > 127) {
            if (topBanked)
                return fail(fmt("send numbers run past 127 (%s in bank %u would be %ld)", who.c_str(), unsigned(topBank + 1), top));
            return fail(fmt("send numbers run past 127 (%s would be %ld)", who.c_str(), top));
        }
    }

    // ---- apply ----
    for (uint8_t b = 0; b < kMaxBanks; ++b) {
        if (!inBank[b]) continue;
        for (size_t i = 0; i < controls.size(); ++i) {
            AnalogBinding& ab = cfg_.analog[controls[i]][b];
            if (hasShared) ab.shared = shared;
            if (hasPickup) ab.pickup = pickup;
            if (hasInvert) ab.invert = invert;
            if (hasSend) {
                long num = first + long(i) + (ab.shared ? 0 : bankOffset * b);
                ab.addr = MidiAddress{sendType, uint8_t(sendChannel), uint8_t(sendType == MidiType::None ? 0 : num)};
            }
        }
        for (size_t i = 0; i < pads.size(); ++i) {
            PadBinding& pb = cfg_.pads[pads[i]][b];
            if (hasShared) pb.shared = shared;
            if (hasAction) {
                pb.action = action;
                pb.arg = action == PadAction::BankSelect ? uint8_t(firstBank - 1 + long(i)) : 0;
                // Bank and "none" pads send nothing unless this group also says what to send.
                if (!isMidiAction(action) && !hasSend) pb.addr = MidiAddress{MidiType::None, channel_, 0};
            }
            if (onValue >= 0) pb.onValue = uint8_t(onValue);
            if (offValue >= 0) pb.offValue = uint8_t(offValue);
            if (hasSend) {
                long num = first + long(controls.size() + i) + (pb.shared ? 0 : bankOffset * b);
                pb.addr = MidiAddress{sendType, uint8_t(sendChannel), uint8_t(sendType == MidiType::None ? 0 : num)};
            }
            if (isMidiAction(pb.action) && pb.addr.type == MidiType::None)
                return fail(fmt("%s: action %s needs a 'send'", padName(pads[i]).c_str(),
                                pb.action == PadAction::Toggle ? "toggle" : "momentary"));
        }
        if (hasLed) {
            for (uint8_t a : controls) {
                uint8_t p = hw_.padUnder[a];
                if (p >= hw_.numPads) continue;
                LedSpec& s = cfg_.leds[p][b];
                s.source = a;   // explicit "source" in the patch wins, below
                led.applyTo(s);
            }
            for (uint8_t p : pads) {
                LedSpec& s = cfg_.leds[p][b];
                led.applyTo(s);
                if (s.mode == LedMode::Level && s.source == kNone && !padHasControl(p))
                    return fail(fmt("%s: led mode \"level\" needs a control to follow, and no control sits above "
                                    "this pad: add \"source\", e.g. \"source\": \"fader1\"",
                                    padName(p).c_str()));
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// A small scan for the commonest hand-editing mistakes, used only when ArduinoJson has
// already refused the text (it does not say where). Returns "" if nothing was found.
std::string jsonSyntaxHint(const char* p, const char* end) {
    struct Open { char c; unsigned line; bool object; bool expectKey; };
    Open stack[32];
    int depth = 0;
    unsigned line = 1;
    // What came last: 0 nothing, 'v' a value (string, number, word, } or ]), 'k' a key,
    // ',' a comma, ':' a colon, '{' / '[' an opening bracket.
    char last = 0;
    unsigned commaLine = 0;
    for (; p < end; ++p) {
        const char c = *p;
        if (c == '\n') { ++line; continue; }
        if (c == ' ' || c == '\t' || c == '\r') continue;
        if (c == '/' && p + 1 < end && p[1] == '/') {
            while (p < end && *p != '\n') ++p;
            --p;
            continue;
        }
        if (c == '/' && p + 1 < end && p[1] == '*') {
            const unsigned start = line;
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') ++line; ++p; }
            if (p + 1 >= end) return fmt("line %u: comment /* is never closed", start);
            ++p;
            continue;
        }
        const bool inObj = depth > 0 && stack[depth - 1].object;
        const bool startsValue = c == '"' || c == '{' || c == '[' || c == '-' || std::isalnum(static_cast<unsigned char>(c));
        if (startsValue && (last == 'v' || last == 'k')) {
            if (last == 'k') return fmt("line %u: missing ':' after a key", line);
            return fmt("line %u: missing comma before this", line);
        }
        if (c == '"') {
            const unsigned start = line;
            for (++p; p < end && *p != '"'; ++p) {
                if (*p == '\\' && p + 1 < end) { ++p; continue; }
                if (*p == '\n') return fmt("line %u: text in quotes is not closed (missing \")", start);
            }
            if (p >= end) return fmt("line %u: text in quotes is not closed (missing \")", start);
            last = (inObj && stack[depth - 1].expectKey) ? 'k' : 'v';
            continue;
        }
        if (c == '{' || c == '[') {
            if (depth == 32) return "";
            stack[depth++] = Open{c, line, c == '{', c == '{'};
            last = c;
            continue;
        }
        if (c == '}' || c == ']') {
            if (last == 'k') return fmt("line %u: missing ':' and value after a key", line);
            if (last == ',') return fmt("line %u: comma before '%c' (remove the trailing comma)", commaLine, c);
            if (depth == 0) return fmt("line %u: '%c' with nothing open to close", line, c);
            const Open& o = stack[depth - 1];
            if ((c == '}') != o.object)
                return fmt("line %u: '%c' does not match the '%c' opened on line %u", line, c, o.c, o.line);
            --depth;
            last = 'v';
            continue;
        }
        if (c == ',') {
            if (last == 'k') return fmt("line %u: missing ':' and value after a key", line);
            if (last == ',' || last == '{' || last == '[' || last == ':' || last == 0)
                return fmt("line %u: unexpected comma", line);
            if (inObj) stack[depth - 1].expectKey = true;
            commaLine = line;
            last = ',';
            continue;
        }
        if (c == ':') {
            if (last != 'k') return fmt("line %u: unexpected ':'", line);
            if (inObj) stack[depth - 1].expectKey = false;
            last = ':';
            continue;
        }
        if (startsValue) {   // number or word (true, false, null)
            if (inObj && stack[depth - 1].expectKey)
                return fmt("line %u: a key must be in double quotes", line);
            while (p + 1 < end && (std::isalnum(static_cast<unsigned char>(p[1])) || p[1] == '.' || p[1] == '-' || p[1] == '+')) ++p;
            last = 'v';
            continue;
        }
        if (c == '\'') return fmt("line %u: use double quotes \" \", not single quotes", line);
        return fmt("line %u: unexpected character '%c'", line, c);
    }
    if (depth > 0) {
        const Open& o = stack[depth - 1];
        return fmt("'%c' opened on line %u is never closed", o.c, o.line);
    }
    return "";
}

}  // namespace

// ---------------------------------------------------------------------------------------

std::string analogName(const HardwareInfo& hw, AnalogId a) {
    if (a >= hw.numAnalog()) return fmt("control%u", unsigned(a) + 1);
    static const char* const kinds[] = {"knob", "fader", "pedal"};
    return fmt("%s%u", kinds[int(hw.kindOf(a))], unsigned(hw.indexInKind(a)) + 1);
}

std::string padName(PadId p) { return fmt("pad%u", unsigned(p) + 1); }

bool parseSelector(const std::string& selIn, const HardwareInfo& hw, bool pads, std::vector<uint8_t>& out,
                   std::string& err) {
    // Lower-case and drop spaces, so "Knob 1 - 4" works too.
    std::string sel;
    for (char c : selIn)
        if (!std::isspace(static_cast<unsigned char>(c))) sel += char(std::tolower(static_cast<unsigned char>(c)));
    const std::string shown = "'" + selIn + "'";

    size_t i = 0;
    while (i < sel.size() && std::isalpha(static_cast<unsigned char>(sel[i]))) ++i;
    std::string kind = sel.substr(0, i);
    if (kind.size() > 1 && kind.back() == 's') kind.pop_back();   // "knobs1-4"

    auto readNum = [&](long& n) {
        size_t start = i;
        n = 0;
        while (i < sel.size() && std::isdigit(static_cast<unsigned char>(sel[i]))) {
            n = n * 10 + (sel[i] - '0');
            if (n > 1000) n = 1000;
            ++i;
        }
        return i > start;
    };
    long lo = 0, hi = 0;
    if (kind.empty() || !readNum(lo)) {
        err = "selector " + shown + " should look like \"knob1-16\", \"fader1\", \"pedal1-2\" or \"pad17\"";
        return false;
    }
    hi = lo;
    if (i < sel.size() && sel[i] == '-') {
        ++i;
        if (!readNum(hi)) { err = "selector " + shown + ": range needs a second number, as in \"knob1-16\""; return false; }
    }
    if (i != sel.size()) { err = "selector " + shown + " has extra characters at the end"; return false; }

    AnalogKind ak = AnalogKind::Knob;
    uint8_t count = 0, base = 0;
    bool isPad = false;
    if (kind == "knob") { ak = AnalogKind::Knob; count = hw.numKnobs; base = 0; }
    else if (kind == "fader") { ak = AnalogKind::Fader; count = hw.numFaders; base = hw.numKnobs; }
    else if (kind == "pedal") { ak = AnalogKind::Pedal; count = hw.numPedals; base = hw.numKnobs + hw.numFaders; }
    else if (kind == "pad") { isPad = true; count = hw.numPads; }
    else {
        err = "selector " + shown + ": unknown kind '" + kind + "' (use knob, fader, pedal or pad)";
        return false;
    }
    (void)ak;
    if (isPad != pads) {
        err = "selector " + shown + (pads ? " is not a pad; knobs, faders and pedals go under \"controls\""
                                          : " is a pad; pads go under \"pads\", not \"controls\"");
        return false;
    }
    if (lo < 1 || hi < 1) { err = "selector " + shown + ": numbers start at 1"; return false; }
    if (hi < lo) { err = "selector " + shown + ": range runs backwards"; return false; }
    if (hi > count) {
        err = fmt("selector %s: this board has %u %s%s", shown.c_str(), unsigned(count), kind.c_str(), count == 1 ? "" : "s");
        return false;
    }
    for (long n = lo; n <= hi; ++n) out.push_back(uint8_t(base + n - 1));
    return true;
}

void defaultConfig(const HardwareInfo& hw, Config& out) {
    // Reset field by field: Config is ~11 kB, too big for a temporary on a small stack.
    out.name = "Default";
    out.numBanks = 1;
    for (uint8_t b = 0; b < kMaxBanks; ++b) {
        out.banks[b].name = fmt("Bank %u", unsigned(b) + 1);
        out.banks[b].hue = kDefaultHue;
    }
    out.ledBrightness = 0.5f;
    out.pickupTolerance = 0.03f;
    for (auto& c : out.cal) c = AnalogCal{};
    for (auto& f : out.filter) f = FilterParams{};

    const uint8_t nA = hw.numAnalog() < kMaxAnalog ? hw.numAnalog() : kMaxAnalog;
    const uint8_t nP = hw.numPads < kMaxPads ? hw.numPads : kMaxPads;

    // Which pads sit under a control (those show its level; the rest are note pads).
    bool under[kMaxPads] = {};
    for (uint8_t a = 0; a < nA; ++a)
        if (hw.padUnder[a] < nP) under[hw.padUnder[a]] = true;

    for (uint8_t b = 0; b < kMaxBanks; ++b) {
        for (uint8_t a = 0; a < kMaxAnalog; ++a) {
            AnalogBinding ab;
            if (a < nA) {
                unsigned n = kDefaultFirstCC + a;
                ab.addr = MidiAddress{MidiType::CC, 1, uint8_t(n > 127 ? 127 : n)};
                ab.pickup = PickupMode::Jump;
            }
            out.analog[a][b] = ab;
        }
        uint8_t note = kDefaultFirstNote;
        for (uint8_t p = 0; p < kMaxPads; ++p) {
            PadBinding pb;
            LedSpec ls;
            if (p < nP) {
                ls.color = Hsv{kDefaultHue, 1, 1};
                if (under[p]) {
                    ls.mode = LedMode::Level;   // source kNone: the control above this pad
                } else {
                    pb.action = PadAction::Momentary;
                    pb.addr = MidiAddress{MidiType::Note, 1, uint8_t(note > 127 ? 127 : note)};
                    ++note;
                    ls.mode = LedMode::State;   // lit while held
                }
            }
            out.pads[p][b] = pb;
            out.leds[p][b] = ls;
        }
    }
}

bool loadConfig(const std::string& json, const HardwareInfo& hw, Config& out, std::string& err) {
    defaultConfig(hw, out);
    err.clear();
    bool ok;
    {
        // Skip a UTF-8 byte order mark (Windows editors add one).
        const char* text = json.data();
        size_t len = json.size();
        if (len >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF) {
            text += 3;
            len -= 3;
        }
        JsonDocument doc;   // heap; freed at the end of this block
        DeserializationError de = deserializeJson(doc, text, len);
        if (de) {
            std::string hint = jsonSyntaxHint(text, text + len);
            err = std::string("config is not valid JSON (") + de.c_str() + "): " +
                  (hint.empty() ? std::string("check for a trailing comma or a missing quote") : hint);
            ok = false;
        } else if (!doc.is<JsonObjectConst>()) {
            err = "config must be a JSON object { ... }";
            ok = false;
        } else {
            Loader loader(hw, out, err);
            ok = loader.load(doc.as<JsonObjectConst>());
        }
    }
    if (!ok) defaultConfig(hw, out);
    return ok;
}

std::vector<std::string> configWarnings(const Config& cfg, const HardwareInfo& hw) {
    std::vector<std::string> w;
    struct Entry { std::string who; MidiAddress addr; bool shared; };
    for (uint8_t b = 0; b < cfg.numBanks && b < kMaxBanks; ++b) {
        std::vector<Entry> e;
        for (uint8_t a = 0; a < hw.numAnalog() && a < kMaxAnalog; ++a) {
            const AnalogBinding& ab = cfg.analog[a][b];
            if (ab.addr.type != MidiType::None) e.push_back({analogName(hw, a), ab.addr, ab.shared});
        }
        for (uint8_t p = 0; p < hw.numPads && p < kMaxPads; ++p) {
            const PadBinding& pb = cfg.pads[p][b];
            if (isMidiAction(pb.action) && pb.addr.type != MidiType::None) e.push_back({padName(p), pb.addr, pb.shared});
        }
        for (size_t i = 0; i < e.size(); ++i)
            for (size_t j = i + 1; j < e.size(); ++j) {
                const MidiAddress &x = e[i].addr, &y = e[j].addr;
                if (x.type != y.type || x.channel != y.channel || x.number != y.number) continue;
                // Two shared bindings clash identically in every bank: report once.
                if (b > 0 && e[i].shared && e[j].shared) continue;
                std::string bank = (e[i].shared && e[j].shared) ? std::string("all banks")
                                                                : fmt("bank %u (%s)", unsigned(b) + 1, cfg.banks[b].name.c_str());
                w.push_back(fmt("%s: %s and %s both send %s %u on channel %u", bank.c_str(), e[i].who.c_str(),
                                e[j].who.c_str(), typeName(x.type), unsigned(x.number), unsigned(x.channel)));
            }
    }
    return w;
}

}  // namespace density
