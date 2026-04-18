/*
 * g102-rebellion firmware — minimal stub that lives at 0x08004000.
 *
 * For now it only demonstrates the round-trip with the bootloader:
 * hold the DPI button for ~1 s while the firmware is running and the
 * firmware writes BOOT_MAGIC into RAM and soft-resets — the bootloader
 * sees the magic and drops back into DFU without needing any physical
 * reset dance.
 */

#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include "board.h"

static void reboot_to_bootloader(void)
{
    BOOT_MAGIC_ADDR = BOOT_MAGIC;
    scb_reset_system();
    __builtin_unreachable();
}

int main(void)
{
    SCB_VTOR = FLASH_FIRMWARE_BASE;

    rcc_clock_setup_in_hsi48_out_48mhz();
    rcc_periph_clock_enable(RCC_GPIOB);
    gpio_mode_setup(BTN_DPI_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, BTN_DPI_PIN);

    // Debounced long-press detector: DPI held for ~1 s → reflash.
    volatile uint32_t held = 0;
    while (1) {
        if (gpio_get(BTN_DPI_PORT, BTN_DPI_PIN) == 0) {
            if (++held > 500000u) reboot_to_bootloader();
        } else {
            held = 0;
        }
    }
}
