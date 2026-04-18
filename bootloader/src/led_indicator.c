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

/* PWM period in ms. 4 ms @ 1/4 duty = 1 ms on, 3 ms off → ~250 Hz,
   well above flicker threshold. */
#define PWM_PERIOD_MS   4u
#define PWM_ON_MS       1u
#define LED_ON_MS       500u

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
    systick_set_reload(48000 - 1);
    systick_clear();
    systick_interrupt_enable();
    systick_counter_enable();
}

void led_indicator_tick(void)
{
    uint32_t now    = tick_ms;
    uint8_t  active = (now / LED_ON_MS) % 3u;
    uint32_t phase  = now % PWM_PERIOD_MS;
    bool     on     = phase < PWM_ON_MS;

    for (int i = 0; i < 3; i++) {
        if (i == active && on) gpio_set(led_ports[i], led_pins[i]);
        else                    gpio_clear(led_ports[i], led_pins[i]);
    }
}
