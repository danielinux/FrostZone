ENTRY(reset_handler)

MEMORY
{
    FLASH (rx)     : ORIGIN = 0x10010000, LENGTH = 0x20000
    RAM(rw)        : ORIGIN = 0x20010000, LENGTH = 32K
    STACK_MEM(rw)  : ORIGIN = 0x20018000, LENGTH = 32K
    MEMPOOL (rwx)  : ORIGIN = 0x20020000, LENGTH = 0x60000
}

SECTIONS
{
    .vectors : {
        KEEP(*(.vectors))
    } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
    } > FLASH

    .data : AT (ADDR(.text) + SIZEOF(.text)) {
        PROVIDE(_ns_sdata = .);
        *(.data*)
        *(.time_critical.tinyusb*)
        . = ALIGN(4);
        __core1_ns_ivt = .;
        KEEP(*(.core1_vectors*));
        . = ALIGN(512);
        PROVIDE(_ns_edata = .);
    } > RAM

    .bss : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM

    _estack = ORIGIN(STACK_MEM) + LENGTH(STACK_MEM) - 0x800;

    __core1_ns_stack_top = ORIGIN(STACK_MEM) + LENGTH(STACK_MEM);

    PROVIDE(_ns_sidata = LOADADDR(.data));

}
