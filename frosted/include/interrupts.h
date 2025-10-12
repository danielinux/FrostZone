
#ifndef FROSTED_INTERRUPTS_H
#define FROSTED_INTERRUPTS_H

extern void __set_BASEPRI(int);
#include <stdint.h>

#ifdef DEBUG
    static void irq_off(void)
    {
    }
    
    static void irq_on(void)
    {
    }

    static void irq_setmask(void)
    {
    }
    
    static void irq_clearmask(void)
    {
    }

    static inline uint32_t irq_save(void)
    {
        return 0;
    }

    static inline void irq_restore(uint32_t primask)
    {
        (void)primask;
    }
#else
    /* Inline kernel utils */

    static inline void irq_setmask(void)
    {
        __set_BASEPRI(3);
    }
    
    static inline void irq_clearmask(void)
    {
        __set_BASEPRI(0u);
    }

    static inline void irq_off(void)
    {
        asm volatile ("cpsid i                \n");
    }
    
    static inline void irq_on(void)
    {
        asm volatile ("cpsie i                \n");
    }

    static inline uint32_t irq_save(void)
    {
        uint32_t primask;
        asm volatile ("mrs %0, primask        \n"
                      "cpsid i                \n"
                      : "=r"(primask)
                      :
                      : "memory");
        return primask;
    }

    static inline void irq_restore(uint32_t primask)
    {
        if (primask & 0x1u) {
            asm volatile ("cpsid i            \n" ::: "memory");
        } else {
            asm volatile ("cpsie i            \n" ::: "memory");
        }
    }
#endif

#endif /* FROSTED_INTERRUPTS_H */
