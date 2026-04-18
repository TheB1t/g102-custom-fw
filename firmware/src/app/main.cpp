/*
 * g102-custom-fw firmware entry point.
 *
 * Wires the board-specific driver instances together and runs the HID
 * poll loop. Holding the DPI button ~0.5 s writes BOOT_MAGIC and
 * soft-resets into the DFU bootloader.
 */

#include <cstring>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/syscfg.h>

#include "board.hpp"
#include "platform/spi.hpp"
#include "platform/systick.hpp"
#include "drivers/buttons.hpp"
#include "drivers/encoder_quad.hpp"
#include "drivers/lighting.hpp"
#include "drivers/sensor_1855.hpp"
#include "services/usb_hid.hpp"

namespace {

constexpr uint32_t DPI_HOLD_MS = 3000u;

[[noreturn]] void reboot_to_bootloader()
{
    /* Set the handoff flag so the bootloader stays in DFU regardless of
       pin state at reset. Still wait for the user to release DPI first —
       otherwise the bootloader's physical-override check also latches
       true and we'd need two independent release/press cycles to escape. */
    board::boot_magic_slot() = board::BOOT_MAGIC;
    while (drivers::Buttons<board::G102>::dpi_pressed()) { }
    scb_reset_system();
    __builtin_unreachable();
}

/* Cortex-M0 (STM32F0) has no writable VTOR — interrupts always dispatch
   through 0x00000000, which is the bootloader's vector table. To get our
   own handlers (SysTick, USB, etc.) we copy the firmware vector table
   into SRAM and flip SYSCFG.MEM_MODE to remap SRAM at address 0. */
__attribute__((section(".vectors_sram"), used))
alignas(256) uint32_t g_vectors_sram[48];

void remap_vectors_to_sram()
{
    std::memcpy(g_vectors_sram,
                reinterpret_cast<const void *>(board::FLASH_FIRMWARE_BASE),
                sizeof(g_vectors_sram));
    rcc_periph_clock_enable(RCC_SYSCFG_COMP);
    SYSCFG_CFGR1 = (SYSCFG_CFGR1 & ~SYSCFG_CFGR1_MEM_MODE) |
                   SYSCFG_CFGR1_MEM_MODE_SRAM;
}

} // namespace

int main()
{
    rcc_clock_setup_in_hsi48_out_48mhz();
    remap_vectors_to_sram();

    using Board = board::G102;
    using SpiBus = platform::SoftCsSpiBus<Board::SensorCs>;

    SpiBus spi{SPI1};
    drivers::Sensor1855<Board, SpiBus> sensor{spi};
    drivers::QuadEncoder<Board> encoder;
    drivers::Lighting<Board> lighting;
    services::UsbHidMouse usb;

    /* Bring USB up first and spin until the host has finished enumeration.
       Anything that might block (soft-SPI sensor bringup, GPIO init) must
       wait — a stalled usbd_poll during SETUP causes LANGID STALL and
       kernel "string descriptor 0 read error -71". */
    platform::systick_init();
    usb.init();
    while (!usb.configured()) usb.poll();

    drivers::Buttons<Board>::init();
    encoder.init();
    lighting.init();
    lighting.set_base(1, 0xFF, 0x00, 0x00);
    lighting.set_base(2, 0x00, 0xFF, 0x00);
    lighting.set_base(3, 0x00, 0x00, 0xFF);
    sensor.init_start();

    uint32_t dpi_pressed_since = 0;
    bool     dpi_was_pressed   = false;
    uint8_t  last_buttons      = 0xFF;

    while (true) {
        uint32_t now = platform::now_ms();

        usb.poll();
        encoder.poll();
        sensor.init_tick(now);
        lighting.tick(now);

        uint8_t buttons = drivers::Buttons<Board>::hid_bitmap();
        int8_t  wheel   = encoder.consume_ticks();

        int16_t sx = 0, sy = 0;
        if (sensor.ready()) sensor.read_motion(sx, sy);
        /* Sensor chip is rotated 90° on the G102 PCB: its X is mouse
           forward/back, its Y is left/right. Remap to HID frame. */
        int16_t dx = sy;
        int16_t dy = static_cast<int16_t>(-sx);

        if (buttons != last_buttons || wheel != 0 || dx != 0 || dy != 0) {
            usb.send_report(buttons, dx, dy, wheel);
            last_buttons = buttons;
        }

        bool dpi = drivers::Buttons<Board>::dpi_pressed();
        if (dpi && !dpi_was_pressed) dpi_pressed_since = now;
        if (dpi && (now - dpi_pressed_since) >= DPI_HOLD_MS) reboot_to_bootloader();
        dpi_was_pressed = dpi;
    }
}
