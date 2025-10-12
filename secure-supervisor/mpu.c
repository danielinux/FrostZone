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
#include <stddef.h>

#define MPU_BASE        (0xE000ED90UL)   /* MPU register block base address in non-secure domain */

#include "mpu_struct.h"
#include "mpu_armv8.h"

/*
 * By default, the MPU is enabled for all builds to provide isolation.
 * Define CONFIG_MPU to 0 on the compiler command line to disable MPU
 * initialization and task‑specific MPU configuration. This is useful for
 * testing the scheduler without memory protection.
 */
#ifndef CONFIG_MPU
#define CONFIG_MPU 1
#endif
#include "limits.h"

#define __DSB() __asm volatile ("dsb")
#define __ISB() __asm volatile ("isb")




volatile MPU_Type *volatile MPU = (MPU_Type *)MPU_BASE;


#ifdef TARGET_STM32H563
#define NS_FILESYSTEM_START  (0x08030000)
#define NS_FILESYSTEM_END    (0x081FFFFF)
#elif defined(TARGET_RP2350)
#define NS_FILESYSTEM_START (0x10010000UL)
#define NS_FILESYSTEM_END   (0x10200000UL - 1)
#endif

#define NS_RAM_START      (0x20010000UL )
#define NS_RAM_END        (0x20080000UL - 1)

#define DEV_START        (0x40000000UL)
#define DEV_END          (0x60000000UL - 1)

// ---- MAIR attribute slot indices ----
// We make the mapping explicit to avoid mismatches with other headers.
#define IDX_NORMAL_WBWA 0u // 0xFF: Normal, WB/WA, Non-shareable
#define IDX_NORMAL_WT 1u // 0x44: Normal, WT, Non-shareable (good for XIP)
#define IDX_DEVICE_nGnRE 2u // 0x04: Device nGnRE
#define IDX_DEVICE_nGnRnE 3u // 0x00: Device nGnRnE (Strongly-ordered)


// MAIR encodings (ARM ARM):
#define MAIR_NORMAL_WBWA 0xFFu
#define MAIR_NORMAL_WT 0x44u
#define MAIR_DEVICE_nGnRE 0x04u
#define MAIR_DEVICE_nGnRnE 0x00u


// Helpers for RBAR/RLAR (ARMv8-M)
// RBAR: [31:5]=BASE, [4:3]=SH, [2:1]=AP, [0]=XN
// RLAR: [31:5]=LIMIT, [4:1]=AttrIdx, [0]=EN
#define RBAR(base, sh, ap, xn) ( ((uint32_t)(base) & 0xFFFFFFE0UL) | (((uint32_t)(sh) & 0x3u) << 3) | (((uint32_t)(ap) & 0x3u) << 1) | ((uint32_t)(xn) & 0x1u) )
#define RLAR(limit, idx) ( ((uint32_t)(limit) & 0xFFFFFFE0UL) | (((uint32_t)(idx) & 0xFu) << 1) | 0x1u )


// Shareability (SH) encodings for RBAR
#define SH_NON_SHAREABLE 0u
#define SH_INNER_SHAREABLE 1u
#define SH_OUTER_SHAREABLE 2u


// Access permission (AP) encodings for RBAR (ARMv8-M):
// 0b00 = RW, privileged only; 0b01 = RW, privileged & unprivileged
// 0b10 = RO, privileged only; 0b11 = RO, privileged & unprivileged
#define AP_RW_PRIVONLY 0u
#define AP_RW_FULL 1u
#define AP_RO_PRIVONLY 2u
#define AP_RO_FULL 3u


// eXecute Never bit
#define XN_EXECUTE 0u
#define XN_NEVER 1u



void mpu_init(void)
{
#if CONFIG_MPU

    // Disable MPU during configuration
    MPU->CTRL = 0U;


    // ---- Program MAIR0 with our 4 attribute slots ----
    uint32_t mair0 = 0u;
    mair0 |= ((uint32_t)MAIR_NORMAL_WBWA << (IDX_NORMAL_WBWA * 8));
    mair0 |= ((uint32_t)MAIR_NORMAL_WT << (IDX_NORMAL_WT * 8));
    mair0 |= ((uint32_t)MAIR_DEVICE_nGnRE << (IDX_DEVICE_nGnRE * 8));
    mair0 |= ((uint32_t)MAIR_DEVICE_nGnRnE << (IDX_DEVICE_nGnRnE * 8));
    MPU->MAIR0 = mair0;


    // ---- Region 0: Peripherals (Device), RW, Privileged, XN ----
    // Covers 0x4000_0000 .. 0x5FFF_FFFF
    MPU->RNR = 0u;
    MPU->RBAR = RBAR(DEV_START, SH_OUTER_SHAREABLE, AP_RW_PRIVONLY, XN_NEVER);
    MPU->RLAR = RLAR(DEV_END, IDX_DEVICE_nGnRE);


    // ---- Region 1: USERLAND XIP window in flash, RO, unprivileged, Executable ----
    // Map as Normal WT (good for XIP on MCUs w/o caches). If you need WBWA, switch to IDX_NORMAL_WBWA.
    MPU->RNR = 1u;
    MPU->RBAR = RBAR(NS_FILESYSTEM_START, SH_NON_SHAREABLE, AP_RO_FULL, XN_EXECUTE);
    MPU->RLAR = RLAR(NS_FILESYSTEM_END, IDX_NORMAL_WT);

    // Disable the remaining regions (2..7) just to be explicit
    for (uint32_t i = 2u; i < 8u; i++) {
        MPU->RNR = i;
        MPU->RBAR = 0u;
        MPU->RLAR = 0u;
    }

    // Enable MPU with background map so addresses not covered by a region use the default memory map.
    MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
    __DSB();
    __ISB();
#endif
}

__attribute__((cmse_nonsecure_entry))
void mpu_task_on(uint16_t pid, uint16_t ppid)
{
#if CONFIG_MPU
    secure_task_t *task = get_secure_task(pid);


    // Configure with MPU disabled
    MPU->CTRL = 0u;


    // Keep Region 0 (peripherals) and Region 1 (USERLAND XIP) intact from mpu_init().
    // We program per-task regions starting at R2..


    // Clear R2..R7
    for (uint32_t i = 2u; i < 8u; i++) {
        MPU->RNR = i;
        MPU->RBAR = 0u;
        MPU->RLAR = 0u;
    }


    if ((pid == 0u) || (task == 0x0)) {
        // Kernel context: rely on base regions + background map
        MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
        __DSB(); __ISB();
        return;
    }


    // --- R2: Task stack (NS RAM), RW, unprivileged, XN ---
    // Inner-shareable makes sense for D-cache-less MCUs too; adjust if needed.
    if (task->stack_segment.size) {
        uintptr_t sb = (uintptr_t)(task->stack_segment.base);
        uintptr_t sl = sb + task->stack_segment.size - 1u;
        MPU->RNR = 2u;
        MPU->RBAR = RBAR(sb, SH_INNER_SHAREABLE, AP_RW_FULL, XN_NEVER);
        MPU->RLAR = RLAR(sl, IDX_NORMAL_WBWA); // Normal memory; XN via RBAR
    }


    if (ppid > 0u) {
        // vfork(): allow access to parent's RAM while still executing parent's code.
        secure_task_t *parent = get_secure_task(ppid);
        if (parent) {
            // R3: Parent stack, RW, XN
            if (parent->stack_segment.size) {
                uintptr_t sb = (uintptr_t)parent->stack_segment.base;
                uintptr_t sl = sb + parent->stack_segment.size - 1u;
                MPU->RNR = 3u;
                MPU->RBAR = RBAR(sb, SH_INNER_SHAREABLE, AP_RW_FULL, XN_NEVER);
                MPU->RLAR = RLAR(sl, IDX_NORMAL_WBWA);
            }
            // R4: Parent main segment (code+rodata in flash, maybe data in RAM)
            // If this lives in flash XIP window: map RO, executable.
            if (parent->main_segment.size) {
                uintptr_t mb = (uintptr_t)parent->main_segment.base;
                uintptr_t ml = mb + parent->main_segment.size - 1u;
                MPU->RNR = 4u;
                // If parent->main in flash: RO+Exec; if in RAM: choose RW and set XN as needed.
                // Here we assume flash XIP: RO, unprivileged, executable
                MPU->RBAR = RBAR(mb, SH_NON_SHAREABLE, AP_RO_FULL, XN_EXECUTE);
                MPU->RLAR = RLAR(ml, IDX_NORMAL_WT); // use WT for XIP regions
            }
        }
        MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
        __DSB(); __ISB();
        return;
    }


    // --- R3: Task main segment ---
    if (task->main_segment.size) {
        uintptr_t mb = (uintptr_t)task->main_segment.base;
        uintptr_t ml = mb + task->main_segment.size - 1u;
        MPU->RNR = 3u;
        // If main lives in flash XIP: map RO+Exec; if it’s RAM, map RW+XN. We assume flash XIP here.
        MPU->RBAR = RBAR(mb, SH_NON_SHAREABLE, AP_RO_FULL, XN_EXECUTE);
        MPU->RLAR = RLAR(ml, IDX_NORMAL_WT); // Normal WT attr for XIP
    }


    // --- R4..R7: Heaps / extra RAM, RW, XN ---
    for (uint32_t k = 0u; k < task->mempool_count && k < 4u; k++) {
        uintptr_t hb = (uintptr_t)task->mempool[k].base;
        uintptr_t hl = hb + task->mempool[k].size - 1u;
        MPU->RNR = (4u + k);
        MPU->RBAR = RBAR(hb, SH_INNER_SHAREABLE, AP_RW_FULL, XN_NEVER);
        MPU->RLAR = RLAR(hl, IDX_NORMAL_WBWA);
    }


    // Enable MPU with background map
    MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
    __DSB(); __ISB();
#endif
}
