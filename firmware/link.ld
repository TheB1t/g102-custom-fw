/* Firmware linker script: starts at 0x08004000, 108 KB available.
   The last 4 KB (two 2 KB pages at 0x0801F000..0x0801FFFF) are reserved
   for persistent settings storage — A/B paged with atomic swap. Settings
   are deliberately inside the DFU-writable range: a firmware reflash
   wipes them, which lets us evolve the settings schema without a
   separate migration path. */
/* First 192 bytes of SRAM are reserved for the relocated vector table
   (48 entries × 4 bytes on M0/M0+). The linker places .data/.bss from
   0x20000100 so g_vectors_sram@0x20000000 doesn't collide with globals. */
MEMORY
{
    rom         (rx)  : ORIGIN = 0x08004000, LENGTH = 108K
    vectors_ram (rw)  : ORIGIN = 0x20000000, LENGTH = 256
    /* Last word (0x20003FFC) is the boot-magic handoff slot — kept
       outside `ram` so neither .bss nor the initial MSP stomps on it. */
    ram         (rwx) : ORIGIN = 0x20000100, LENGTH = 16K - 256 - 8
}

_boot_magic_addr   = 0x20003FFC;
_settings_page_a   = 0x0801F000;
_settings_page_b   = 0x0801F800;
_settings_page_sz  = 0x00000800;

SECTIONS {
    .vectors_sram (NOLOAD) : { KEEP(*(.vectors_sram)) } > vectors_ram
}

INCLUDE cortex-m-generic.ld
