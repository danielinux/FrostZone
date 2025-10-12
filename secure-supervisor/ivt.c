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
#include "limits.h"
#include "task.h"

void Reset_Handler(void);
void Default_Handler(void)        { while (1); }
void NMI_Handler(void)            { while (1); }

#if 0
void HardFault_Handler(void)
{
    /* Assign values for debugging; place a bkpt */
    volatile uint32_t r0;
    volatile uint32_t r1;
    volatile uint32_t r2;
    volatile uint32_t r3;
    volatile uint32_t r12;
    volatile uint32_t lr;
    volatile uint32_t pc;
    volatile uint32_t psr;

    (void)r0;
    (void)r1;
    (void)r2;
    (void)r3;
    (void)r12;
    (void)lr;
    (void)pc;
    (void)psr;


    asm volatile ("TST lr, #4 \n"
                 "ITE EQ \n"
                 "MRSEQ r0, MSP \n"
                 "MRSNE r0, PSP \n"
                 "BKPT #0 \n");
    while (1)
        ;

}
#else

void HardFault_Handler(void)
{
    asm volatile ("BKPT #0");
    while(1)
        ;
}

#endif

void MemManage_Handler(void)
{
    while(1)
        ;
}
void BusFault_Handler(void)
{
    while(1)
        ;
}

#define DOORBELL_ILLEGAL_INSTRUCTION_IRQn (4u) /* Using TAMP IRQ as doorbell for illegal instruction */
#define NVIC_ISPR0 (*(volatile uint32_t *)0xE000E200)

void SecureFault_Handler(void)
{
    uint32_t sp = 0;
    secure_task_t *offending_task;

    __asm volatile ("MRS %0, PSP_NS" : "=r" (sp));

    offending_task = get_secure_task_by_addr(sp);
    if (!offending_task) {
        while (1) {
        }
    }

    NVIC_ISPR0 |= (1 << DOORBELL_ILLEGAL_INSTRUCTION_IRQn);
}

void UsageFault_Handler(void)
{
    while(1)
        ;
}

void SVC_Handler(void)
{
    while(1)
        ;
}

void DebugMon_Handler(void)
{
    while(1)
        ;
}

void PendSV_Handler(void)
{
    while(1)
        ;
}

void SysTick_Handler(void)
{
    while(1)
        ;
}


extern uint32_t __stack;

__attribute__ ((section(".custom_ivt"), used))
void (* const g_pfnVectors[])(void) = {
    (void (*)(void))(&__stack),
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    SecureFault_Handler,
    0,
    0,
    0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,

    Default_Handler, /* IRQ 0 */
    Default_Handler, /* IRQ 1 */
    Default_Handler, /* IRQ 2 */
    Default_Handler, /* IRQ 3 */
    Default_Handler, /* IRQ 4 */
    Default_Handler, /* IRQ 5 */
    Default_Handler, /* IRQ 6 */
    Default_Handler, /* IRQ 7 */
    Default_Handler, /* IRQ 8 */
    Default_Handler, /* IRQ 9 */
    Default_Handler, /* IRQ 10 */
    Default_Handler, /* IRQ 11 */
    Default_Handler, /* IRQ 12 */
    Default_Handler, /* IRQ 13 */
    Default_Handler, /* IRQ 14 */
    Default_Handler, /* IRQ 15 */
    Default_Handler, /* IRQ 16 */
    Default_Handler, /* IRQ 17 */
    Default_Handler, /* IRQ 18 */
    Default_Handler, /* IRQ 19 */
    Default_Handler, /* IRQ 20 */
    Default_Handler, /* IRQ 21 */
    Default_Handler, /* IRQ 22 */
    Default_Handler, /* IRQ 23 */
    Default_Handler, /* IRQ 24 */
    Default_Handler, /* IRQ 25 */
    Default_Handler, /* IRQ 26 */
    Default_Handler, /* IRQ 27 */
    Default_Handler, /* IRQ 28 */
    Default_Handler, /* IRQ 29 */
    Default_Handler, /* IRQ 30 */
    Default_Handler, /* IRQ 31 */
};
