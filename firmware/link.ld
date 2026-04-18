/* Firmware linker script: starts at 0x08004000, 112 KB available */
MEMORY
{
    rom  (rx)  : ORIGIN = 0x08004000, LENGTH = 112K
    ram  (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
}

_boot_magic_addr = 0x20003FFC;

INCLUDE cortex-m-generic.ld
