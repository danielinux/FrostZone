#include "stm32h563_regs.h"

#include <stdint.h>

#ifndef PLL_SRC_HSE
#define PLL_SRC_HSE 1
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


void stm32h563_clock_init(void)
{
    uint32_t reg32;
    uint32_t plln, pllm, pllq, pllp, pllr, hpre, apb1pre, apb2pre, apb3pre, flash_waitstates;


#if PLL_SRC_HSE
    pllm = 4;
    plln = 250;
    pllp = 2;
    pllq = 2;
    pllr = 2;
#else
    pllm = 1;
    plln = 129;
    pllp = 2;
    pllq = 2;
    pllr = 2;
#endif
    flash_waitstates = 5;

    /* Set voltage scaler */
    reg32 = PWR_VOSCR & (~PWR_VOS_MASK);
    PWR_VOSCR = reg32 | PWR_VOS_SCALE_0;

    /* Wait until scale has changed */
    while ((PWR_VOSSR & PWR_VOSRDY) == 0)
        ;

    /* Disable PLL1 */
    RCC_CR &= ~RCC_CR_PLL1ON;

    /* Wait until PLL1 is disabled */
    while ((RCC_CR & RCC_CR_PLL1RDY) != 0)
        ;

    /* Set flash wait states */
    flash_set_waitstates(flash_waitstates);

#if PLL_SRC_HSE
    /* PLL Oscillator configuration */
    RCC_CR |= RCC_CR_HSEON | RCC_CR_HSEBYP;

    /* Wait until HSE is Ready */
    while ((RCC_CR & RCC_CR_HSERDY) == 0)
        ;

    /* Configure PLL1 div/mul factors */
    reg32 = RCC_PLL1CFGR;
    reg32 &= ~((0x3F << RCC_PLLCFGR_PLLM_SHIFT) | (0x03));
    reg32 |= (pllm << RCC_PLLCFGR_PLLM_SHIFT) | RCC_PLLCFGR_PLLSRC_HSE;
    RCC_PLL1CFGR = reg32;
#else
    RCC_CR |= RCC_CR_HSION;

    /* Wait until HSI is Ready */
    while ((RCC_CR & RCC_CR_HSIRDY) == 0)
          ;

    RCC_CR |= RCC_CR_CSION;

    /* Wait until CSI is Ready */
    while ((RCC_CR & RCC_CR_HSIRDY) == 0)
          ;

    /* Configure PLL1 div/mul factors */
    reg32 = RCC_PLL1CFGR;
    reg32 &= ~((0x3F << RCC_PLLCFGR_PLLM_SHIFT) | (0x03));
    reg32 |= (pllm << RCC_PLLCFGR_PLLM_SHIFT) | RCC_PLLCFGR_PLLSRC_CSI;
    RCC_PLL1CFGR = reg32;

#endif
    stm32_data_memory_barrier();

    RCC_PLL1DIVR = ((plln - 1) << RCC_PLLDIVR_DIVN_SHIFT) | ((pllp - 1) << RCC_PLLDIVR_DIVP_SHIFT) |
        ((pllq - 1) << RCC_PLLDIVR_DIVQ_SHIFT) | ((pllr - 1) << RCC_PLLDIVR_DIVR_SHIFT);
    stm32_data_memory_barrier();


    /* Disable Fractional PLL */
    RCC_PLL1CFGR &= ~RCC_PLLCFGR_PLLFRACEN;
    stm32_data_memory_barrier();


    /* Configure Fractional PLL factor */
    RCC_PLL1FRACR = 0x00000000;
    stm32_data_memory_barrier();

    /* Enable Fractional PLL */
    RCC_PLL1CFGR |= RCC_PLLCFGR_PLLFRACEN;
    stm32_data_memory_barrier();

    /* Select PLL1 Input frequency range: VCI */
    RCC_PLL1CFGR |= RCC_PLLCFGR_PLLRGE_2_4 << RCC_PLLCFGR_PLLRGE_SHIFT;

    /* Select PLL1 Output frequency range: VCO = 0 */
    RCC_PLL1CFGR &= ~RCC_PLLCFGR_PLLVCOSEL;
    stm32_data_memory_barrier();

    /* Enable PLL1 system clock out (DIV: P) */
    RCC_PLL1CFGR |= RCC_PLLCFGR_PLL1PEN;

    /* Enable PLL1 */
    RCC_CR |= RCC_CR_PLL1ON;

    /* Set up APB3, 2, 1 and AHB prescalers */
    hpre = RCC_AHB_PRESCALER_DIV_NONE;
    apb1pre = RCC_APB_PRESCALER_DIV_NONE;
    apb2pre = RCC_APB_PRESCALER_DIV_NONE;
    apb3pre = RCC_APB_PRESCALER_DIV_NONE;
    reg32 = RCC_CFGR2;
    reg32 &= ~( (0x0F << RCC_CFGR2_HPRE_SHIFT) |
            (0x07 << RCC_CFGR2_PPRE1_SHIFT) |
            (0x07 << RCC_CFGR2_PPRE2_SHIFT) |
            (0x07 << RCC_CFGR2_PPRE3_SHIFT));
    reg32 |= ((hpre) << RCC_CFGR2_HPRE_SHIFT) | ((apb1pre) << RCC_CFGR2_PPRE1_SHIFT) |
        ((apb2pre) << RCC_CFGR2_PPRE2_SHIFT) | ((apb3pre) << RCC_CFGR2_PPRE3_SHIFT);
    RCC_CFGR2 = reg32;
    stm32_data_memory_barrier();

    /* Wait until PLL1 is Ready */
    while ((RCC_CR & RCC_CR_PLL1RDY) == 0)
        ;

    /* Set PLL as clock source */
    reg32 = RCC_CFGR1 & (~RCC_CFGR1_SW_MASK);
    RCC_CFGR1 = reg32 | RCC_CFGR1_SW_PLL1;
    stm32_data_memory_barrier();

    /* Wait until selection of PLL as source is complete */
    while ((RCC_CFGR1 & (RCC_CFGR1_SW_PLL1 << RCC_CFGR1_SWS_SHIFT)) == 0)
        ;

    /* Set PLL1 as system clock */
    RCC_PLL1CFGR |= RCC_PLLCFGR_PLL1PEN;

}

