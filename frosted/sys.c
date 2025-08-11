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
#include "sys/frosted.h"
#include "sys/frosted-io.h"
#include "sys/reboot.h"
#include "lowpower.h"
#include "string.h"
#include "time.h"

#ifndef CLOCK_MONOTONIC
# define CLOCK_MONOTONIC (4)
#endif

struct timeval_kernel
{
    /* Assuming newlib time_t is long */
    long tv_sec;
    long tv_usec;
};

#ifdef CONFIG_LOWPOWER
int sys_suspend_hdlr(uint32_t interval)
{
    if (interval > 0) {
        lowpower_sleep(0, interval);
    }
    return 0;
}

int sys_standby_hdlr(uint32_t interval)
{
    if (interval > 0) {
        lowpower_sleep(1, interval);
    }
    return 0;
}
#else
int sys_suspend_hdlr(uint32_t interval)
{
    return -ENOSYS;
}

int sys_standby_hdlr(uint32_t interval)
{
    return -ENOSYS;
}
#endif

#ifndef CONFIG_PTY_UNIX
int sys_ptsname_hdlr(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5)
{
    return -ENOSYS;
}
#endif

int sys_clock_gettime_hdlr(clockid_t clkid, struct timeval_kernel *now)
{
    if (!now)
        return -EINVAL;
    if (task_ptr_valid(now))
        return -EACCES;

    if (clkid == CLOCK_MONOTONIC) {
        now->tv_sec = jiffies / 1000;
        now->tv_usec = (jiffies % 1000) * 1000;
    } else if (clkid == CLOCK_REALTIME) {
        now->tv_sec = rt_offset + (jiffies / 1000);
        now->tv_usec = ((jiffies % 1000) * 1000) * 1000;
    }

    return 0;
}

int sys_clock_settime_hdlr(clockid_t clkid, struct timeval_kernel *now)
{
    if (clkid == CLOCK_MONOTONIC)
        return -EPERM;
    if (!now)
        return -EINVAL;
    if (task_ptr_valid(now))
        return -EACCES;
    if (clkid == CLOCK_REALTIME) {
        unsigned int temp = now->tv_sec;
        temp = temp + (now->tv_usec / 1000 / 1000);
        rt_offset = temp - (jiffies / 1000);
    }
    return 0;
}

#define AIRCR *(volatile uint32_t *)(0xE000ED0C)
#define AIRCR_VKEY (0x05FA << 16)
#   define AIRCR_SYSRESETREQ (1 << 2)

static inline void arch_armv8m_reboot(void)
{
    AIRCR = AIRCR_SYSRESETREQ | AIRCR_VKEY;
    while(1)
        ;
}

int sys_reboot_hdlr(uint32_t fadeoff, int cmd, uint32_t interval)
{
    if (fadeoff != SYS_FROSTED_FADEOFF)
        return -EINVAL;
    switch(cmd) {
        case RB_REBOOT:
            arch_armv8m_reboot();
            break;
        case RB_STANDBY:
            lowpower_sleep(1, interval);
            break;
        case RB_SUSPEND:
            lowpower_sleep(0, interval);
            return 0;
        default:
            return -ENOENT;
    }
    return -EFAULT;
}


struct utsname {
    char sysname[16];    /* Operating system name (e.g., "Frosted") */
    char nodename[16];   /* Name within network */
    char release[16];    /* Operating system release (e.g., "16.03") */
    char version[16];    /* Operating system version (e.g., "16") */
    char machine[16];    /* Hardware identifier */
    char domainname[16]; /* NIS or YP domain name */
};

const struct utsname uts_frosted = { "Frosted", "frosted", "16.03", "16", "arm", "local"};

int sys_uname_hdlr( struct utsname *uts)
{
    if (!uts)
        return -EFAULT;
    if (task_ptr_valid(uts))
        return -EACCES;
    memcpy(uts, &uts_frosted, sizeof(struct utsname));
    return 0;
}
