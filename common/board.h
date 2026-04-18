#pragma once

// Pin assignments for the G102 PCB (STM32F072CB).
// Traced by continuity on 2026-04-18.
// All buttons active-low with internal pull-up.

#include <libopencm3/stm32/gpio.h>

#define BOOT_MAGIC          0xB007F00Du
// Last word of F072CB SRAM (16 KiB @ 0x20000000). Linker reserves this slot;
// see bootloader/link.ld and firmware/link.ld.
#define BOOT_MAGIC_ADDR     (*(volatile uint32_t *)0x20003FFC)

#define FLASH_BOOTLOADER_BASE   0x08000000u
#define FLASH_FIRMWARE_BASE     0x08004000u
#define FLASH_FIRMWARE_END      0x08020000u

// Buttons
#define BTN_RMB_PORT        GPIOB
#define BTN_RMB_PIN         GPIO12
#define BTN_SCROLL_PORT     GPIOB
#define BTN_SCROLL_PIN      GPIO13
#define BTN_LMB_PORT        GPIOB
#define BTN_LMB_PIN         GPIO14
#define BTN_DPI_PORT        GPIOB
#define BTN_DPI_PIN         GPIO15

// Scroll encoder (quadrature)
#define ENC_A_PORT          GPIOA
#define ENC_A_PIN           GPIO8
#define ENC_B_PORT          GPIOA
#define ENC_B_PIN           GPIO9

// Side macro buttons (forward / back)
#define BTN_MACRO1_PORT     GPIOB
#define BTN_MACRO1_PIN      GPIO7
#define BTN_MACRO2_PORT     GPIOB
#define BTN_MACRO2_PIN      GPIO6

// "1855" optical sensor SPI — CS is a plain GPIO (soft chip-select).
// Vendor not publicly identified; silk reads `1855 Rev 007`. Protocol is
// PixArt-family (SROM 2E/2F upload, 0x02 commit) but not a documented PAW part.
// Active-low, idle-high. Frame every transaction.
#define SENSOR_CS_PORT      GPIOA
#define SENSOR_CS_PIN       GPIO15

// TODO: SCK/MOSI/MISO alt-fn pins for the sensor (PB3/4/5 = SPI1 by default,
// verify against the PCB traces when wiring up the driver).
// TODO: status / DPI-indicator LED pin.
