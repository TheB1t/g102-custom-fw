#pragma once

/*
 * Button reader for the G102 primary mouse buttons (LMB/RMB/Middle).
 * Macro buttons and DPI are exposed as raw pin reads — they are not part
 * of the HID boot-protocol 3-button bitmap.
 *
 * Templated on a Board trait (see common/board.hpp). Zero runtime state.
 */

#include <cstdint>

namespace drivers {

/* HID button bits — must match the report descriptor (5 usable buttons).
   Bits 3/4 are the standard "thumb back / thumb forward" on Logitech mice. */
constexpr uint8_t BTN_LEFT    = 1u << 0;
constexpr uint8_t BTN_RIGHT   = 1u << 1;
constexpr uint8_t BTN_MIDDLE  = 1u << 2;
constexpr uint8_t BTN_BACK    = 1u << 3;   // thumb rear  — BTN_SIDE  in evdev
constexpr uint8_t BTN_FORWARD = 1u << 4;   // thumb front — BTN_EXTRA in evdev

template <typename Board>
class Buttons {
public:
    static void init()
    {
        using namespace platform;
        enable_port_clock(Board::BtnLmb::port);
        Board::BtnLmb::setup_input_pullup();
        Board::BtnRmb::setup_input_pullup();
        Board::BtnScroll::setup_input_pullup();
        Board::BtnDpi::setup_input_pullup();
        Board::BtnMacro1::setup_input_pullup();
        Board::BtnMacro2::setup_input_pullup();
    }

    /* HID-bitmap read for all 5 reported buttons. Active-low → inverted. */
    static uint8_t hid_bitmap()
    {
        uint8_t b = 0;
        if (!Board::BtnLmb::read())    b |= BTN_LEFT;
        if (!Board::BtnRmb::read())    b |= BTN_RIGHT;
        if (!Board::BtnScroll::read()) b |= BTN_MIDDLE;
        if (!Board::BtnMacro1::read()) b |= BTN_BACK;
        if (!Board::BtnMacro2::read()) b |= BTN_FORWARD;
        return b;
    }

    static bool dpi_pressed()    { return !Board::BtnDpi::read(); }
    static bool macro1_pressed() { return !Board::BtnMacro1::read(); }
    static bool macro2_pressed() { return !Board::BtnMacro2::read(); }
};

} // namespace drivers
