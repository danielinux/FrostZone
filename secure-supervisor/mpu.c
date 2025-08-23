/*
 *      This file is part of frosted.
 *
 *      frosted is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the Free Software Foundation.
 *
 *
 *      frosted is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frosted.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Authors:
 *
 */

#include <stdint.h>

#define MPU_BASE        (0xE002ED90UL)   /* MPU register block base address in non-secure domain */

#include "mpu_struct.h"
#include "mpu_armv8.h"
#include "limits.h"

volatile MPU_Type *volatile MPU = (MPU_Type *)MPU_BASE;



#define NS_FILESYSTEM_START (0x10010000UL)
#define NS_FILESYSTEM_END   (0x10200000UL - 1)

#define NS_RAM_START      (0x20010000UL )
#define NS_RAM_END        (0x20080000UL - 1)

#define DEV_START        (0x40000000UL)
#define DEV_END          (0x60000000UL - 1)

#define REG_START        (0xE0000000UL)
#define REG_END          (0xE0100000UL - 1)


enum attr_layout {
  ATTR_NORMAL_WBWA = 0,  /* Normal, WB/WA (inner+outer) */
  ATTR_NORMAL_NC   = 1,  /* Normal, Non-cacheable (inner+outer) */
  ATTR_DEV_nGnRE   = 2,  /* Device nGnRE */
  ATTR_DEV_nGnRnE  = 3   /* Device nGnRnE */
};


void mpu_init(void)
{
    uint32_t mair0, o, b, i;
    MPU->CTRL = 0U; /* Disable */
    /* Fill attribute tables */
    /* Slot 0: normal WB/WA (out = in = 0xF) */
    mair0 = 0;
    o = ARM_MPU_ATTR_MEMORY_(1U, 1U, 1U, 1U);
    b = ARM_MPU_ATTR(o, o);
    mair0 = (mair0 & ~(0xFFU << (ATTR_NORMAL_WBWA * 8))) |
        (b << (ATTR_NORMAL_WBWA * 8));
    MPU->MAIR0 = mair0;

    /* Slot 1: Normal Non-cacheable (0x44) */
    mair0 = MPU->MAIR0;
    b = ARM_MPU_ATTR(ARM_MPU_ATTR_NON_CACHEABLE, ARM_MPU_ATTR_NON_CACHEABLE);
    mair0 = (mair0 & ~(0xFFU << (ATTR_NORMAL_NC * 8))) |
        (b << (ATTR_NORMAL_NC * 8));
    MPU->MAIR0 = mair0;

    /* Slot 2: Device memory (nGnRE) */
    mair0 = MPU->MAIR0;
    b = ARM_MPU_ATTR(ARM_MPU_ATTR_DEVICE, ARM_MPU_ATTR_DEVICE_nGnRE);
    mair0 = (mair0 & ~(0xFFU << (ATTR_DEV_nGnRE * 8))) |
        (b << (ATTR_DEV_nGnRE * 8));
    MPU->MAIR0 = mair0;

    /* Slot 3: Strongly ordered memory (nGnRnE) */
    mair0 = MPU->MAIR0;
    b = ARM_MPU_ATTR(ARM_MPU_ATTR_DEVICE, ARM_MPU_ATTR_DEVICE_nGnRnE);
    mair0 = (mair0 & ~(0xFFU << (ATTR_DEV_nGnRnE * 8))) |
        (b << (ATTR_DEV_nGnRnE * 8));
    MPU->MAIR0 = mair0;

    /* NS blanket regions */
    /* Access is denied by default via PRIVDEFENA=0 */

    /* Region 0: Device + PPB space. Read-write. Privileged. Non-executable. */
    MPU->RNR = 0;
    MPU->RBAR = ARM_MPU_RBAR(DEV_START, ARM_MPU_SH_OUTER,
            0U, /* RW */
            0U, /* P */
            1U /* XN */
            );
    MPU->RLAR = ARM_MPU_RLAR(REG_END, ATTR_DEV_nGnRnE); /* Ends after registers. */

    /* Region 1: Kernel and processes: Non-secure flash. Read-only. Non-privileged. Executable. */
    MPU->RNR = 1;
    MPU->RBAR = ARM_MPU_RBAR(NS_FILESYSTEM_START, ARM_MPU_SH_NON,
            1U, /* RO */
            1U, /* NP */
            0U /* X */
            );
    MPU->RLAR = ARM_MPU_RLAR(NS_FILESYSTEM_END, ATTR_NORMAL_NC);

    /* Regions 2-7: Disable */
    for (i = 2; i < 8; i++) {
        MPU->RNR = i;
        MPU->RBAR = 0;
        MPU->RLAR = 0;
    }

    /* Go! */
    MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
    asm volatile("dsb");
    asm volatile("isb");
}

__attribute__((cmse_nonsecure_entry))
void mpu_task_on(uint16_t pid, uint16_t ppid)
{
    secure_task_t *task;
    uint32_t i;
    task = get_secure_task(pid);
    MPU->CTRL = 0;
    for (i = 2; i < 8; i++) {
        MPU->RNR = i;
        MPU->RBAR = 0;
        MPU->RLAR = 0;
    }


    if ((pid == 0) || (!task)) {
        MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
        asm volatile("dsb");
        asm volatile("isb");
        return;
    }

    /* Stack space for the task */
    MPU->RNR = 2;
    MPU->RBAR = ARM_MPU_RBAR((uintptr_t)task->stack_segment.base, ARM_MPU_SH_INNER,
            0U, /* RW */
            1U, /* NP */
            1U /* XN */
            );
    MPU->RLAR = ARM_MPU_RLAR((uintptr_t)task->stack_segment.base + task->stack_segment.size - 1,
            ATTR_NORMAL_NC);
    asm volatile("dsb");
    asm volatile("isb");


    if (ppid > 0) {
        /* This task has just been vforked.
         * it does not have a valid main space yet, as it's executing from
         * the parent's memory. Allow accessing parent's stack and data space,
         * until exec().
         */
        secure_task_t *parent;
        parent = get_secure_task(ppid);
        if (parent) {
            MPU->RNR = 3;
            MPU->RBAR = ARM_MPU_RBAR((uintptr_t)parent->stack_segment.base, ARM_MPU_SH_INNER,
                    0U, /* RW */
                    1U, /* NP */
                    1U /* XN */
                    );
            MPU->RLAR = ARM_MPU_RLAR((uintptr_t)parent->stack_segment.base + parent->stack_segment.size - 1,
                    ATTR_NORMAL_NC);
            MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
            asm volatile("dsb");
            asm volatile("isb");
            /* Main segment for the parent */
            MPU->RNR = 4;
            MPU->RBAR = ARM_MPU_RBAR((uintptr_t)parent->main_segment.base, ARM_MPU_SH_INNER,
                    0U, /* RW */
                    1U, /* NP */
                    0U /* XN */
                    );
            MPU->RLAR = ARM_MPU_RLAR((uintptr_t)parent->main_segment.base + parent->main_segment.size - 1,
                    ATTR_NORMAL_NC);
            asm volatile("dsb");
            asm volatile("isb");
        }
        MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
        return;
    }

    /* Main segment for the task */
    MPU->RNR = 3;
    MPU->RBAR = ARM_MPU_RBAR((uintptr_t)task->main_segment.base, ARM_MPU_SH_INNER,
            0U, /* RW */
            1U, /* NP */
            0U /* XN */
            );
    MPU->RLAR = ARM_MPU_RLAR((uintptr_t)task->main_segment.base + task->main_segment.size - 1,
            ATTR_NORMAL_NC);
    asm volatile("dsb");
    asm volatile("isb");

    /* Up to four heap segments for the task */
    for (int i = 0; i < task->mempool_count && i < 4; i++) {
        MPU->RNR = 4 + i;
        MPU->RBAR = ARM_MPU_RBAR((uintptr_t)task->mempool[i].base, ARM_MPU_SH_INNER,
                0U, /* RW */
                1U, /* NP */
                1U /* XN */
                );
        MPU->RLAR = ARM_MPU_RLAR((uintptr_t)task->mempool[i].base + task->mempool[i].size - 1,
                ATTR_NORMAL_NC);
        asm volatile("dsb");
        asm volatile("isb");
    }
    MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
    asm volatile("dsb");
    asm volatile("isb");
}
