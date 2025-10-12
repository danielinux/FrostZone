/*
 *
 *  Geometry:
 *  0x0C000000 - 0x0C007FFF=  32KB - Secure supervisor
 *  0x0C008000 - 0x0C00FFFF=  32KB - Non-secure callables
 *  0x0C010000 - 0x0C03FFFF=  128KB - Frosted non-secure kernel
 *  0x0C040000 - 0x0C1FFFFF=  Userspace
 *
 *
 *  Linker script for STM32H563 target used by the secure supervisor.
 *  The layout mirrors the STM32 TrustZone memory map described in the
 *  project documentation.
 */

MEMORY
{
    FLASH(rx)      : ORIGIN = 0x0C000000, LENGTH = 0x8000
    FLASH_NSC(rx)  : ORIGIN = 0x0C008000, LENGTH = 0x8000
    RAM(rwx)       : ORIGIN = 0x30000000, LENGTH = 0x10000
    MEMPOOL(rwx)   : ORIGIN = 0x20050000, LENGTH = 0x50000
}


PROVIDE(__StackTop = ORIGIN(RAM) + LENGTH(RAM));
PROVIDE(__stack = __StackTop);

ENTRY(Reset_Handler)

SECTIONS
{
    .vectors : {
        KEEP(*(.custom_ivt))
        KEEP(*(.vectors))
    } > FLASH

    .text : {
        *(.init)
        *(.text*)
        *(.rodata*)
        *(.fini)
        PROVIDE(__etext = .);
    } > FLASH

    .edidx :
    {
        . = ALIGN(4);
        PROVIDE(__exidx_start = .);
        *(.ARM.exidx*)
        PROVIDE(__exidx_end = .);
    } > FLASH

    .gnu.sgstubs :
    {
        *(.gnu.sgstubs*)
        *(.ns_callable*)
        . = ALIGN(4);
    } > FLASH_NSC

    .data : {
        PROVIDE(__data_start__ = .);
        *(.data*)
        PROVIDE(__data_end__ = .);
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        PROVIDE(__bss_start__ = .);
        *(.bss*)
        *(COMMON)
        PROVIDE(__bss_end__ = .);
    } > RAM

    .ns_callable : {
        *(.ns_callable*)
    } > FLASH_NSC

    .mempool (NOLOAD) : {
        __mempool_start__ = .;
        *(.mempool.*)
        . = ALIGN(4);
        __mempool_end__ = .;
    } > MEMPOOL
}
