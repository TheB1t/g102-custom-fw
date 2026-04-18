/*
 * g102-custom-fw bootloader
 *
 * At reset: decide between "stay in bootloader and accept DFU upload" or
 * "jump to firmware at 0x08004000".
 *
 * Stay-in-bootloader triggers:
 *   - BOOT_MAGIC_ADDR holds BOOT_MAGIC (firmware asked for reflash)
 *   - DPI button held at reset (physical override)
 *   - firmware vector table looks invalid (pristine chip / bad flash)
 */

#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/vector.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include "board.h"
#include "led_indicator.h"

void usb_dfu_run(void);

static int dpi_button_pressed(void)
{
    rcc_periph_clock_enable(RCC_GPIOB);
    gpio_mode_setup(BTN_DPI_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, BTN_DPI_PIN);
    for (volatile int i = 0; i < 10000; i++) { }
    return gpio_get(BTN_DPI_PORT, BTN_DPI_PIN) == 0;
}

static int firmware_looks_valid(void)
{
    const uint32_t *vt = (const uint32_t *)FLASH_FIRMWARE_BASE;
    uint32_t sp    = vt[0];
    uint32_t reset = vt[1];
    if ((sp & 0xFFFC0000u) != 0x20000000u) return 0;
    if (reset < FLASH_FIRMWARE_BASE || reset >= FLASH_FIRMWARE_END) return 0;
    if ((reset & 1) == 0) return 0;
    return 1;
}

static int should_stay_in_bootloader(void)
{
    if (BOOT_MAGIC_ADDR == BOOT_MAGIC) {
        BOOT_MAGIC_ADDR = 0;
        return 1;
    }
    if (dpi_button_pressed()) return 1;
    if (!firmware_looks_valid()) return 1;
    return 0;
}

static void jump_to_firmware(void)
{
    const uint32_t *vt = (const uint32_t *)FLASH_FIRMWARE_BASE;
    uint32_t sp    = vt[0];
    uint32_t reset = vt[1];

    // Deinit: turn off peripherals the bootloader enabled so firmware starts clean.
    rcc_periph_clock_disable(RCC_GPIOB);

    SCB_VTOR = FLASH_FIRMWARE_BASE;
    __asm__ volatile (
        "msr msp, %0\n"
        "bx  %1\n"
        :: "r"(sp), "r"(reset)
    );
    __builtin_unreachable();
}

int main(void)
{
    if (!should_stay_in_bootloader()) {
        jump_to_firmware();
    }

    // USB on STM32F072 needs 48 MHz. HSI48 is perfect — no crystal required.
    rcc_clock_setup_in_hsi48_out_48mhz();
    rcc_periph_clock_enable(RCC_GPIOA);
    led_indicator_init();

    usb_dfu_run();   // never returns; exits via system reset after download

    while (1) { }
}
