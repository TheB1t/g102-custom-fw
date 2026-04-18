/*
 * Flash programming helpers for the DFU class.
 *
 * STM32F072 has 2 KiB pages. We only touch the firmware region
 * (0x08004000 .. 0x0801FFFF) — the bootloader protects itself by refusing
 * writes outside that range.
 */

#include <libopencm3/stm32/flash.h>
#include "board.h"
#include "flash.h"

#define PAGE_SIZE 2048u

int flash_is_writable(uint32_t addr, uint32_t len)
{
    if (addr < FLASH_FIRMWARE_BASE) return 0;
    if (addr + len > FLASH_FIRMWARE_END) return 0;
    return 1;
}

int flash_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (!flash_is_writable(addr, len)) return -1;
    if (addr & 1u) return -1;            // half-word aligned required

    flash_unlock();

    // Erase every page this write touches. Callers stream page-sized chunks
    // (DFU's wTransferSize is 2048 for us) so each call typically erases one.
    for (uint32_t a = addr & ~(PAGE_SIZE - 1u); a < addr + len; a += PAGE_SIZE) {
        flash_erase_page(a);
        if (FLASH_SR & FLASH_SR_PGERR) { flash_lock(); return -2; }
    }

    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t hw = data[i] | ((i + 1 < len ? data[i + 1] : 0xFF) << 8);
        flash_program_half_word(addr + i, hw);
        if (FLASH_SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) {
            flash_lock();
            return -3;
        }
    }

    flash_lock();
    return 0;
}
