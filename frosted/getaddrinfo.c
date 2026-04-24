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
#include "socket_in.h"
#include "net.h"
#include "locks.h"
#include "wolfip.h"
#include "mdns.h"
#include <string.h>

#ifndef EAI_FAIL
#define EAI_BADFLAGS     (-1)
#define EAI_NONAME       (-2)
#define EAI_AGAIN        (-3)
#define EAI_FAIL         (-4)
#define EAI_NODATA       (-5)
#define EAI_FAMILY       (-6)
#define EAI_SOCKTYPE     (-7)
#define EAI_SERVICE      (-8)
#define EAI_ADDRFAMILY   (-9)
#define EAI_MEMORY       (-10)
#endif

#ifndef AI_PASSIVE
#define AI_PASSIVE     (1 << 0)
#define AI_CANONNAME   (1 << 1)
#define AI_NUMERICHOST (1 << 2)
#define AI_NUMERICSERV (1 << 3)
#endif

struct addrinfo;
static int dns_getaddrinfo(const char *node,
                           const char *service,
                           const struct addrinfo *hints,
                           struct addrinfo **res);
static int dns_freeaddrinfo(struct addrinfo *res);

#define DNS_TIMEOUT_MS 5000U

static int parse_ipv4_literal(const char *node, uint32_t *addr)
{
    uint32_t parts[4];
    uint32_t value = 0;
    int idx = 0;
    const char *p = node;

    if (!node || !node[0])
        return -1;

    while (*p) {
        if (*p == '.') {
            if (idx >= 3)
                return -1;
            parts[idx++] = value;
            value = 0;
            p++;
            continue;
        }
        if (*p < '0' || *p > '9')
            return -1;
        value = value * 10U + (uint32_t)(*p - '0');
        if (value > 255U)
            return -1;
        p++;
    }
    if (idx != 3)
        return -1;
    parts[3] = value;
    *addr = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 0;
}

static int parse_service_port(const char *service, uint16_t *port)
{
    uint32_t val = 0;
    const char *p;

    if (!port)
        return -EAI_SERVICE;
    if (!service || service[0] == '\0') {
        *port = 0;
        return 0;
    }

    for (p = service; *p; p++) {
        if (*p < '0' || *p > '9')
            return -EAI_SERVICE;
        val = val * 10U + (uint32_t)(*p - '0');
        if (val > 65535U)
            return -EAI_SERVICE;
    }

    *port = (uint16_t)val;
    return 0;
}

static int build_addrinfo(uint32_t addr,
                          uint16_t port,
                          const char *canon,
                          const struct addrinfo *hints,
                          struct addrinfo **res)
{
    struct addrinfo *ai;
    struct sockaddr_in *sin;

    ai = kalloc(sizeof(*ai));
    if (!ai)
        return -EAI_MEMORY;
    sin = kalloc(sizeof(*sin));
    if (!sin) {
        kfree(ai);
        return -EAI_MEMORY;
    }

    memset(ai, 0, sizeof(*ai));
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_port = ee16(port);
    sin->sin_addr.s_addr = ee32(addr);

    ai->ai_family = AF_INET;
    ai->ai_socktype = hints ? hints->ai_socktype : 0;
    ai->ai_protocol = hints ? hints->ai_protocol : 0;
    ai->ai_addrlen = sizeof(*sin);
    ai->ai_addr = (struct sockaddr *)sin;
    ai->ai_next = NULL;

    if (canon && hints && (hints->ai_flags & AI_CANONNAME)) {
        size_t len = strlen(canon) + 1U;
        ai->ai_canonname = kalloc(len);
        if (!ai->ai_canonname) {
            kfree(sin);
            kfree(ai);
            return -EAI_MEMORY;
        }
        memcpy(ai->ai_canonname, canon, len);
    }

    *res = ai;
    return 0;
}

#if CONFIG_TCPIP

extern struct wolfIP *IPStack;

static mutex_t *dns_reslv_mutex;
static struct task *dns_waiter;
static volatile uint32_t dns_reslv_ip;
static volatile int dns_reslv_ready;
static int dns_reslv_timer_id = -1;

static void dns_reslv_cb(uint32_t ip)
{
    dns_reslv_ip = ip;
    dns_reslv_ready = 1;
    if (dns_reslv_timer_id >= 0) {
        ktimer_del(dns_reslv_timer_id);
        dns_reslv_timer_id = -1;
    }
    if (dns_waiter) {
        struct task *w = dns_waiter;
        dns_waiter = NULL;
        task_resume(w);
    }
}

static void dns_reslv_timeout(uint32_t ms, void *arg)
{
    (void)ms;
    (void)arg;
    dns_reslv_timer_id = -1;
    if (!dns_reslv_ready && dns_waiter) {
        struct task *w = dns_waiter;
        dns_waiter = NULL;
        task_resume(w);
    }
}

static int resolve_hostname(const char *node, uint32_t *addr)
{
    uint16_t id = 0;

    if (!IPStack)
        return -EAI_FAIL;

    if (mdns_is_local_name(node)) {
        uint32_t mip = 0;
        if (mdns_lookup(node, &mip) == 0) {
            *addr = mip;
            return 0;
        }
        return -EAI_NONAME;
    }

    if (!dns_reslv_mutex)
        dns_reslv_mutex = mutex_init();
    if (!dns_reslv_mutex)
        return -EAI_FAIL;

    mutex_lock(dns_reslv_mutex);
    dns_reslv_ready = 0;
    dns_reslv_ip = 0;
    dns_waiter = this_task();

    if (nslookup(IPStack, node, &id, dns_reslv_cb) < 0) {
        dns_waiter = NULL;
        mutex_unlock(dns_reslv_mutex);
        return -EAI_AGAIN;
    }

    dns_reslv_timer_id = ktimer_add(DNS_TIMEOUT_MS, dns_reslv_timeout, NULL);
    task_suspend();
    if (dns_reslv_timer_id >= 0) {
        ktimer_del(dns_reslv_timer_id);
        dns_reslv_timer_id = -1;
    }
    dns_waiter = NULL;

    if (dns_reslv_ready && dns_reslv_ip) {
        *addr = dns_reslv_ip;
        mutex_unlock(dns_reslv_mutex);
        return 0;
    }
    mutex_unlock(dns_reslv_mutex);
    return -EAI_AGAIN;
}

#else

static int resolve_hostname(const char *node, uint32_t *addr)
{
    (void)node;
    (void)addr;
    return -EAI_FAIL;
}

#endif

static int dns_getaddrinfo(const char *node,
                           const char *service,
                           const struct addrinfo *hints,
                           struct addrinfo **res)
{
    uint32_t addr = 0;
    uint16_t port = 0;
    int ret;
    int flags = hints ? hints->ai_flags : 0;

    if (!res)
        return -EAI_FAIL;
    *res = NULL;

    if (hints && hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET)
        return -EAI_FAMILY;

    ret = parse_service_port(service, &port);
    if (ret)
        return ret;

    if (!node || node[0] == '\0') {
        addr = (flags & AI_PASSIVE) ? 0U : 0x7f000001U;
    } else if (flags & AI_NUMERICHOST) {
        if (parse_ipv4_literal(node, &addr) != 0)
            return -EAI_NONAME;
    } else {
        if (parse_ipv4_literal(node, &addr) != 0) {
            ret = resolve_hostname(node, &addr);
            if (ret)
                return ret;
        }
    }

    ret = build_addrinfo(addr, port, node, hints, res);
    return ret;
}

static int dns_freeaddrinfo(struct addrinfo *res)
{
    struct addrinfo *next;
    if (!res)
        return -EINVAL;

    while (res) {
        next = res->ai_next;
        if (res->ai_addr)
            kfree(res->ai_addr);
        if (res->ai_canonname)
            kfree(res->ai_canonname);
        kfree(res);
        res = next;
    }
    return 0;
}

int sys_getaddrinfo_hdlr(const char *node, const char *service, 
        const struct addrinfo *hints, struct addrinfo **res)
{
    if (!res)
        return -EINVAL;
    if ((node && task_ptr_valid(node)) ||
        (service && task_ptr_valid(service)) ||
        (hints && task_ptr_valid(hints)) ||
        task_ptr_valid(res))
        return -EACCES;
    return dns_getaddrinfo(node, service, hints, res);
}

int sys_freeaddrinfo_hdlr(struct addrinfo *res)
{
    if (!res)
        return -EINVAL;
    if (task_ptr_valid(res))
        return -EACCES;
    return dns_freeaddrinfo(res);
}
