/*
 *      This file is part of frostzone.
 *
 *      frostzone is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 3, as
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

#include "armv8m_tz.h"
#include "task.h"
#include "limits.h"
#include "mempool.h"

#include "pico/stdlib.h"
    
#define NS_START_ADDR     0x10010000U               /* Base of NS Flash */
#define SCB_VTOR_NS       (*(volatile uint32_t *)0xE002ED08)
#define SCB_VTOR          (*(volatile uint32_t *)0xE000ED08)

#define NS_RESET_VECTOR   (*(uint32_t *)(NS_START_ADDR + 4))
#define NS_MSP_INIT       (*(uint32_t *)(NS_START_ADDR))

#define NVIC_ICER0 (*(volatile uint32_t *)(0xE000E180))
#define NVIC_ICPR0 (*(volatile uint32_t *)(0xE000E280))
#define NVIC_ITNS0 (*(volatile uint32_t *)(0xE000EF00))
#define NVIC_ITNS1 (*(volatile uint32_t *)(0xE000EF04))
#define NVIC_ITNS2 (*(volatile uint32_t *)(0xE000EF08))


#define NSACR (*(volatile uint32_t *)(0xE000ED8C))
#define CPACR (*(volatile uint32_t *)(0xE000ED88))

#define SHCSR (*(volatile uint32_t *)(0xE000ED24))
#define SHCSR_MEMFAULTENA (1 << 16)
#define SHCSR_BUSFAULTENA (1 << 17)
#define SHCSR_USGFAULTENA (1 << 18)

extern void mpu_disable(void);


#ifdef CONFIG_FPU
static void fpu_enable(void)
{
    /* CP10 and CP11 full access */
    CPACR |= (0xF << 20);
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");
}
#endif



static void sau_init(void)
{
    /* Enable secure fault handler */
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;

    /* Secure supervisor flash */
    sau_init_region(0, 0x10000000, 0x10007FFF, 1);

    /* NSC area: supervisor flash callables */
    sau_init_region(1, 0x10008000, 0x1000FFFF, 1);

    /* Non-secure frosted scheduler */
    sau_init_region(2, 0x10010000, 0x1001FFFF, 0);

    /* XIPfs */
    sau_init_region(3, 0x10020000, 0x101FFFFF, 0);

    /* RAM: secure area stack */
    sau_init_region(4, 0x20000000, 0x20003FFF, 1);

    /* RAM: non-secure area (managed via MPU) */
    sau_init_region(5, 0x20010000, 0x2007FFFF, 0);
    
    /* RAM: secure area for supervisor's stack */
    sau_init_region(6, 0x20080000, 0x20081FFF, 1);

    /* USB DRAM: NS */
    sau_init_region(7, 0x50100000, 0x5011FFFF, 0);

    SAU_CTRL = SAU_INIT_CTRL_ENABLE;

    /* Enable MemFault, BusFault and UsageFault */
    SHCSR |= SHCSR_MEMFAULTENA | SHCSR_BUSFAULTENA | SHCSR_USGFAULTENA;

    /* Add flag to trap misaligned accesses */
    *((volatile uint32_t *)0xE000ED14) |= 0x00000008;
}

__attribute__((weak))
void machine_init(void)
{
}


void main(void) {
    uint32_t ivt_ns_reset = (NS_RESET_VECTOR) + 1;

    /* Machine-specific code */
    machine_init();

#ifdef CONFIG_FPU
    /* FPU: enable */
    fpu_enable();
#endif

    /* SAU */
    sau_init();

    /* MPU */
    mpu_disable();

    /* Mem pool init */
    mempool_init();

    /* Secure tasks table init */
    secure_task_table_init();

    /* Configure Non-Secure vector table */
    SCB_VTOR_NS = NS_START_ADDR;

    /* Set Non-Secure MSP */
    asm volatile ( "msr msp_ns, %0\n" : : "r" (NS_MSP_INIT));

    /* Transition to Non-Secure domain kernel */
    asm volatile( "bxns %0\n" : : "r"(ivt_ns_reset) );

    /* Idle loop - Never reached */
    while (1) {
        __asm__("wfi");
    }
}
