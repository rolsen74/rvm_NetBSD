
ENTRY(_start)

SECTIONS
{
    . = 0;

    .text : {
        *(.text)
        *(.text.*)
    }

    . = ALIGN(16);

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    . = ALIGN(16);

    .data : {
        *(.data)
        *(.data.*)
    }

    . = ALIGN(16);

    .bss (NOLOAD) : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
}
