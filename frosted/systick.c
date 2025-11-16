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
#include "lowpower.h"
#include "heap.h"
#include "nvic.h"
#include "systick.h"
#include "kprintf.h"
#include "errno.h"
#include <stdbool.h>


volatile unsigned int jiffies = 0u;
volatile unsigned int _n_int = 0u;
volatile int ktimer_check_pending = 0;
volatile int sleep_mode = 0;
static int _sched_active = 0;

void frosted_scheduler_on(void)
{
    nvic_set_priority(NVIC_PENDSV_IRQ, 4 << 5);
    nvic_set_priority(NVIC_SV_CALL_IRQ, 3 << 5);
    nvic_set_priority(NVIC_SYSTICK_IRQ, 2 << 5);
    nvic_enable_irq(NVIC_SYSTICK_IRQ);
    systick_counter_enable();
    systick_interrupt_enable();
    _sched_active = 1;
    asm volatile ("cpsie i");
}

void frosted_scheduler_off(void)
{
    _sched_active = 0;
}

void __attribute__((weak)) SysTick_Hook(void)
{
}

typedef struct ktimer {
    uint32_t expire_time;
    void *arg;
    void (*handler)(uint32_t time, void *arg);
} ktimer;

DECLARE_HEAP(ktimer, expire_time);
static struct heap_ktimer *ktimer_list = NULL;
static bool ktimer_handler_valid(void (*handler)(uint32_t, void *))
{
    uintptr_t addr;

    if (handler == NULL) {
        return false;
    }

    addr = (uintptr_t)handler;
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

static void ktimer_log_invalid(const char *where, void (*handler)(uint32_t, void *), void *arg)
{
    kprintf("ktimer: dropping handler %p (%s) arg=%p\n", handler, where, arg);
    __asm__ volatile("bkpt #0");
}

/* Init function */
void ktimer_init(void)
{
    ktimer_list = heap_init();
    if (!ktimer_list) {
        kprintf("ktimer: heap_init failed\n");
    }
}

/* Add kernel timer */
int ktimer_add(uint32_t count, void (*handler)(uint32_t, void *), void *arg)
{
    struct ktimer t;
    int ret;

    if (!ktimer_list) {
        return -ENOMEM;
    }

    if (!ktimer_handler_valid(handler)) {
        ktimer_log_invalid("add", handler, arg);
        return -EINVAL;
    }

    memset(&t, 0, sizeof(t));
    t.expire_time = jiffies + count;
    t.handler = handler;
    t.arg = arg;
    if (!task_in_syscall())
        irq_off();
    ret = heap_insert(ktimer_list, &t);
    if (!task_in_syscall())
        irq_on();
    return ret;
}

/* Delete kernel timer */
int ktimer_del(int tid)
{
    int ret;
    if (tid < 0)
        return -1;
    if (!ktimer_list)
        return -ENOMEM;
    if (!task_in_syscall())
        irq_off();
    ret = heap_delete(ktimer_list, tid);
    if (!task_in_syscall())
        irq_on();
    return ret;
}

static inline int ktimer_expired(void)
{
    struct ktimer *t;
    return ((ktimer_list) && (ktimer_list->n > 0) &&
            (t = heap_first(ktimer_list)) && (t->expire_time < jiffies));
}


/* Tasklet that checks expired timers */
static void ktimers_check_tasklet(void *arg)
{
    struct ktimer current;

    (void)arg;

    while (1) {
        struct ktimer head;

        irq_off();
        if (!ktimer_list || (ktimer_list->n == 0) || (ktimer_list->top == NULL)) {
            irq_on();
            break;
        }

        head = ktimer_list->top[1].data;
        if (head.expire_time > jiffies) {
            irq_on();
            break;
        }

        heap_peek(ktimer_list, &current);
        irq_on();

        if (!ktimer_handler_valid(current.handler)) {
            ktimer_log_invalid("run", current.handler, current.arg);
            continue;
        }

        if (current.handler) {
            current.handler(jiffies, current.arg);
        }
    }

    ktimer_check_pending = 0;
}

void sys_tick_handler(void)
{
    uint32_t next_timer = 0;
    volatile uint32_t reload = systick_get_reload();
    uint32_t this_timeslice;
    SysTick_Hook();
    jiffies ++;
    _n_int++;

    if (ktimer_expired()) {
        task_preempt_all();
        goto end;
    }

    if (_sched_active && ((task_timeslice() == 0) || (!task_running()))) {
        schedule();
        (void)next_timer;
    }
end:
    if (!ktimer_check_pending) {
        ktimer_check_pending++;
        tasklet_add(ktimers_check_tasklet, NULL);
    }
}
