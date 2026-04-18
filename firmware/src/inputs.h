#pragma once

#include <stdint.h>

void inputs_init(void);

/* Read debounced button state (HID_BTN_* bitmap) and accumulated wheel
   ticks since the last call. Clears the wheel counter on read. */
uint8_t inputs_buttons(void);
int8_t  inputs_wheel_consume(void);

/* Call often (e.g. from the main loop) to drive the quadrature decoder. */
void inputs_poll(void);
