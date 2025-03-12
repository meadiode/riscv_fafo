MEMORY
{
    FLASH (rx)  : ORIGIN = 0x8000000, LENGTH = 1K
    RAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 256
}


ENTRY(_start)

STACK_SIZE = 64;

SECTIONS
{
    . = 0x80000000;

    .text : {
        *(.text)
    } > FLASH

    .data : {
        *(.data)
    } >RAM AT> FLASH

    .sdata : {
        *(.sdata)
    } >RAM

    .bss : {
        *(.bss)
    } > RAM

    .stack (NOLOAD) : {
        . = ALIGN(16);
        __stack_start = .;
        . = . + 64;
        __stack_end = .;
    } > RAM

    /* Discard unnecessary sections */
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}
