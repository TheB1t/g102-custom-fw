#ifndef G102_LED_INDICATOR_H
#define G102_LED_INDICATOR_H

#include <stdbool.h>
#include <stdint.h>

/* 1 kHz monotonic counter owned by the SysTick handler in led_indicator.c.
   Exposed so the DFU loop can implement a DPI long-press timer. */
extern volatile uint32_t tick_ms;

/* Configure GPIOs + SysTick. Must be called after the 48 MHz clock tree
   is up (rcc_clock_setup_in_hsi48_out_48mhz). Installs sys_tick_handler. */
void led_indicator_init(void);

/* Drive the cycling PWM — call as often as possible from the main loop. */
void led_indicator_tick(void);

#endif
