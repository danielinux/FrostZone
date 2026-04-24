/*
 *      This file is part of frosted.
 *
 *      frosted is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the Free Software Foundation.
 *
 *      In-kernel multicast DNS (RFC 6762) responder + resolver.
 *      Answers A-record queries for <hostname>.local with the primary
 *      interface address, and provides mdns_lookup() for .local names.
 */

#include "frosted.h"
#include "config.h"
#include "wolfip.h"
#include "mdns.h"
#include <string.h>

#if CONFIG_MDNS

#ifndef CONFIG_MDNS_HOSTNAME
#define CONFIG_MDNS_HOSTNAME "frosted"
#endif

#define MDNS_PORT          5353U
#define MDNS_MCAST_IP      0xE00000FBU   /* 224.0.0.251 */
#define MDNS_TTL           120U
#define MDNS_LOCAL_SUFFIX  "local"

#define DNS_FLAG_QR        0x8000U
#define DNS_FLAG_AA        0x0400U
#define DNS_TYPE_A         1U
#define DNS_CLASS_IN       1U
#define DNS_CLASS_IN_FLUSH 0x8001U
#define DNS_NAME_MAX_LEN   255U
#define DNS_LABEL_MAX_LEN  63U

#define MDNS_LOOKUP_TIMEOUT_MS 2000U
#define MDNS_RX_BUF_SZ         512U
#define MDNS_TX_BUF_SZ         512U

extern struct wolfIP *IPStack;

struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

static int mdns_sock = -1;
static uint8_t mdns_rxbuf[MDNS_RX_BUF_SZ];
static uint8_t mdns_txbuf[MDNS_TX_BUF_SZ];

/* Lookup state — serialised via mdns_mutex. */
static mutex_t *mdns_mutex;
static struct task *mdns_waiter;
static volatile uint32_t mdns_result_ip;
static volatile int mdns_result_ready;
static volatile uint16_t mdns_pending_id;
static char mdns_pending_name[DNS_NAME_MAX_LEN];
static size_t mdns_pending_len;
static int mdns_timeout_tid = -1;

static size_t str_copy_lower(char *dst, const char *src, size_t max)
{
    size_t i = 0;
    while (src[i] && i + 1 < max) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        dst[i] = c;
        i++;
    }
    dst[i] = '\0';
    return i;
}

/* Encode "foo.bar" as "\x03foo\x03bar\x00". Returns encoded length (incl. 0), or <0. */
static int dns_name_encode(const char *name, uint8_t *out, size_t outlen)
{
    const char *p = name;
    size_t pos = 0;
    while (*p) {
        const char *seg = p;
        size_t seg_len = 0;
        while (*p && *p != '.') {
            p++;
            seg_len++;
        }
        if (seg_len == 0 || seg_len > DNS_LABEL_MAX_LEN)
            return -1;
        if (pos + seg_len + 1 >= outlen)
            return -1;
        out[pos++] = (uint8_t)seg_len;
        memcpy(out + pos, seg, seg_len);
        pos += seg_len;
        if (*p == '.')
            p++;
    }
    if (pos + 1 > outlen)
        return -1;
    out[pos++] = 0;
    return (int)pos;
}

/* Decode DNS name (handles compression pointers) into dotted lowercase string. */
static int dns_name_decode(const uint8_t *msg, size_t msg_len, size_t off,
                           char *out, size_t outlen, size_t *name_end_off)
{
    size_t pos = off;
    size_t out_pos = 0;
    int jumped = 0;
    size_t end_off = off;
    int hops = 0;

    while (pos < msg_len) {
        uint8_t len = msg[pos];
        if (len == 0) {
            pos++;
            if (!jumped)
                end_off = pos;
            break;
        }
        if ((len & 0xC0) == 0xC0) {
            if (pos + 1 >= msg_len)
                return -1;
            if (!jumped) {
                end_off = pos + 2;
                jumped = 1;
            }
            pos = ((size_t)(len & 0x3F) << 8) | msg[pos + 1];
            if (++hops > 8)
                return -1;
            continue;
        }
        if (len > DNS_LABEL_MAX_LEN || pos + 1 + len > msg_len)
            return -1;
        if (out_pos && out_pos + 1 < outlen)
            out[out_pos++] = '.';
        for (uint8_t i = 0; i < len && out_pos + 1 < outlen; i++) {
            char c = (char)msg[pos + 1 + i];
            if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
            out[out_pos++] = c;
        }
        pos += 1 + len;
    }
    if (out_pos >= outlen)
        return -1;
    out[out_pos] = '\0';
    if (name_end_off)
        *name_end_off = end_off;
    return (int)out_pos;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t sl = strlen(s);
    size_t xl = strlen(suffix);
    if (xl > sl)
        return 0;
    return strcmp(s + sl - xl, suffix) == 0;
}

/* "hostname.local" (lowercase) — built at first use. */
static char mdns_fqdn[64];
static size_t mdns_fqdn_len;

static void mdns_fqdn_init(void)
{
    size_t n;
    if (mdns_fqdn_len)
        return;
    n = str_copy_lower(mdns_fqdn, CONFIG_MDNS_HOSTNAME, sizeof(mdns_fqdn) - 7);
    if (n + 7 > sizeof(mdns_fqdn))
        n = sizeof(mdns_fqdn) - 7;
    memcpy(mdns_fqdn + n, ".local", 7);
    mdns_fqdn_len = n + 6;
}

static uint32_t mdns_primary_ip(void)
{
    ip4 ip = 0, nm = 0, gw = 0;
    unsigned int idx;
    for (idx = 0; idx < WOLFIP_MAX_INTERFACES; idx++) {
        wolfIP_ipconfig_get_ex(IPStack, idx, &ip, &nm, &gw);
        if (ip && ip != 0x7F000001U)
            return ip;
    }
    return 0;
}

static void mdns_send_response(uint16_t query_id, const uint8_t *qname,
                               size_t qname_len, uint32_t ip,
                               const struct wolfIP_sockaddr_in *dst)
{
    struct dns_header *hdr = (struct dns_header *)mdns_txbuf;
    size_t pos;
    uint32_t ttl_be;
    uint16_t type_be, class_be, rdlen_be;
    uint32_t ip_be;

    if (qname_len == 0 || qname_len + sizeof(*hdr) + 10 + 4 > sizeof(mdns_txbuf))
        return;

    memset(hdr, 0, sizeof(*hdr));
    hdr->id = ee16(query_id);
    hdr->flags = ee16(DNS_FLAG_QR | DNS_FLAG_AA);
    hdr->qdcount = 0;
    hdr->ancount = ee16(1);
    pos = sizeof(*hdr);

    memcpy(mdns_txbuf + pos, qname, qname_len);
    pos += qname_len;

    type_be = ee16(DNS_TYPE_A);
    memcpy(mdns_txbuf + pos, &type_be, 2);
    pos += 2;
    class_be = ee16(DNS_CLASS_IN_FLUSH);
    memcpy(mdns_txbuf + pos, &class_be, 2);
    pos += 2;
    ttl_be = ee32(MDNS_TTL);
    memcpy(mdns_txbuf + pos, &ttl_be, 4);
    pos += 4;
    rdlen_be = ee16(4);
    memcpy(mdns_txbuf + pos, &rdlen_be, 2);
    pos += 2;
    ip_be = ee32(ip);
    memcpy(mdns_txbuf + pos, &ip_be, 4);
    pos += 4;

    (void)wolfIP_sock_sendto(IPStack, mdns_sock, mdns_txbuf, pos, 0,
                             (const struct wolfIP_sockaddr *)dst,
                             sizeof(*dst));
}

static void mdns_handle_query(const uint8_t *msg, size_t msg_len,
                              const struct wolfIP_sockaddr_in *src)
{
    const struct dns_header *hdr;
    size_t pos;
    uint16_t qdcount;
    uint32_t our_ip;
    struct wolfIP_sockaddr_in mcast_dst;

    if (msg_len < sizeof(*hdr))
        return;
    hdr = (const struct dns_header *)msg;
    if (hdr->flags & ee16(DNS_FLAG_QR))
        return; /* response, not a query */

    qdcount = ee16(hdr->qdcount);
    pos = sizeof(*hdr);
    our_ip = mdns_primary_ip();
    if (!our_ip)
        return;

    memset(&mcast_dst, 0, sizeof(mcast_dst));
    mcast_dst.sin_family = AF_INET;
    mcast_dst.sin_port = ee16(MDNS_PORT);
    mcast_dst.sin_addr.s_addr = ee32(MDNS_MCAST_IP);

    while (qdcount-- > 0 && pos < msg_len) {
        char name[DNS_NAME_MAX_LEN];
        size_t name_start = pos;
        size_t end_off = 0;
        uint16_t qtype, qclass;
        int name_len;

        name_len = dns_name_decode(msg, msg_len, pos, name, sizeof(name), &end_off);
        if (name_len < 0)
            return;
        pos = end_off;
        if (pos + 4 > msg_len)
            return;
        memcpy(&qtype, msg + pos, 2);
        qtype = ee16(qtype);
        memcpy(&qclass, msg + pos + 2, 2);
        qclass = ee16(qclass);
        pos += 4;

        if ((qclass & 0x7FFFU) != DNS_CLASS_IN)
            continue;
        if (qtype != DNS_TYPE_A && qtype != 255 /* ANY */)
            continue;

        mdns_fqdn_init();
        if (strcmp(name, mdns_fqdn) != 0)
            continue;

        /* Reply from name_start in original message (already wire format). */
        {
            const uint8_t *qname = msg + name_start;
            size_t qname_len;
            /* Recompute raw length: end_off - name_start, but if it used
             * compression the raw bytes in the query are still the same. */
            qname_len = end_off - name_start;
            /* If the original query used a compression pointer, encode
             * fresh. Otherwise pass through. */
            if (qname_len == 2 && (msg[name_start] & 0xC0) == 0xC0) {
                uint8_t fresh[DNS_NAME_MAX_LEN];
                int fresh_len = dns_name_encode(mdns_fqdn, fresh, sizeof(fresh));
                if (fresh_len < 0)
                    continue;
                mdns_send_response(ee16(hdr->id), fresh, (size_t)fresh_len,
                                   our_ip, &mcast_dst);
            } else {
                mdns_send_response(ee16(hdr->id), qname, qname_len,
                                   our_ip, &mcast_dst);
            }
        }
    }
}

static void mdns_handle_response(const uint8_t *msg, size_t msg_len)
{
    const struct dns_header *hdr;
    size_t pos;
    uint16_t ancount;

    if (!mdns_waiter)
        return;
    if (msg_len < sizeof(*hdr))
        return;
    hdr = (const struct dns_header *)msg;
    if (!(hdr->flags & ee16(DNS_FLAG_QR)))
        return;

    ancount = ee16(hdr->ancount);
    pos = sizeof(*hdr);

    /* Skip any questions (mDNS responses may echo them, usually not). */
    {
        uint16_t qd = ee16(hdr->qdcount);
        while (qd-- > 0 && pos < msg_len) {
            size_t end_off = 0;
            char tmp[DNS_NAME_MAX_LEN];
            if (dns_name_decode(msg, msg_len, pos, tmp, sizeof(tmp), &end_off) < 0)
                return;
            pos = end_off;
            if (pos + 4 > msg_len)
                return;
            pos += 4;
        }
    }

    while (ancount-- > 0 && pos < msg_len) {
        char name[DNS_NAME_MAX_LEN];
        size_t end_off = 0;
        uint16_t type_be, class_be, rdlen_be;
        uint32_t ttl_be;
        uint16_t rtype, rdlen;
        int name_len;

        name_len = dns_name_decode(msg, msg_len, pos, name, sizeof(name), &end_off);
        if (name_len < 0)
            return;
        pos = end_off;
        if (pos + 10 > msg_len)
            return;
        memcpy(&type_be, msg + pos, 2);
        rtype = ee16(type_be);
        memcpy(&class_be, msg + pos + 2, 2);
        (void)class_be;
        memcpy(&ttl_be, msg + pos + 4, 4);
        (void)ttl_be;
        memcpy(&rdlen_be, msg + pos + 8, 2);
        rdlen = ee16(rdlen_be);
        pos += 10;
        if (pos + rdlen > msg_len)
            return;

        if (rtype == DNS_TYPE_A && rdlen == 4 &&
            mdns_pending_len && strcmp(name, mdns_pending_name) == 0) {
            uint32_t ip_be;
            memcpy(&ip_be, msg + pos, 4);
            mdns_result_ip = ee32(ip_be);
            mdns_result_ready = 1;
            if (mdns_timeout_tid >= 0) {
                ktimer_del(mdns_timeout_tid);
                mdns_timeout_tid = -1;
            }
            if (mdns_waiter) {
                struct task *w = mdns_waiter;
                mdns_waiter = NULL;
                task_resume(w);
            }
            return;
        }
        pos += rdlen;
    }
}

static void mdns_socket_event(int sockfd, uint16_t events, void *arg)
{
    (void)arg;
    if (!(events & CB_EVENT_READABLE))
        return;

    for (;;) {
        struct wolfIP_sockaddr_in src;
        socklen_t addrlen = sizeof(src);
        int ret = wolfIP_sock_recvfrom(IPStack, sockfd, mdns_rxbuf,
                                       sizeof(mdns_rxbuf), 0,
                                       (struct wolfIP_sockaddr *)&src,
                                       &addrlen);
        if (ret <= 0)
            break;
        if ((size_t)ret < sizeof(struct dns_header))
            continue;
        {
            const struct dns_header *hdr = (const struct dns_header *)mdns_rxbuf;
            if (hdr->flags & ee16(DNS_FLAG_QR))
                mdns_handle_response(mdns_rxbuf, (size_t)ret);
            else
                mdns_handle_query(mdns_rxbuf, (size_t)ret, &src);
        }
    }
}

static void mdns_lookup_timeout(uint32_t ms, void *arg)
{
    (void)ms;
    (void)arg;
    mdns_timeout_tid = -1;
    if (!mdns_result_ready && mdns_waiter) {
        struct task *w = mdns_waiter;
        mdns_waiter = NULL;
        task_resume(w);
    }
}

void mdns_init(void)
{
#ifdef IP_MULTICAST
    struct wolfIP_sockaddr_in bind_addr;
    struct wolfIP_ip_mreq mreq;
    int one = 1;
    uint8_t ttl_one = 1;

    if (!IPStack || mdns_sock >= 0)
        return;

    mdns_fqdn_init();
    if (!mdns_mutex)
        mdns_mutex = mutex_init();

    mdns_sock = wolfIP_sock_socket(IPStack, AF_INET, IPSTACK_SOCK_DGRAM, 0);
    if (mdns_sock < 0)
        return;

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = ee16(MDNS_PORT);
    bind_addr.sin_addr.s_addr = 0;
    if (wolfIP_sock_bind(IPStack, mdns_sock,
                         (struct wolfIP_sockaddr *)&bind_addr,
                         sizeof(bind_addr)) < 0) {
        wolfIP_sock_close(IPStack, mdns_sock);
        mdns_sock = -1;
        return;
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = ee32(MDNS_MCAST_IP);
    mreq.imr_interface.s_addr = 0;
    (void)wolfIP_sock_setsockopt(IPStack, mdns_sock, WOLFIP_SOL_IP,
                                 WOLFIP_IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    (void)wolfIP_sock_setsockopt(IPStack, mdns_sock, WOLFIP_SOL_IP,
                                 WOLFIP_IP_MULTICAST_TTL, &ttl_one,
                                 sizeof(ttl_one));
    (void)one;

    wolfIP_register_callback(IPStack, mdns_sock, mdns_socket_event, NULL);
}
#else
void mdns_init(void) { }
#endif

#ifdef IP_MULTICAST
static int mdns_send_query(const char *dotted_name)
{
    struct dns_header *hdr = (struct dns_header *)mdns_txbuf;
    struct wolfIP_sockaddr_in dst;
    int enc_len;
    size_t pos;
    uint16_t type_be, class_be;

    enc_len = dns_name_encode(dotted_name, mdns_txbuf + sizeof(*hdr),
                              sizeof(mdns_txbuf) - sizeof(*hdr) - 4);
    if (enc_len < 0)
        return -1;

    memset(hdr, 0, sizeof(*hdr));
    mdns_pending_id = (uint16_t)(wolfIP_getrandom() & 0xFFFFU);
    hdr->id = ee16(mdns_pending_id);
    hdr->flags = 0;
    hdr->qdcount = ee16(1);
    pos = sizeof(*hdr) + (size_t)enc_len;

    type_be = ee16(DNS_TYPE_A);
    memcpy(mdns_txbuf + pos, &type_be, 2);
    pos += 2;
    class_be = ee16(DNS_CLASS_IN);
    memcpy(mdns_txbuf + pos, &class_be, 2);
    pos += 2;

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = ee16(MDNS_PORT);
    dst.sin_addr.s_addr = ee32(MDNS_MCAST_IP);

    return wolfIP_sock_sendto(IPStack, mdns_sock, mdns_txbuf, pos, 0,
                              (const struct wolfIP_sockaddr *)&dst,
                              sizeof(dst));
}

int mdns_lookup(const char *name, uint32_t *addr)
{
    int ret = -1;
    size_t nlen;

    if (!name || !addr || !IPStack || mdns_sock < 0)
        return -1;
    if (!mdns_mutex)
        return -1;

    nlen = strlen(name);
    if (nlen == 0 || nlen >= sizeof(mdns_pending_name))
        return -1;

    mutex_lock(mdns_mutex);

    str_copy_lower(mdns_pending_name, name, sizeof(mdns_pending_name));
    mdns_pending_len = nlen;
    mdns_result_ready = 0;
    mdns_result_ip = 0;
    mdns_waiter = this_task();

    if (mdns_send_query(mdns_pending_name) < 0) {
        mdns_waiter = NULL;
        mdns_pending_len = 0;
        mutex_unlock(mdns_mutex);
        return -1;
    }

    mdns_timeout_tid = ktimer_add(MDNS_LOOKUP_TIMEOUT_MS,
                                  mdns_lookup_timeout, NULL);
    task_suspend();
    /* Resumed by rx callback or timeout. */
    if (mdns_timeout_tid >= 0) {
        ktimer_del(mdns_timeout_tid);
        mdns_timeout_tid = -1;
    }
    mdns_waiter = NULL;

    if (mdns_result_ready) {
        *addr = mdns_result_ip;
        ret = 0;
    }
    mdns_pending_len = 0;
    mutex_unlock(mdns_mutex);
    return ret;
}
#else
int mdns_lookup(const char *name, uint32_t *addr)
{
    (void)name;
    (void)addr;
    return -1;
}
#endif

int mdns_is_local_name(const char *name)
{
    if (!name)
        return 0;
    return ends_with(name, "." MDNS_LOCAL_SUFFIX) || ends_with(name, MDNS_LOCAL_SUFFIX);
}

#endif /* CONFIG_MDNS */
