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
 *      Authors: Daniele Lacamera, Maxime Vincent, brabo
 *
 */
#include "frosted.h"
#include "locks.h"
#include "pool.h"
#include "time.h"

#define SIGN_MUTEX (0xCAFEC0C0)
#define SIGN_SEMAP (0xCAFECAFE)

#define CONFIG_MAX_MUTEXES 32
POOL_DEFINE(mutex_pool, mutex_t, CONFIG_MAX_MUTEXES);
static int mutex_pool_inited = 0;

static int __attribute__((naked)) _mutex_lock(void *m) {
    __asm__ volatile (
        "_mutex_lock:\n"
        "   LDREX   r1, [r0]\n"
        "   CMP     r1, #0\n"
        "   BEQ     _mutex_lock_fail\n"
        "   SUBS    r1, #1\n"
        "   STREX   r2, r1, [r0]\n"
        "   CMP     r2, #0\n"
        "   BEQ     _mutex_lock_done\n"
        "   CLREX\n"
        "   B       _mutex_lock\n"
        "_mutex_lock_done:\n"
        "   DMB\n"
        "   MOVS    r0, #0\n"
        "   BX      lr\n"
        "_mutex_lock_fail:\n"
        "   CLREX\n"
        "   DMB\n"
        "   MOV     r0, #-1\n"
        "   BX      lr\n"
        ::: "r0", "r1", "r2"
    );
}

static int __attribute__((naked)) _mutex_unlock(void *m) {
    __asm__ volatile (
        "_mutex_unlock:\n"
        "   LDREX   r1, [r0]\n"
        "   CMP     r1, #0\n"
        "   BNE     _mutex_unlock_fail\n"
        "   ADDS    r1, #1\n"
        "   STREX   r2, r1, [r0]\n"
        "   CMP     r2, #0\n"
        "   BEQ     _mutex_unlock_done\n"
        "   CLREX\n"
        "   B       _mutex_unlock\n"
        "_mutex_unlock_done:\n"
        "   DMB\n"
        "   MOVS    r0, #0\n"
        "   BX      lr\n"
        "_mutex_unlock_fail:\n"
        "   CLREX\n"
        "   DMB\n"
        "   MOV     r0, #-1\n"
        "   BX      lr\n"
        ::: "r0", "r1", "r2"
    );
}

static int __attribute__((naked)) _sem_wait(void *s) {
    __asm__ volatile (
        "_sem_wait:\n"
        "   LDREX   r1, [r0]\n"
        "   CMP     r1, #0\n"
        "   BEQ     _sem_wait_fail\n"
        "   SUBS    r1, #1\n"
        "   STREX   r2, r1, [r0]\n"
        "   CMP     r2, #0\n"
        "   BEQ     _sem_wait_done\n"
        "   CLREX\n"
        "   B       _sem_wait\n"
        "_sem_wait_done:\n"
        "   DMB\n"
        "   MOVS    r0, #0\n"
        "   BX      lr\n"
        "_sem_wait_fail:\n"
        "   CLREX\n"
        "   DMB\n"
        "   MOV     r0, #-1\n"
        "   BX      lr\n"
        ::: "r0", "r1", "r2"
    );
}

static int __attribute__((naked)) _sem_post(void *s) {
    __asm__ volatile (
        "_sem_post:\n"
        "   LDREX   r1, [r0]\n"
        "   ADDS    r1, #1\n"
        "   BVS     _sem_post_overflow\n"
        "   STREX   r2, r1, [r0]\n"
        "   CMP     r2, #0\n"
        "   BEQ     _sem_post_done\n"
        "   CLREX\n"
        "   B       _sem_post\n"
        "_sem_post_done:\n"
        "   CMP     r1, #1\n"
        "   DMB\n"
        "   BGE     _sem_signal_up\n"
        "   MOVS    r0, #0\n"
        "   BX      lr\n"
        "_sem_post_overflow:\n"
        "   CLREX\n"
        "   MVN     r0, #74\n"
        "   BX      lr\n"
        "_sem_signal_up:\n"
        "   MOVS    r0, #1\n"
        "   BX      lr\n"
        ::: "r0", "r1", "r2"
    );
}

/* Semaphore: internal functions */
static void _add_listener(sem_t *s)
{
    int i;
    struct task *t = this_task();
    uint16_t pid = this_task_getpid();

    irq_off();

    if (s->last >= 0) {
        if ((t == s->listener[s->last]) && (pid == s->listener_pid[s->last])) {
            irq_on();
            return;
        }
    }

    for (i = s->last + 1; i < s->listeners; i++) {
        if (s->listener[i] == NULL) {
            s->listener[i] = t;
            s->listener_pid[i] = pid;
            s->last = i;
            irq_on();
            return;
        }
    }
    for (i = 0; i < s->last; i++) {
        if (s->listener[i] == NULL) {
            s->listener[i] = t;
            s->listener_pid[i] = pid;
            s->last = i;
            irq_on();
            return;
        }
    }
    irq_on();
}

static void _del_listener(sem_t *s)
{
    int i;
    struct task *t = this_task();

    irq_off();
    for (i = 0; i < s->listeners; i++) {
        if (s->listener[i] == t) {
            s->listener[i] = NULL;
            s->listener_pid[i] = 0;
            irq_on();
            return;
        }
    }
    irq_on();
}

static int sem_spinwait(sem_t *s)
{
    if (!s)
        return -EINVAL;
    while (_sem_wait(s) != 0) {
        /* spin ... */
    }
    return 0;
}

/* Semaphore: API */

int sem_trywait(sem_t *s)
{
    if (!s)
        return -EINVAL;
    if(_sem_wait(s) != 0)
        return -EAGAIN;
    return 0;
}

int sem_wait(sem_t *s, struct timespec *timeout)
{
    if (this_task() == NULL)
        return sem_spinwait(s);
    if (!s)
        return -EINVAL;
    if(_sem_wait(s) != 0) {
        if (timeout) {
            uint32_t deadline = (uint32_t)((timeout->tv_sec * 1000) +
                                (timeout->tv_nsec / 1000 / 1000));
            int32_t time_left = (int32_t)(deadline - (uint32_t)jiffies);
            if ((time_left > 0) && (task_get_timer_id() < 0)) {
                task_set_timer_id(ktimer_add((uint32_t)time_left, sleepy_task_wakeup, NULL));
            } else {
                if (time_left < 0) {
                    return -ETIMEDOUT;
                }
                return SYS_CALL_AGAIN;
            }
        }
        _add_listener(s);
        task_suspend();
        return SYS_CALL_AGAIN;
    }
    _del_listener(s);
    return 0;
}

int sem_post(sem_t *s)
{
    int ret;

    if (!s)
        return -EINVAL;
    ret = _sem_post(s);
    if (ret < 0)
        return ret;
    if (ret > 0) {
        int i;
        irq_off();
        for(i = s->last+1; i < s->listeners; i++) {
            struct task *t = s->listener[i];
            if (t && task_is_live(t, s->listener_pid[i])) {
                task_resume_lock(t);
            }
            s->listener[i] = NULL;
            s->listener_pid[i] = 0;
        }
        for(i = 0; i <= s->last; i++) {
            struct task *t = s->listener[i];
            if (t && task_is_live(t, s->listener_pid[i])) {
                task_resume_lock(t);
            }
            s->listener[i] = NULL;
            s->listener_pid[i] = 0;
        }
        irq_on();
    }
    return 0;
}

int sem_destroy(sem_t *sem)
{
    kfree(sem);
    return 0;
}

int sem_init(sem_t *s, int val)
{
    int i;
    s->signature = SIGN_SEMAP;
    s->value = val;
    s->listeners = SEM_MAX_LISTENERS;
    s->last = -1;
    for (i = 0; i < s->listeners; i++)
        s->listener[i] = NULL;
    for (i = 0; i < s->listeners; i++)
        s->listener_pid[i] = 0;

    return 0;
}

/* Semaphore: Syscalls */
int sys_sem_init_hdlr(struct semaphore *s, int count)
{

    if (task_ptr_valid(s))
        return -EPERM;
    if (!s)
        return -EACCES;
    return sem_init(s, count);
}

int sys_sem_post_hdlr(struct semaphore *s)
{
    if (!s || task_ptr_valid(s))
        return -EACCES;
    if (s->signature != SIGN_SEMAP)
        return -EINVAL;
    return sem_post(s);
}

int sys_sem_wait_hdlr(struct semaphore *s, struct timespec *timeout)
{
    if (!s || task_ptr_valid(s))
        return -EACCES;

    if (timeout && task_ptr_valid(timeout))
        return -EACCES;

    if (s->signature != SIGN_SEMAP)
        return -EINVAL;

    return sem_wait(s, timeout);
}

int sys_sem_trywait_hdlr(struct semaphore *s)
{
    if (!s || task_ptr_valid(s))
        return -EACCES;
    if (s->signature != SIGN_SEMAP)
        return -EINVAL;
    return sem_trywait(s);
}

int sys_sem_destroy_hdlr(struct semaphore *s, int arg2, int arg3, int arg4, int arg5)
{
    if (!s || task_ptr_valid(s))
        return -EACCES;
    if (s->signature != SIGN_SEMAP)
        return -EINVAL;
    return sem_destroy(s);
}

int suspend_on_sem_wait(sem_t *s)
{
    int ret;
    if (!s)
        return -EINVAL;
    ret = _sem_wait(s);
    if (ret != 0) {
        _add_listener(s);
        return EAGAIN;
    }
    return 0;
}

/* Mutex: API */
mutex_t *mutex_init()
{
    int i;
    mutex_t *s;
    if (!mutex_pool_inited) {
        pool_init(&mutex_pool);
        mutex_pool_inited = 1;
    }
    s = pool_alloc(&mutex_pool);
    if (s) {
        s->signature = SIGN_MUTEX;
        s->value = 1; /* Unlocked. */
        s->listeners = SEM_MAX_LISTENERS;
        s->last = -1;
        for (i = 0; i < s->listeners; i++)
            s->listener[i] = NULL;
    }
    return s;
}

void mutex_destroy(mutex_t *s)
{
    pool_free(&mutex_pool, s);
}

static int mutex_spinlock(mutex_t *s)
{
    if (!s)
        return -EINVAL;
    while (_mutex_lock(s) != 0) {
        /* spin... */
    }
    return 0;
}

int mutex_trylock(mutex_t *s)
{
    if (!s)
        return -EINVAL;
    if(_mutex_lock(s) != 0)
        return -EAGAIN;
    return 0;
}

int mutex_lock(mutex_t *s)
{
    if (this_task() == NULL)
        return mutex_spinlock(s);
    if (!s)
        return -EINVAL;
    if(_mutex_lock(s) != 0) {
        _add_listener(s);
        task_suspend();
        return SYS_CALL_AGAIN;
    }
    _del_listener(s);
    return 0;
}

int mutex_unlock(mutex_t *s)
{
    if (!s)
        return -EINVAL;
    if (_mutex_unlock(s) == 0) {
        int i;
        for(i = s->last+1; i < s->listeners; i++) {
            struct task *t = s->listener[i];
            if (t) {
                task_resume_lock(t);
                s->listener[i] = NULL;
                return 0;
            }
        }
        for(i = 0; i <= s->last; i++) {
            struct task *t = s->listener[i];
            if (t) {
                task_resume_lock(t);
                s->listener[i] = NULL;
                return 0;
            }
        }
        return 0;
    }
    return -EAGAIN;
}

int suspend_on_mutex_lock(mutex_t *s)
{
    int ret;
    if (!s)
        return -EINVAL;
    ret = _mutex_lock(s);
    if (ret != 0) {
        _add_listener(s);
        return EAGAIN;
    }
    return 0;
}

/* Mutex: Syscalls (deprecated)*/
int sys_mutex_init_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}

int sys_mutex_lock_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}

int sys_mutex_unlock_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}

int sys_mutex_destroy_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
