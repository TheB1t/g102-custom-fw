#pragma once
#include <stdint.h>

int flash_is_writable(uint32_t addr, uint32_t len);
int flash_program(uint32_t addr, const uint8_t *data, uint32_t len);
