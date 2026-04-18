#pragma once

/*
 * C-compatible subset of board.hpp for the bootloader (which stays in C).
 * Only the flash-layout constants and BOOT_MAGIC handoff slot — no pin
 * traits, since the bootloader doesn't touch GPIO drivers.
 */

#include <stdint.h>
#include <libopencm3/stm32/gpio.h>

/* DPI button — the physical "stay in DFU" override at reset.
   Must match board.hpp G102::BtnDpi (GPIOB, GPIO15). */
#define BTN_DPI_PORT            GPIOB
#define BTN_DPI_PIN             GPIO15

/* Forward side-macro button — used in DFU mode as the "boot firmware"
   shortcut. PB6 is the upper/front side button; PB7 is the lower/back. */
#define BTN_FWD_PORT            GPIOB
#define BTN_FWD_PIN             GPIO6

/* RGB LED aggregate pins (all 3 LEDs, one channel per primary). The
   bootloader's DFU indicator only uses one colour per LED, so we drive
   just the red cathodes — full map lives in board.hpp. HIGH = lit. */
#define LED1_PORT               GPIOB
#define LED1_PIN                GPIO9
#define LED2_PORT               GPIOA
#define LED2_PIN                GPIO6
#define LED3_PORT               GPIOA
#define LED3_PIN                GPIO7

#define BOOT_MAGIC              0xB007F00Du
#define BOOT_MAGIC_ADDR         (*(volatile uint32_t *)0x20003FFCu)

#define FLASH_BOOTLOADER_BASE   0x08000000u
#define FLASH_FIRMWARE_BASE     0x08004000u
#define FLASH_FIRMWARE_END      0x08020000u
