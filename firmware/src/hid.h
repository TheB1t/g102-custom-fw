#pragma once

#include <stdint.h>

/* Buttons bitmap: bit0 = left, bit1 = right, bit2 = middle. */
#define HID_BTN_LEFT    (1u << 0)
#define HID_BTN_RIGHT   (1u << 1)
#define HID_BTN_MIDDLE  (1u << 2)

void usb_hid_init(void);
void usb_hid_poll(void);
void usb_hid_send_report(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);
