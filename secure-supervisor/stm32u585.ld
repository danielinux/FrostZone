/*
 *
 *  Geometry:
 *  0x0C000000 - 0x0C007FFF=  32KB - Secure supervisor
 *  0x0C008000 - 0x0C00FFFF=  32KB - Non-secure callables
 *  0x0C010000 - 0x0C03FFFF=  128KB - Frosted non-secure kernel
 *  0x0C040000 - 0x0C1FFFFF=  Userspace
 *
 *
 *  Linker script for STM32U585 target used by the secure‑supervisor.
 *  The configuration follows the memory map from the SVD and the
 *  documentation provided in the repository README.
 */

/* Defines the following symbols for use by code:
    __exidx_start
    __exidx_end
    __etext
    __data_start__
    __preinit_array_start
    __preinit_array_end
    __init_array_start
    __init_array_end
    __fini_array_start
    __fini_array_end
    __data_end__
    __bss_start__
    __bss_end__
    __end__
    end
    __HeapLimit
    __StackLimit
    __StackTop
    __stack (== StackTop)
*/

MEMORY
{
    FLASH(rx)      : ORIGIN = 0x0C000000, LENGTH = 0x8000
    FLASH_NSC(rx)  : ORIGIN = 0x0C008000, LENGTH = 0x8000
    RAM(rwx)       : ORIGIN = 0x30000000, LENGTH = 0x10000
    MEMPOOL(rwx)   : ORIGIN = 0x30030000, LENGTH = 0x50000
}


PROVIDE(__StackTop = ORIGIN(RAM) + LENGTH(RAM));
PROVIDE(__stack = __StackTop);

ENTRY(Reset_Handler)

SECTIONS
{
    /* Vector table and code */
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
        *(.gnu.sgstubs*)   /* Secure Gateway stubs */
        *(.ns_callable*)
        . = ALIGN(4);
    } >FLASH_NSC

    .data : {
        PROVIDE(__data_start__ = .);
        *(.data*)
        PROVIDE(__data_end__ = .);
    } > RAM AT > FLASH

    .bss (NOLOAD): {
        PROVIDE(__bss_start__ = .);
        *(.bss*)
        *(COMMON)
        PROVIDE(__bss_end__ = .);
    } > RAM

    /* Non‑Secure callable stubs */
    .ns_callable : {
        *(.ns_callable*)
    } > FLASH_NSC
    
    /* Mempool section on non-secure RAM */
    .mempool (NOLOAD) : {
        __mempool_start__ = .;
        *(.mempool.*)
        . = ALIGN(4);
        __mempool_end__ = .;
    } > MEMPOOL
}
