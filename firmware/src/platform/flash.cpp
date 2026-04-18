#include <libopencm3/stm32/flash.h>
#include "platform/flash.hpp"

extern "C" {
    extern uint32_t _settings_page_a;
    extern uint32_t _settings_page_b;
    extern uint32_t _settings_page_sz;
}

namespace platform::flash {

static uint32_t settings_base() { return reinterpret_cast<uint32_t>(&_settings_page_a); }
static uint32_t settings_end()  { return reinterpret_cast<uint32_t>(&_settings_page_b)
                                       + reinterpret_cast<uint32_t>(&_settings_page_sz); }

bool is_settings_range(uint32_t addr, uint32_t len)
{
    return addr >= settings_base() && (addr + len) <= settings_end();
}

bool erase_page(uint32_t addr)
{
    if (!is_settings_range(addr, kPageSize)) return false;
    if (addr & (kPageSize - 1u)) return false;

    flash_unlock();
    flash_erase_page(addr);
    bool ok = (FLASH_SR & FLASH_SR_PGERR) == 0;
    flash_lock();
    return ok;
}

bool program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (!is_settings_range(addr, len)) return false;
    if (addr & 1u) return false;

    flash_unlock();
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t hw = data[i] | ((i + 1 < len ? data[i + 1] : 0xFF) << 8);
        flash_program_half_word(addr + i, hw);
        if (FLASH_SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) {
            flash_lock();
            return false;
        }
    }
    flash_lock();
    return true;
}

} // namespace platform::flash
