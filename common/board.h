#ifndef G102_BOARD_H
#define G102_BOARD_H

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

#define BOOT_MAGIC              0xB007F00Du
#define BOOT_MAGIC_ADDR         (*(volatile uint32_t *)0x20003FFCu)

#define FLASH_BOOTLOADER_BASE   0x08000000u
#define FLASH_FIRMWARE_BASE     0x08004000u
#define FLASH_FIRMWARE_END      0x08020000u

#endif
