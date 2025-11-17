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

static mutex_t *dns_mutex;
static volatile uint32_t dns_result_ip;
static volatile int dns_result_ready;

static void dns_mutex_init(void)
{
    if (!dns_mutex)
        dns_mutex = mutex_init();
}

static void wolfip_dns_cb(uint32_t ip)
{
    dns_result_ip = ee32(ip);
    dns_result_ready = 1;
}

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

static int resolve_hostname(const char *node, uint32_t *addr)
{
    uint16_t id;
    uint32_t start;
    int ret;

    if (!IPStack)
        return -EAI_FAIL;

    dns_mutex_init();
retry_lock:
    if (mutex_trylock(dns_mutex) != 0)
        return SYS_CALL_AGAIN;
    dns_result_ready = 0;
    dns_result_ip = 0;

    if (tcpip_trylock() != 0) {
        mutex_unlock(dns_mutex);
        return SYS_CALL_AGAIN;
    }
    ret = nslookup(IPStack, node, &id, wolfip_dns_cb);
    tcpip_unlock();

    if (ret < 0) {
        mutex_unlock(dns_mutex);
        if (ret == -16)
            return -EAI_AGAIN;
        if (ret == -101)
            return -EAI_FAIL;
        return -EAI_FAIL;
    }

    start = jiffies;
    while (!dns_result_ready) {
        uint32_t elapsed = jiffies - start;
        if (elapsed >= DNS_TIMEOUT_MS) {
            mutex_unlock(dns_mutex);
            return -EAI_AGAIN;
        }
        kthread_sleep_ms(10);
    }

    *addr = dns_result_ip;
    mutex_unlock(dns_mutex);
    return 0;
}

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
