#ifndef _SYSTICK_H
#define _SYSTICK_H

#include <stdint.h>

/* SysTick register base (part of the System Control Block) */
#define SYSTICK_BASE       (0xE000E010UL)

/* SysTick register map */
#define SYST_CSR           (*(volatile uint32_t *)(SYSTICK_BASE + 0x00)) /* Control and status */
#define SYST_RVR           (*(volatile uint32_t *)(SYSTICK_BASE + 0x04)) /* Reload value */
#define SYST_CVR           (*(volatile uint32_t *)(SYSTICK_BASE + 0x08)) /* Current value */
#define SYST_CALIB         (*(volatile uint32_t *)(SYSTICK_BASE + 0x0C)) /* Calibration */

/* Control register bits */
#define SYST_CSR_ENABLE     (1U << 0)  /* Counter enable */
#define SYST_CSR_TICKINT    (1U << 1)  /* Interrupt enable */
#define SYST_CSR_CLKSOURCE  (1U << 2)  /* Clock source: 0 = external, 1 = processor */

/* SysTick API */

static inline void systick_counter_enable(void)
{
    SYST_CSR |= SYST_CSR_ENABLE;
}

static inline void systick_interrupt_enable(void)
{
    SYST_CSR |= SYST_CSR_TICKINT;
}

static inline uint32_t systick_get_reload(void)
{
    return SYST_RVR;
}

static inline void frosted_systick_config(uint32_t sys_clock) {
    uint32_t ticks = sys_clock / 1000ULL; /* 1ms tick */
    SYST_RVR = ticks - 1;
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}

#endif /* _SYSTICK_H */

