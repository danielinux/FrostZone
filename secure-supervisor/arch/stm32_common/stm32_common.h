#ifndef STM32_COMMON_H
#define STM32_COMMON_H

#include <stdint.h>

#define BIT(n) (1U << (n))

#define NVIC_ITNS_BASE (0xE000EF00U)

static inline void stm32_mark_irq_non_secure(uint32_t irqn)
{
    volatile uint32_t *itns = (volatile uint32_t *)(NVIC_ITNS_BASE + ((irqn >> 5U) << 2U));
    uint32_t bit = irqn & 0x1FU;
    *itns |= BIT(bit);
}

static inline void stm32_mark_irq_secure(uint32_t irqn)
{
    volatile uint32_t *itns = (volatile uint32_t *)(NVIC_ITNS_BASE + ((irqn >> 5U) << 2U));
    uint32_t bit = irqn & 0x1FU;
    *itns &= ~BIT(bit);
}

static inline void stm32_data_memory_barrier(void)
{
    __asm__ volatile ("dmb" ::: "memory");
}

#endif /* STM32_COMMON_H */
