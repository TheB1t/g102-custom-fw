#pragma once

/*
 * GPIO pin abstraction — compile-time typed Pin<Port, Mask>.
 *
 * Pins are template parameters, so every call compiles down to the same
 * libopencm3 code that raw macros would produce. Zero runtime overhead,
 * but you can't accidentally pass a GPIOA pin to a GPIOB call.
 *
 *   using LMB = Pin<GPIOB, GPIO14>;
 *   LMB::setup_input_pullup();
 *   if (!LMB::read()) { ... }
 */

#include <cstdint>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

namespace platform {

template <uint32_t Port, uint16_t Mask>
struct Pin {
    static constexpr uint32_t port = Port;
    static constexpr uint16_t mask = Mask;

    static void setup_input_pullup()
    {
        gpio_mode_setup(Port, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, Mask);
    }

    static void setup_output_pp(uint8_t speed = GPIO_OSPEED_HIGH)
    {
        gpio_mode_setup(Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, Mask);
        gpio_set_output_options(Port, GPIO_OTYPE_PP, speed, Mask);
    }

    static void setup_af(uint8_t af, uint8_t otype = GPIO_OTYPE_PP,
                         uint8_t speed = GPIO_OSPEED_HIGH)
    {
        gpio_mode_setup(Port, GPIO_MODE_AF, GPIO_PUPD_NONE, Mask);
        gpio_set_output_options(Port, otype, speed, Mask);
        gpio_set_af(Port, af, Mask);
    }

    static bool read()  { return gpio_get(Port, Mask) != 0; }
    static void set()   { gpio_set(Port, Mask); }
    static void clear() { gpio_clear(Port, Mask); }
    static void write(bool v) { v ? set() : clear(); }
};

/* RCC clock enable helpers — forward the Port constant to the right bus.
   Expressed as free functions so Pin<> stays data-only. */
inline void enable_port_clock(uint32_t port)
{
    switch (port) {
        case GPIOA: rcc_periph_clock_enable(RCC_GPIOA); break;
        case GPIOB: rcc_periph_clock_enable(RCC_GPIOB); break;
        case GPIOC: rcc_periph_clock_enable(RCC_GPIOC); break;
        case GPIOD: rcc_periph_clock_enable(RCC_GPIOD); break;
        case GPIOF: rcc_periph_clock_enable(RCC_GPIOF); break;
        default: break;
    }
}

} // namespace platform
