/*
 * Button matrix and scroll encoder for the G102.
 *
 * Buttons (all active-low with internal pull-up):
 *   LMB     PB14    RMB     PB12
 *   middle  PB13    DPI     PB15
 *   macro1  PB7     macro2  PB6
 *
 * Scroll encoder: quadrature on PA8 (A) / PA9 (B). One detent ≈ 4 state
 * transitions; we decode the Gray sequence and emit one tick per detent.
 */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include "board.h"
#include "hid.h"
#include "inputs.h"

static int8_t  wheel_acc;
static uint8_t enc_last;
static uint8_t enc_accum;   // 2-bit phase accumulator for ×4 → 1 tick

void inputs_init(void)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);

    gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                    BTN_LMB_PIN | BTN_RMB_PIN | BTN_SCROLL_PIN | BTN_DPI_PIN |
                    BTN_MACRO1_PIN | BTN_MACRO2_PIN);

    gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                    ENC_A_PIN | ENC_B_PIN);

    enc_last = (gpio_get(ENC_A_PORT, ENC_A_PIN) ? 1 : 0) |
               (gpio_get(ENC_B_PORT, ENC_B_PIN) ? 2 : 0);
}

uint8_t inputs_buttons(void)
{
    uint8_t b = 0;
    if (!gpio_get(BTN_LMB_PORT,    BTN_LMB_PIN))    b |= HID_BTN_LEFT;
    if (!gpio_get(BTN_RMB_PORT,    BTN_RMB_PIN))    b |= HID_BTN_RIGHT;
    if (!gpio_get(BTN_SCROLL_PORT, BTN_SCROLL_PIN)) b |= HID_BTN_MIDDLE;
    return b;
}

int8_t inputs_wheel_consume(void)
{
    int8_t w = wheel_acc;
    wheel_acc = 0;
    return w;
}

/* Gray-code quadrature decoder. Each valid transition moves by +1 / -1 in
   the ×4 counter; we report a wheel tick every 4 counts (one mechanical detent). */
void inputs_poll(void)
{
    uint8_t now = (gpio_get(ENC_A_PORT, ENC_A_PIN) ? 1 : 0) |
                  (gpio_get(ENC_B_PORT, ENC_B_PIN) ? 2 : 0);
    if (now == enc_last) return;

    // Encoded transition table: (old<<2)|new → +1, -1, or 0 (invalid/bounce).
    static const int8_t step[16] = {
         0, -1, +1,  0,
        +1,  0,  0, -1,
        -1,  0,  0, +1,
         0, +1, -1,  0,
    };
    int8_t d = step[(enc_last << 2) | now];
    enc_last = now;

    enc_accum = (uint8_t)((int8_t)enc_accum + d);
    if ((int8_t)enc_accum >= 4)  { wheel_acc = (wheel_acc < 127)  ? wheel_acc + 1 : 127;  enc_accum = 0; }
    if ((int8_t)enc_accum <= -4) { wheel_acc = (wheel_acc > -127) ? wheel_acc - 1 : -127; enc_accum = 0; }
}
