#!/usr/bin/env bash
# One-time: flash the bootloader via ST-Link. After this, firmware iterations
# use `dfu-util` over USB — no more BOOT0 / NRST manipulation.
set -euo pipefail

ELF="$(dirname "$0")/../.pio/build/bootloader/firmware.elf"
[ -f "$ELF" ] || { echo "Build bootloader first: pio run -e bootloader"; exit 1; }

openocd -f interface/stlink.cfg -c "transport select hla_swd" \
        -f target/stm32f0x.cfg \
        -c "program $ELF verify reset exit"
