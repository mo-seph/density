#include "ui_pages.h"
#include "ui_calibration.h"
#include "ui_text.h"
#include <cstdio>
#include <cstring>

namespace density {

using namespace ui;

namespace {
constexpr int16_t kRow = 9;          // size-1 line pitch used in lists
constexpr uint16_t kUnpluggedRaw = 64; // a max below this means nothing is connected
constexpr uint32_t kNoteMs = 2500;   // how long a calibration note stays up

const char* bankName(const Config& c, BankId b) {
    return b < c.numBanks && b < kMaxBanks ? c.banks[b].name.c_str() : "";
}
}  // namespace

// ---------------------------------------------------------------- Banks

void BanksPage::onTurn(int steps) {
    const Config& c = e_.config();
    int b = static_cast<int>(e_.bank()) + steps;
    if (b < 0) b = 0;
    if (b > c.numBanks - 1) b = c.numBanks - 1;
    if (b != e_.bank()) e_.setBank(static_cast<BankId>(b));
}

uint32_t BanksPage::contentKey() const {
    const Config& c = e_.config();
    uint32_t h = hashAdd(kHashSeed, e_.bank());
    h = hashAdd(h, c.numBanks);
    for (uint8_t b = 0; b < c.numBanks && b < kMaxBanks; ++b) h = hashStr(h, c.banks[b].name.c_str());
    return h;
}

void BanksPage::draw(Display& d, int16_t height) const {
    const Config& c = e_.config();
    const int16_t w = d.width();
    char buf[32];
    BankId cur = e_.bank();
    fitText(buf, sizeof buf, bankName(c, cur), w / 12);
    d.text(0, 0, buf, 2);
    d.hline(0, 17, w);

    uint8_t n = c.numBanks < kMaxBanks ? c.numBanks : kMaxBanks;
    bool twoCols = n > 4;
    int16_t colW = twoCols ? w / 2 : w;
    size_t colChars = static_cast<size_t>(colW / 6) - (twoCols ? 1 : 0);  // one space between columns
    for (uint8_t b = 0; b < n; ++b) {
        int16_t col = twoCols ? b / 4 : 0, row = twoCols ? b % 4 : b;
        int16_t y = 20 + row * kRow;
        if (y + 8 > height) continue;
        char label[32];
        snprintf(label, sizeof label, "%u %s", b + 1u, c.banks[b].name.c_str());
        fitText(buf, sizeof buf, label, colChars);
        // pad so the highlight is a full-width bar
        size_t len = strlen(buf);
        while (len < colChars && len + 1 < sizeof buf) buf[len++] = ' ';
        buf[len] = 0;
        d.text(col * colW, y, buf, 1, b == cur);
    }
}

// ---------------------------------------------------------------- Levels

uint8_t LevelsPage::count() const {
    uint8_t n = hal_.info().numAnalog();
    return n < kMaxAnalog ? n : kMaxAnalog;
}

void LevelsPage::tick(uint32_t) {
    if (last_ != kNone && last_ < count() && e_.analog(last_).moving) return;
    for (AnalogId a = 0; a < count(); ++a)
        if (e_.analog(a).moving) { last_ = a; return; }
}

uint32_t LevelsPage::contentKey() const {
    uint32_t h = hashAdd(kHashSeed, e_.bank());
    h = hashAdd(h, last_);
    for (AnalogId a = 0; a < count(); ++a) {
        AnalogState s = e_.analog(a);
        uint32_t v = to7(s.stored) | (s.pickedUp ? 0x100u : 0u);
        if (!s.pickedUp) v |= static_cast<uint32_t>(to7(s.position)) << 16;
        h = hashAdd(h, v);
    }
    return h;
}

void LevelsPage::draw(Display& d, int16_t height) const {
    const HardwareInfo& hw = hal_.info();
    const int16_t w = d.width();
    char buf[32];

    // Top line: the last touched control and its value, or the bank name.
    if (last_ != kNone && last_ < count()) {
        char name[16];
        controlName(hw, last_, name, sizeof name);
        fitText(buf, sizeof buf, name, 7);
        d.text(0, 0, buf, 2);
        snprintf(buf, sizeof buf, "%3u", to7(e_.analog(last_).stored));
        d.text(w - 36, 0, buf, 2);
    } else {
        fitText(buf, sizeof buf, bankName(e_.config(), e_.bank()), w / 12);
        d.text(0, 0, buf, 2);
    }

    // Bars: knobs, then faders, then pedals, with a gap between groups.
    const uint8_t n = count();
    if (n == 0) return;
    const int16_t top = 19, bottom = height - 3;   // bars occupy rows top..bottom
    const int16_t H = bottom - top + 1;
    if (H < 4) return;
    const int16_t kGroupGap = 4;
    int16_t gaps = (hw.numFaders > 0 && hw.numKnobs > 0) + (hw.numPedals > 0 && n > hw.numPedals);
    int16_t slot = (w - gaps * kGroupGap) / n;
    if (slot > 7) slot = 7;
    if (slot < 2) slot = 2;
    const int16_t bw = slot > 3 ? slot - 2 : slot - 1;

    int16_t x = 0;
    AnalogKind prev = hw.kindOf(0);
    for (AnalogId a = 0; a < n; ++a) {
        AnalogKind k = hw.kindOf(a);
        if (k != prev) { x += kGroupGap; prev = k; }
        AnalogState s = e_.analog(a);
        int16_t fh = static_cast<int16_t>(to7(s.stored) * H / 127);
        if (s.pickedUp) {
            if (fh > 0) d.rect(x, bottom - fh + 1, bw, fh, true);
        } else {
            if (fh > 0) d.rect(x, bottom - fh + 1, bw, fh, false);
            int16_t ty = bottom - static_cast<int16_t>(to7(s.position) * (H - 1) / 127);
            int16_t tx = x > 0 ? x - 1 : 0;
            d.hline(tx, ty, bw + 2);
        }
        if (a == last_) d.hline(x, bottom + 2, bw);   // underline the control named at the top
        x += slot;
    }
}

// ---------------------------------------------------------------- Pedal calibration

AnalogId CalibrationPage::analogId(uint8_t pedal) const {
    const HardwareInfo& hw = hal_.info();
    return static_cast<AnalogId>(hw.numKnobs + hw.numFaders + pedal);
}

const char* CalibrationPage::editHint() const {
    return running_ ? "push:save hold:cancel" : "push:start hold:back";
}

void CalibrationPage::setNote(const char* s) {
    fitText(note_, sizeof note_, s, 21);
    noteOn_ = true;
    noteUntil_ = now_ + kNoteMs;
}

void CalibrationPage::onEnter() {
    running_ = false;
    noteOn_ = false;
    if (sel_ >= hal_.info().numPedals) sel_ = 0;
}

void CalibrationPage::onLeave() {
    if (running_) stop();
}

void CalibrationPage::stop() {
    e_.suspend(analogId(sel_), false);
    running_ = false;
}

void CalibrationPage::onTurn(int steps) {
    if (running_) return;
    int n = hal_.info().numPedals;
    int s = sel_ + steps;
    if (s < 0) s = 0;
    if (s > n - 1) s = n - 1;
    sel_ = static_cast<uint8_t>(s < 0 ? 0 : s);
}

PageResult CalibrationPage::onPress() {
    AnalogId a = analogId(sel_);
    if (!running_) {
        e_.suspend(a, true);
        running_ = true;
        raw_ = e_.analog(a).rawSmooth;   // spikes removed: one glitch must not become an end stop
        min_ = max_ = raw_;
        noteOn_ = false;
        return PageResult::Stay;
    }
    if (max_ < kUnpluggedRaw) { setNote("No signal: plugged in?"); return PageResult::Stay; }
    if (max_ - min_ < kMinCalRange) { setNote("Too small: move more"); return PageResult::Stay; }
    // Pull the end stops in by 2% so the pedal reliably reaches 0 and 127.
    uint16_t margin = static_cast<uint16_t>((max_ - min_) / 50);
    AnalogCal c;
    c.rawMin = static_cast<uint16_t>(min_ + margin);
    c.rawMax = static_cast<uint16_t>(max_ - margin);
    e_.setCalibration(a, c);
    cal_[sel_] = c;
    calSet_[sel_] = true;
    bool ok = save();
    stop();
    char msg[24];
    snprintf(msg, sizeof msg, ok ? "Pedal %u saved" : "Pedal %u: save failed", sel_ + 1u);
    setNote(msg);
    return PageResult::Stay;
}

PageResult CalibrationPage::onLongPress() {
    if (!running_) return PageResult::Leave;
    stop();
    setNote("Cancelled");
    return PageResult::Stay;
}

void CalibrationPage::tick(uint32_t nowMs) {
    now_ = nowMs;
    if (noteOn_ && static_cast<int32_t>(nowMs - noteUntil_) >= 0) noteOn_ = false;
    if (running_) {
        raw_ = e_.analog(analogId(sel_)).rawSmooth;
        if (raw_ < min_) min_ = raw_;
        if (raw_ > max_) max_ = raw_;
    }
}

bool CalibrationPage::save() {
    const HardwareInfo& hw = hal_.info();
    uint8_t n = hw.numPedals;
    AnalogCal cals[kMaxAnalog];
    bool set[kMaxAnalog];
    for (uint8_t i = 0; i < n && i < kMaxAnalog; ++i) {
        set[i] = calSet_[i];
        cals[i] = calSet_[i] ? cal_[i] : e_.config().cal[analogId(i)];
    }
    return hal_.writeFile(kCalibrationPath, formatCalibration(cals, set, n));
}

bool CalibrationPage::load() {
    std::string text;
    if (!hal_.readFile(kCalibrationPath, text)) return false;
    uint8_t n = hal_.info().numPedals;
    AnalogCal cals[kMaxAnalog];
    bool set[kMaxAnalog];
    if (!parseCalibration(text, cals, set, n)) return false;
    for (uint8_t i = 0; i < n; ++i) {
        if (!set[i]) continue;
        e_.setCalibration(analogId(i), cals[i]);
        cal_[i] = cals[i];
        calSet_[i] = true;
    }
    return true;
}

uint32_t CalibrationPage::contentKey() const {
    uint32_t h = hashAdd(kHashSeed, running_ | (sel_ << 1));
    h = hashAdd(h, noteOn_);
    if (noteOn_) h = hashStr(h, note_);
    if (running_) {
        h = hashAdd(h, raw_ >> 2);
        h = hashAdd(h, min_ | (static_cast<uint32_t>(max_) << 16));
    } else {
        for (uint8_t i = 0; i < hal_.info().numPedals; ++i) {
            const AnalogCal& c = calSet_[i] ? cal_[i] : e_.config().cal[analogId(i)];
            h = hashAdd(h, c.rawMin | (static_cast<uint32_t>(c.rawMax) << 16));
        }
    }
    return h;
}

void CalibrationPage::draw(Display& d, int16_t height) const {
    const int16_t w = d.width();
    char buf[32];
    const uint8_t n = hal_.info().numPedals;
    if (n == 0) {
        d.text(0, 0, "Pedal cal", 2);
        d.text(0, 20, "No pedal inputs");
        return;
    }
    if (!running_) {
        d.text(0, 0, "Pedal cal", 2);
        for (uint8_t i = 0; i < n; ++i) {
            int16_t y = 20 + i * kRow;
            if (y + 8 > height - kRow) break;   // keep the last row for the note
            const AnalogCal& c = calSet_[i] ? cal_[i] : e_.config().cal[analogId(i)];
            snprintf(buf, sizeof buf, "Pedal %-2u %4u-%-4u ", i + 1u, c.rawMin, c.rawMax);
            d.text(0, y, buf, 1, i == sel_);
        }
        if (noteOn_) d.text(0, height - kRow, note_);
        return;
    }
    // Running: live raw value and bar, with the tracked min / max.
    snprintf(buf, sizeof buf, "Calibrate Pedal %u", sel_ + 1u);
    d.text(0, 0, buf);
    d.text(0, 14, "raw");
    snprintf(buf, sizeof buf, "%u", raw_);
    d.text(24, 10, buf, 2);
    d.text(0, 28, noteOn_ ? note_ : "Move full travel");
    const int16_t by = 38, bh = 8, inner = w - 2;
    d.rect(0, by, w, bh, false);
    int16_t fw = static_cast<int16_t>(static_cast<int32_t>(raw_) * inner / 4095);
    if (fw > 0) d.rect(1, by + 1, fw, bh - 2, true);
    if (max_ >= min_) {
        int16_t xl = 1 + static_cast<int16_t>(static_cast<int32_t>(min_) * (inner - 1) / 4095);
        int16_t xh = 1 + static_cast<int16_t>(static_cast<int32_t>(max_) * (inner - 1) / 4095);
        d.vline(xl, by - 2, 2); d.vline(xl, by + bh, 2);
        d.vline(xh, by - 2, 2); d.vline(xh, by + bh, 2);
        snprintf(buf, sizeof buf, "min %-4u  max %u", min_, max_);
        d.text(0, by + bh + 2, buf);
    }
}

// ---------------------------------------------------------------- About

void AboutPage::setMessage(const char* s) {
    if (!s) s = "";
    // Longer than we keep: keep the end (from a word boundary), where the detail usually is.
    size_t n = strlen(s);
    if (n > kMaxMessage) {
        s += n - kMaxMessage;
        const char* sp = strchr(s, ' ');
        if (sp && sp[1]) s = sp + 1;
    }
    fitText(msg_, sizeof msg_, s, kMaxMessage);
    ++msgVersion_;
}

uint32_t AboutPage::contentKey() const {
    const Config& c = e_.config();
    uint32_t h = hashStr(kHashSeed, c.name.c_str());
    h = hashAdd(h, c.numBanks);
    return hashAdd(h, msgVersion_);
}

void AboutPage::draw(Display& d, int16_t height) const {
    const HardwareInfo& hw = hal_.info();
    const Config& c = e_.config();
    size_t cols = static_cast<size_t>(d.width() / 6);
    char buf[48], line[48];
    if (cols >= sizeof line) cols = sizeof line - 1;
    int16_t y = 0;
    auto put = [&](const char* s, bool inv = false) {
        if (y + 8 > height) return;
        fitText(line, sizeof line, s, cols);
        d.text(0, y, line, 1, inv);
        y += kRow;
    };
    const int rows = height >= 8 ? (height - 8) / kRow + 1 : 0;
    put(c.name.c_str(), true);

    // The message, wrapped at word boundaries: [start, length) of each line.
    constexpr int kMaxLines = 32;
    uint16_t start[kMaxLines], len[kMaxLines];
    int nLines = 0;
    if (msg_[0]) {
        const char* p = msg_;
        while (*p && nLines < kMaxLines) {
            size_t n = wrapLength(p, cols);
            start[nLines] = static_cast<uint16_t>(p - msg_);
            len[nLines] = static_cast<uint16_t>(n);
            ++nLines;
            p += n;
            while (*p == ' ') ++p;
        }
    }
    const int room = rows - 1;   // below the name
    const int first = nLines > room ? nLines - room : 0;   // too long: keep the end
    for (int i = first; i < nLines; ++i) {
        size_t n = len[i] < sizeof buf ? len[i] : sizeof buf - 1;
        memcpy(buf, msg_ + start[i], n);
        buf[n] = 0;
        put(buf);
    }

    snprintf(buf, sizeof buf, "Banks: %u", c.numBanks);
    put(buf);
    snprintf(buf, sizeof buf, "Board: %s", hw.name ? hw.name : "");
    put(buf);
    snprintf(buf, sizeof buf, "K%u F%u P%u Pad%u E%u D%u", hw.numKnobs, hw.numFaders, hw.numPedals,
             hw.numPads, hw.numEncoders, hw.numDisplays);
    put(buf);
    if (!msg_[0]) put("No messages");
}

// ---------------------------------------------------------------- Bank name (display 1)

uint32_t BankNameView::contentKey() const {
    const Config& c = e_.config();
    uint32_t h = hashAdd(kHashSeed, e_.bank() | (c.numBanks << 8));
    return hashStr(h, bankName(c, e_.bank()));
}

void BankNameView::draw(Display& d, int16_t height) const {
    const Config& c = e_.config();
    char buf[32];
    snprintf(buf, sizeof buf, "Bank %u/%u", e_.bank() + 1u, c.numBanks);
    d.text(0, 0, buf);
    // Name in large text on up to two lines.
    const size_t cols = static_cast<size_t>(d.width() / 12);
    const char* name = bankName(c, e_.bank());
    size_t n = wrapLength(name, cols);
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, name, n);
    buf[n] = 0;
    d.text(0, 20, buf, 2);
    const char* rest = name + n;
    while (*rest == ' ') ++rest;
    if (*rest && 38 + 16 <= height) {
        fitText(buf, sizeof buf, rest, cols);
        d.text(0, 38, buf, 2);
    }
}

}  // namespace density
