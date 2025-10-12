#include "stm32h563_regs.h"

#include <stdint.h>

#ifndef PLL_SRC_HSE
#define PLL_SRC_HSE 0
#endif

static void flash_set_waitstates(uint32_t waitstates)
{
    uint32_t reg = FLASH_ACR;

    reg &= ~FLASH_ACR_LATENCY_MASK;
    reg |= (waitstates & FLASH_ACR_LATENCY_MASK);
    FLASH_ACR = reg;

    while ((FLASH_ACR & FLASH_ACR_LATENCY_MASK) != (waitstates & FLASH_ACR_LATENCY_MASK)) {
    }
}

static void clock_pll_on(void)
{
    uint32_t reg32;
    uint32_t pll_source;
    uint32_t pll_rge;
    uint32_t plln;
    uint32_t pllm;
    uint32_t pllq;
    uint32_t pllp;
    uint32_t pllr;
    uint32_t flash_waitstates;

#if PLL_SRC_HSE
    pllm = 4U;
    plln = 250U;
    pllp = 2U;
    pllq = 2U;
    pllr = 2U;
    pll_source = RCC_PLLCFGR_PLLSRC_HSE;
    pll_rge = RCC_PLLCFGR_PLLRGE_4_8;
#else
    pllm = 1U;
    plln = 129U;
    pllp = 2U;
    pllq = 2U;
    pllr = 2U;
    pll_source = RCC_PLLCFGR_PLLSRC_CSI;
    pll_rge = RCC_PLLCFGR_PLLRGE_2_4;
#endif

    flash_waitstates = 5U;

    reg32 = PWR_VOSCR & ~PWR_VOS_MASK;
    PWR_VOSCR = reg32 | PWR_VOS_SCALE_0;
    while ((PWR_VOSSR & PWR_VOSRDY) == 0U) {
    }

    RCC_CR &= ~RCC_CR_PLL1ON;
    while ((RCC_CR & RCC_CR_PLL1RDY) != 0U) {
    }

    flash_set_waitstates(flash_waitstates);

#if PLL_SRC_HSE
    RCC_CR |= RCC_CR_HSEBYP | RCC_CR_HSEON;
    while ((RCC_CR & RCC_CR_HSERDY) == 0U) {
    }
#else
    RCC_CR |= RCC_CR_HSION;
    while ((RCC_CR & RCC_CR_HSIRDY) == 0U) {
    }

    RCC_CR |= RCC_CR_CSION;
    while ((RCC_CR & RCC_CR_CSIRDY) == 0U) {
    }
#endif

    stm32_data_memory_barrier();

    reg32 = RCC_PLL1CFGR;
    reg32 &= ~(RCC_PLLCFGR_PLLM_MASK | RCC_PLLCFGR_PLLSRC_MASK | RCC_PLLCFGR_PLLRGE_MASK);
    reg32 &= ~(RCC_PLLCFGR_PLL1PEN | RCC_PLLCFGR_PLL1QEN | RCC_PLLCFGR_PLL1REN);
    reg32 |= ((pllm & 0x3FU) << RCC_PLLCFGR_PLLM_SHIFT);
    reg32 |= pll_source;
    reg32 |= (pll_rge << RCC_PLLCFGR_PLLRGE_SHIFT);
    reg32 &= ~RCC_PLLCFGR_PLLVCOSEL;
    RCC_PLL1CFGR = reg32;

    stm32_data_memory_barrier();

    RCC_PLL1DIVR = ((plln - 1U) << RCC_PLLDIVR_DIVN_SHIFT)
        | ((pllp - 1U) << RCC_PLLDIVR_DIVP_SHIFT)
        | ((pllq - 1U) << RCC_PLLDIVR_DIVQ_SHIFT)
        | ((pllr - 1U) << RCC_PLLDIVR_DIVR_SHIFT);

    stm32_data_memory_barrier();

    RCC_PLL1CFGR &= ~RCC_PLLCFGR_PLLFRACEN;
    stm32_data_memory_barrier();

    RCC_PLL1FRACR = 0U;
    stm32_data_memory_barrier();

    RCC_PLL1CFGR |= RCC_PLLCFGR_PLLFRACEN;
    stm32_data_memory_barrier();

    RCC_PLL1CFGR |= RCC_PLLCFGR_PLL1PEN;

    RCC_CR |= RCC_CR_PLL1ON;

    uint32_t cfgr2 = RCC_CFGR2;
    cfgr2 &= ~((0x0FU << RCC_CFGR2_HPRE_SHIFT)
        | (0x07U << RCC_CFGR2_PPRE1_SHIFT)
        | (0x07U << RCC_CFGR2_PPRE2_SHIFT)
        | (0x07U << RCC_CFGR2_PPRE3_SHIFT));
    cfgr2 |= (RCC_AHB_PRESCALER_DIV_NONE << RCC_CFGR2_HPRE_SHIFT)
        | (RCC_APB_PRESCALER_DIV_NONE << RCC_CFGR2_PPRE1_SHIFT)
        | (RCC_APB_PRESCALER_DIV_NONE << RCC_CFGR2_PPRE2_SHIFT)
        | (RCC_APB_PRESCALER_DIV_NONE << RCC_CFGR2_PPRE3_SHIFT);
    RCC_CFGR2 = cfgr2;

    stm32_data_memory_barrier();

    while ((RCC_CR & RCC_CR_PLL1RDY) == 0U) {
    }

    uint32_t cfgr1 = RCC_CFGR1 & ~RCC_CFGR1_SW_MASK;
    cfgr1 |= RCC_CFGR1_SW_PLL1;
    RCC_CFGR1 = cfgr1;

    stm32_data_memory_barrier();

    while (((RCC_CFGR1 & RCC_CFGR1_SWS_MASK) >> RCC_CFGR1_SWS_SHIFT) != RCC_CFGR1_SW_PLL1) {
    }
}

void stm32h563_clock_init(void)
{
    clock_pll_on();
}
