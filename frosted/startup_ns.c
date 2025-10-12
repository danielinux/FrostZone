/*
 *      This file is part of frostzone.
 *
 *      frostzone is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the Free Software Foundation.
 *
 *
 *      frostzone is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frostzone.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Authors: Daniele Lacamera
 *
 */
#include <stdint.h>
#include "frosted.h"
/* Disable TinyUSB for STM32U585 – terminal will use standard USART. */

extern void frosted_main(void);
extern unsigned long _ns_sidata[], _ns_sdata[], _ns_edata[], _sbss[], _ebss[], _estack[];

void empty_handler(void)
{
}


void reset_handler(void) {
    uint32_t *src, *dst;
    asm volatile ("dmb");
    src = (void *)_ns_sidata;
    dst = (void *)_ns_sdata;

    while (dst < (uint32_t *)_ns_edata)
        *(dst++) = *(src++);

    for (dst = (void *)_sbss; dst < (void *)_ebss; dst++)
        *dst = 0;

    frosted_main();
    while (1);
}

void nmi_handler(void) { while (1); }

extern void hard_fault_handler(void);
extern void mem_manage_handler(void);
extern void bus_fault_handler(void);
extern void usage_fault_handler(void);
extern int sv_call_handler(void);
extern void pend_sv_handler(void);
extern void sys_tick_handler(void);
extern void secure_violation_handler(void);

void debug_mon_handler(void) { while (1); }

void usb_irq_handler(void);
#if defined(TARGET_stm32h563)
void usart3_irq_handler(void);
#else
void usart2_irq_handler(void);
#endif

__attribute__((section(".vectors")))
void (* const vector_table[])(void) = {
    (void (*)(void))(&_estack),
    reset_handler,
    nmi_handler,
    hard_fault_handler,
    mem_manage_handler,
    bus_fault_handler,
    usage_fault_handler,
    0, 0, 0, 0,
    (void*)sv_call_handler,
    debug_mon_handler,
    0,
    pend_sv_handler,
    sys_tick_handler,
    empty_handler, /* 0 */
    empty_handler, /* 1 */
    empty_handler, /* 2 */
    empty_handler, /* 3 */
    secure_violation_handler, /* 4: TAMP used as Illegal access to secure mode via  doorbell*/
    empty_handler, /* 5 */
    empty_handler, /* 6 */
    empty_handler, /* 7 */
    empty_handler, /* 8 */
    empty_handler, /* 9 */
    empty_handler, /* 10 */
    empty_handler, /* 11 */
    empty_handler, /* 12 */
    empty_handler, /* 13 */
#if defined(TARGET_rp2350)
    usb_irq_handler, /* 14 */
#else
    empty_handler, /* 14 */
#endif
    empty_handler, /* 15 */
    empty_handler, /* 16 */
    empty_handler, /* 17 */
    empty_handler, /* 18 */
    empty_handler, /* 19 */
    empty_handler, /* 20 */
    empty_handler, /* 21 */
    empty_handler, /* 22 */
    empty_handler, /* 23 */
    empty_handler, /* 24 */
    empty_handler, /* 25 */
    empty_handler, /* 26 */
    empty_handler, /* 27 */
    empty_handler, /* 28 */
    empty_handler, /* 29 */
    empty_handler, /* 30 */
    empty_handler, /* 31 */
    empty_handler, /* 32 */
    empty_handler, /* 33 */
    empty_handler, /* 34 */
    empty_handler, /* 35 */
    empty_handler, /* 36 */
    empty_handler, /* 37 */
    empty_handler, /* 38 */
    empty_handler, /* 39 */
    empty_handler, /* 40 */
    empty_handler, /* 41 */
    empty_handler, /* 42 */
    empty_handler, /* 43 */
    empty_handler, /* 44 */
    empty_handler, /* 45 */
    empty_handler, /* 46 */
    empty_handler, /* 47 */
    empty_handler, /* 48 */
    empty_handler, /* 49 */
    empty_handler, /* 50 */
    empty_handler, /* 51 */
    empty_handler, /* 52 */
    empty_handler, /* 53 */
    empty_handler, /* 54 */
    empty_handler, /* 55 */
    empty_handler, /* 56 */
    empty_handler, /* 57 */
#if defined(TARGET_stm32h563)
    empty_handler, /* 58 */
    empty_handler, /* 59 */
    usart3_irq_handler, /* 60 */
    empty_handler, /* 61 */
    empty_handler, /* 62 */
#else
    empty_handler, /* 58 */
    empty_handler, /* 59 */
    empty_handler, /* 60 */
    empty_handler, /* 61 */
    usart2_irq_handler, /* 62 */
#endif
    empty_handler, /* 63 */
    empty_handler, /* 64 */
    empty_handler, /* 65 */
    empty_handler, /* 66 */
    empty_handler, /* 67 */
    empty_handler, /* 68 */
    empty_handler, /* 69 */
    empty_handler, /* 70 */
    empty_handler, /* 71 */
    empty_handler, /* 72 */
#if defined(TARGET_stm32h563)
    empty_handler, /* 73 */
    usb_irq_handler, /* 74 */
#else
    empty_handler, /* 73 */
    empty_handler, /* 74 */
#endif
    empty_handler, /* 75 */
    empty_handler, /* 76 */
    empty_handler, /* 77 */
    empty_handler, /* 78 */
    empty_handler, /* 79 */
};

#define NUM_IRQS (80)
