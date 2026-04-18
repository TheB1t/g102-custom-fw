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
#include "sensor.h"

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
    sensor_init();
    usb_hid_init();

    uint16_t dpi_held = 0;
    uint8_t  last_buttons = 0xFF;

    while (1) {
        usb_hid_poll();
        inputs_poll();

        uint8_t buttons = inputs_buttons();
        int8_t  wheel   = inputs_wheel_consume();
        int8_t  sx = 0, sy = 0;
        sensor_read_motion(&sx, &sy);
        // Sensor chip is rotated 90° on the G102 PCB: its X axis is the
        // mouse's forward/back, its Y axis is left/right. Remap to HID.
        int8_t  dx = sy;
        int8_t  dy = -sx;

        // Transmit when anything is non-zero or buttons changed. Otherwise
        // stay silent — keeps the bus quiet and avoids spamming the host.
        if (buttons != last_buttons || wheel != 0 || dx != 0 || dy != 0) {
            usb_hid_send_report(buttons, dx, dy, wheel);
            last_buttons = buttons;
        }

        if (!gpio_get(BTN_DPI_PORT, BTN_DPI_PIN)) {
            if (++dpi_held > DPI_HOLD_POLLS) reboot_to_bootloader();
        } else {
            dpi_held = 0;
        }
    }
}
