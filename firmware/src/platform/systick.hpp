#pragma once

/*
 * 1 kHz SysTick — monotonic millisecond counter.
 *
 * Single global tick_ms incremented by the Cortex SysTick IRQ. Call
 * systick_init() once after the clock tree is set up; read now_ms()
 * anywhere. 32-bit aligned read on M0 is atomic vs. the handler's
 * single-word increment, so no special guard needed.
 *
 * Rolls over after ~49 days; all wait logic uses `(now - start) >= dt`
 * unsigned arithmetic which is rollover-safe.
 */

#include <cstdint>

namespace platform {

extern volatile uint32_t tick_ms;

void systick_init(uint32_t ahb_hz = 48'000'000u);

inline uint32_t now_ms() { return tick_ms; }

} // namespace platform
