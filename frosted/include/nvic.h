#ifndef _NVIC_H
#define _NVIC_H

#include <stdint.h>

#define NVIC_ISER_BASE  ((volatile uint32_t *)0xE000E100)
#define NVIC_ICER_BASE  ((volatile uint32_t *)0xE000E180)
#define NVIC_ISPR_BASE  ((volatile uint32_t *)0xE000E200)
#define NVIC_ICPR_BASE  ((volatile uint32_t *)0xE000E280)
#define NVIC_IPR_BASE   ((volatile uint8_t  *)0xE000E400)
#define NVIC_STIR       (*(volatile uint32_t *)0xE000EF00)

#define NVIC_NUM_IRQS   240  /* Cortex-M max, adjust if needed */

/* Vector base register */
#define SCB_VTOR       (*(volatile uint32_t *)0xE000ED08)


/* System interrupt priority */
#define SCB_SHPR       ((volatile uint8_t *)(0xE000ED18))

/* System interrupt control register */ 
#define SCB_SHCSR      (*(volatile uint32_t *)0xE000ED24)

/* System exception numbers (ARM Cortex-M) */
#define NVIC_NMI_IRQ              (-14)
#define NVIC_HARDFAULT_IRQ        (-13)
#define NVIC_MEMMANAGE_IRQ        (-12)
#define NVIC_BUSFAULT_IRQ         (-11)
#define NVIC_USAGEFAULT_IRQ       (-10)
#define NVIC_SV_CALL_IRQ          (-5)
#define NVIC_DEBUGMON_IRQ         (-4)
#define NVIC_PENDSV_IRQ           (-2)
#define NVIC_SYSTICK_IRQ          (-1)


static inline void nvic_enable_irq(uint32_t irq)
{
    if (irq < NVIC_NUM_IRQS)
        NVIC_ISER_BASE[irq >> 5] = (1U << (irq & 0x1F));
}

static inline void nvic_disable_irq(uint32_t irq)
{
    if (irq < NVIC_NUM_IRQS)
        NVIC_ICER_BASE[irq >> 5] = (1U << (irq & 0x1F));
}

static inline void nvic_set_pending(uint32_t irq)
{
    if (irq < NVIC_NUM_IRQS)
        NVIC_ISPR_BASE[irq >> 5] = (1U << (irq & 0x1F));
}

static inline void nvic_clear_pending(uint32_t irq)
{
    if (irq < NVIC_NUM_IRQS)
        NVIC_ICPR_BASE[irq >> 5] = (1U << (irq & 0x1F));
}

static inline void nvic_set_priority(int irq, uint8_t priority)
{
    if (irq > 0)
        NVIC_IPR_BASE[irq] = priority;
    else
        SCB_SHPR[(uint32_t)(irq & 0x0FUL) - 4] = priority;
}

static inline void nvic_trigger_irq(uint32_t irq)
{
    if (irq < NVIC_NUM_IRQS)
        NVIC_STIR = irq & 0x1FF;
} 

static inline void **nvic_vector_base(void)
{
    return (void **)(SCB_VTOR);
}

static inline void nvic_enable_memfault(void)
{
    SCB_SHCSR |= (1 << 16);
    SCB_SHPR[0] |= 0x80;
}

static inline void nvic_enable_busfault(void)
{
    SCB_SHCSR |= (1 << 17);
    SCB_SHPR[1] |= 0x80;
}

#define nvic_enable_secure_violation() nvic_enable_irq(4)


void empty_handler(void);

#define default_irq_handler empty_handler

#endif /* _NVIC_H */
