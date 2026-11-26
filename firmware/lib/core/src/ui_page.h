// The UI's page interface. To add a page: write a class deriving from Page (see ui_pages.h
// for examples), add it as a member of Ui and put a pointer to it in Ui's page list (ui.cpp).
//
// Interaction, the same on every page:
//   browsing: turn = previous / next page; press = enter the page (only if adjustable())
//   inside:   turn = onTurn; press = onPress; long press (600 ms) = onLongPress
//             onPress / onLongPress return Leave to go back to browsing.
// The Ui draws the bottom hint line on display 0; pages draw above it (height given to draw).
#pragma once
#include "density/hal.h"
#include <cstdint>

namespace density {

enum class PageResult : uint8_t { Stay, Leave };

class Page {
public:
    virtual ~Page() = default;

    // Short name for the hint line ("Banks"). At most 9 characters.
    virtual const char* title() const = 0;
    // Has something to adjust, so a press enters it.
    virtual bool adjustable() const { return false; }
    // Hint shown (inverted) on the bottom line while inside the page. At most 21 characters.
    virtual const char* editHint() const { return "hold:back"; }

    virtual void onEnter() {}
    virtual void onLeave() {}
    virtual void onTurn(int /*steps*/) {}
    virtual PageResult onPress() { return PageResult::Leave; }
    virtual PageResult onLongPress() { return PageResult::Leave; }

    // Called every Ui::update, whether or not the page is visible. Must be cheap.
    virtual void tick(uint32_t /*nowMs*/) {}

    // A hash of everything draw() shows. The Ui redraws a display only when it changes.
    virtual uint32_t contentKey() const = 0;
    // Draw into rows 0..height-1 of a cleared display.
    virtual void draw(Display& d, int16_t height) const = 0;
};

}  // namespace density
