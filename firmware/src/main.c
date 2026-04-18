/*
 * g102-rebellion firmware — HID mouse skeleton.
 *
 * For now: buttons + scroll wheel over USB HID. No sensor yet, so X/Y are
 * always zero. Holding the DPI button for ~1 s writes BOOT_MAGIC and
 * soft-resets, dropping back into the DFU bootloader for a flash update
 * without touching BOOT0 / NRST.
 */

#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include "board.h"
#include "hid.h"
#include "inputs.h"

#define DPI_HOLD_POLLS  500u    // roughly 0.5 s at ~1 ms HID poll

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

    inputs_init();
    usb_hid_init();

    uint16_t dpi_held = 0;
    uint8_t  last_buttons = 0xFF;
    int8_t   last_wheel = 0;

    while (1) {
        usb_hid_poll();
        inputs_poll();

        uint8_t buttons = inputs_buttons();
        int8_t  wheel   = inputs_wheel_consume();

        // Only transmit when something changed — avoids flooding the bus
        // and lets the interrupt endpoint stay idle when the mouse is still.
        if (buttons != last_buttons || wheel != 0) {
            usb_hid_send_report(buttons, 0, 0, wheel);
            last_buttons = buttons;
            last_wheel = wheel;
        }
        (void)last_wheel;

        if (!gpio_get(BTN_DPI_PORT, BTN_DPI_PIN)) {
            if (++dpi_held > DPI_HOLD_POLLS) reboot_to_bootloader();
        } else {
            dpi_held = 0;
        }
    }
}
