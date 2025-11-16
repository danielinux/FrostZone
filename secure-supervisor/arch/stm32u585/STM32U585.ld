/*
 *  Linker script for STM32U585 secure supervisor.
 *  Uses the 2 MiB flash and 1 MiB SRAM mapping described in the
 *  repository documentation.
 */

MEMORY
{
    FLASH        : ORIGIN = 0x08000000, LENGTH = 2M   /* Non‑Secure flash */
    FLASH_NSC    : ORIGIN = 0x0C000000, LENGTH = 2M   /* Secure alias */
    SRAM_NS      : ORIGIN = 0x20000000, LENGTH = 1M   /* Non‑Secure SRAM */
    SRAM_NS_ALIAS: ORIGIN = 0x30000000, LENGTH = 1M   /* Secure alias */
    PERIPH_NS    : ORIGIN = 0x40000000, LENGTH = 1M   /* Peripheral */
    PERIPH_ALIAS : ORIGIN = 0x50000000, LENGTH = 1M   /* Secure alias */
}

ENTRY(_entry_point)

SECTIONS
{
    /* Vector table in non‑secure flash */
    .vectors : {
        KEEP(*(.vectors))
    } > FLASH

    /* Code */
    .text : {
        *(.init)
        *(.text*)
        *(.fini)
    } > FLASH

    /* .ns_callable section maps to flash alias */
    .ns_callable : {
        *(.ns_callable*)
    } > FLASH_NSC

    /* Data */
    .data : AT (ADDR(.text)+SIZEOF(.text)) {
        *(.data*)
    } > SRAM_NS AT > FLASH

    /* BSS */
    .bss : {
        *(.bss*)
        *(COMMON)
    } > SRAM_NS
}

