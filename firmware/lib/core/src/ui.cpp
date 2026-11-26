#include "ui.h"
#include "ui_text.h"
#include <cstdio>
#include <cstring>

namespace density {

using namespace ui;

namespace {
constexpr int16_t kHintH = 8;   // bottom line of display 0
}

Ui::Ui(Hal& hal, EngineView& engine)
    : hal_(hal),
      banks_(engine),
      levels_(hal, engine),
      cal_(hal, engine),
      about_(hal, engine),
      bankName_(engine),
      pages_{&banks_, &levels_, &cal_, &about_} {}

// Display 1 policy: Levels next to the Banks page, otherwise the bank name large.
Page& Ui::secondaryView() {
    if (cur_ == kBanks) return levels_;
    return bankName_;
}

void Ui::setMessage(const char* msg) {
    about_.setMessage(msg);
    if (msg && msg[0] && !editing_) cur_ = kAbout;
}

// ---------------------------------------------------------------- input

void Ui::handleInput(uint32_t nowMs) {
    const HardwareInfo& hw = hal_.info();
    if (hw.numEncoders == 0) return;
    int steps = hal_.encoderSteps(0);
    if (steps) turn(steps);

    bool pressed = hal_.encoderPressed(0);
    if (pressed && !down_) {
        down_ = true;
        longFired_ = false;
        downAt_ = nowMs;
    } else if (pressed && down_) {
        if (!longFired_ && nowMs - downAt_ >= kLongPressMs) {
            longFired_ = true;
            longPress();
        }
    } else if (!pressed && down_) {
        down_ = false;
        if (!longFired_) press();
    }
}

void Ui::turn(int steps) {
    if (editing_) {
        pages_[cur_]->onTurn(steps);
        return;
    }
    int n = kNumPages;
    int p = (static_cast<int>(cur_) + steps) % n;
    if (p < 0) p += n;
    cur_ = static_cast<uint8_t>(p);
}

void Ui::press() {
    Page& p = *pages_[cur_];
    if (!editing_) {
        if (p.adjustable()) {
            editing_ = true;
            p.onEnter();
        }
        return;
    }
    if (p.onPress() == PageResult::Leave) leavePage();
}

void Ui::longPress() {
    if (!editing_) return;
    if (pages_[cur_]->onLongPress() == PageResult::Leave) leavePage();
}

void Ui::leavePage() {
    pages_[cur_]->onLeave();
    editing_ = false;
}

// ---------------------------------------------------------------- drawing

uint32_t Ui::hintKey() const {
    uint32_t h = hashAdd(kHashSeed, cur_ | (editing_ ? 0x100u : 0u));
    if (editing_) h = hashStr(h, pages_[cur_]->editHint());
    else h = hashAdd(h, pages_[cur_]->adjustable());
    return h;
}

void Ui::drawHint(Display& d) const {
    const Page& p = *pages_[cur_];
    const size_t cols = static_cast<size_t>(d.width() / 6);
    const int16_t y = d.height() - kHintH;
    char buf[40];
    if (editing_) {
        // Inverted, full width: says "you are inside a page".
        fitText(buf, sizeof buf, p.editHint(), cols);
        size_t len = strlen(buf);
        while (len < cols && len + 1 < sizeof buf) buf[len++] = ' ';
        buf[len] = 0;
        d.text(0, y, buf, 1, true);
        return;
    }
    char title[16];
    fitText(title, sizeof title, p.title(), 9);
    snprintf(buf, sizeof buf, "<%s>", title);
    d.text(0, y, buf);
    const char* right = p.adjustable() ? "push:open" : "turn:page";
    d.text(static_cast<int16_t>(d.width() - 6 * static_cast<int16_t>(strlen(right))), y, right);
}

void Ui::render(uint8_t i) {
    Display& d = hal_.display(i);
    d.clear();
    if (i == 0) {
        shown_[0]->draw(d, static_cast<int16_t>(d.height() - kHintH));
        drawHint(d);
    } else {
        shown_[i]->draw(d, d.height());
    }
}

void Ui::update(uint32_t nowMs) {
    for (Page* p : pages_) p->tick(nowMs);
    bankName_.tick(nowMs);
    handleInput(nowMs);

    const HardwareInfo& hw = hal_.info();
    uint8_t nd = hw.numDisplays < kMaxDisplays ? hw.numDisplays : kMaxDisplays;
    if (nd == 0) return;

    // What each display shows now, and whether that differs from what is on the glass.
    shown_[0] = pages_[cur_];
    if (nd > 1) shown_[1] = &secondaryView();
    for (uint8_t i = 0; i < nd; ++i) {
        // Identity of the view is part of the key, so switching views always redraws.
        uint32_t k = hashAdd(shown_[i]->contentKey(), static_cast<uint32_t>(
                                 reinterpret_cast<uintptr_t>(shown_[i]) & 0xFFFFFFFFu));
        if (i == 0) k = hashAdd(k, hintKey());
        if (k != key_[i]) { key_[i] = k; dirty_[i] = true; }
    }

    // Redraw and flush at most one display, round-robin so neither starves.
    for (uint8_t j = 0; j < nd; ++j) {
        uint8_t i = static_cast<uint8_t>((nextDisplay_ + j) % nd);
        if (!dirty_[i]) continue;
        if (everFlushed_[i] && nowMs - lastFlush_[i] < kMinRedrawMs) continue;
        render(i);
        hal_.flushDisplay(i);
        dirty_[i] = false;
        everFlushed_[i] = true;
        lastFlush_[i] = nowMs;
        nextDisplay_ = static_cast<uint8_t>((i + 1) % nd);
        break;
    }
}

}  // namespace density
