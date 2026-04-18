#include <libopencm3/cm3/systick.h>
#include "platform/systick.hpp"

namespace platform {

volatile uint32_t tick_ms = 0;

void systick_init(uint32_t ahb_hz)
{
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_set_reload(ahb_hz / 1000u - 1u);
    systick_clear();
    systick_counter_enable();
    systick_interrupt_enable();
}

} // namespace platform

extern "C" void sys_tick_handler()
{
    platform::tick_ms++;
}
