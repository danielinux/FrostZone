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
 *      Authors: Daniele Lacamera, Maxime Vincent
 *
 */
#include "frosted.h"
#include "stddef.h"
#include "stdbool.h"
#include "kprintf.h"

#define MAX_TASKLETS 64

struct tasklet {
    void (*exe)(void *);
    void *arg;
};

static struct tasklet tasklet_array[MAX_TASKLETS];
uint32_t n_tasklets = 0;
uint32_t max_tasklets = 0;

static bool tasklet_handler_valid(void (*exe)(void *))
{
    uintptr_t addr;

    if (exe == NULL) {
        return false;
    }

    addr = (uintptr_t)exe;
#if defined(__thumb__)
    if ((addr & 0x1U) == 0U) {
        return false;
    }
    addr &= ~0x1U;
#endif
#if defined(TARGET_RP2350)
    if (addr >= 0x10000000UL && addr < 0x11000000UL) {
        return true;
    }
#else
    if (addr >= 0x08000000UL && addr < 0x09000000UL) {
        return true;
    }
#endif
    return false;
}

static void tasklet_log_invalid(const char *where, void (*exe)(void *), void *arg)
{
    kprintf("tasklet: dropping invalid handler %p (%s) arg=%p\n", exe, where, arg);
    __asm__ volatile("bkpt #0");
}

void tasklet_add(void (*exe)(void*), void *arg)
{
    int i;
    bool already_in_syscall = task_in_syscall();

    if (!tasklet_handler_valid(exe)) {
        tasklet_log_invalid("enqueue", exe, arg);
        return;
    }

    if (!already_in_syscall) {
        irq_off();
    }

    for (i = 0; i < MAX_TASKLETS; i++) {
        if (!tasklet_array[i].exe) {
            tasklet_array[i].exe = exe;
            tasklet_array[i].arg = arg;
            n_tasklets++;
            if (n_tasklets > max_tasklets)
                max_tasklets = n_tasklets;
            if (!already_in_syscall)
                irq_on();
            return;
        }
    }
    while (1) {
        __asm__ volatile("bkpt #0");
    }

}

void check_tasklets(void)
{
    int i;
    uint32_t cur_tasklets = n_tasklets;
    if (n_tasklets == 0)
        return;
    irq_off();
    for (i = 0; i < cur_tasklets; i++) {
        void (*exe)(void *) = tasklet_array[i].exe;
        void *arg = tasklet_array[i].arg;

        if (exe) {
            if (!tasklet_handler_valid(exe)) {
                tasklet_log_invalid("dispatch", exe, arg);
            } else {
                exe(arg);
            }

            tasklet_array[i].exe = NULL;
            tasklet_array[i].arg = NULL;
            if (n_tasklets > 0) {
                n_tasklets--;
            }
        }
    }
    irq_on();
}
