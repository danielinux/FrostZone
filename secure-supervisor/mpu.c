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
#include "task.h"
#include "limits.h"

extern secure_task_t secure_tasks[];

/* ARMv8‑M MPU registers */
#define MPU_TYPE       (*(volatile uint32_t *)0xE000ED90)
#define MPU_CTRL       (*(volatile uint32_t *)0xE000ED94)
#define MPU_RNR        (*(volatile uint32_t *)0xE000ED98)
#define MPU_RBAR       (*(volatile uint32_t *)0xE000ED9C)
#define MPU_RLAR       (*(volatile uint32_t *)0xE000EDA0)

/* Helper macros for region attributes */
#define MPU_AP_FULL      (0x07 << 8)   /* Full access (AP[2:0] = 111) */
#define MPU_AP_RW        (0x03 << 8)   /* Read/Write (AP[2:0] = 011) */
#define MPU_XN           (0x0 << 4)    /* Execute allowed (XN = 0) */
#define MPU_REGION_ENABLE (0x1)        /* Enable bit in RLAR */

/* Disable the MPU and clear all regions */
void mpu_disable(void)
{
    /* Disable MPU */
    MPU_CTRL = 0;

    /* Clear all regions */
    uint32_t regions = ((MPU_TYPE >> 8) & 0xFF); /* DREGION field */
    for (uint32_t i = 0; i < regions; i++) {
        MPU_RNR  = i;
        MPU_RBAR = 0;
        MPU_RLAR = 0;
    }

    /* Ensure the changes take effect */
    __asm volatile ("dsb");
    __asm volatile ("isb");
}
    
static void add_region(uint32_t base, uint32_t size, uint32_t ap, uint32_t nr) {
    MPU_RNR  = nr;
    MPU_RBAR = base & 0xFFFFFFE0;          /* 32‑byte alignment */
    MPU_RLAR = (base + size - 1) & 0xFFFFFFE0
        | ap | MPU_XN | MPU_REGION_ENABLE;
}

/* Configure MPU regions for all secure tasks (including the kernel) */
void mpu_configure(void)
{
    /* First disable MPU and clear all regions */
    mpu_disable();

    /* Determine how many regions the MPU supports */
    uint32_t max_regions = ((MPU_TYPE >> 8) & 0xFF);
    uint32_t region_idx  = 0;

    /* Helper to add a region */

    /* Iterate over all secure tasks */
    for (int i = 0; i < MAX_SECURE_TASKS; i++) {
        secure_task_t *t = &secure_tasks[i];
        if (t->task_id == 0xFFFF) {
            continue; /* unused entry */
        }

        /* Determine access permissions */
        uint32_t ap = (t->task_id == 0) ? MPU_AP_FULL : MPU_AP_RW;

        /* Main segment (data/bss) */
        if (t->main_segment.base && t->main_segment.size) {
            add_region((uint32_t)t->main_segment.base,
                       t->main_segment.size, ap, region_idx++);
        }

        /* Mempool blocks */
        for (int j = 0; j < CONFIG_MEMPOOL_SEGMENTS_PER_TASK; j++) {
            if (t->mempool[j].base && t->mempool[j].size) {
                add_region((uint32_t)t->mempool[j].base,
                           t->mempool[j].size, ap, region_idx++);
            }
        }
    }

    /* Enable MPU with default privileged settings */
    MPU_CTRL = MPU_CTRL | 0x1; /* Enable MPU */
}
