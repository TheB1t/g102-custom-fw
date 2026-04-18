/* Bootloader linker script: first 16 KB of flash. RAM is truncated by 4
   bytes so the top word (0x20003FFC) stays outside .data/.bss AND outside
   the initial MSP — otherwise the first push from either bootloader or
   firmware wipes the magic slot we just wrote. */
MEMORY
{
    rom  (rx)  : ORIGIN = 0x08000000, LENGTH = 16K
    ram  (rwx) : ORIGIN = 0x20000000, LENGTH = 16K - 8
}

_boot_magic_addr = 0x20003FFC;

INCLUDE cortex-m-generic.ld
