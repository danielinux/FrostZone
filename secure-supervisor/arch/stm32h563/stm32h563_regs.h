#ifndef STM32H563_REGS_H
#define STM32H563_REGS_H

#include <stdint.h>

#include "../stm32_common/stm32_common.h"

#define STM32H563_RCC_BASE 0x54020C00U
#define RCC_CR             (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x000U))
#define RCC_CFGR1          (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x01CU))
#define RCC_CFGR2          (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x020U))
#define RCC_PLL1CFGR       (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x028U))
#define RCC_PLL1DIVR       (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x034U))
#define RCC_PLL1FRACR      (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x038U))
#define RCC_AHB1RSTR       (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x060U))
#define RCC_AHB1ENR        (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x088U))
#define RCC_AHB2ENR        (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x08CU))
#define RCC_APB3ENR        (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x0A8U))

#define RCC_CR_HSION   BIT(0)
#define RCC_CR_HSIRDY  BIT(1)
#define RCC_CR_CSION   BIT(8)
#define RCC_CR_CSIRDY  BIT(9)
#define RCC_CR_HSEON   BIT(16)
#define RCC_CR_HSERDY  BIT(17)
#define RCC_CR_HSEBYP  BIT(18)
#define RCC_CR_PLL1ON  BIT(24)
#define RCC_CR_PLL1RDY BIT(25)

#define RCC_PLLCFGR_PLLSRC_MASK 0x3U
#define RCC_PLLCFGR_PLLSRC_HSI  0x1U
#define RCC_PLLCFGR_PLLSRC_CSI  0x2U
#define RCC_PLLCFGR_PLLSRC_HSE  0x3U
#define RCC_PLLCFGR_PLLRGE_SHIFT 2U
#define RCC_PLLCFGR_PLLRGE_MASK  (0x3U << RCC_PLLCFGR_PLLRGE_SHIFT)
#define RCC_PLLCFGR_PLLRGE_1_2   0x0U
#define RCC_PLLCFGR_PLLRGE_2_4   0x1U
#define RCC_PLLCFGR_PLLRGE_4_8   0x2U
#define RCC_PLLCFGR_PLLRGE_8_16  0x3U
#define RCC_PLLCFGR_PLLFRACEN    BIT(4)
#define RCC_PLLCFGR_PLLVCOSEL    BIT(5)
#define RCC_PLLCFGR_PLLM_SHIFT   8U
#define RCC_PLLCFGR_PLLM_MASK    (0x3FU << RCC_PLLCFGR_PLLM_SHIFT)
#define RCC_PLLCFGR_PLL1PEN      BIT(16)
#define RCC_PLLCFGR_PLL1QEN      BIT(17)
#define RCC_PLLCFGR_PLL1REN      BIT(18)

#define RCC_PLLDIVR_DIVN_SHIFT 0U
#define RCC_PLLDIVR_DIVP_SHIFT 9U
#define RCC_PLLDIVR_DIVQ_SHIFT 16U
#define RCC_PLLDIVR_DIVR_SHIFT 24U

#define RCC_SECCFGR        (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x110U))
#define RCC_PRIVCFGR       (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x114U))
#define RCC_APB1LRSTR      (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x074U))
#define RCC_APB1LENR       (*(volatile uint32_t *)(STM32H563_RCC_BASE + 0x09CU))

#define RCC_CFGR1_SW_MASK 0x7U
#define RCC_CFGR1_SW_HSI  0x0U
#define RCC_CFGR1_SW_PLL1 0x3U
#define RCC_CFGR1_SWS_SHIFT 3U
#define RCC_CFGR1_SWS_MASK (0x7U << RCC_CFGR1_SWS_SHIFT)

#define RCC_CFGR2_HPRE_SHIFT  0U
#define RCC_CFGR2_PPRE1_SHIFT 4U
#define RCC_CFGR2_PPRE2_SHIFT 8U
#define RCC_CFGR2_PPRE3_SHIFT 12U

#define RCC_AHB_PRESCALER_DIV_NONE 0x0U
#define RCC_APB_PRESCALER_DIV_NONE 0x0U

#define RCC_AHB1ENR_ETHEN   BIT(19)
#define RCC_AHB1ENR_ETHTXEN BIT(20)
#define RCC_AHB1ENR_ETHRXEN BIT(21)

#define RCC_AHB1RSTR_ETHRST BIT(19)

#define RCC_AHB2ENR_GPIOAEN BIT(0)
#define RCC_AHB2ENR_GPIOBEN BIT(1)
#define RCC_AHB2ENR_GPIOCEN BIT(2)
#define RCC_AHB2ENR_GPIODEN BIT(3)
#define RCC_AHB2ENR_GPIOEEN BIT(4)
#define RCC_AHB2ENR_GPIOFEN BIT(5)
#define RCC_AHB2ENR_GPIOGEN BIT(6)

#define RCC_APB1LENR_USART2EN BIT(17)
#define RCC_APB1LENR_USART3EN BIT(18)

#define RCC_APB3ENR_SBSEN BIT(1)

#define PWR_BASE        0x54020800U
#define PWR_VOSCR       (*(volatile uint32_t *)(PWR_BASE + 0x010U))
#define PWR_VOSSR       (*(volatile uint32_t *)(PWR_BASE + 0x014U))
#define PWR_VOS_MASK    (0x3U << 4)
#define PWR_VOS_SCALE_0 (0x3U << 4)
#define PWR_VOSRDY      BIT(3)

#define FLASH_BASE              0x50022000U
#define FLASH_ACR               (*(volatile uint32_t *)(FLASH_BASE + 0x000U))
#define FLASH_ACR_LATENCY_MASK  0xFU

#define SBS_BASE          0x54000400U
#define SBS_PMCR          (*(volatile uint32_t *)(SBS_BASE + 0x100U))
#define SBS_PMCR_ETH_SEL_PHY_MASK (0x7U << 21)
#define SBS_PMCR_ETH_SEL_PHY_RMII (0x4U << 21)

#define ETH_BASE 0x50028000U
#define ETH_MACCR      (*(volatile uint32_t *)(ETH_BASE + 0x0000U))
#define ETH_MACPFR     (*(volatile uint32_t *)(ETH_BASE + 0x0004U))
#define ETH_MACMDIOAR  (*(volatile uint32_t *)(ETH_BASE + 0x0200U))
#define ETH_MACMDIODR  (*(volatile uint32_t *)(ETH_BASE + 0x0204U))
#define ETH_DMACRDR    (*(volatile uint32_t *)(ETH_BASE + 0x338CU))

#define ETH_MACCR_RE BIT(0)
#define ETH_MACCR_TE BIT(1)
#define ETH_MACCR_DM BIT(13)
#define ETH_MACCR_FES BIT(14)

#define ETH_MACMDIOAR_MB      BIT(0)
#define ETH_MACMDIOAR_C45E    BIT(1)
#define ETH_MACMDIOAR_GOC_SHIFT 2U
#define ETH_MACMDIOAR_GOC_WRITE 0x1U
#define ETH_MACMDIOAR_GOC_READ  0x3U
#define ETH_MACMDIOAR_CR_SHIFT 8U
#define ETH_MACMDIOAR_NTC_SHIFT 12U
#define ETH_MACMDIOAR_RDA_SHIFT 16U
#define ETH_MACMDIOAR_PA_SHIFT  21U

#define GPIOA_BASE 0x52020000U
#define GPIOB_BASE 0x52020400U
#define GPIOC_BASE 0x52020800U
#define GPIOG_BASE 0x52021800U

#define GPIO_MODER_OFFSET   0x00U
#define GPIO_OTYPER_OFFSET  0x04U
#define GPIO_OSPEEDR_OFFSET 0x08U
#define GPIO_PUPDR_OFFSET   0x0CU
#define GPIO_AFRL_OFFSET    0x20U
#define GPIO_AFRH_OFFSET    0x24U

#endif /* STM32H563_REGS_H */
