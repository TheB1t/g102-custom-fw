#pragma once

/*
 * Board trait for the Logitech G102 LIGHTSYNC (PCB 210-002146 Rev.006).
 * Every pin is a typed Pin<> alias so drivers can be templated on the board
 * and new hardware variants are just another trait struct.
 *
 * Traced by continuity on 2026-04-18. All buttons active-low with internal
 * pull-up. See docs/G102_BOARD.md for the full pinout and schematic notes.
 */

#include <cstdint>
#include <libopencm3/stm32/gpio.h>
#include "platform/gpio.hpp"

namespace board {

/* Boot handoff — firmware writes BOOT_MAGIC to the last SRAM word then
   soft-resets; bootloader checks this slot and stays in DFU if set. */
constexpr uint32_t BOOT_MAGIC      = 0xB007F00Du;
inline volatile uint32_t &boot_magic_slot()
{
    return *reinterpret_cast<volatile uint32_t *>(0x20003FFCu);
}

constexpr uint32_t FLASH_BOOTLOADER_BASE = 0x08000000u;
constexpr uint32_t FLASH_FIRMWARE_BASE   = 0x08004000u;
constexpr uint32_t FLASH_FIRMWARE_END    = 0x08020000u;

struct G102 {
    /* Primary buttons */
    using BtnLmb    = platform::Pin<GPIOB, GPIO14>;
    using BtnRmb    = platform::Pin<GPIOB, GPIO12>;
    using BtnScroll = platform::Pin<GPIOB, GPIO13>;
    using BtnDpi    = platform::Pin<GPIOB, GPIO15>;

    /* Side macro buttons (thumb forward/back) */
    using BtnMacro1 = platform::Pin<GPIOB, GPIO7>;
    using BtnMacro2 = platform::Pin<GPIOB, GPIO6>;

    /* Scroll encoder (quadrature, internal pull-up, common to GND) */
    using EncA      = platform::Pin<GPIOA, GPIO8>;
    using EncB      = platform::Pin<GPIOA, GPIO9>;

    /* "1855" optical sensor SPI. CS is a plain GPIO (soft chip-select,
       active-low, idle-high). Frame every transaction. */
    using SensorCs   = platform::Pin<GPIOA, GPIO15>;
    using SensorSck  = platform::Pin<GPIOB, GPIO3>;   // SPI1 SCK  (AF0)
    using SensorMiso = platform::Pin<GPIOB, GPIO4>;   // SPI1 MISO (AF0)
    using SensorMosi = platform::Pin<GPIOB, GPIO5>;   // SPI1 MOSI (AF0)
    static constexpr uint8_t SensorSpiAf = GPIO_AF0;
};

} // namespace board
