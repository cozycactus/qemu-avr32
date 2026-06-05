ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x80000000, LENGTH = 1024K
    SRAM  (rwx) : ORIGIN = 0x00000000, LENGTH = 128K
}

SECTIONS
{
    .text : ALIGN(4)
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > FLASH

    . = ALIGN(4);
    __text_end = .;

    .data : AT(__text_end)
    {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    } > SRAM
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } > SRAM

    .results 0x100 (NOLOAD) : ALIGN(4)
    {
        KEEP(*(.results))
    } > SRAM

    __stack_top = ORIGIN(SRAM) + LENGTH(SRAM);

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
