/*
 * g102-custom-fw firmware entry point.
 *
 * Wires the board-specific driver instances together and runs the HID
 * poll loop. Holding the DPI button ~0.5 s writes BOOT_MAGIC and
 * soft-resets into the DFU bootloader.
 */

#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>

#include "board.hpp"
#include "platform/spi.hpp"
#include "drivers/buttons.hpp"
#include "drivers/encoder_quad.hpp"
#include "drivers/sensor_1855.hpp"
#include "services/usb_hid.hpp"

namespace {

constexpr uint16_t DPI_HOLD_POLLS = 500u;

[[noreturn]] void reboot_to_bootloader()
{
    board::boot_magic_slot() = board::BOOT_MAGIC;
    scb_reset_system();
    __builtin_unreachable();
}

} // namespace

int main()
{
    SCB_VTOR = board::FLASH_FIRMWARE_BASE;
    rcc_clock_setup_in_hsi48_out_48mhz();

    using Board = board::G102;
    using SpiBus = platform::SoftCsSpiBus<Board::SensorCs>;

    SpiBus spi{SPI1};
    drivers::Sensor1855<Board, SpiBus> sensor{spi};
    drivers::QuadEncoder<Board> encoder;
    services::UsbHidMouse usb;

    drivers::Buttons<Board>::init();
    encoder.init();
    sensor.init();
    usb.init();

    uint16_t dpi_held = 0;
    uint8_t  last_buttons = 0xFF;

    while (true) {
        usb.poll();
        encoder.poll();

        uint8_t buttons = drivers::Buttons<Board>::hid_bitmap();
        int8_t  wheel   = encoder.consume_ticks();

        int8_t sx = 0, sy = 0;
        sensor.read_motion(sx, sy);
        /* Sensor chip is rotated 90° on the G102 PCB: its X is mouse
           forward/back, its Y is left/right. Remap to HID frame. */
        int8_t dx = sy;
        int8_t dy = -sx;

        if (buttons != last_buttons || wheel != 0 || dx != 0 || dy != 0) {
            usb.send_report(buttons, dx, dy, wheel);
            last_buttons = buttons;
        }

        if (drivers::Buttons<Board>::dpi_pressed()) {
            if (++dpi_held > DPI_HOLD_POLLS) reboot_to_bootloader();
        } else {
            dpi_held = 0;
        }
    }
}
