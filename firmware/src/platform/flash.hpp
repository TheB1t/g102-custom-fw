#pragma once

/*
 * Bare-minimum flash programming helpers for in-firmware persistent
 * storage. Bootloader has its own copy in bootloader/src/flash.c with
 * a different range guard — this one intentionally only exposes
 * erase_page / program for the last two flash pages (the settings
 * region). Any attempt to write outside that range is rejected.
 */

#include <cstdint>
#include <cstddef>

namespace platform::flash {

constexpr uint32_t kPageSize = 2048u;

/* Returns true on success, false on any flash error or range violation.
   addr must be page-aligned for erase_page; program requires half-word
   alignment on addr and len. */
bool erase_page(uint32_t addr);
bool program(uint32_t addr, const uint8_t *data, uint32_t len);

/* True only for addresses inside the reserved settings region
   (see firmware/link.ld: _settings_page_a/_b). */
bool is_settings_range(uint32_t addr, uint32_t len);

} // namespace platform::flash
