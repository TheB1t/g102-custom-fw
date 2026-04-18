#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/gpio.h>

#include "drivers/lighting.hpp"
#include "platform/gpio.hpp"
#include "board.hpp"

namespace drivers {

using Board = board::G102;

/* Base tick = 5 µs @ 48 MHz → PSC = 239, ARR reloaded per phase.
   Phase N period = (1 << N) base ticks. Sum = 1023 → refresh ~5.12 ms
   (~195 Hz, above flicker threshold). 10 bits of BCM = 1024 levels.
   Shortest ISR period = 10 µs (phase 0) — leaves CPU headroom. */
static constexpr uint16_t kPrescaler = 239;
static constexpr uint8_t  kPhaseCount = 10;

template <>
const Lighting<Board>::PinRef Lighting<Board>::pins_[9] = {
    {Board::Led1R::port, Board::Led1R::mask},
    {Board::Led1G::port, Board::Led1G::mask},
    {Board::Led1B::port, Board::Led1B::mask},
    {Board::Led2R::port, Board::Led2R::mask},
    {Board::Led2G::port, Board::Led2G::mask},
    {Board::Led2B::port, Board::Led2B::mask},
    {Board::Led3R::port, Board::Led3R::mask},
    {Board::Led3G::port, Board::Led3G::mask},
    {Board::Led3B::port, Board::Led3B::mask},
};

template <> Lighting<Board> *Lighting<Board>::instance_ = nullptr;

template <>
void Lighting<Board>::off()
{
    for (auto &d : duty_) d = 0;
    /* Immediately drop the lines so the ISR doesn't need to catch up. */
    for (const auto &p : pins_) gpio_clear(p.port, p.mask);
}

template <>
void Lighting<Board>::init()
{
    instance_ = this;

    /* All 9 pins are push-pull outputs driving the low-side switches. */
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    Board::Led1R::setup_output_pp();
    Board::Led1G::setup_output_pp();
    Board::Led1B::setup_output_pp();
    Board::Led2R::setup_output_pp();
    Board::Led2G::setup_output_pp();
    Board::Led2B::setup_output_pp();
    Board::Led3R::setup_output_pp();
    Board::Led3G::setup_output_pp();
    Board::Led3B::setup_output_pp();
    off();

    /* TIM6: basic up-counter, update event every ARR+1 ticks, no outputs.
       ARR is reloaded each IRQ to time the next BCM phase. Start with the
       longest period (phase 9 = 512 ticks) so the very first IRQ doesn't
       fire before main() reaches set(). */
    rcc_periph_clock_enable(RCC_TIM6);
    timer_set_prescaler(TIM6, kPrescaler);
    timer_set_period(TIM6, 511);  // start with phase 9 (longest) so first IRQ lags
    timer_enable_update_event(TIM6);
    timer_update_on_overflow(TIM6);
    timer_clear_flag(TIM6, TIM_SR_UIF);
    timer_enable_irq(TIM6, TIM_DIER_UIE);
    nvic_enable_irq(NVIC_TIM6_DAC_IRQ);
    timer_enable_counter(TIM6);
}

template <>
void Lighting<Board>::set(uint8_t led, uint8_t r, uint8_t g, uint8_t b)
{
    if (led < 1 || led > kLedCount) return;
    uint8_t base = (led - 1) * 3;
    duty_[base + 0] = r;
    duty_[base + 1] = g;
    duty_[base + 2] = b;
}

template <>
void Lighting<Board>::set_base(uint8_t led, uint8_t r, uint8_t g, uint8_t b)
{
    if (led < 1 || led > kLedCount) return;
    uint8_t i = (led - 1) * 3;
    base_[i + 0] = r;
    base_[i + 1] = g;
    base_[i + 2] = b;
}

template <>
void Lighting<Board>::tick(uint32_t now_ms)
{
    /* Update ~every 8 ms → full 0..255..0 sweep in ~4 s at step 1. */
    if ((now_ms - last_breath_ms_) < 8u) return;
    last_breath_ms_ = now_ms;

    /* Triangular wave from a 9-bit phase: 0..255 rising, 256..511 falling.
       Breath waveform stays 8-bit — gamma expansion widens the LED range. */
    uint16_t t = static_cast<uint16_t>((now_ms >> 3) & 0x1FFu);
    uint16_t bright = (t < 256u) ? t : (511u - t);

    /* Perceptual gamma ≈ 2.0: bright² (max 255²=65025) scaled back into
       10-bit duty range. Linear-perception ramp, compressed low end. */
    uint32_t b2 = bright * bright;                     // 0..65025
    uint32_t bq = (b2 * 1023u + 32512u) / 65025u;      // 0..1023, rounded
    for (uint8_t i = 0; i < 9; ++i) {
        uint32_t scaled = (static_cast<uint32_t>(base_[i]) * bq + 127u) / 255u;
        duty_[i] = static_cast<uint16_t>(scaled);      // 0..1023
    }
}

template <>
void Lighting<Board>::on_tick()
{
    /* Advance phase before computing the next mask — the current ARR was
       sized for the outgoing phase, so we're entering phase_+1. */
    phase_ = (phase_ + 1) % kPhaseCount;
    uint8_t bit = phase_;

    /* Build per-port set/clear masks in one pass. BSRR high halfword = BR
       (reset), low halfword = BS (set). Write once per port to keep the
       transition atomic for all channels on that port. */
    uint32_t set_a = 0, clr_a = 0, set_b = 0, clr_b = 0;
    for (uint8_t i = 0; i < 9; ++i) {
        bool on = (duty_[i] >> bit) & 1u;
        uint32_t mask = pins_[i].mask;
        if (pins_[i].port == GPIOA) {
            if (on) set_a |= mask; else clr_a |= mask;
        } else {
            if (on) set_b |= mask; else clr_b |= mask;
        }
    }
    GPIO_BSRR(GPIOA) = set_a | (clr_a << 16);
    GPIO_BSRR(GPIOB) = set_b | (clr_b << 16);

    /* Program ARR for the next phase. STM32 TIM6 with ARR=0 stops counting
       (RM0091 §22.4), so bias phase_=0 to ARR=1. Also reset CNT: the ISR
       can fire a few ticks late and CNT may already be > new_ARR, which
       would wrap through 0xFFFF instead of triggering an update. */
    uint32_t period = (1u << phase_);
    timer_set_period(TIM6, period - (phase_ == 0 ? 0u : 1u));
    TIM_CNT(TIM6) = 0;
}

/* Explicit instantiation — Board is fixed so the linker only needs one copy. */
template class Lighting<Board>;

} // namespace drivers

extern "C" void tim6_dac_isr()
{
    /* Clear UIF with a direct write (rc_w0) — not read-modify-write, which
       races with the next update event and can drop it when ARR is tiny. */
    TIM_SR(TIM6) = static_cast<uint32_t>(~TIM_SR_UIF);
    if (auto *l = drivers::Lighting<board::G102>::instance_) l->on_tick();
}
