/* Firmware linker script: starts at 0x08004000, 112 KB available */
/* First 192 bytes of SRAM are reserved for the relocated vector table
   (48 entries × 4 bytes on M0/M0+). The linker places .data/.bss from
   0x20000100 so g_vectors_sram@0x20000000 doesn't collide with globals. */
MEMORY
{
    rom         (rx)  : ORIGIN = 0x08004000, LENGTH = 112K
    vectors_ram (rw)  : ORIGIN = 0x20000000, LENGTH = 256
    /* Last word (0x20003FFC) is the boot-magic handoff slot — kept
       outside `ram` so neither .bss nor the initial MSP stomps on it. */
    ram         (rwx) : ORIGIN = 0x20000100, LENGTH = 16K - 256 - 8
}

_boot_magic_addr = 0x20003FFC;

SECTIONS {
    .vectors_sram (NOLOAD) : { KEEP(*(.vectors_sram)) } > vectors_ram
}

INCLUDE cortex-m-generic.ld
