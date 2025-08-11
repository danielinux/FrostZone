#ifndef INCLUDED_PICO_H
#define INCLUDED_PICO_H
#include <stdint.h>
#include "nvic.h"
#include "hardware/regs/addressmap.h"


#define __force_inline inline
#define __unused __attribute__((__unused__))
//#define static_assert(x, ...) do { } while(!(x))
#define static_assert(expr, msg)            typedef int static_assert_##__LINE__[(expr) ? 1 : -1]
#define hard_assert(x, ...) do { } while(!(x))
#define assert(x, ...) do { } while(!(x))
#define panic(...) do { } while(1)

#ifndef remove_volatile_cast
#define remove_volatile_cast(t, x) (t)(x)
#endif

static inline uint8_t rp2040_chip_version(void) {
    return 2;
}

#define USBCTRL_IRQ (14) /* on RP2350 */


/* ---- RESETS ---- */
#ifndef RESETS_BASE
#define RESETS_BASE             (0x40020000u)
#endif
#define RESETS_RESET            (*(volatile uint32_t *)(RESETS_BASE + 0x00))
#define RESETS_WDSEL            (*(volatile uint32_t *)(RESETS_BASE + 0x04))
#define RESETS_RESET_DONE       (*(volatile uint32_t *)(RESETS_BASE + 0x08))

/* Bits in RESETS_* registers */
#define RESETS_RESET_USBCTRL_BITS   (1u << 28)

/* ---- USB controller ---- */
#ifndef USBCTRL_DPRAM_BASE
#define USBCTRL_DPRAM_BASE      (0x50100000u)
#endif

#ifndef USBCTRL_REGS_BASE
#define USBCTRL_REGS_BASE       (0x50110000u)
#endif

#define irq_set_enabled(x,y) (y)?nvic_enable_irq(x):nvic_disable_irq(x)

/* Optional helpers: busy-wait loops */
static inline void resets_assert(uint32_t bits)
{
    RESETS_RESET |= bits;
}

static inline void resets_deassert_wait(uint32_t bits)
{
    RESETS_RESET &= ~bits;
    while ((RESETS_RESET_DONE & bits) != bits) { }
}

static inline void irq_add_shared_handler(unsigned irq, void (*handler)(void),
        int order)
{
    void **vt = nvic_vector_base();
    (void)order; /* not used */
    /* External IRQ i lives at vector index 16 + i */
    vt[16u + irq] = (void *)handler;
}

static inline void irq_remove_handler(unsigned irq, void (*handler)(void))
{
    (void)handler; /* ignore, we just restore default */
    void **vt = nvic_vector_base();
    vt[16u + irq] = (void *)default_irq_handler;
}

#define reset_block(bits) do{resets_assert(bits); } while(0)
#define unreset_block_wait(bits) do{resets_deassert_wait(bits); } while(0)

static inline void busy_wait_at_least_cycles(uint32_t cycles)
{
    volatile int i;
    for (i = 0; i < cycles; i++) {
        asm volatile ("" : : : "memory");
    }
}

#endif // INCLUDED_PICO_H
