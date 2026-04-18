#!/usr/bin/env bash
# Build firmware and upload via USB DFU. Mouse must be in bootloader mode:
# either pristine (no firmware), held DPI at power-on, or firmware reboot-triggered.
set -euo pipefail

BIN="$(dirname "$0")/../.pio/build/firmware/firmware.bin"
pio run -e firmware
dfu-util -d 0483:df11 -a 0 -D "$BIN"
