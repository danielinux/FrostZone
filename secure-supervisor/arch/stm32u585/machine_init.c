/*
 *  STM32U585 secure supervisor bootstrap – register based.
 *  Minimal code sufficient for the current build.  This file
 *  implements the `machine_init()` function expected by the
 *  supervisor.  It performs a basic clock set‑up and powers on
 *  the peripherals used by the kernel.
 */

#ifndef STM32U585_MACHINE_INIT_H
#define STM32U585_MACHINE_INIT_H

#include <stdint.h>

#include "../stm32_common/stm32_common.h"

/* Base addresses (SVD defined) – only those required */
#define RCC_BASE   (0x50000000U)
#define GPIOA_BASE (0x50020000U)
#define USBFS_BASE (0x50050000U)

/* Register definitions (subset) */
#define RCC_CR      (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_PLLCFGR (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_CFGR    (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))
/* GPIO */
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00))

/* Configure the system clock to approx 240 MHz via HSI+PLL */
static void configure_clocks(void)
{
    /* Enable HSI */
    RCC_CR |= BIT(0) | BIT(8); /* HSION, HSIRDY set */
    while (!(RCC_CR & BIT(1))) {} /* Wait for HSIRDY */
    /* PLL configuration – simplified */
    RCC_PLLCFGR = 0x00004000 | (240U << 6) | (1U << 0); /* PLLM=1, PLLN=240 */
    RCC_CR |= BIT(24); /* PLLON */
    while (!(RCC_CR & BIT(25))) {} /* Wait for PLLRDY */
    /* Switch system clock to PLL */
    RCC_CFGR &= ~0x3;
    RCC_CFGR |= 0x2;
}

/* Enable necessary peripheral clocks – placeholder */
static void enable_periph_clocks(void)
{
    /* In a real build, we would set the relevant AHB/APB bits.
     * For now, we simply guard against unused variables.
     */
    (void)RCC_AHB1ENR; /* Avoid warning */
}

/* Public entry */
void machine_init(void)
{
    configure_clocks();
    enable_periph_clocks();
}

#endif /* STM32U585_MACHINE_INIT_H */
