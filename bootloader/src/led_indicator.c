/*
 * DFU-mode LED indicator.
 *
 * Cycles LED1 → LED2 → LED3 → repeat, each on for ~500 ms, at 1/4
 * brightness via software PWM. Runs entirely from led_tick(), which
 * must be called from the main usbd_poll loop as often as possible —
 * the PWM granularity is whatever the poll period happens to be, and
 * a 1/4 duty is coarse enough that jitter is invisible.
 */

#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include "board.h"
#include "led_indicator.h"

volatile uint32_t tick_ms = 0;

void sys_tick_handler(void)
{
    tick_ms++;
}

/* Software PWM off the SysTick downcounter: SysTick reloads at 48000
   every 1 ms, so SYST->VAL gives us a sub-millisecond phase for free
   without touching the tick rate the rest of the bootloader depends on.
   Period = 48000 CPU cycles = 1 ms → ~1 kHz PWM, above flicker threshold.

   Duty is in [0..SYSTICK_RELOAD]; MAX_DUTY caps brightness at ~1/4 to
   match the old indicator feel.

   Crossfade: every FADE_MS a pair of LEDs hands off — the outgoing LED
   ramps MAX_DUTY → 0, the incoming one ramps 0 → MAX_DUTY in lockstep. */
#define SYSTICK_RELOAD  48000u
#define MAX_DUTY        (SYSTICK_RELOAD / 4u)
#define FADE_MS         800u

static const uint32_t led_ports[3] = {LED1_PORT, LED2_PORT, LED3_PORT};
static const uint16_t led_pins[3]  = {LED1_PIN,  LED2_PIN,  LED3_PIN};

void led_indicator_init(void)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    for (int i = 0; i < 3; i++) {
        gpio_mode_setup(led_ports[i], GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, led_pins[i]);
        gpio_set_output_options(led_ports[i], GPIO_OTYPE_PP,
                                GPIO_OSPEED_LOW, led_pins[i]);
        gpio_clear(led_ports[i], led_pins[i]);
    }

    /* 1 kHz SysTick — 48 MHz / 48000 = 1 ms. */
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_set_reload(SYSTICK_RELOAD - 1);
    systick_clear();
    systick_interrupt_enable();
    systick_counter_enable();
}

void led_indicator_tick(void)
{
    uint32_t now      = tick_ms;
    uint32_t fade_pos = now % FADE_MS;          /* 0..FADE_MS-1 within hand-off */
    uint8_t  outgoing = (now / FADE_MS) % 3u;
    uint8_t  incoming = (outgoing + 1u) % 3u;

    /* Linear ramp — cheap and good enough visually. Swap to sine-LUT if
       we ever care about perceptual linearity. */
    uint32_t duty_in  = (MAX_DUTY * fade_pos) / FADE_MS;
    uint32_t duty_out = MAX_DUTY - duty_in;

    /* SysTick is a downcounter: VAL decrements from RELOAD-1 to 0 and
       reloads. Convert to an upcounting phase for the compare. */
    uint32_t phase = (SYSTICK_RELOAD - 1u) - systick_get_value();

    for (int i = 0; i < 3; i++) {
        uint32_t duty = (i == outgoing) ? duty_out
                      : (i == incoming) ? duty_in
                      : 0u;
        if (phase < duty) gpio_set(led_ports[i], led_pins[i]);
        else              gpio_clear(led_ports[i], led_pins[i]);
    }
}
