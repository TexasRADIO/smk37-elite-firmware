/* Offline link envelope.
 * XIP base and SRAM boundaries follow the retained SMK FMEL layout.
 * The app budget is capped by the smaller OPEN V01 package expression.
 * The reserved stack regions are per-owner accounting only. This file
 * does not implement board startup or the OPEN runtime's own memory map. */
ENTRY(elite_pi32_configure)

MEMORY
{
    XIP   (rx) : ORIGIN = 0x02000120, LENGTH = 453921
    RAM   (rw) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL  (rw) : ORIGIN = 0x01C20000, LENGTH = 336K
}

SECTIONS
{
    .text : {
        KEEP(*(.text.probe_api))
        *(.text .text.*)
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
    } > XIP

    .data : {
        *(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
    } > RAM AT > XIP

    .bss (NOLOAD) : {
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        __ram_used_end = .;
    } > RAM

    .pool (NOLOAD) : {
        *(.pool .pool.*)
        . = ALIGN(4);
        __pool_used_end = .;
    } > POOL

    __app_limit = ORIGIN(XIP) + LENGTH(XIP);
    __app_used_end = LOADADDR(.data) + SIZEOF(.data);
    __ram_limit = ORIGIN(RAM) + LENGTH(RAM);
    __pool_limit = ORIGIN(POOL) + LENGTH(POOL);

    /* The 256-byte intervals immediately before each bottom are guards. */
    __control_stack_bottom = 0x01C74100;
    __control_stack_top = 0x01C7A000;
    __audio_stack_bottom = 0x01C7A100;
    __audio_stack_top = 0x01C7C000;
    ASSERT(__pool_limit == 0x01C74000, "Pool must stop at the stack guard")

    /DISCARD/ : { *(.comment .note .note.*) }
}
