#pragma once

/*
 * RGB lighting driver — 3 common-anode LEDs on 9 MCU pins, brightness
 * via Binary Code Modulation (BCM) so one timer drives all 9 channels
 * with only 10 IRQs per refresh cycle instead of 1024 for plain PWM.
 *
 * 10-bit BCM: refresh period = 1023 base ticks. Phase N holds the "bit N"
 * pattern for 2^N ticks — phase 9 dominates (512 ticks), phase 0 flickers
 * for 1 tick. With a 48 MHz system clock and a 5 µs base tick the full
 * cycle is ~5.12 ms (~195 Hz) — above flicker threshold for any duty.
 *
 * Usage:
 *     drivers::Lighting<Board> leds;
 *     leds.init();
 *     leds.set(1, 0xFF, 0x40, 0x00);  // LED1: warm orange
 */

#include <cstdint>

namespace drivers {

template <typename Board>
class Lighting {
public:
    static constexpr uint8_t kLedCount = 3;

    void init();

    /* LED index is 1-based (matches the silkscreen / physical labelling). */
    void set(uint8_t led, uint8_t r, uint8_t g, uint8_t b);
    void off();

    /* Breathing: stores a base colour per LED and scales by a global
       triangular-wave brightness (0..255..0) advanced from now_ms().
       Call tick() every main-loop iteration; it throttles itself. */
    void set_base(uint8_t led, uint8_t r, uint8_t g, uint8_t b);
    void tick(uint32_t now_ms);

    /* Called from TIM6_DAC IRQ handler; public so the extern "C" shim can
       reach it without friend gymnastics. Not meant for external callers. */
    void on_tick();

    static Lighting *instance_;

private:
    /* 9 cathode GPIOs, one per R/G/B channel of each LED. Indexed:
       0=Led1R 1=Led1G 2=Led1B 3=Led2R 4=Led2G 5=Led2B 6=Led3R 7=Led3G 8=Led3B */
    struct PinRef { uint32_t port; uint16_t mask; };
    static const PinRef pins_[9];

    /* Per-channel 10-bit duty. 16-bit reads/writes are atomic on M0 so
       single-channel ISR reads don't need a lock. */
    volatile uint16_t duty_[9] = {};

    /* Current BCM phase, advances 0..9 inside the ISR. */
    volatile uint8_t phase_ = 0;

    /* Base colour (pre-brightness) for each LED, RGB triples, 8-bit. */
    uint8_t base_[9] = {};
    uint32_t last_breath_ms_ = 0;
};

} // namespace drivers
