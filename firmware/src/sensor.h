#pragma once

#include <stdint.h>

/* "1855" optical sensor driver — SPI mode 3, soft CS.
   Vendor not publicly identified; protocol is PixArt-family. */

void sensor_init(void);

/* Read motion burst; returns 1 if motion present (sets *dx, *dy), 0 otherwise.
   dx/dy are signed 8-bit counts since the previous read. */
int sensor_read_motion(int8_t *dx, int8_t *dy);

/* Mirror register 0x20 / 0x24 (X / Y resolution). Non-linear scale —
   see project memory on empirical mapping. */
void sensor_set_dpi(uint8_t code);
