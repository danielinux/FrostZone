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
 *      Authors: Daniele Lacamera, Maxime Vincent, Antonio Cardace, brabo
 *
 */
typedef void * _PTR;
#define _CONST const
#include "signal.h"
#include "kprintf.h"
#include "fpb.h"
#include "lowpower.h"
#include "syscalls.h"
#include "malloc.h"
#include "signal.h"
#include "poll.h"
#include "user.h"
#include "sys/wait.h"
#include "sys/fcntl.h"
#include "fpb.h"
#include "frosted.h"
#include "mempool.h"

/* Minimal libc */
#include "string.h"
#include "stdint.h"
#include "stddef.h"

/* generics */
volatile unsigned int rt_offset;
volatile int _syscall_retval;
extern int _clock_interval;
struct module *MODS;

#define __inl inline
#define __naked __attribute__((naked))


#ifndef NULL
#define NULL ((void *)0u)
#endif


/* Full kernel space separation */
#define RUN_HANDLER (0xfffffff1u)
#define MSP "msp"
#define PSP "psp"
#define RUN_KERNEL (0xffffffb8u)
#define RUN_USER (0xffffffbcu)

#define SV_CALL_SIGRETURN 0xFFFFFFF8
#define STACK_THRESHOLD 64

/* TOP to Bottom: EXTRA | NVIC | T_EXTRA | T_NVIC */
volatile struct extra_stack_frame *cur_extra;
volatile struct nvic_stack_frame *cur_nvic;
volatile struct extra_stack_frame *tramp_extra;
volatile struct nvic_stack_frame *tramp_nvic;
volatile struct extra_stack_frame *extra_usr;

int task_ptr_valid(const void *ptr);
static int task_ptr_valid_for_task(const void *ptr, const struct task *t);

#ifdef CONFIG_SYSCALL_TRACE
#define STRACE_SIZE 10
struct strace {
    int pid;
    int n;
    uint32_t sp;
};

volatile struct strace Strace[STRACE_SIZE];
volatile int StraceTop = 0;
#endif

#ifdef CONFIG_EXTENDED_MEMFAULT
static char _my_x_str[11] = "";
static char *x_str(uint32_t x)
{
    int i;
    uint8_t val;
    _my_x_str[0] = '0';
    _my_x_str[1] = 'x';
    for (i = 0; i < 8; i++) {
        val = (((x >> ((7 - i) << 2)) & 0x0000000F));
        if (val < 10)
            _my_x_str[i + 2] = val + '0';
        else
            _my_x_str[i + 2] = (val - 10) + 'A';
    }
    _my_x_str[10] = 0;
    return _my_x_str;
}

/* Global for printing segfault info */
static char _my_pid_str[6];
static char *pid_str(uint16_t p)
{
    int i = 0;
    if (p >= 10000) {
        _my_pid_str[i++] = (p / 10000) + '0';
        p = p % 10000;
    }
    if (i > 0 || p >= 1000) {
        _my_pid_str[i++] = (p / 1000) + '0';
        p = p % 1000;
    }
    if (i > 0 || p >= 100) {
        _my_pid_str[i++] = (p / 100) + '0';
        p = p % 100;
    }
    if (i > 0 || p >= 10) {
        _my_pid_str[i++] = (p / 10) + '0';
        p = p % 10;
    }
    _my_pid_str[i++] = p + '0';
    _my_pid_str[i] = 0;
    return _my_pid_str;
}
#endif

/* Array of syscalls */
static void *sys_syscall_handlers[_SYSCALLS_NR] = {

};

int sys_register_handler(uint32_t n, int (*_sys_c)(uint32_t arg1, uint32_t arg2,
                                                   uint32_t arg3, uint32_t arg4,
                                                   uint32_t arg5))
{
    if (n >= _SYSCALLS_NR)
        return -1; /* Attempting to register non-existing syscall */

    if (sys_syscall_handlers[n] != NULL)
        return -1; /* Syscall already registered */

    sys_syscall_handlers[n] = _sys_c;
    return 0;
}

#define MAX_TASKS 16
#define BASE_TIMESLICE (20)

//#define TIMESLICE(x) ((BASE_TIMESLICE) - ((x)->tb.nice >> 1))
#define TIMESLICE(x) BASE_TIMESLICE
#define INIT_SCHEDULER_STACK_SIZE (256)

struct __attribute__((packed)) nvic_stack_frame {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t psr;
#if (__CORTEX_M == 4) /* CORTEX-M4 saves FPU frame as well */
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t s12;
    uint32_t s13;
    uint32_t s14;
    uint32_t s15;
    uint32_t fpscr;
    uint32_t dummy;
#endif
};
/* In order to keep the code efficient, the stack layout of armv6 and armv7 do NOT match! */
struct __attribute__((packed)) extra_stack_frame {
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
};

#define NVIC_FRAME_SIZE ((sizeof(struct nvic_stack_frame)))
#define EXTRA_FRAME_SIZE ((sizeof(struct extra_stack_frame)))

static void *_top_stack;
#define TASK_FLAG_VFORK 0x01
#define TASK_FLAG_IN_SYSCALL 0x02
#define TASK_FLAG_SIGNALED 0x04
#define TASK_FLAG_INTR 0x40
#define TASK_FLAG_SYSCALL_STOP 0x80
/* thread related */
#define TASK_FLAG_DETACHED 0x0100
#define TASK_FLAG_CANCELABLE 0x0200
#define TASK_FLAG_PENDING_CANC 0x0400
/* Timer expired */
#define TASK_FLAG_TIMEOUT 0x0800

struct filedesc {
    struct fnode *fno;
    uint32_t off;
    uint32_t mask;
    uint32_t flags;
};

struct filedesc_table {
    uint32_t n_files;
    uint32_t usage_count;
    struct filedesc *fdesc;
};

struct task_handler {
    int signo;
    void (*hdlr)(int);
    uint32_t mask;
    struct task_handler *next;
};

#ifdef CONFIG_PTHREADS
struct thread_group {
    struct task **threads;
    uint16_t active_threads;
    uint16_t n_threads;
    uint16_t max_tid;
    pthread_key_t max_key;
};
#else
struct thread_group {
    uint32_t _off;
};
#endif
static void destroy_thread_group(struct thread_group *group, uint16_t tid);


struct __attribute__((packed)) task_block {
    /* Watch out for alignment here
     * (try to pack togehter smaller fields)
     * */
    void (*start)(void *);
    void *arg;

    uint16_t flags;
    uint8_t state;
    int8_t nice;

    uint16_t timeslice;
    uint16_t pid;

    uint16_t ppid;
    uint16_t tid;
    uint16_t joiner_thread_tid;
    uint16_t _padding;
    struct thread_group *tgroup;
    struct task *tracer;
    int exitval;
    struct fnode *cwd;
    struct task_handler *sighdlr;
    sigset_t sigmask;
    sigset_t sigpend;
    struct filedesc_table *filedesc_table;
    void *sp;
    void *osp;
    void *cur_stack;
    struct task *next;
    struct task_exec_info exec_info;
    int timer_id;
    uint32_t *specifics;
    uint32_t n_specifics;
};

struct __attribute__((packed)) task {
    uint32_t *stack;
    struct task_block tb;
};

static struct task struct_task_kernel;
static struct task *const kernel = (struct task *)(&struct_task_kernel);

struct task * const get_kernel(void)
{
    return kernel;
}

static int number_of_tasks = 0;

static void tasklist_add(struct task **list, struct task *el)
{
    el->tb.next = *list;
    *list = el;
}

static int tasklist_del(struct task **list, struct task *togo)
{
    struct task *t = *list;
    struct task *p = NULL;

    while (t) {
        if (t == togo) {
            if (p == NULL)
                *list = t->tb.next;
            else
                p->tb.next = t->tb.next;
            return 0;
        }
        p = t;
        t = t->tb.next;
    }
    return -1;
}

static int tasklist_len(struct task **list)
{
    struct task *t = *list;
    struct task *p = NULL;
    int len = 0;
    while (t) {
        len++;
        t = t->tb.next;
    }
    return len;
}

static struct task *tasklist_get(struct task **list, uint16_t pid)
{
    struct task *t = *list;
    while (t) {
        if ((t->tb.pid == pid) && (t->tb.tid == 1))
            return t;
        t = t->tb.next;
    }
    return NULL;
}

static struct task *tasks_running = NULL;
static struct task *tasks_idling = NULL;
void task_resume(struct task *t);
void task_resume_lock(struct task *t);
void task_stop(struct task *t);
void task_continue(struct task *t);
void task_terminate(struct task *t);
static void task_suspend_to(int newstate);
void task_deliver_sigchld(void *arg);
void task_deliver_sigtrap(void *arg);

static void ftable_destroy(struct task *t);
static void idling_to_running(struct task *t)
{
    if (tasklist_del(&tasks_idling, t) == 0)
        tasklist_add(&tasks_running, t);
}

static void running_to_idling(struct task *t)
{
    if ((t->tb.pid < 1) && (t->tb.tid <= 1))
        return;
    if (tasklist_del(&tasks_running, t) == 0)
        tasklist_add(&tasks_idling, t);
}

static int task_filedesc_del_from_task(struct task *t, int fd);


/* Catch-all destroy functions for processes and threads.
 *
 * Single point of deallocation for all the memory related
 * to task management.
 */
static void task_destroy(void *arg)
{
    struct task *t = arg;
    int i;
    struct filedesc_table *ft;
    struct thread_group *grp;
    if (!t)
        return;
    tasklist_del(&tasks_running, t);
    tasklist_del(&tasks_idling, t);
#ifdef CONFIG_PTHREADS
    grp = t->tb.tgroup;
    if ((grp) && (grp->active_threads > 0)) {
        /* if single sub-thread being destroyed, delete from
         * the group->threads array, so the position can
         * be reused.
         *
         * We never get here after a destroy_thread_group()
         * that has been called by the leader, because t->group
         * has been set to NULL.
         */
        for (i = 0; i < grp->n_threads; i++) {
            if (grp->threads[i] == t) {
                grp->threads[i] = NULL;
            }
        }
    } else {
        /* Last (or the one) thread in group being destroyed. Free resources. */
        if (grp)
            destroy_thread_group(grp, t->tb.tid);
        /* Get rid of allocated arguments */
        if (t->tb.arg) {
            char **arg = (char **)(t->tb.arg);
            i = 0;
            while (arg[i]) {
                kfree(arg[i]);
                i++;
            }
        }
        kfree(t->tb.arg);
        /* Close files & Destroy the file table */
        ft = t->tb.filedesc_table;
        for (i = 0; (ft) && (i < ft->n_files); i++) {
            task_filedesc_del_from_task(t, i);
        }
        ftable_destroy(t);
        /* free pre-allocated thread space */
        if (grp) {
            kfree(grp->threads);
            kfree(grp);
            t->tb.tgroup = NULL;
        }
        /* Remove all heap allocations, stack, .data and .bss
         * associated to this pid.
         */
        secure_munmap_task(t->tb.pid);
    }
    /* Free any pthread-specific key value */
    if (t->tb.specifics)
        kfree(t->tb.specifics);
#else
    /* Get rid of allocated arguments */
    if (t->tb.arg) {
        char **arg = (char **)(t->tb.arg);
        i = 0;
        while (arg[i]) {
            kfree(arg[i]);
            i++;
        }
    }
    kfree(t->tb.arg);
    /* Close files & Destroy the file table */
    ft = t->tb.filedesc_table;
    for (i = 0; (ft) && (i < ft->n_files); i++) {
        task_filedesc_del_from_task(t, i);
    }
    ftable_destroy(t);
    /* Remove heap allocations spawned by this pid. */
    secure_munmap_task(t->tb.pid);
#endif
    /* Get rid of stack space allocation, timer. */
    if (t->tb.timer_id >= 0)
        ktimer_del(t->tb.timer_id);
    task_space_free(t);
    number_of_tasks--;
}

static struct task *_cur_task = NULL;
static struct task *forced_task = NULL;

static __inl int in_kernel(void)
{
    return ((_cur_task->tb.pid == 0) && (_cur_task->tb.tid <= 1));
}

struct task *this_task(void)
{
    /* External modules like locks.c expect this to
     * return NULL when in kernel
     */
    if (in_kernel())
        return NULL;
    return _cur_task;
}

int task_get_timer_id(void)
{
    return _cur_task->tb.timer_id;
}

void task_set_timer_id(int id)
{
    _cur_task->tb.timer_id = id;
}

int task_in_syscall(void)
{
    return ((_cur_task->tb.flags & TASK_FLAG_IN_SYSCALL) ==
            TASK_FLAG_IN_SYSCALL);
}

static int next_pid(void)
{
    static unsigned int next_available = 0;
    uint16_t ret = (uint16_t)((next_available)&0xFFFF);
    next_available++;
    if (next_available > 0xFFFF) {
        next_available = 2;
    }
    while (tasklist_get(&tasks_idling, next_available) ||
           tasklist_get(&tasks_running, next_available))
        next_available++;
    return ret;
}

/********************************/
/* Handling of file descriptors */
/********************************/
/********************************/
/********************************/
/**/
/**/
/**/

static struct filedesc_table *ftable_create(struct task *t)
{
    struct filedesc_table *ft = t->tb.filedesc_table;
    if (ft)
        return ft;
    ft = kcalloc(sizeof(struct filedesc_table), 1);
    if (!ft)
        return NULL;
    ft->usage_count = 1;
    t->tb.filedesc_table = ft;
    return ft;
}

static void ftable_destroy(struct task *t)
{
    struct filedesc_table *ft = t->tb.filedesc_table;
    if (ft) {
        if (--ft->usage_count == 0) {
            if (ft->fdesc)
                kfree(ft->fdesc);
            kfree(ft);
        }
    }
    t->tb.filedesc_table = NULL;
}

static int task_filedesc_add_to_task(struct task *t, struct fnode *f)
{
    int i;
    void *re;
    struct filedesc_table *ft = t->tb.filedesc_table;
    if (!t || !f)
        return -EINVAL;

    if (!ft)
        ft = ftable_create(t);

    for (i = 0; i < ft->n_files; i++) {
        if (ft->fdesc[i].fno == NULL) {
            f->usage_count++;
            ft->fdesc[i].fno = f;
            return i;
        }
    }
    ft->n_files++;
    re = (void *)krealloc(ft->fdesc, ft->n_files * sizeof(struct filedesc));
    if (!re)
        return -1;
    ft->fdesc = re;
    memset(&(ft->fdesc[ft->n_files - 1]), 0, sizeof(struct filedesc));
    ft->fdesc[ft->n_files - 1].fno = f;
    if (f->flags & FL_TTY) {
        struct module *mod = f->owner;
        if (mod && mod->ops.tty_attach) {
            mod->ops.tty_attach(f, t->tb.pid);
        }
    }
    f->usage_count++;
    return ft->n_files - 1;
}

int task_filedesc_add(struct fnode *f)
{
    return task_filedesc_add_to_task(_cur_task, f);
}

static int task_filedesc_del_from_task(struct task *t, int fd)
{
    struct fnode *fno;
    struct filedesc_table *ft;
    if (!t)
        return -EINVAL;

    ft = t->tb.filedesc_table;
    fno = ft->fdesc[fd].fno;
    if (!fno)
        return -ENOENT;

    /* Reattach controlling tty to parent task */
    if ((fno->flags & FL_TTY) && ((ft->fdesc[fd].mask & O_NOCTTY) == 0)) {
        struct module *mod = fno->owner;
        if (mod && mod->ops.tty_attach) {
            mod->ops.tty_attach(fno, t->tb.ppid);
        }
    }

    /* If this was the last user of the file, close it. */
    fno->usage_count--;
    if (fno->usage_count <= 0) {
        if (fno->owner && fno->owner->ops.close)
            fno->owner->ops.close(fno);
    }
    ft->fdesc[fd].fno = NULL;
}

int task_filedesc_del(int fd)
{
    return task_filedesc_del_from_task(_cur_task, fd);
}

int task_fd_setmask(int fd, uint32_t mask)
{
    struct filedesc_table *ft;
    struct fnode *fno;

    ft = _cur_task->tb.filedesc_table;

    if (!ft)
        return -EINVAL;

    if (fd < 0 || fd > ft->n_files)
        return -ENOENT;

    fno = ft->fdesc[fd].fno;
    if (!fno)
        return -ENOENT;

    if ((mask & O_ACCMODE) != O_RDONLY) {
        if ((fno->flags & FL_WRONLY) == 0)
            return -EPERM;
    }
    ft->fdesc[fd].mask = mask;
    return 0;
}

uint32_t task_fd_getmask(int fd)
{
    struct filedesc_table *ft;
    ft = _cur_task->tb.filedesc_table;
    if (fd < 0 || fd > ft->n_files)
        return 0;
    if (ft->fdesc[fd].fno)
        return ft->fdesc[fd].mask;
    return 0;
}

uint32_t task_fd_set_flags(int fd, uint32_t flags)
{
    struct filedesc_table *ft;
    struct fnode *fno;

    ft = _cur_task->tb.filedesc_table;

    if (!ft)
        return -EINVAL;

    if (fd < 0 || fd > ft->n_files)
        return -ENOENT;

    fno = ft->fdesc[fd].fno;
    if (!fno)
        return -ENOENT;

    ft->fdesc[fd].flags = flags;
    return 0;
}

uint32_t task_fd_get_flags(int fd)
{
    struct fnode *fno;
    struct filedesc_table *ft;
    struct filedesc *fdesc;
    ft = _cur_task->tb.filedesc_table;
    if (!ft)
        return -EINVAL;

    if (fd < 0 || fd > ft->n_files)
        return -ENOENT;

    fdesc = &ft->fdesc[fd];
    if (!fdesc)
        return -ENOENT;
    return fdesc->flags;
}

uint32_t task_fd_set_off(struct fnode *fno, uint32_t off)
{
    struct filedesc_table *ft;
    struct filedesc *fdesc;
    int fd;
    int found = 0;
    ft = _cur_task->tb.filedesc_table;
    if (!ft)
        return 0;

    /* Set offset to all instances of the file in the ft */
    for (fd = 0; fd < ft->n_files; fd++) {
        if (ft->fdesc[fd].fno == fno) {
            ft->fdesc[fd].off = off;
            found++;
        }
    }
    if (found)
        return off;
    return 0;
}

uint32_t task_fd_get_off(struct fnode *fno)
{
    struct filedesc_table *ft;
    struct filedesc *fdesc;
    int fd;
    ft = _cur_task->tb.filedesc_table;
    if (!ft)
        return 0;

    for (fd = 0; fd < ft->n_files; fd++) {
        if (ft->fdesc[fd].fno == fno)
            return ft->fdesc[fd].off;
    }
    return 0;
}

struct fnode *task_filedesc_get(int fd)
{
    struct task *t;
    struct filedesc_table *ft;

    if (fd < 0)
        return NULL;

    t = _cur_task;
    if (!t)
        return NULL;

    ft = t->tb.filedesc_table;
    if (!ft)
        return NULL;

    if (fd >= ft->n_files)
        return NULL;

    if ((ft->n_files - 1) < fd)
        return NULL;

    if (ft->fdesc[fd].fno == NULL)
        return NULL;

    return ft->fdesc[fd].fno;
}

int task_fd_readable(int fd)
{
    if (!task_filedesc_get(fd))
        return 0;
    return 1;
}

int task_fd_writable(int fd)
{
    if (!task_filedesc_get(fd))
        return 0;
    if ((_cur_task->tb.filedesc_table->fdesc[fd].mask & O_ACCMODE) == O_RDONLY)
        return 0;
    return 1;
}

int sys_dup_hdlr(int fd)
{
    struct task *t = _cur_task;
    struct fnode *f = task_filedesc_get(fd);
    int newfd = -1;
    if (!f)
        return -1;
    newfd = task_filedesc_add(f);
    if (newfd >= 0)
        _cur_task->tb.filedesc_table->fdesc[newfd].mask =
            _cur_task->tb.filedesc_table->fdesc[fd].mask;
    return newfd;
}

int sys_dup2_hdlr(int fd, int newfd)
{
    struct task *t = _cur_task;
    struct fnode *f = task_filedesc_get(fd);
    struct filedesc_table *ft = t->tb.filedesc_table;

    if (!ft)
        return -1;
    if (newfd < 0)
        return -1;
    if (newfd == fd)
        return -1;
    if (!f)
        return -1;

    /* TODO: create empty fnodes up until newfd */
    if (newfd >= ft->n_files)
        return -1;
    if (ft->fdesc[newfd].fno != NULL)
        task_filedesc_del(newfd);
    ft->fdesc[newfd].fno = f;
    return newfd;
}

/********************************/
/*            Signals           */
/********************************/
/********************************/
/********************************/
/**/
/**/
/**/

#ifdef CONFIG_SIGNALS

static void sig_trampoline(struct task *t, struct task_handler *h, int signo);
int sys_kill_hdlr(uint32_t pid, uint32_t sig);
static int catch_signal(struct task *t, int signo, sigset_t orig_mask)
{
    int i;
    struct task_handler *sighdlr;
    struct task_handler *h = NULL;

    if (!t || (t->tb.pid < 1))
        return -EINVAL;

    if ((t->tb.state == TASK_ZOMBIE) || (t->tb.state == TASK_OVER) ||
        (t->tb.state == TASK_FORKED))
        return -ESRCH;

    if (((1 << signo) & t->tb.sigmask) && (h->hdlr != SIG_IGN)) {
        /* Signal is blocked via t->tb.sigmask */
        t->tb.sigpend |= (1 << signo);
        return 0;
    }

    /* If process is being traced, deliver SIGTRAP to tracer */
    if (t->tb.tracer != NULL) {
        tasklet_add(task_deliver_sigtrap, t->tb.tracer);
    }

    /* Reset signal, if pending, as it's going to be handled. */
    t->tb.sigpend &= ~(1 << signo);

    sighdlr = t->tb.sighdlr;
    while (sighdlr) {
        if (signo == sighdlr->signo)
            h = sighdlr;
        sighdlr = sighdlr->next;
    }

    if ((h) && (signo != SIGKILL) && (signo != SIGSEGV)) {
        /* Handler is present */
        if ((h->hdlr == NULL) || (h->hdlr == SIG_IGN))
            return 0;

        if (_cur_task == t) {
            h->hdlr(signo);
        } else {
            sig_trampoline(t, h, signo);
        }
    } else {
        /* Handler not present: SIG_DFL */
        if (signo == SIGSTOP) {
            task_stop(t);
        } else if (signo == SIGCHLD) {
            task_resume(t);
        } else if (signo == SIGCONT) {
            /* If not in stopped state, SIGCONT is ignored. */
            task_continue(t);
        } else {
            task_terminate(t);
        }
    }
    return 0;
}

static void check_pending_signals(struct task *t)
{
    int i;
    t->tb.sigpend &= ~(t->tb.sigmask);
    while (t->tb.sigpend != 0u) {
        for (i = 1; i < SIGMAX; i++) {
            if ((1 << i) & t->tb.sigpend)
                catch_signal(t, i, t->tb.sigmask);
        }
    }
}

static int add_handler(struct task *t, int signo, void (*hdlr)(int),
                       uint32_t mask)
{
    struct task_handler *sighdlr;
    if (!t || (t->tb.pid < 1))
        return -EINVAL;

    sighdlr = kalloc(sizeof(struct task_handler));
    if (!sighdlr)
        return -ENOMEM;

    sighdlr->signo = signo;
    sighdlr->hdlr = hdlr;
    sighdlr->mask = mask;
    sighdlr->next = t->tb.sighdlr;
    t->tb.sighdlr = sighdlr;
    check_pending_signals(t);
    return 0;
}

static int del_handler(struct task *t, int signo)
{
    struct task_handler *sighdlr;
    struct task_handler *prev = NULL;
    if (!t || (t->tb.pid < 1))
        return -EINVAL;

    sighdlr = t->tb.sighdlr;
    while (sighdlr) {
        if (sighdlr->signo == signo) {
            if (prev == NULL) {
                t->tb.sighdlr = sighdlr->next;
            } else {
                prev->next = sighdlr->next;
            }
            kfree(sighdlr);
            check_pending_signals(t);
            return 0;
        }
        prev = sighdlr;
        sighdlr = sighdlr->next;
    }
    return -ESRCH;
}

static void sig_hdlr_return(uint32_t arg)
{
    /* XXX: In order to use per-sigaction sa_mask, we need to set
     * t->tb.sigmask in the catch, and restore it here.
     */

    /* call special svc with n = SV_CALL_SIGRETURN */
    asm volatile("mov r0, %0" ::"r"(SV_CALL_SIGRETURN));
    asm volatile("svc 0\n");
    // asm volatile ("pop {r4-r11}\n");
}

static void sig_trampoline(struct task *t, struct task_handler *h, int signo)
{
    cur_nvic = t->tb.sp + EXTRA_FRAME_SIZE;
    cur_extra = t->tb.sp;
    tramp_nvic = t->tb.sp - NVIC_FRAME_SIZE;
    tramp_extra = t->tb.sp - EXTRA_FRAME_SIZE;
    tramp_extra = t->tb.sp - (EXTRA_FRAME_SIZE + NVIC_FRAME_SIZE);
    extra_usr = t->tb.sp;

    /* Save stack pointer for later */
    memcpy((void *)t->tb.sp, (void *)cur_extra, EXTRA_FRAME_SIZE);
    t->tb.osp = t->tb.sp;

    /* Copy the EXTRA_FRAME into the trampoline extra, to preserve R9 for
     * userspace relocations etc. */
    memcpy((void *)tramp_extra, (void *)cur_extra, EXTRA_FRAME_SIZE);
    memset((void *)tramp_nvic, 0, NVIC_FRAME_SIZE);
    tramp_nvic->pc = (uint32_t)h->hdlr | 1; /* enforce thumb */
    tramp_nvic->lr = (uint32_t)sig_hdlr_return;
    tramp_nvic->r0 = (uint32_t)signo;
    tramp_nvic->psr = cur_nvic->psr | (1 << 24); /* enforce T bit */
    t->tb.sp = (t->tb.osp - (EXTRA_FRAME_SIZE + NVIC_FRAME_SIZE));
    t->tb.flags |= TASK_FLAG_SIGNALED;
    task_resume(t);
}

#else
#define check_pending_signals(...) do{}while(0)
#define add_handler(...) (0)
#define del_handler(...) (0)
#define sig_hdlr_return NULL

static int catch_signal(struct task *t, int signo, sigset_t orig_mask)
{
    (void)orig_mask;
    if (signo != SIGCHLD)
        task_terminate(t);
    return 0;
}
#endif

void task_resume(struct task *t);
void task_resume_lock(struct task *t);
void task_stop(struct task *t);
void task_continue(struct task *t);

int sys_sigaction_hdlr(int signum, struct sigaction *sa, struct sigaction *sa_old, int arg4, int arg5)
{
    struct task_handler *sighdlr;

    if (sa && task_ptr_valid(sa))
        return -EACCES;
    if (sa_old && task_ptr_valid(sa_old))
        return -EACCES;

    if (_cur_task->tb.pid < 1)
        return -EINVAL;

    if (signum >= SIGMAX || signum < 1)
        return -EINVAL;

    /* Populating sa_old */
    if (sa_old) {
        sighdlr = _cur_task->tb.sighdlr;
        while (sighdlr) {
            if (signum == sighdlr->signo)
                break;
            sighdlr = sighdlr->next;
        }
        if (sighdlr) {
            sa_old->sa_mask = (sigset_t)sighdlr->mask;
            sa_old->sa_handler = sighdlr->hdlr;
        } else {
            sa_old->sa_mask = (sigset_t)0u;
            sa_old->sa_handler = SIG_DFL;
        }
        sa_old->sa_flags = 0;
        sa_old->sa_restorer = NULL;
    }
    if (sa)
        add_handler(_cur_task, signum, sa->sa_handler, sa->sa_mask);
    return 0;
}

int sys_sigprocmask_hdlr(int how, const sigset_t *set, sigset_t *oldset)
{
    if (set &&
        ((how != SIG_SETMASK) && (how != SIG_BLOCK) && (how != SIG_UNBLOCK)))
        return -EINVAL;

    if (!set && !oldset)
        return -EINVAL;

    if (oldset) {
        if (task_ptr_valid(oldset))
            return -EACCES;
        *oldset = _cur_task->tb.sigmask;
    }

    if (set) {
        if (task_ptr_valid(set))
            return -EACCES;
        if (how == SIG_SETMASK)
            _cur_task->tb.sigmask = *set;
        else if (how == SIG_BLOCK)
            _cur_task->tb.sigmask |= *set;
        else
            _cur_task->tb.sigmask &= ~(*set);
        check_pending_signals(_cur_task);
    }
    return 0;
}

int sys_sigsuspend_hdlr(const sigset_t *mask)
{
    uint32_t orig_mask = _cur_task->tb.sigmask;
    if (!mask)
        return -EINVAL;
    if (task_ptr_valid(mask))
        return -EACCES;

    _cur_task->tb.sigmask = ~(*mask);
    task_suspend();
    _cur_task->tb.sigmask = orig_mask;

    /* Success. */
    return -EINTR;
}

/********************************/
/*           working dir        */
/********************************/
/********************************/
/********************************/
/**/
/**/
/**/
struct fnode *task_getcwd(void)
{
    return _cur_task->tb.cwd;
}
void task_chdir(struct fnode *f)
{
    _cur_task->tb.cwd = f;
}
static __inl void *msp_read(void)
{
    void *ret = NULL;
    asm volatile("mrs %0, msp" : "=r"(ret));
    return ret;
}

static __inl void *psp_read(void)
{
    void *ret = NULL;
    asm volatile("mrs %0, psp" : "=r"(ret));
    return ret;
}

int scheduler_ntasks(void)
{
    return number_of_tasks;
}
int scheduler_task_state(int pid)
{
    struct task *t = tasklist_get(&tasks_running, pid);
    if (!t)
        t = tasklist_get(&tasks_idling, pid);
    if (t)
        return t->tb.state;
    else
        return TASK_OVER;
}

int scheduler_can_sleep(void)
{
    if (tasklist_len(&tasks_running) == 1)
        return 1;
    else
        return 0;
}

unsigned scheduler_stack_used(int pid)
{
    struct task *t = tasklist_get(&tasks_running, pid);
    if (!t)
        t = tasklist_get(&tasks_idling, pid);
    if (t)
        return SCHEDULER_STACK_SIZE -
               ((char *)t->tb.sp - (char *)t->tb.cur_stack);
    else
        return 0;
}

char *scheduler_task_name(int pid)
{
    struct task *t = tasklist_get(&tasks_running, pid);
    if (!t)
        t = tasklist_get(&tasks_idling, pid);
    if (t) {
        char **argv = t->tb.arg;
        if (argv)
            return argv[0];
    } else
        return NULL;
}

static uint16_t scheduler_get_cur_pid(void)
{
    if (!_cur_task)
        return 0;
    return _cur_task->tb.pid;
}

uint16_t this_task_getpid(void)
{
    return scheduler_get_cur_pid();
}
int task_running(void)
{
    return (_cur_task->tb.state == TASK_RUNNING);
}
int task_timeslice(void)
{
    return (--_cur_task->tb.timeslice);
}
void task_end(void)
{
    /* We have to set the stack pointer because we jumped here
     * after setting lr to task_end into the NVIC_FRAME and there
     * we were using the sp of the task's parent */
    asm volatile("msr " PSP ", %0" ::"r"(_cur_task->tb.sp));
    /* here we need to be in a irqoff context
       because we are dealing with the scheduler
       data structures, otherwise we could produce dead code
       after callling running_to_idling */
    irq_off();
    running_to_idling(_cur_task);
    _cur_task->tb.state = TASK_ZOMBIE;
    asm volatile("mov %0, r0" : "=r"(_cur_task->tb.exitval));
    irq_on();
    while (1) {
        task_suspend_to(TASK_ZOMBIE);
    }
}

/********************************/
/*         Task creation        */
/***      vfork() / exec()    ***/
/********************************/
/********************************/
/**/
/**/
/**/

static void task_resume_vfork(struct task *t);

/* Duplicate exec() args into the new process address space.
*/
static void *task_pass_args(void *_args, uint16_t pid, uint8_t **sp)
{
    char **args = (char **)_args;
    char **new = NULL;
    int i = 0, n = 0;
    uintptr_t *ptr;
    if (!_args)
        return NULL;
    while (args[n] != NULL) {
        n++;
    }
    if (n == 0)
        return NULL;
    /* Allocate space for pointers to each argument */
    *sp -= (sizeof(uintptr_t) * (n + 1));
    ptr = (uintptr_t *)(*sp);
    /* Push the terminator for argv[] */
    ptr[n] = 0;
    for (i = 0; i < n; i++) {
        uint32_t l = strlen(args[i]);
        if (l > 0) {
            uint32_t off;
            /* Push the argument string in the process' stack */
            *sp -= (l + 1);
            off = ((uintptr_t)(*sp)) % sizeof(uintptr_t);
            *sp -= off;
            memcpy(*sp, args[i], l + 1);
            /* Save the pointer in the table on top of the stack */
            ptr[i] = *sp;
        }
    }
    return ptr;
}

static void task_create_real(struct task *new, void *arg, unsigned int nice)
{
    struct nvic_stack_frame *nvic_frame;
    struct extra_stack_frame *extra_frame;
    uint8_t *sp;

    if (nice < NICE_RT)
        nice = NICE_RT;

    if (nice > NICE_MAX)
        nice = NICE_MAX;

    new->tb.start = new->tb.exec_info.init;
    new->tb.timeslice = TIMESLICE(new);
    new->tb.state = TASK_RUNNABLE;
    new->tb.sighdlr = NULL;
    new->tb.sigpend = 0;
    new->tb.sigmask = 0;
    new->tb.tracer = NULL;
    new->tb.timer_id = -1;
    new->tb.specifics = NULL;
    new->tb.n_specifics = 0;

    if ((new->tb.flags &TASK_FLAG_VFORK) != 0) {
        struct task *pt = tasklist_get(&tasks_idling, new->tb.ppid);
        if (!pt)
            pt = tasklist_get(&tasks_running, new->tb.ppid);
        if (pt) {
            uint32_t stack_size = SCHEDULER_STACK_SIZE;
            /* Restore parent's stack and put it back in the schedule */
            memcpy(pt->stack, new->stack, SCHEDULER_STACK_SIZE);
            secure_swap_stack(pt->tb.pid, new->tb.pid);
            task_resume_vfork(pt);
        }
        new->tb.flags &= (~TASK_FLAG_VFORK);
    } else {
        new->stack = secure_mmap_stack(SCHEDULER_STACK_SIZE, new->tb.pid);
    }
    if (!new->stack)
        return;

    /* Base/Top of the stack memory */
    new->tb.cur_stack = new->stack;
    sp = (((uint8_t *)(new->stack)) + SCHEDULER_STACK_SIZE - 32);

    /* Push the arguments at the top */
    new->tb.arg = task_pass_args(arg, new->tb.pid, &sp);

    /* Push the NVIC stack frame */
    sp -= NVIC_FRAME_SIZE;
    nvic_frame = (struct nvic_stack_frame *)sp;
    memset(nvic_frame, 0, NVIC_FRAME_SIZE);
    nvic_frame->r0 = (uint32_t) new->tb.arg;
    nvic_frame->pc = (uint32_t) new->tb.start;
    nvic_frame->lr = (uint32_t)task_end;
    nvic_frame->psr = 0x01000000u;

    /* Push the extra frame */
    sp -= EXTRA_FRAME_SIZE;
    extra_frame = (struct extra_stack_frame *)sp;
    extra_frame->r9 = (uint32_t)new->tb.exec_info.got_loc;
    new->tb.sp = (uint32_t *)sp;
    asm volatile ("dsb");
    asm volatile ("isb");
}

int task_create(struct task_exec_info *exec_info, void *arg, unsigned int nice)
{
    struct task *new;
    int i;
    struct filedesc_table *ft;

    new = task_space_alloc();
    if (!new) {
        return -ENOMEM;
    }
    memset(&new->tb, 0, sizeof(struct task_block));
    new->tb.pid = next_pid();
    new->tb.tid = 1;
    new->tb.tgroup = NULL;

    new->tb.ppid = scheduler_get_cur_pid();
    new->tb.nice = nice;
    new->tb.filedesc_table = NULL;
    new->tb.flags = 0;
    new->tb.cwd = fno_search("/");
    new->tb.tracer = NULL;

    ft = _cur_task->tb.filedesc_table;

    /* Inherit cwd, file descriptors from parent */
    if (new->tb.ppid > 1) { /* Start from parent #2 */
        new->tb.cwd = task_getcwd();
        for (i = 0; (ft) && (i < ft->n_files); i++) {
            task_filedesc_add_to_task(new, ft->fdesc[i].fno);
            new->tb.filedesc_table->fdesc[i].mask = ft->fdesc[i].mask;
        }
    }

    new->tb.next = NULL;
    tasklist_add(&tasks_running, new);

    number_of_tasks++;
    memcpy(&new->tb.exec_info, exec_info, sizeof(struct task_exec_info));
    secure_mempool_chown(new->tb.exec_info.mmap_base, new->tb.pid, 0);
    task_create_real(new, arg, nice);
    new->tb.state = TASK_RUNNABLE;
    return new->tb.pid;
}

int scheduler_exec(struct task_exec_info *info, void *args)
{
    struct task *t = _cur_task;
    memcpy(&t->tb.exec_info, info, sizeof(struct task_exec_info));
    secure_mempool_chown(t->tb.exec_info.mmap_base, t->tb.pid, 0);
    task_create_real(t, (void *)args, t->tb.nice);
    asm volatile("msr " PSP ", %0" ::"r"(_cur_task->tb.sp));
    t->tb.state = TASK_RUNNING;
    mpu_task_on(_cur_task->tb.pid);
    return 0;
}

int sys_vfork_hdlr(void)
{
    struct task *new;
    int i;
    uint32_t vpid;
    struct filedesc_table *ft = _cur_task->tb.filedesc_table;

    if (_cur_task->tb.tid != 1) {
        /* Prohibit vfork() from a thread */
        return -ENOSYS;
    }

    new = task_space_alloc();
    if (!new) {
        return -ENOMEM;
    }
    memset(&new->tb, 0, sizeof(struct task_block));
    vpid = next_pid();
    new->tb.pid = vpid;
    new->tb.tid = 1;
    new->tb.tgroup = NULL;
    new->tb.ppid = scheduler_get_cur_pid();
    new->tb.nice = _cur_task->tb.nice;
    new->tb.filedesc_table = NULL;
    new->tb.arg = NULL;
    memcpy(&new->tb.exec_info, &_cur_task->tb.exec_info, sizeof(struct task_exec_info));
    new->tb.flags = TASK_FLAG_VFORK;
    new->tb.cwd = task_getcwd();
    new->tb.timer_id = -1;
    new->tb.specifics = NULL;
    new->tb.n_specifics = 0;

    /* Inherit cwd, file descriptors from parent */
    if (new->tb.ppid > 1) { /* Start from parent #2 */
        for (i = 0; (ft) && (i < ft->n_files); i++) {
            task_filedesc_add_to_task(new, ft->fdesc[i].fno);
            new->tb.filedesc_table->fdesc[i].mask = ft->fdesc[i].mask;
        }
        /* Inherit signal mask */
        new->tb.sigmask = _cur_task->tb.sigmask;
    }
    

    new->tb.next = NULL;
    tasklist_add(&tasks_running, new);
    number_of_tasks++;

    /* Set parent's vfork retval by writing on stacked r0 */
    *((uint32_t *)(_cur_task->tb.sp + EXTRA_FRAME_SIZE)) = vpid;
    
    /* Create a new stack space for the process, but 
     * attach it to the parent's stack temporarily, until
     * we're done exec'ing or exiting
     */
    new->stack = secure_mmap_stack(SCHEDULER_STACK_SIZE, new->tb.pid);
    memcpy(new->stack, _cur_task->stack, SCHEDULER_STACK_SIZE);
    new->tb.sp = _cur_task->tb.sp;


    if (new != _cur_task) {
        /* Swap the stack spaces */
        secure_swap_stack(new->tb.ppid, new->tb.pid);
        new->tb.cur_stack = _cur_task->tb.cur_stack;
        new->tb.state = TASK_RUNNABLE;
    }
    /* Vfork: Caller task suspends until child calls exec or exits */
    asm volatile("msr " PSP ", %0" ::"r"(new->tb.sp));
    task_suspend_to(TASK_FORKED);

    return vpid;
}
/********************************/
/*         POSIX threads        */
/********************************/
/********************************/
/********************************/
/**/
/**/
/**/

#ifdef CONFIG_PTHREADS

static struct task *pthread_get_task(int pid, int tid)
{
    struct task *t = NULL;
    struct task *leader = NULL;
    struct thread_group *group = NULL;
    int i;

    leader = tasklist_get(&tasks_running, pid);
    if (!leader)
        leader = tasklist_get(&tasks_idling, pid);
    if (!leader)
        return NULL;

    if (tid == 1)
        return leader;

    group = leader->tb.tgroup;

    if (!group || !group->threads || group->n_threads < 2)
        return NULL;

    for (i = 0; i < group->n_threads; i++) {
        t = group->threads[i];
        if (t->tb.tid == tid) {
            if (t->tb.state == TASK_OVER)
                return NULL;
            return t;
        }
    }
    return NULL;
}

static int pthread_add(struct task *cur, struct task *new)
{
    int i;
    struct thread_group *group = cur->tb.tgroup;
    struct task **old_tgroup;

    if (!group) {
        group = kcalloc(sizeof(struct thread_group), 1);
        if (!group)
            return -ENOMEM;
        group->threads = kcalloc(sizeof(struct task *), 2);
        if (!group->threads) {
            kfree(group);
            return -ENOMEM;
        }
        cur->tb.tgroup = group;
        new->tb.tgroup = group;
        group->threads[0] = cur;
        group->threads[1] = new;
        group->n_threads = 2;
        group->max_tid = 2;
        group->active_threads = 2;
        return 2;
    }
    for (i = 0; i < group->n_threads; i++) {
        if (group->threads[i] == NULL) {
            group->threads[i] = new;
            new->tb.tgroup = group;
            new->tb.tid = ++group->max_tid;
            group->active_threads++;
            return new->tb.tid;
        }
    }
    ++group->n_threads;
    old_tgroup = group->threads;
    group->threads =
        krealloc(group->threads, sizeof(struct task *) * group->n_threads);
    if (!group->threads) {
        group->threads = old_tgroup;
        --group->n_threads;
        return -ENOMEM;
    }
    group->threads[group->n_threads - 1] = new;
    new->tb.tgroup = group;
    new->tb.tid = ++group->max_tid;
    group->active_threads++;
    return new->tb.tid;
}

static __inl int pthread_destroy_task(struct task *t)
{
    struct task *t_joiner;
    if (!t)
        return TASK_OVER;

    running_to_idling(t);
    if (t->tb.tid > 1 &&
        (t->tb.flags & TASK_FLAG_DETACHED) == TASK_FLAG_DETACHED) {
        t->tb.state = TASK_OVER;
        tasklet_add(task_destroy, t);
        return TASK_OVER;
    } else {
        t->tb.state = TASK_ZOMBIE;
        if (t->tb.tid > 1 && t->tb.joiner_thread_tid) {
            t_joiner = pthread_get_task(t->tb.pid, t->tb.joiner_thread_tid);
            if (t_joiner)
                task_resume(t_joiner);
        }
        return TASK_ZOMBIE;
    }
}

static void pthread_end(void)
{
    /* We have to set the stack pointer because we jumped here
     * after setting lr to pthread_end into the NVIC_FRAME and there
     * we were using the sp of the thread's parent */
    asm volatile("msr " PSP ", %0" ::"r"(_cur_task->tb.sp));
    int thread_state;
    /* storing thread return value */
    asm volatile("mov %0, r0" : "=r"(_cur_task->tb.exitval));
    irq_off();
    thread_state = pthread_destroy_task(_cur_task);
    _cur_task->tb.tgroup->active_threads--;
    irq_on();
    while (1) {
        task_suspend_to(thread_state);
    }
}

/* Finalize thread creation, code shared by pthreads/kthreads. */
static inline void thread_create(struct task *new,
                                 void (*start_routine)(void *), void *arg)
{
    struct nvic_stack_frame *nvic_frame;
    struct extra_stack_frame *extra_frame;
    uint8_t *sp;
    new->tb.joiner_thread_tid = 0;
    new->tb.start = start_routine;
    new->tb.arg = arg;
    new->tb.next = NULL;
    tasklist_add(&tasks_running, new);
    number_of_tasks++;
    new->tb.timeslice = TIMESLICE(new);
    new->tb.state = TASK_RUNNABLE;
    sp = (((uint8_t *)(&new->stack)) + SCHEDULER_STACK_SIZE - NVIC_FRAME_SIZE);
    new->tb.cur_stack = &new->stack;

    /* Stack frame is at the end of the stack space */
    nvic_frame = (struct nvic_stack_frame *)sp;
    memset(nvic_frame, 0, NVIC_FRAME_SIZE);
    nvic_frame->r0 = (uint32_t) new->tb.arg;
    nvic_frame->pc = (uint32_t) new->tb.start;
    nvic_frame->lr = (uint32_t)pthread_end;
    nvic_frame->psr = 0x01000000u;
    sp -= EXTRA_FRAME_SIZE;
    extra_frame = (struct extra_stack_frame *)sp;
    extra_frame->r9 = (uint32_t )new->tb.exec_info->got_loc;
    new->tb.sp = (uint32_t *)sp;
}

/* Pthread create handler. Call be like:
 * int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void
 * *(*start_routine) (void *), void *arg)
 */
int sys_pthread_create_hdlr(pthread_t *thread, const pthread_attr_t *attr,
        void (*start_routine) (void *), void *arg)
{
    struct task *new;
    int i;
    struct filedesc_table *ft;
    uint32_t initaddr, finaddr, start_routine_addr;

    if (!thread || !start_routine)
        return -EINVAL;

    if (task_ptr_valid(thread) || task_ptr_valid(attr))
        return -EACCES;

    /* verify start_routine is inside our own xipfs address range */
    initaddr = (uint32_t)_cur_task->tb.exec_info.init;
    finaddr = initaddr + _cur_task->tb.exec_info.text_size;
    start_routine_addr = (uint32_t)(start_routine);
    if ((start_routine_addr < initaddr) || (start_routine_addr > finaddr))
        return -EACCES;

    ft = _cur_task->tb.filedesc_table;

    new = task_space_alloc();
    if (!new) {
        return -ENOMEM;
    }
    memset(&new->tb, 0, sizeof(struct task_block));

    new->tb.tid = pthread_add(_cur_task, new);
    if (new->tb.tid < 0) {
        task_space_free(new);
        return -ENOMEM;
    }

    if (attr && *attr == PTHREAD_CREATE_DETACHED)
        new->tb.flags = TASK_FLAG_DETACHED | TASK_FLAG_CANCELABLE;
    else
        new->tb.flags = TASK_FLAG_CANCELABLE;

    new->tb.pid = _cur_task->tb.pid;
    new->tb.ppid = _cur_task->tb.ppid;
    new->tb.nice = _cur_task->tb.nice;
    new->tb.filedesc_table = _cur_task->tb.filedesc_table;
    memcpy(&new->tb.exec_info, _cur_task->tb.exec_info, sizeof(struct task_exec_info));
    new->tb.cwd = task_getcwd();
    new->tb.sigmask = _cur_task->tb.sigmask;
    new->tb.sighdlr = _cur_task->tb.sighdlr;
    new->tb.tracer = NULL;
    new->tb.timer_id = -1;
    new->tb.n_specifics = _cur_task->tb.n_specifics;

    if (new->tb.n_specifics > 0) {
        new->tb.specifics = kalloc(new->tb.n_specifics * (sizeof(pthread_key_t)));
        if (new->tb.specifics) {
            memcpy(new->tb.specifics, _cur_task->tb.specifics, new->tb.n_specifics * (sizeof(pthread_key_t)));
        } else {
            new->tb.n_specifics = 0;
        }
    }

    thread_create(new, start_routine, arg);
    *thread = ((new->tb.pid << 16) | (new->tb.tid & 0xFFFF));

    return 0;
}

int sys_pthread_kill_hdlr(pthread_t thread, int sig)
{

    struct task *t;
    if (!thread)
        return -EINVAL;
    t = pthread_get_task((thread & 0xFFFF0000) >> 16, (thread & 0xFFFF));
    if (!t)
        return -ESRCH;
    t->tb.tgroup->active_threads--;
    pthread_destroy_task(t);

    while(t == this_task()) {
        task_preempt();
    }
}

struct task *kthread_create(void(routine)(void *), void *arg)
{
    struct task *new = task_space_alloc();
    void (*start_routine)(void *) = (void (*)(void *))routine;
    if (!new) {
        return NULL;
    }
    irq_off();
    memset(&new->tb, 0, sizeof(struct task_block));
    new->tb.tid = pthread_add(kernel, new);
    if (new->tb.tid < 0) {
        task_space_free(new);
        return NULL;
    }
    new->tb.flags = TASK_FLAG_DETACHED | TASK_FLAG_CANCELABLE;
    new->tb.pid = 0;
    new->tb.ppid = 0;
    new->tb.nice = NICE_DEFAULT;
    new->tb.filedesc_table = NULL;
    memset(new->tb.exec_info, 0, sizeof(struct task_exec_info));
    new->tb.cwd = NULL;
    thread_create(new, start_routine, arg);
    irq_on();
    return new;
}

int kthread_cancel(struct task *t)
{
    if (!t || (t->tb.pid != 0) || (t->tb.tid <= 1))
        return -1;
    irq_off();
    if (tasklist_del(&tasks_running, t) == 0)
        tasklist_add(&tasks_idling, t);
    t->tb.state = TASK_OVER;
    tasklet_add(task_destroy, t);
    irq_on();
    return 0;
}

int sys_pthread_exit_hdlr(int exitval)
{
    _cur_task->tb.exitval = exitval;
    pthread_destroy_task(_cur_task);
    /* deliver SIGCHLD if this is the last thread */
    if (!_cur_task->tb.tgroup || (_cur_task->tb.tgroup->active_threads == 1)) {
            struct task *t  = _cur_task;
            struct task *pt = tasklist_get(&tasks_idling, t->tb.ppid);
            if (!pt)
                pt = tasklist_get(&tasks_running, t->tb.ppid);
            if (pt)
                tasklet_add(task_deliver_sigchld, pt);
    }
    else
        _cur_task->tb.tgroup->active_threads--;
}

int sys_pthread_join_hdlr(pthread_t thread, void **retval)
{
    struct task *to_join;
    if (task_ptr_valid((void *)retval) < 0)
        return -EINVAL;
    to_join = pthread_get_task((thread & 0xFFFF0000) >> 16, (thread & 0xFFFF));
    if (!to_join)
        return -ESRCH;
    if ((to_join->tb.flags & TASK_FLAG_DETACHED) == TASK_FLAG_DETACHED)
        return -EINVAL;
    if (to_join == _cur_task ||
        _cur_task->tb.joiner_thread_tid == to_join->tb.tid)
        return -EDEADLK;
    if (to_join->tb.joiner_thread_tid &&
        to_join->tb.joiner_thread_tid != _cur_task->tb.tid) {
        return -EINVAL;
    }
    to_join->tb.joiner_thread_tid = _cur_task->tb.tid;
    if (to_join->tb.state != TASK_ZOMBIE) {
        task_suspend();
        return SYS_CALL_AGAIN;
    }
    if (retval)
        *retval = (void *)to_join->tb.exitval;
    to_join->tb.state = TASK_OVER;
    tasklet_add(task_destroy, to_join);
    return 0;
}

int sys_pthread_detach_hdlr(pthread_t thread)
{
    struct task *t =
        pthread_get_task((thread & 0xFFFF0000) >> 16, (thread & 0xFFFF));
    if (!t)
        return -ESRCH;
    t->tb.flags |= TASK_FLAG_DETACHED;
    return 0;
}

int sys_pthread_setcancelstate_hdlr(int state, int* oldstate)
{
    struct task *t_joiner;

    if (task_ptr_valid((void *)oldstate) < 0)
        return -EINVAL;
    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
        return -EINVAL;
    if (oldstate) {
        if ((_cur_task->tb.flags & TASK_FLAG_CANCELABLE) ==
            TASK_FLAG_CANCELABLE)
            *oldstate = PTHREAD_CANCEL_ENABLE;
        else
            *oldstate = PTHREAD_CANCEL_DISABLE;
    }
    if (state == PTHREAD_CANCEL_ENABLE) {
        _cur_task->tb.flags |= TASK_FLAG_CANCELABLE;
        if ((_cur_task->tb.flags & TASK_FLAG_PENDING_CANC) ==
            TASK_FLAG_PENDING_CANC) {
            running_to_idling(_cur_task);
            if ((_cur_task->tb.flags & TASK_FLAG_DETACHED) ==
                TASK_FLAG_DETACHED) {
                _cur_task->tb.state = TASK_OVER;
                tasklet_add(task_destroy, _cur_task);
            } else {
                _cur_task->tb.state = TASK_ZOMBIE;
                _cur_task->tb.exitval = PTHREAD_CANCELED;
                if (_cur_task->tb.joiner_thread_tid) {
                    t_joiner = pthread_get_task(
                        _cur_task->tb.pid, _cur_task->tb.joiner_thread_tid);
                    if (t_joiner)
                        task_resume(t_joiner);
                }
            }
        }
    } else
        _cur_task->tb.flags &= ~TASK_FLAG_CANCELABLE;
    return 0;
}

int sys_pthread_cancel_hdlr(pthread_t thread)
{
    struct task *t =
        pthread_get_task((thread & 0xFFFF0000) >> 16, (thread & 0xFFFF));
    struct task *t_joiner;

    if (t) {
        if ((t->tb.flags & TASK_FLAG_CANCELABLE) == TASK_FLAG_CANCELABLE) {
            running_to_idling(t);
            if ((t->tb.flags & TASK_FLAG_DETACHED) == TASK_FLAG_DETACHED) {
                t->tb.state = TASK_OVER;
                tasklet_add(task_destroy, t);
            } else {
                t->tb.state = TASK_ZOMBIE;
                t->tb.exitval = PTHREAD_CANCELED;
                if (t->tb.joiner_thread_tid) {
                    t_joiner =
                        pthread_get_task(t->tb.pid, t->tb.joiner_thread_tid);
                    if (t_joiner)
                        task_resume(t_joiner);
                }
            }
        } else {
            t->tb.flags |= TASK_FLAG_PENDING_CANC;
        }
        return 0;
    }
    return -ESRCH;
}

int sys_pthread_self_hdlr(void)
{
    pthread_t thread;
    thread = (_cur_task->tb.pid << 16) | (_cur_task->tb.tid & 0xFFFF);
    return (int)thread;
}

int sys_pthread_mutex_init_hdlr(mutex_t **mutex)
{
    if (!mutex || (task_ptr_valid(mutex) < 0))
        return -EPERM;
    else
        *mutex = mutex_init();
    if (!(*mutex))
        return -ENOMEM;
    fmalloc_chown(*mutex, _cur_task->tb.pid);
    return 0;
}

int sys_pthread_mutex_destroy_hdlr(mutex_t *mutex)
{

    if (!mutex)
        return -EINVAL;
    if (task_ptr_valid(mutex))
        return -EACCES;
    mutex_destroy(mutex);
    return 0;
}

int sys_pthread_mutex_lock_hdlr(mutex_t **mutex)
{
    if (!mutex || task_ptr_valid(mutex))
        return -EACCES;

    if (task_ptr_valid(*mutex))
        return -EACCES;

    /* the mutex has to be initialized first if it's NULL*/
    if (!(*mutex)) {
        *mutex = mutex_init();
        if (!(*mutex))
            return -EAGAIN;
        fmalloc_chown(*mutex, _cur_task->tb.pid);
    }

    return mutex_lock(*mutex);
}

int sys_pthread_mutex_trylock_hdlr(mutex_t **mutex)
{

    if (!mutex || task_ptr_valid(mutex))
        return -EACCES;

    if (task_ptr_valid(*mutex))
        return -EACCES;

    /* the mutex has to be initialized first if it's NULL*/
    if (!(*mutex)) {
        *mutex = mutex_init();
        if (!(*mutex))
            return -EAGAIN;
        fmalloc_chown(*mutex, _cur_task->tb.pid);
    }
    return mutex_trylock(*mutex);
}

int sys_pthread_mutex_unlock_hdlr(mutex_t *mutex)
{
    if (!mutex)
        return -EINVAL;
    if (task_ptr_valid(mutex))
        return -EACCES;
    return mutex_unlock(mutex);
}


int sys_pthread_key_create_hdlr(pthread_key_t *key, void *cb_unused)
{
    struct task *t = this_task();
    void *newarray;
    (void)(cb_unused);

    /* key argument is used to store the opaque address of the key, so it must
     * be valid and non-null
     */
    if (!key || task_ptr_valid(key))
        return -EINVAL;

    newarray = krealloc(t->tb.specifics, sizeof(pthread_key_t) * (t->tb.n_specifics + 1));
    if (newarray) {
        t->tb.specifics = newarray;
        *key = t->tb.n_specifics;
        t->tb.n_specifics++;
    }
    return 0;
}

int sys_pthread_setspecific_hdlr(pthread_key_t key, const void *value)
{
    struct task *t = this_task();
    if (key >= t->tb.n_specifics)
        return -EINVAL;
    t->tb.specifics[key] = (uint32_t)value;
    return 0;
}

int sys_pthread_getspecific_hdlr(pthread_key_t key, uint32_t *value)
{
    struct task *t = this_task();
    if (key >= t->tb.n_specifics)
        return -EINVAL;
    /* Redundant check if used through libc proxy (arg2 is a temp variable
     * in stack)
     */
    if (!value || task_ptr_valid(value))
        return -EINVAL;
    *value = t->tb.specifics[key];
    return 0;
}
#else /* if !CONFIG_PTHREADS */
int sys_pthread_create_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}

int sys_pthread_exit_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}


int sys_pthread_join_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_detach_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_cancel_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_self_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_setcancelstate_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_mutex_init_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_mutex_destroy_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_mutex_lock_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_mutex_trylock_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_mutex_unlock_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_kill_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_key_create_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_setspecific_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}
int sys_pthread_getspecific_hdlr(int arg1, int arg2, int arg3, int arg4, int arg5)
{
    return -EOPNOTSUPP;
}

#endif /* CONFIG_PTHREADS */


/********************************/
/*         Task switching       */
/********************************/
/********************************/
/********************************/
/**/
/**/
/**/
/* In order to keep the code efficient, the stack layout of armv6 and armv7 do NOT match! */
static __naked void save_kernel_context(void)
{
    asm volatile("mrs r0, " MSP "           ");
    asm volatile("stmdb r0!, {r4-r11}   ");
    asm volatile("msr " MSP ", r0           ");
    asm volatile("isb");
    asm volatile("bx lr                 ");
}

static __naked void save_task_context(void)
{
    asm volatile("mrs r0, " PSP "           ");
    asm volatile("stmdb r0!, {r4-r11}   ");
    asm volatile("msr " PSP ", r0           ");
    asm volatile("isb");
    asm volatile("bx lr                 ");
}

static uint32_t runnable = RUN_HANDLER;

static __naked void restore_kernel_context(void)
{
    asm volatile("mrs r0, " MSP "          ");
    asm volatile("ldmfd r0!, {r4-r11}  ");
    asm volatile("msr " MSP ", r0");
    asm volatile("isb");
    asm volatile("bx lr                 ");
}

static __naked void restore_task_context(void)
{
    asm volatile("mrs r0, " PSP "          ");
    asm volatile("ldmfd r0!, {r4-r11}  ");
    asm volatile("msr " PSP ", r0          ");
    asm volatile("isb");
    asm volatile("bx lr                 ");
}

static __inl void task_switch(void)
{
    int i;
    struct task *t;
    if (forced_task) {
        _cur_task = forced_task;
        forced_task = NULL;
        return;
    }
    t = _cur_task;
    /* Checks that the _cur_task hasn't left the "task_running" list.
     * If it's not in the valid state, revert task-switching to the head of
     * task_running */
    if (((t->tb.state != TASK_RUNNING) && (t->tb.state != TASK_RUNNABLE)) ||
        (t->tb.next == NULL))
        t = tasks_running;
    else
        t = t->tb.next;
    t->tb.timeslice = TIMESLICE(t);
    t->tb.state = TASK_RUNNING;
    _cur_task = t;
}

#pragma GCC push_options
#pragma GCC optimize ("O0")

/* C ABI cannot mess with the stack, we will */
void __naked pend_sv_handler(void)
{
    asm volatile("cpsid i");
    /* save current context on current stack */
    if (in_kernel()) {
        save_kernel_context();
        asm volatile("mrs %0, " MSP "" : "=r"(_top_stack));
    } else {
        save_task_context();
        asm volatile("mrs %0, " PSP "" : "=r"(_top_stack));
    }
    asm volatile("isb");

    /* save current SP to TCB */
    _cur_task->tb.sp = _top_stack;
    if (_cur_task->tb.state == TASK_RUNNING)
        _cur_task->tb.state = TASK_RUNNABLE;

    /* choose next task */
    //    if ((_cur_task->tb.flags & TASK_FLAG_SIGNALED) == 0)
    task_switch();

    /* if switching to a signaled task, adjust sp */
    //    if ((_cur_task->tb.flags & (TASK_FLAG_IN_SYSCALL |
    //    TASK_FLAG_SIGNALED)) == ((TASK_FLAG_SIGNALED))) {
    //        _cur_task->tb.sp += 32;
    //    }

    if (((int)(_cur_task->tb.sp) - (int)(&_cur_task->stack)) <
        STACK_THRESHOLD) {
        kprintf("PendSV: Process %d is running out of stack space!\n",
                _cur_task->tb.pid);
    }

    /* write new stack pointer and restore context */
    if (in_kernel()) {
        asm volatile("msr " MSP ", %0" ::"r"(_cur_task->tb.sp));
        asm volatile("isb");
        restore_kernel_context();
        runnable = RUN_KERNEL;
        mpu_task_on(0);
    } else {
        asm volatile("msr " PSP ", %0" ::"r"(_cur_task->tb.sp));
        asm volatile("isb");
        restore_task_context();
        runnable = RUN_USER;
        mpu_task_on(_cur_task->tb.pid);
    }

    /* Set control bit for non-kernel threads */
    if (_cur_task->tb.pid != 0) {
        asm volatile("msr CONTROL, %0" ::"r"(0x01));
    } else {
        asm volatile("msr CONTROL, %0" ::"r"(0x00));
    }
    asm volatile("isb");

    /* Set return value selected by the restore procedure;
       RUN_KERNEL/RUN_USER are special values which inform
       the CPU we are returning from an exception handler,
       after detecting such a value the processor pops the
       correct stack frame (NVIC_FRAME) and jumps to the
       program counter stored into it, thus resuming task
       execution.  */
    asm volatile("mov lr, %0" ::"r"(runnable));

    asm volatile("cpsie i");
    /* return (function is naked) */
    asm volatile("bx lr          \n");
}
#pragma GCC pop_options


void kernel_task_init(void)
{
    /* task0 = kernel */
    irq_off();
    kernel->tb.sp = msp_read(); // SP needs to be current SP
    kernel->tb.pid = next_pid();
    kernel->tb.tid = 1;
    kernel->tb.tgroup = NULL;
    kernel->tb.ppid = scheduler_get_cur_pid();
    kernel->tb.nice = NICE_DEFAULT;
    kernel->tb.start = NULL;
    kernel->tb.arg = NULL;
    kernel->tb.timer_id = -1;
    kernel->tb.filedesc_table = NULL;
    kernel->tb.timeslice = TIMESLICE(kernel);
    kernel->tb.state = TASK_RUNNABLE;
    kernel->tb.cwd = fno_search("/");
    kernel->tb.state = TASK_RUNNABLE;
    kernel->tb.next = NULL;
    tasklist_add(&tasks_running, kernel);
    irq_on();

    /* Set kernel as current task */
    _cur_task = kernel;
}

static void task_suspend_to(int newstate)
{
    /* Refuse to suspend on main kernel thread. */
    if ((_cur_task->tb.pid < 1) && (_cur_task->tb.tid < 2))
        return;
    running_to_idling(_cur_task);
    if (_cur_task->tb.state == TASK_RUNNABLE ||
        _cur_task->tb.state == TASK_RUNNING) {
        _cur_task->tb.timeslice = 0;
    }
    _cur_task->tb.state = newstate;
    schedule();
}

void task_suspend(void)
{
    return task_suspend_to(TASK_WAITING);
}

void task_stop(struct task *t)
{
    if (!t)
        return;
    if (t->tb.state == TASK_RUNNABLE || t->tb.state == TASK_RUNNING) {
        t->tb.timeslice = 0;
    }
    t->tb.state = TASK_STOPPED;
    running_to_idling(t);
    schedule();
}

void task_hit_breakpoint(struct task *t)
{
    if (t->tb.tracer) {
        task_stop(t);
        task_resume(t->tb.tracer);
    }
}

void task_preempt(void)
{
    _cur_task->tb.timeslice = 0;
    schedule();
}

void task_preempt_all(void)
{
    struct task *t = tasks_running;
    while (t) {
        if (t->tb.pid != 0)
            t->tb.timeslice = 0;
        t = t->tb.next;
    }
    schedule();
}


static void task_resume_real(struct task *t, int lock)
{
    if (!t)
        return;
    if (t->tb.state == TASK_WAITING) {
        idling_to_running(t);
        t->tb.state = TASK_RUNNABLE;
    }
    if (!lock && (t->tb.nice == NICE_RT)) {
        forced_task = t;
        task_preempt_all();
    }
}

void task_resume_lock(struct task *t)
{
    task_resume_real(t, 1);
}

void task_resume(struct task *t)
{
    task_resume_real(t, 0);
}

void task_wakeup(struct task *t)
{
    task_preempt_all();
    t->tb.timeslice = TIMESLICE(t);
    task_resume(t);
}

void task_continue(struct task *t)
{
    if ((t) && (t->tb.state == TASK_STOPPED)) {
        idling_to_running(t);
        t->tb.state = TASK_RUNNABLE;
    }
}

static void task_resume_vfork(struct task *t)
{
    if ((t) && t->tb.state == TASK_FORKED) {
        idling_to_running(t);
        t->tb.state = TASK_RUNNABLE;
    }
}

void task_deliver_sigchld(void *arg)
{
    struct task *t = (struct task *)arg;
    if (t)
    task_kill(t->tb.pid, SIGCHLD);
}

void task_deliver_sigtrap(void *arg)
{
    struct task *t = (struct task *)arg;
    if (t)
        task_kill(t->tb.pid, SIGTRAP);
}


static void destroy_thread_group(struct thread_group *group, uint16_t tid)
{
#ifdef CONFIG_PTHREADS
    int i;
    /* Destroy the entire thread family */
    for (i = 0; i < group->n_threads; i++) {
        if (group->threads[i] != NULL) {
            struct task *th = group->threads[i];
            if (th->tb.tid != tid) {
                running_to_idling(th);
                th->tb.state = TASK_OVER;
                kfree(th->tb.tgroup->threads);
                kfree(th->tb.tgroup);
                th->tb.tgroup = NULL;
                tasklet_add(task_destroy, th);
            }
        }
    }
#endif
}



void task_terminate(struct task *t)
{
    int i;
    if (t) {
        running_to_idling(t);
        t->tb.state = TASK_ZOMBIE;
        t->tb.timeslice = 0;

        if (t->tb.ppid > 0) {
            struct task *pt = tasklist_get(&tasks_idling, t->tb.ppid);
            if (!pt)
                pt = tasklist_get(&tasks_running, t->tb.ppid);
            if (t->tb.flags & TASK_FLAG_VFORK) {
                /* Restore parent's stack copy */
                if (pt) {
                    memcpy(t->stack, pt->stack, SCHEDULER_STACK_SIZE);
                    secure_swap_stack(pt->tb.pid, t->tb.pid);
                }
                task_resume_vfork(t);
            }
            if (!pt || (pt->tb.state == TASK_ZOMBIE) || (pt->tb.state == TASK_OVER)) {
                /* Parent task is not there anymore. Init tries to adopt orphan child. */
                t->tb.ppid = 1;
                pt = tasklist_get(&tasks_running, 1);
                if (!pt)
                    pt = tasklist_get(&tasks_idling, 1);
            }
            if (pt)
                tasklet_add(task_deliver_sigchld, pt);

            task_preempt();
        }
    }
}

int scheduler_get_nice(int pid)
{
    struct task *t;
    t = tasklist_get(&tasks_running, pid);
    if (!t)
        t = tasklist_get(&tasks_idling, pid);

    if (!t)
        return 0;
    return (int)t->tb.nice;
}

int sys_getpid_hdlr(void)
{
    if (!_cur_task)
        return 0;
    return _cur_task->tb.pid;
}

int sys_getppid_hdlr(void)
{
    if (!_cur_task)
        return 0;
    return _cur_task->tb.ppid;
}

void sleepy_task_wakeup(uint32_t now, void *arg)
{
    struct task *t = (struct task *)arg;
    t->tb.timer_id = -1;
    t->tb.flags |= TASK_FLAG_TIMEOUT;
    if (t->tb.state == TASK_WAITING)
        task_resume(t);
}

int sys_sleep_hdlr(uint32_t arg1, uint32_t arg2)
{
    uint32_t timeout;
    if (arg1 < 0)
        return -EINVAL;
    if (!arg2)
        return -EINVAL;

    timeout = jiffies + arg1;

    if ((_cur_task->tb.flags & TASK_FLAG_TIMEOUT) != 0) {
        _cur_task->tb.flags &= (~TASK_FLAG_TIMEOUT);
        if (_cur_task->tb.timer_id >= 0) {
            ktimer_del(_cur_task->tb.timer_id);
            _cur_task->tb.timer_id = -1;
        }
        return 0;
    }
    _cur_task->tb.timer_id = ktimer_add(arg1, sleepy_task_wakeup, this_task());
    task_suspend();
    return SYS_CALL_AGAIN;
}

void kthread_sleep_ms(uint32_t ms)
{
    unsigned dl = jiffies + ms;
    while(dl > jiffies) {
        kthread_yield();
    }
}

void alarm_task(uint32_t now, void *arg)
{
    struct task *t = (struct task *)arg;
    if (t) {
        t->tb.timer_id = -1;
        task_kill(t->tb.pid, SIGALRM);
    }
}

int sys_alarm_hdlr(uint32_t arg1)
{
    if (arg1 < 0)
        return -EINVAL;

    int ret = 0;
    if (_cur_task->tb.timer_id >= 0) {
        ktimer_del(_cur_task->tb.timer_id);
        ret = 1;
    }

    _cur_task->tb.timer_id = ktimer_add((arg1 * 1000), alarm_task, this_task());
    return ret;
}

int sys_ualarm_hdlr(uint32_t arg1, uint32_t arg2)
{
    if (arg1 < 0)
        return -EINVAL;

    int ret = 0;
    if (_cur_task->tb.timer_id >= 0) {
        ktimer_del(_cur_task->tb.timer_id);
        ret = 1;
    }

    _cur_task->tb.timer_id = ktimer_add(((arg1 / 1000) + 1), alarm_task, this_task());
    return ret;
}

void task_yield(void)
{
    _cur_task->tb.timeslice = 0;
    schedule();
}

void __naked kthread_yield(void)
{
    struct task *t = _cur_task;
    if (!t || (t->tb.pid != 0) || (t->tb.tid < 2))
        return;
    _cur_task->tb.timeslice = 0;
    schedule();
    /* return (function is naked) */
    asm volatile("bx lr");
}

int sys_sched_yield_hdlr(void)
{
    task_yield();
    return 0;
}

int sys_poll_hdlr(struct pollfd *pfd, int n, int timeout_param)
{
    int i;
    uint32_t timeout;
    int endless = 0;
    int ret = 0;
    struct fnode *f;

    if (!pfd || task_ptr_valid(pfd))
        return -EACCES;

    if (timeout_param < 0) {
        endless = 1;
    }

    if ((_cur_task->tb.flags & TASK_FLAG_TIMEOUT) != 0) {
        _cur_task->tb.flags &= (~TASK_FLAG_TIMEOUT);
        return 0;
    }

    if ((_cur_task->tb.flags & TASK_FLAG_TIMEOUT) == 0)
        timeout = jiffies + timeout_param;
    else
        timeout = jiffies;

    while (endless || (jiffies <= timeout)) {
        for (i = 0; i < n; i++) {
            f = task_filedesc_get(pfd[i].fd);
            if (!f || !f->owner || !f->owner->ops.poll) {
                return -EOPNOTSUPP;
            }
            pfd[i].revents = 0;
            ret += f->owner->ops.poll(f, pfd[i].events, &pfd[i].revents);
        }
        if (ret > 0) {
            if (this_task()->tb.timer_id >= 0) {
                ktimer_del(this_task()->tb.timer_id);
                this_task()->tb.timer_id = -1;
            }
            _cur_task->tb.flags &= (~TASK_FLAG_TIMEOUT);
            return ret;
        }

        if (!endless && (this_task()->tb.timer_id < 0)) {
            this_task()->tb.timer_id =
                ktimer_add(timeout - jiffies, sleepy_task_wakeup, this_task());
        }
        task_suspend();
        return SYS_CALL_AGAIN;
    }
    if (_cur_task->tb.timer_id >= 0) {
        ktimer_del(_cur_task->tb.timer_id);
        _cur_task->tb.timer_id = -1;
    }
    return 0;
}

int sys_waitpid_hdlr(int pid, int *status , int options)
{
    struct task *t = NULL;
    if (status && task_ptr_valid(status))
        return -EACCES;
    if (pid == 0)
        return -EINVAL;

    if (pid < -1)
        return -ENOSYS;

    if (pid != -1) {
        t = tasklist_get(&tasks_running, pid);
        /* Check if pid is running, but it's not a child */
        if (t) {
            if (t->tb.ppid != _cur_task->tb.ppid)
                return -ESRCH;
            else {
                if ((options & WNOHANG) != 0)
                    return 0;
                task_suspend();
                return SYS_CALL_AGAIN;
            }
        }

        t = tasklist_get(&tasks_idling, pid);
        if (!t || (t->tb.ppid != _cur_task->tb.pid))
            return -ESRCH;
        if (t->tb.state == TASK_ZOMBIE)
            goto child_found;
        if (t->tb.state == TASK_STOPPED)
        {
            /* TODO: set status */
            return pid;
        }
        if (options & WNOHANG)
            return 0;
        task_suspend();
        return SYS_CALL_AGAIN;
    }

    /* wait for all (pid = -1) */
    t = tasks_idling;
    while (t) {

        if ((t->tb.state == TASK_STOPPED) && (t->tb.ppid == _cur_task->tb.pid))
        {
            /* TODO: set status */
            return pid;
        }
        if ((t->tb.state == TASK_ZOMBIE) && (t->tb.ppid == _cur_task->tb.pid))
            goto child_found;
        t = t->tb.next;
    }
    if (options & WNOHANG)
        return 0;
    task_suspend();
    return SYS_CALL_AGAIN;

child_found:
    if (status) {
        *status = t->tb.exitval;
    }
    pid = t->tb.pid;
    /* if this is a thread this is the last active one because it sent a SIGCHLD
     */
#ifdef CONFIG_PTHREADS
    if (t->tb.tgroup)
        t->tb.tgroup->active_threads = 0;
#endif
    tasklet_add(task_destroy, t);
    t->tb.state = TASK_OVER;
    return pid;
}

enum __ptrace_request {
    PTRACE_TRACEME = 0,
    PTRACE_PEEKTEXT = 1,
    PTRACE_PEEKDATA = 2,
    PTRACE_PEEKUSER = 3,
    PTRACE_POKETEXT = 4,
    PTRACE_POKEDATA = 5,
    PTRACE_POKEUSER = 6,
    PTRACE_CONT = 7,
    PTRACE_KILL = 8,
    PTRACE_SINGLESTEP = 9,
    PTRACE_GETREGS = 12,
    PTRACE_SETREGS = 13,
    PTRACE_ATTACH = 16,
    PTRACE_DETACH = 17,
    PTRACE_SYSCALL = 24,
    PTRACE_SEIZE = 0x4206
};

int ptrace_getregs(struct task *t, struct user *u)
{
    struct extra_stack_frame *cur_extra = t->tb.sp + NVIC_FRAME_SIZE + EXTRA_FRAME_SIZE;
    struct nvic_stack_frame *cur_nvic = t->tb.sp + EXTRA_FRAME_SIZE;
    memcpy(&u->regs[0], cur_nvic, (4 * sizeof(uint32_t))); /* r0 - r3 */
    memcpy(&u->regs[4], cur_extra, (8 * sizeof(uint32_t))); /* r4 - r11 */
    u->regs[12] = cur_nvic->r12;
    u->regs[13] = (uint32_t)(t->tb.sp);
    u->regs[14] = cur_nvic->lr;
    u->regs[15] = cur_nvic->pc;
    u->regs[16] = cur_nvic->psr;
    u->tsize = t->tb.exec_info.text_size;
    u->dsize = t->tb.exec_info.data_size;
    u->start_code = (uint32_t)t->tb.exec_info.init;
    u->ssize = CONFIG_TASK_STACK_SIZE;
    u->start_stack = (uint32_t)(t->tb.cur_stack);
    u->signal = 0;
    u->magic = 0xd0ab1e50;
    return 0;
}

int ptrace_peekuser(struct task *t, uint32_t addr)
{
    struct user u;
    if (ptrace_getregs(t, &u) == 0)
        return *(int *)(((char *)(&u)) + addr);
    else return -1;
}

int ptrace_pokeuser(struct task *t, uint32_t addr, uint32_t data)
{
    uint32_t *cur_extra = t->tb.sp + NVIC_FRAME_SIZE + EXTRA_FRAME_SIZE;
    uint32_t *cur_nvic = t->tb.sp + EXTRA_FRAME_SIZE;
    int pos = addr / 4;

    if (addr > (16 * sizeof(uint32_t)))
        return -1;
    if ((addr % 4) != 0)
        return -1;
    if (pos < 4) {
        cur_nvic[pos] = data;
        return 0;
    }
    if (pos < 12) {
        cur_extra[pos - 4] = data;
        return 0;
    }
    if (pos == 12) {
        cur_nvic[4] = data;
        return 0;
    }

    /* Breakpoints */
    if ((pos > 56) && (pos < 64)) {
        pos -= 56;
        if (data == 0)
            return fpb_delbrk(pos);
        return fpb_setbrk(t->tb.pid, (void *)data, pos);
    }
    return -1;
}

int ptrace_setregs(struct task *t, uint32_t *regs)
{
    int i;
    for (i = 0; i < 13; i++) {
        ptrace_pokeuser(t, i * 4, regs[i]);
    }
    return 0;
}

int sys_ptrace_hdlr(enum __ptrace_request request, uint32_t pid, void *addr, void *data)

{
    struct task *tracee = NULL;
    /* Prepare tracee based on pid */
    tracee = tasklist_get(&tasks_idling, pid);
    if (!tracee)
        tracee = tasklist_get(&tasks_running, pid);
    if (!tracee) {
        return -1;
    }

    switch (request) {
    case PTRACE_TRACEME:
        _cur_task->tb.tracer = _cur_task;
        break;
    case PTRACE_PEEKTEXT:
    case PTRACE_PEEKDATA:
        if (task_ptr_valid_for_task(tracee, addr)) {
            uint32_t initaddr = (uint32_t)tracee->tb.exec_info.init;
            uint32_t finaddr = initaddr + tracee->tb.exec_info.text_size + tracee->tb.exec_info.data_size;
            if (((uint32_t)addr < initaddr) || ((uint32_t)addr > finaddr))
                return -1;
        }
        return *((uint32_t *)addr);
    case PTRACE_PEEKUSER:
        if (task_ptr_valid_for_task(tracee, addr))
            return -1;
        return ptrace_peekuser(tracee, (uint32_t)addr);
    case PTRACE_POKETEXT:
    case PTRACE_POKEDATA:
        break;
    case PTRACE_POKEUSER:
        if (task_ptr_valid_for_task(tracee, addr))
            return -1;
        if (!data || task_ptr_valid(data))
            return -1;
        return ptrace_pokeuser(tracee, (uint32_t)addr, (uint32_t)data);
    case PTRACE_CONT:
        if (!tracee)
            return -1;
        if (tracee->tb.tracer != _cur_task)
            return -1;
        task_continue(tracee);
        return 0;

    case PTRACE_KILL:
        if (!tracee)
            return -1;
        if (tracee->tb.tracer != _cur_task)
            return -1;
        task_kill(pid, SIGKILL);
        return 0;

    case PTRACE_SINGLESTEP:
        {
            struct user u;
            ptrace_getregs(tracee, &u);
            if (fpb_setbrk(pid, (void *)((u.regs[15]) / 2 * 2 + 2), 0) >= 0) {
                task_continue(tracee);
                return 0;
            } else
                return -1;
        }

    case PTRACE_GETREGS:
        if (!data || task_ptr_valid(data))
            return -1;
        return ptrace_getregs(tracee, (struct user *)data);
        break;
    case PTRACE_SETREGS:
        if (!data || task_ptr_valid(data))
            return -1;
        return ptrace_setregs(tracee, (uint32_t *)data);
        break;

    case PTRACE_ATTACH:
    case PTRACE_SEIZE:
        if (!tracee)
            return -1;
        tracee->tb.tracer = _cur_task;
        if (request == PTRACE_ATTACH) {
            task_stop(tracee);
        }
        return 0;

    case PTRACE_DETACH:
        if (!tracee)
            return -1;
        if (tracee->tb.tracer != _cur_task)
            return -1;
        tracee->tb.tracer = NULL;
        task_continue(tracee);
        return 0;
    case PTRACE_SYSCALL:
        if (!tracee)
            return -1;
        if (tracee->tb.tracer != _cur_task)
            return -1;
        tracee->tb.flags |= TASK_FLAG_SYSCALL_STOP;
        return 0;
    }
    return -1;
}

int sys_setpriority_hdlr(int which, int pid, int nice)
{
    struct task *t = tasklist_get(&tasks_idling, pid);
    if (!t)
        t = tasklist_get(&tasks_running, pid);

    if (which != 0) /* ONLY PRIO_PROCESS IS VALID */
        return -EINVAL;
    if (!t)
        return -ESRCH;

    t->tb.nice = nice;
    return 0;
}

int sys_getpriority_hdlr(int which, int pid)
{
    struct task *t = tasklist_get(&tasks_idling, pid);
    if (!t)
        t = tasklist_get(&tasks_running, pid);

    if (which != 0) /* ONLY PRIO_PROCESS IS VALID */
        return -EINVAL;
    if (!t)
        return -ESRCH;

    return (int)t->tb.nice;
}

int sys_kill_hdlr(uint32_t pid, uint32_t sig)
{
    struct task *t = tasklist_get(&tasks_idling, pid);
    if (!t)
        t = tasklist_get(&tasks_running, pid);
    if (!t)
        return -ESRCH;
    return catch_signal(t, sig, t->tb.sigmask);
}

int task_kill(int pid, int signal)
{
    if (pid > 0) {
        return sys_kill_hdlr(pid, signal);
    }
}

int sys_exit_hdlr(int val)
{
    _cur_task->tb.exitval = val;
    task_terminate(_cur_task);
}

int sys_setsid_hdlr(void)
{
    int i;
    struct filedesc_table *ft = _cur_task->tb.filedesc_table;
    if (!ft)
        return -1;
    for (i = 0; (ft) && (i < ft->n_files); i++) {
        if (ft->fdesc[i].fno != NULL) {
            struct fnode *fno = ft->fdesc[i].fno;
            if ((fno->flags & FL_TTY) &&
                ((ft->fdesc[i].mask & O_NOCTTY) == 0)) {
                struct module *mod = fno->owner;
                if (mod && mod->ops.tty_attach) {
                    mod->ops.tty_attach(fno, _cur_task->tb.ppid);
                    ft->fdesc[i].mask |= O_NOCTTY;
                    return 0;
                }
            }
        }
    }
}

int task_segfault(uint32_t address, uint32_t instruction, int flags)
{
    struct filedesc_table *ft;
    if (in_kernel())
        return -1;
    if (_cur_task->tb.state == TASK_ZOMBIE)
        return 0;

    ft = _cur_task->tb.filedesc_table;
    if (ft && (ft->n_files > 2) && ft->fdesc[2].fno->owner->ops.write) {
#ifdef CONFIG_EXTENDED_MEMFAULT
        char segv_msg[128] = "Memory fault: process (pid=";
        strcat(segv_msg, pid_str(_cur_task->tb.pid));
        if (flags == MEMFAULT_ACCESS) {
            strcat(segv_msg, ") attempted access to memory at ");
            strcat(segv_msg, x_str(address));
        }
        if (flags == MEMFAULT_DOUBLEFREE) {
            strcat(segv_msg, ") attempted double free");
        }
        strcat(segv_msg, ". Killed.\r\n");
#else
        char segv_msg[] = ">_< -- Segfault -- >_<";
#endif
        ft->fdesc[2].fno->owner->ops.write(ft->fdesc[2].fno, segv_msg,
                                           strlen(segv_msg));
    }
    task_terminate(_cur_task);
    return 0;
}

static int task_ptr_valid_for_task(const void *ptr, const struct task *t)
{
    struct task *pt;
    uint8_t *stack_start = (uint8_t *)t->tb.cur_stack;
    uint8_t *stack_end = stack_start + SCHEDULER_STACK_SIZE;
    uint8_t *data_start;
    uint8_t *data_end;

    if (!ptr)
        return 0; /* NULL is a permitted value */

    if (t->tb.pid == 0)
        return 0; /* Kernel mode */
    if (((uint8_t *)ptr >= stack_start) && ((uint8_t *)ptr < stack_end))
        return 0; /* In the process own's  stack */
    if (secure_mempool_owner(ptr, t->tb.pid))
        return 0; /* In the process own's  heap */

    /* Check ownership of static data */
    data_start = t->tb.exec_info.mmap_base;

    /* Allow own data */
    if (secure_mempool_owner(data_start, t->tb.pid))
        return 0;

    /* Allow parent's data, if forked */
    if (secure_mempool_owner(data_start,  t->tb.ppid)) {
        pt = tasklist_get(&tasks_idling, t->tb.ppid);
        if (pt && (pt->tb.state == TASK_FORKED))
            return 0;
    }

    return -1;
}

int task_ptr_valid(const void *ptr)
{
    return task_ptr_valid_for_task(ptr, _cur_task);
}

#pragma GCC push_options
#pragma GCC optimize ("O0")
struct nvic_stack_frame *n_stack = NULL;
static uint32_t *a0 = NULL;
static uint32_t *a1 = NULL;
static uint32_t *a2 = NULL;
static uint32_t *a3 = NULL;
static uint32_t *a4 = NULL;
static uint32_t *a5 = NULL;
static uint32_t n_syscall = 0xFFFFFFFF;

int __naked sv_call_handler(void)
{
    irq_off();

    save_task_context();
    asm volatile("mrs %0, " PSP "" : "=r"(_top_stack));

    /* save current SP to TCB */
    _cur_task->tb.sp = _top_stack;

    /* Get function arguments */
    n_stack = (struct nvic_stack_frame *)(_cur_task->tb.sp + NVIC_FRAME_SIZE);
    a0 = &n_stack->r0;
    a1 = &n_stack->r1;
    a2 = &n_stack->r2;
    a3 = &n_stack->r3;
    a4 = (uint32_t *)((uint8_t *)_cur_task->tb.sp +
                      (EXTRA_FRAME_SIZE + NVIC_FRAME_SIZE + 8*4));
    a5 = (uint32_t *)((uint8_t *)_cur_task->tb.sp +
                      (EXTRA_FRAME_SIZE + NVIC_FRAME_SIZE + 8*4 + 4));

    n_syscall = *a0;


    if (n_syscall == SV_CALL_SIGRETURN) {
        uint32_t *syscall_retval =
            (uint32_t *)(_cur_task->tb.osp + EXTRA_FRAME_SIZE);
        _cur_task->tb.sp = _cur_task->tb.osp;
        _cur_task->tb.flags &= (~(TASK_FLAG_SIGNALED));
        if (*syscall_retval == SYS_CALL_AGAIN_VAL) {
            *syscall_retval = -EINTR;
        }
        cur_extra = _cur_task->tb.sp + NVIC_FRAME_SIZE + EXTRA_FRAME_SIZE;
        irq_on();
        goto return_from_syscall;
    }
    if (n_syscall >= _SYSCALLS_NR) {
        restore_task_context();
        irq_on();
        return -1;
    }
    if (sys_syscall_handlers[n_syscall] == NULL) {
        restore_task_context();
        irq_on();
        return -1;
    }

#ifdef CONFIG_SYSCALL_TRACE
    Strace[StraceTop].n = n;
    Strace[StraceTop].pid = _cur_task->tb.pid;
    Strace[StraceTop].sp = (uint32_t)_top_stack;
    StraceTop++;
    if (StraceTop > 9)
        StraceTop = 0;
#endif

    /* Execute syscall */
    int (*call)(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4,
                uint32_t arg5) = NULL;

    _cur_task->tb.flags |= TASK_FLAG_IN_SYSCALL;
    call = sys_syscall_handlers[n_syscall];
    if (!call) {
        irq_on();
        goto return_from_syscall;
    }
    call(*a1, *a2, *a3, *a4, *a5);

    /* sys_exec uses r0 as args for main()*/
    if (n_syscall != SYS_EXEC) {
        asm volatile(
            "mov %0, r0"
            : "=r"(*((uint32_t *)(_cur_task->tb.sp + EXTRA_FRAME_SIZE))));
    } else {
        asm volatile(
            "mov r0, %0"
            :: "r"(_cur_task->tb.arg));
    }

    /* out of syscall */
    _cur_task->tb.flags &= (~TASK_FLAG_IN_SYSCALL);
    irq_on();

    if (_cur_task->tb.state != TASK_RUNNING) {
        task_switch();
    }

return_from_syscall:

    /* write new stack pointer and restore context */
    if (in_kernel()) {
        asm volatile("msr " MSP ", %0" ::"r"(_cur_task->tb.sp));
        asm volatile("isb");
        restore_kernel_context();
        runnable = RUN_KERNEL;
        mpu_task_on(0);
    } else {
        asm volatile("msr " PSP ", %0" ::"r"(_cur_task->tb.sp));
        asm volatile("isb");
        restore_task_context();
        runnable = RUN_USER;
        mpu_task_on(_cur_task->tb.pid);
    }

    /* Set control bit for non-kernel threads */
    if (_cur_task->tb.pid != 0) {
        asm volatile("msr CONTROL, %0" ::"r"(0x01));
    } else {
        asm volatile("msr CONTROL, %0" ::"r"(0x00));
    }
    asm volatile("isb");

    /* Set return value selected by the restore procedure */
    asm volatile("mov lr, %0" ::"r"(runnable));

    /* return (function is naked) */
    asm volatile("bx lr");
}

#pragma GCC pop_options
