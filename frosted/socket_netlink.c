/*
 *      This file is part of frosted.
 *
 *      AF_NETLINK (NETLINK_ROUTE) socket family. Structures follow the Linux
 *      rtnetlink ABI so application code ports with minimal changes, but the
 *      backend talks directly to wolfIP via the sock_io_* helpers shared with
 *      socket_in.c — there is no parallel kernel route table.
 *
 *      Phase 1 coverage:
 *        - NETLINK_ROUTE only (one protocol per AF_NETLINK socket)
 *        - RTM_GETLINK / RTM_NEWLINK (flags up/down)
 *        - RTM_GETADDR / RTM_NEWADDR / RTM_DELADDR (IPv4)
 *        - RTM_GETROUTE / RTM_NEWROUTE / RTM_DELROUTE
 *        - sendto + sendmsg (single nlmsghdr per datagram)
 *        - DUMP responses fully materialised inside sendto — recv* never blocks
 *      Deferred: multicast groups, RTMGRP_* broadcast, IPv6 families,
 *      setsockopt(NETLINK_ADD_MEMBERSHIP), neighbor/RTM_*NEIGH messages.
 */

#include "frosted.h"
#include "config.h"
#include "string.h"
#include "fcntl.h"
#include "errno.h"

#if CONFIG_TCPIP

#include "socket_in.h"     /* pulls in wolfip.h: AF_INET, struct sockaddr_in,
                              struct iovec, struct msghdr, socklen_t */
#include <net/if.h>        /* IFF_*, struct ifreq, IFNAMSIZ */
#include <net/route.h>     /* struct rtentry */
#include <frosted/netlink.h>
#include <frosted/rtnetlink.h>

/* Not provided by wolfip.h in kernel mode — define minimally here.
 * SOCK_DGRAM is already 17 from frosted_api.h; leave it alone. */
#ifndef SOCK_RAW
#define SOCK_RAW   3
#endif
#ifndef AF_UNSPEC
#define AF_UNSPEC  0
#endif

/* socket_in.c keeps these private as file-scoped defines; mirror them here
 * — wolfIP itself reserves idx 0 for loopback by convention. */
#ifndef FROSTED_WOLFIP_LOOPBACK_IF_IDX
#define FROSTED_WOLFIP_LOOPBACK_IF_IDX 0U
#endif

/* Small strnlen since kernel libc doesn't export it. */
static unsigned int nl_strnlen(const char *s, unsigned int maxlen)
{
    unsigned int i = 0;
    while (i < maxlen && s[i])
        i++;
    return i;
}

extern struct wolfIP *IPStack;

/* Allocation caps — netlink is small-message RPC; a single dump must not
 * be able to exhaust kernel memory. */
#define NL_MSG_MAX     4096
#define NL_QUEUE_MAX  16384

/* Bitcount helper for converting netmask <-> prefix length. */
static uint8_t mask_to_prefix(uint32_t nm_host)
{
    uint8_t n = 0;
    while (nm_host & 0x80000000u) {
        n++;
        nm_host <<= 1;
    }
    return n;
}

static uint32_t prefix_to_mask(uint8_t prefix)
{
    if (prefix == 0)
        return 0;
    if (prefix >= 32)
        return 0xFFFFFFFFu;
    return (uint32_t)(0xFFFFFFFFu << (32 - prefix));
}

static int nl_requires_admin(uint16_t type)
{
    switch (type) {
    case RTM_NEWLINK:
    case RTM_SETLINK:
    case RTM_NEWADDR:
    case RTM_DELADDR:
    case RTM_NEWROUTE:
    case RTM_DELROUTE:
        return 1;
    default:
        return 0;
    }
}

/* --- Per-socket state --------------------------------------------------- */

struct nl_reply {
    struct nl_reply *next;
    uint32_t         len;
    uint8_t          data[];
};

struct nl_sock {
    struct fnode    *node;
    int              fd;
    uint32_t         nl_pid;     /* 0 before bind / auto-assigned */
    uint32_t         nl_groups;
    int              protocol;
    int              type;
    uint32_t         queued_bytes;
    struct nl_reply *rx_head;
    struct nl_reply *rx_tail;
    struct nl_sock  *link_next;
};

static struct module mod_socket_nl;
static struct nl_sock *nl_socks = NULL;
static uint32_t nl_next_pid = 0xC0000000u;

/* --- Queue management --------------------------------------------------- */

static void nl_queue_drain(struct nl_sock *s)
{
    struct nl_reply *r = s->rx_head, *next;
    while (r) {
        next = r->next;
        kfree(r);
        r = next;
    }
    s->rx_head = s->rx_tail = NULL;
    s->queued_bytes = 0;
}

static int nl_queue_push(struct nl_sock *s, const void *buf, uint32_t len)
{
    struct nl_reply *r;
    if (s->queued_bytes + len > NL_QUEUE_MAX)
        return -ENOBUFS;
    r = kalloc(sizeof(*r) + len);
    if (!r)
        return -ENOMEM;
    r->next = NULL;
    r->len = len;
    memcpy(r->data, buf, len);
    if (!s->rx_tail)
        s->rx_head = r;
    else
        s->rx_tail->next = r;
    s->rx_tail = r;
    s->queued_bytes += len;
    return 0;
}

/* --- Netlink message emission ------------------------------------------ */

/* Append a fully-formed nlmsghdr to the socket's RX queue. */
static int nl_emit(struct nl_sock *s, uint16_t type, uint16_t flags,
                   uint32_t seq, uint32_t pid,
                   const void *payload, uint32_t paylen)
{
    uint8_t buf[NL_MSG_MAX];
    struct nlmsghdr *nh = (struct nlmsghdr *)buf;
    uint32_t aligned = NLMSG_ALIGN(NLMSG_HDRLEN + paylen);

    if (aligned > sizeof(buf))
        return -EMSGSIZE;

    memset(buf, 0, aligned);
    nh->nlmsg_len = NLMSG_LENGTH(paylen);
    nh->nlmsg_type = type;
    nh->nlmsg_flags = flags;
    nh->nlmsg_seq = seq;
    nh->nlmsg_pid = pid;
    if (paylen && payload)
        memcpy(buf + NLMSG_HDRLEN, payload, paylen);
    return nl_queue_push(s, buf, aligned);
}

/* Append NLMSG_DONE — multipart terminator. */
static int nl_emit_done(struct nl_sock *s, uint32_t seq, uint32_t pid)
{
    int32_t zero = 0;
    return nl_emit(s, NLMSG_DONE, NLM_F_MULTI, seq, pid, &zero, sizeof(zero));
}

/* Append NLMSG_ERROR — error=0 is an ack, non-zero is -errno. */
static int nl_emit_error(struct nl_sock *s, int error,
                         const struct nlmsghdr *orig)
{
    struct nlmsgerr body;
    memset(&body, 0, sizeof(body));
    body.error = error;
    if (orig)
        body.msg = *orig;
    return nl_emit(s, NLMSG_ERROR, 0,
                   orig ? orig->nlmsg_seq : 0,
                   orig ? orig->nlmsg_pid : 0,
                   &body, sizeof(body));
}

/* --- RTA builder: append a TLV to a caller-owned scratch buffer ---------- */

static int rta_append(uint8_t *buf, uint32_t bufsz, uint32_t *offp,
                      uint16_t type, const void *data, uint16_t datalen)
{
    uint32_t off = *offp;
    uint32_t rlen = RTA_LENGTH(datalen);
    uint32_t aligned = RTA_ALIGN(rlen);
    struct rtattr *rta;

    if (off + aligned > bufsz)
        return -EMSGSIZE;
    rta = (struct rtattr *)(buf + off);
    rta->rta_len = (unsigned short)rlen;
    rta->rta_type = type;
    if (datalen && data)
        memcpy(buf + off + RTA_LENGTH(0), data, datalen);
    /* Zero the alignment padding to keep userland tools happy. */
    if (aligned > rlen)
        memset(buf + off + rlen, 0, aligned - rlen);
    *offp = off + aligned;
    return 0;
}

/* --- Interface enumeration ---------------------------------------------- */

static int nl_dump_one_link(struct nl_sock *s, uint32_t seq, uint32_t pid,
                            unsigned int idx, struct wolfIP_ll_dev *ll)
{
    uint8_t buf[NL_MSG_MAX];
    struct ifinfomsg *ifi = (struct ifinfomsg *)buf;
    uint32_t off;
    ip4 ip, nm, gw;
    uint32_t mtu;

    memset(buf, 0, sizeof(buf));
    ifi->ifi_family = AF_UNSPEC;
    ifi->ifi_index = (int)(idx + 1); /* 1-based to match ifconfig output */
    ifi->ifi_type = (idx == FROSTED_WOLFIP_LOOPBACK_IF_IDX) ?
                        ARPHRD_LOOPBACK : ARPHRD_ETHER;
    ifi->ifi_flags = IFF_UP;
    if (idx == FROSTED_WOLFIP_LOOPBACK_IF_IDX)
        ifi->ifi_flags |= IFF_LOOPBACK;
    else
        ifi->ifi_flags |= IFF_RUNNING | IFF_MULTICAST | IFF_BROADCAST;
    ifi->ifi_change = 0xFFFFFFFFu;

    off = NLMSG_ALIGN(sizeof(*ifi));
    if (rta_append(buf, sizeof(buf), &off, IFLA_IFNAME,
                   ll->ifname, nl_strnlen(ll->ifname, IFNAMSIZ) + 1) < 0)
        return -EMSGSIZE;
    if (rta_append(buf, sizeof(buf), &off, IFLA_ADDRESS,
                   ll->mac, sizeof(ll->mac)) < 0)
        return -EMSGSIZE;
    mtu = ll->mtu ? ll->mtu : 1500;
    if (rta_append(buf, sizeof(buf), &off, IFLA_MTU, &mtu, sizeof(mtu)) < 0)
        return -EMSGSIZE;
    /* Broadcast MAC for completeness. */
    {
        static const uint8_t bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
        rta_append(buf, sizeof(buf), &off, IFLA_BROADCAST, bcast, sizeof(bcast));
    }

    (void)ip; (void)nm; (void)gw;
    return nl_emit(s, RTM_NEWLINK, NLM_F_MULTI, seq, pid, buf, off);
}

static int nl_handle_get_link(struct nl_sock *s, const struct nlmsghdr *nlh)
{
    unsigned int idx;
    int ret;
    for (idx = 0; idx < WOLFIP_MAX_INTERFACES; idx++) {
        struct wolfIP_ll_dev *ll = wolfIP_getdev_ex(IPStack, idx);
        if (!ll || ll->ifname[0] == '\0')
            continue;
        ret = nl_dump_one_link(s, nlh->nlmsg_seq, nlh->nlmsg_pid, idx, ll);
        if (ret < 0)
            return ret;
    }
    return nl_emit_done(s, nlh->nlmsg_seq, nlh->nlmsg_pid);
}

/* Walk rtattrs starting just past a fixed-size rtgen header. */
static struct rtattr *first_rta(const void *hdr, uint32_t hdrsize,
                                uint32_t payload_len, int *remaining)
{
    uint32_t off = NLMSG_ALIGN(hdrsize);
    if (off > payload_len) {
        *remaining = 0;
        return NULL;
    }
    *remaining = (int)(payload_len - off);
    return (struct rtattr *)((const uint8_t *)hdr + off);
}

static int nl_rta_payload(const struct rtattr *rta)
{
    int plen;

    if (!rta || rta->rta_len < RTA_LENGTH(0))
        return -1;
    plen = RTA_PAYLOAD(rta);
    return (plen < 0) ? -1 : plen;
}

static struct rtattr *nl_rta_next(struct rtattr *rta, int *remaining)
{
    int aligned;

    if (!rta || !remaining)
        return NULL;
    aligned = (int)RTA_ALIGN(rta->rta_len);
    if ((aligned <= 0) || (*remaining < aligned)) {
        *remaining = 0;
        return NULL;
    }
    *remaining -= aligned;
    return (struct rtattr *)((uint8_t *)rta + aligned);
}

static int nl_handle_set_link(struct nl_sock *s, const struct nlmsghdr *nlh)
{
    const struct ifinfomsg *ifi;
    struct rtattr *rta;
    int rem;
    struct wolfIP_ll_dev *ll;
    struct ifreq ifr;

    if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*ifi)))
        return nl_emit_error(s, -EINVAL, nlh);
    ifi = (const struct ifinfomsg *)NLMSG_DATA(nlh);
    if (ifi->ifi_index < 1 || (unsigned)ifi->ifi_index > WOLFIP_MAX_INTERFACES)
        return nl_emit_error(s, -ENODEV, nlh);
    ll = wolfIP_getdev_ex(IPStack, (unsigned)ifi->ifi_index - 1);
    if (!ll || ll->ifname[0] == '\0')
        return nl_emit_error(s, -ENODEV, nlh);

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ll->ifname, IFNAMSIZ);
    ifr.ifr_flags = (short)(ifi->ifi_flags & 0xFFFFu);

    /* IFLA_IFNAME override if provided. */
    rta = first_rta(ifi, sizeof(*ifi),
                    nlh->nlmsg_len - NLMSG_HDRLEN, &rem);
    while (rta && RTA_OK(rta, rem)) {
        int plen = nl_rta_payload(rta);
        if (rta->rta_type == IFLA_IFNAME && plen > 0) {
            if (plen > IFNAMSIZ)
                plen = IFNAMSIZ;
            memcpy(ifr.ifr_name, RTA_DATA(rta), plen);
            ifr.ifr_name[IFNAMSIZ - 1] = '\0';
        }
        rta = nl_rta_next(rta, &rem);
    }

    if (sock_io_setflags(&ifr) < 0)
        return nl_emit_error(s, -EIO, nlh);
    return nl_emit_error(s, 0, nlh);
}

/* --- Address dumps ------------------------------------------------------ */

static int nl_dump_one_addr(struct nl_sock *s, uint32_t seq, uint32_t pid,
                            unsigned int idx, ip4 ip, ip4 nm)
{
    uint8_t buf[NL_MSG_MAX];
    struct ifaddrmsg *ifa = (struct ifaddrmsg *)buf;
    uint32_t off, ip_net, bcast_net;

    memset(buf, 0, sizeof(buf));
    ifa->ifa_family = AF_INET;
    ifa->ifa_prefixlen = mask_to_prefix(nm);
    ifa->ifa_flags = 0;
    ifa->ifa_scope = (idx == FROSTED_WOLFIP_LOOPBACK_IF_IDX) ?
                         RT_SCOPE_HOST : RT_SCOPE_UNIVERSE;
    ifa->ifa_index = idx + 1;

    off = NLMSG_ALIGN(sizeof(*ifa));
    ip_net = ee32(ip);
    bcast_net = ee32(ip | ~nm);
    if (rta_append(buf, sizeof(buf), &off, IFA_LOCAL,
                   &ip_net, sizeof(ip_net)) < 0)
        return -EMSGSIZE;
    if (rta_append(buf, sizeof(buf), &off, IFA_ADDRESS,
                   &ip_net, sizeof(ip_net)) < 0)
        return -EMSGSIZE;
    if (idx != FROSTED_WOLFIP_LOOPBACK_IF_IDX)
        if (rta_append(buf, sizeof(buf), &off, IFA_BROADCAST,
                       &bcast_net, sizeof(bcast_net)) < 0)
            return -EMSGSIZE;
    return nl_emit(s, RTM_NEWADDR, NLM_F_MULTI, seq, pid, buf, off);
}

static int nl_handle_get_addr(struct nl_sock *s, const struct nlmsghdr *nlh)
{
    unsigned int idx;
    int ret;
    ip4 ip, nm, gw;
    for (idx = 0; idx < WOLFIP_MAX_INTERFACES; idx++) {
        struct wolfIP_ll_dev *ll = wolfIP_getdev_ex(IPStack, idx);
        if (!ll || ll->ifname[0] == '\0')
            continue;
        wolfIP_ipconfig_get_ex(IPStack, idx, &ip, &nm, &gw);
        if (ip == 0 && nm == 0)
            continue;
        ret = nl_dump_one_addr(s, nlh->nlmsg_seq, nlh->nlmsg_pid, idx, ip, nm);
        if (ret < 0)
            return ret;
    }
    return nl_emit_done(s, nlh->nlmsg_seq, nlh->nlmsg_pid);
}

static int nl_handle_new_addr(struct nl_sock *s, const struct nlmsghdr *nlh,
                              int del)
{
    const struct ifaddrmsg *ifa;
    struct rtattr *rta;
    int rem;
    uint32_t ip_net = 0;
    int has_addr = 0;
    struct wolfIP_ll_dev *ll;
    ip4 old_ip, old_nm, old_gw;
    unsigned int idx;
    ip4 new_ip, new_nm;

    if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*ifa)))
        return nl_emit_error(s, -EINVAL, nlh);
    ifa = (const struct ifaddrmsg *)NLMSG_DATA(nlh);
    if (ifa->ifa_family != AF_INET)
        return nl_emit_error(s, -EAFNOSUPPORT, nlh);
    if (ifa->ifa_index < 1 || ifa->ifa_index > WOLFIP_MAX_INTERFACES)
        return nl_emit_error(s, -ENODEV, nlh);
    idx = ifa->ifa_index - 1;
    ll = wolfIP_getdev_ex(IPStack, idx);
    if (!ll || ll->ifname[0] == '\0')
        return nl_emit_error(s, -ENODEV, nlh);

    rta = first_rta(ifa, sizeof(*ifa),
                    nlh->nlmsg_len - NLMSG_HDRLEN, &rem);
    while (rta && RTA_OK(rta, rem)) {
        int plen = nl_rta_payload(rta);
        if ((rta->rta_type == IFA_LOCAL || rta->rta_type == IFA_ADDRESS) &&
            plen >= 4) {
            memcpy(&ip_net, RTA_DATA(rta), 4);
            has_addr = 1;
        }
        rta = nl_rta_next(rta, &rem);
    }
    if (!has_addr && !del)
        return nl_emit_error(s, -EINVAL, nlh);

    wolfIP_ipconfig_get_ex(IPStack, idx, &old_ip, &old_nm, &old_gw);
    if (del) {
        new_ip = 0;
        new_nm = 0;
    } else {
        new_ip = ee32(ip_net);
        new_nm = prefix_to_mask(ifa->ifa_prefixlen);
    }
    wolfIP_ipconfig_set_ex(IPStack, idx, new_ip, new_nm, old_gw);
    return nl_emit_error(s, 0, nlh);
}

/* --- Route dumps -------------------------------------------------------- */

static int nl_dump_one_route(struct nl_sock *s, uint32_t seq, uint32_t pid,
                             unsigned int idx, ip4 dst, uint8_t dst_len,
                             ip4 gw, int is_default)
{
    uint8_t buf[NL_MSG_MAX];
    struct rtmsg *rtm = (struct rtmsg *)buf;
    uint32_t off;
    uint32_t oif = idx + 1;
    uint32_t gw_net = ee32(gw);
    uint32_t dst_net = ee32(dst);

    memset(buf, 0, sizeof(buf));
    rtm->rtm_family = AF_INET;
    rtm->rtm_dst_len = dst_len;
    rtm->rtm_src_len = 0;
    rtm->rtm_tos = 0;
    rtm->rtm_table = RT_TABLE_MAIN;
    rtm->rtm_protocol = is_default ? RTPROT_BOOT : RTPROT_KERNEL;
    rtm->rtm_scope = is_default ? RT_SCOPE_UNIVERSE : RT_SCOPE_LINK;
    rtm->rtm_type = RTN_UNICAST;
    rtm->rtm_flags = 0;

    off = NLMSG_ALIGN(sizeof(*rtm));
    if (dst_len > 0) {
        if (rta_append(buf, sizeof(buf), &off, RTA_DST,
                       &dst_net, sizeof(dst_net)) < 0)
            return -EMSGSIZE;
    }
    if (is_default && gw != 0) {
        if (rta_append(buf, sizeof(buf), &off, RTA_GATEWAY,
                       &gw_net, sizeof(gw_net)) < 0)
            return -EMSGSIZE;
    }
    if (rta_append(buf, sizeof(buf), &off, RTA_OIF, &oif, sizeof(oif)) < 0)
        return -EMSGSIZE;
    return nl_emit(s, RTM_NEWROUTE, NLM_F_MULTI, seq, pid, buf, off);
}

static int nl_handle_get_route(struct nl_sock *s, const struct nlmsghdr *nlh)
{
    unsigned int idx;
    int ret;
    ip4 ip, nm, gw;
    for (idx = 0; idx < WOLFIP_MAX_INTERFACES; idx++) {
        struct wolfIP_ll_dev *ll = wolfIP_getdev_ex(IPStack, idx);
        if (!ll || ll->ifname[0] == '\0')
            continue;
        wolfIP_ipconfig_get_ex(IPStack, idx, &ip, &nm, &gw);
        if (ip != 0 && nm != 0) {
            /* connected route */
            ret = nl_dump_one_route(s, nlh->nlmsg_seq, nlh->nlmsg_pid,
                                    idx, ip & nm, mask_to_prefix(nm), 0, 0);
            if (ret < 0)
                return ret;
        }
        if (gw != 0) {
            /* default via gw */
            ret = nl_dump_one_route(s, nlh->nlmsg_seq, nlh->nlmsg_pid,
                                    idx, 0, 0, gw, 1);
            if (ret < 0)
                return ret;
        }
    }
    return nl_emit_done(s, nlh->nlmsg_seq, nlh->nlmsg_pid);
}

static int nl_handle_new_route(struct nl_sock *s, const struct nlmsghdr *nlh,
                               int del)
{
    const struct rtmsg *rtm;
    struct rtattr *rta;
    int rem;
    uint32_t gw_net = 0;
    uint32_t dst_net = 0;
    int has_gw = 0;
    char ifname[IFNAMSIZ] = "";
    struct rtentry rte;
    struct sockaddr_in *gw_sin;
    struct sockaddr_in *dst_sin;
    struct sockaddr_in *mask_sin;

    if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*rtm)))
        return nl_emit_error(s, -EINVAL, nlh);
    rtm = (const struct rtmsg *)NLMSG_DATA(nlh);
    if (rtm->rtm_family != AF_INET)
        return nl_emit_error(s, -EAFNOSUPPORT, nlh);

    rta = first_rta(rtm, sizeof(*rtm),
                    nlh->nlmsg_len - NLMSG_HDRLEN, &rem);
    while (rta && RTA_OK(rta, rem)) {
        int plen = nl_rta_payload(rta);
        if (rta->rta_type == RTA_GATEWAY && plen >= 4) {
            memcpy(&gw_net, RTA_DATA(rta), 4);
            has_gw = 1;
        } else if (rta->rta_type == RTA_DST && plen >= 4) {
            memcpy(&dst_net, RTA_DATA(rta), 4);
        } else if (rta->rta_type == RTA_OIF && plen >= 4) {
            uint32_t oif;
            struct wolfIP_ll_dev *ll;
            memcpy(&oif, RTA_DATA(rta), 4);
            if (oif >= 1 && oif <= WOLFIP_MAX_INTERFACES) {
                ll = wolfIP_getdev_ex(IPStack, oif - 1);
                if (ll && ll->ifname[0] != '\0')
                    strncpy(ifname, ll->ifname, IFNAMSIZ);
            }
        }
        rta = nl_rta_next(rta, &rem);
    }

    memset(&rte, 0, sizeof(rte));
    gw_sin = (struct sockaddr_in *)&rte.rt_gateway;
    dst_sin = (struct sockaddr_in *)&rte.rt_dst;
    mask_sin = (struct sockaddr_in *)&rte.rt_genmask;
    gw_sin->sin_family = AF_INET;
    dst_sin->sin_family = AF_INET;
    mask_sin->sin_family = AF_INET;
    if (has_gw) {
        /* sock_io_addroute expects host-order s_addr (see socket_in.c). */
        gw_sin->sin_addr.s_addr = ee32(gw_net);
        rte.rt_flags |= 0x0002 /* RTF_GATEWAY */;
    }
    dst_sin->sin_addr.s_addr = ee32(dst_net);
    mask_sin->sin_addr.s_addr = prefix_to_mask(rtm->rtm_dst_len);
    if (rtm->rtm_dst_len == 0)
        rte.rt_flags |= 0x0001 /* RTF_UP + default */;
    if (ifname[0])
        rte.rt_dev = ifname;

    if (del) {
        if (sock_io_delroute(&rte) < 0)
            return nl_emit_error(s, -EIO, nlh);
    } else {
        if (sock_io_addroute(&rte) < 0)
            return nl_emit_error(s, -EIO, nlh);
    }
    return nl_emit_error(s, 0, nlh);
}

/* --- Dispatch entry ----------------------------------------------------- */

static int nl_dispatch(struct nl_sock *s, const uint8_t *buf, uint32_t len)
{
    const struct nlmsghdr *nlh;
    uint32_t payload_len;
    if (len < sizeof(*nlh))
        return -EINVAL;
    nlh = (const struct nlmsghdr *)buf;
    if (!NLMSG_OK(nlh, (int)len))
        return -EINVAL;
    if (nlh->nlmsg_len < (uint32_t)NLMSG_HDRLEN || nlh->nlmsg_len > len)
        return -EINVAL;
    payload_len = nlh->nlmsg_len - (uint32_t)NLMSG_HDRLEN;
    if (payload_len > (len - (uint32_t)NLMSG_HDRLEN))
        return -EINVAL;
    if (nl_requires_admin(nlh->nlmsg_type) && this_task() != get_kernel())
        return nl_emit_error(s, -EPERM, nlh);

    switch (nlh->nlmsg_type) {
    case RTM_GETLINK:
        return nl_handle_get_link(s, nlh);
    case RTM_NEWLINK:
    case RTM_SETLINK:
        return nl_handle_set_link(s, nlh);
    case RTM_GETADDR:
        return nl_handle_get_addr(s, nlh);
    case RTM_NEWADDR:
        return nl_handle_new_addr(s, nlh, 0);
    case RTM_DELADDR:
        return nl_handle_new_addr(s, nlh, 1);
    case RTM_GETROUTE:
        return nl_handle_get_route(s, nlh);
    case RTM_NEWROUTE:
        return nl_handle_new_route(s, nlh, 0);
    case RTM_DELROUTE:
        return nl_handle_new_route(s, nlh, 1);
    default:
        return nl_emit_error(s, -EOPNOTSUPP, nlh);
    }
}

/* --- Socket file descriptor helpers ------------------------------------- */

static struct nl_sock *nl_from_fd(int fd)
{
    struct fnode *fno = task_filedesc_get(fd);
    if (!fno || fno->owner != &mod_socket_nl)
        return NULL;
    return (struct nl_sock *)fno->priv;
}

static uint32_t nl_alloc_pid(void)
{
    struct nl_sock *it;
    uint32_t pid;
    int tries = 1 << 20;
    while (tries-- > 0) {
        pid = nl_next_pid++;
        if (pid == 0)
            continue;
        for (it = nl_socks; it; it = it->link_next) {
            if (it->nl_pid == pid)
                break;
        }
        if (!it)
            return pid;
    }
    return 0;
}

/* --- Module ops --------------------------------------------------------- */

static int sock_socket(int domain, int type, int protocol)
{
    struct nl_sock *s;
    int fd;

    if (domain != FAMILY_NETLINK)
        return -EAFNOSUPPORT;
    if (type != SOCK_RAW && type != SOCK_DGRAM)
        return -ESOCKTNOSUPPORT;
    if (protocol != NETLINK_ROUTE)
        return -EPROTONOSUPPORT;

    s = kcalloc(sizeof(*s), 1);
    if (!s)
        return -ENOMEM;
    s->node = kcalloc(sizeof(struct fnode), 1);
    if (!s->node) {
        kfree(s);
        return -ENOMEM;
    }
    s->node->owner = &mod_socket_nl;
    s->node->flags = FL_RDWR;
    s->node->priv = s;
    s->protocol = protocol;
    s->type = type;
    s->nl_pid = 0;
    s->link_next = nl_socks;
    nl_socks = s;

    fd = task_filedesc_add(s->node);
    if (fd < 0) {
        /* unlink */
        struct nl_sock **pp = &nl_socks;
        while (*pp && *pp != s) pp = &((*pp)->link_next);
        if (*pp) *pp = s->link_next;
        kfree(s->node);
        kfree(s);
        return fd;
    }
    s->fd = fd;
    task_fd_setmask(fd, O_RDWR);
    return fd;
}

static int sock_bind(int fd, struct sockaddr *addr, unsigned int addrlen)
{
    struct nl_sock *s = nl_from_fd(fd);
    struct sockaddr_nl *nladdr;
    struct nl_sock *it;

    if (!s)
        return -EBADF;
    if (!addr || addrlen < sizeof(*nladdr))
        return -EINVAL;
    nladdr = (struct sockaddr_nl *)addr;
    if (nladdr->nl_family != FAMILY_NETLINK)
        return -EAFNOSUPPORT;

    if (nladdr->nl_pid == 0) {
        uint32_t pid = nl_alloc_pid();
        if (pid == 0)
            return -EADDRNOTAVAIL;
        s->nl_pid = pid;
    } else {
        for (it = nl_socks; it; it = it->link_next) {
            if (it != s && it->nl_pid == nladdr->nl_pid)
                return -EADDRINUSE;
        }
        s->nl_pid = nladdr->nl_pid;
    }
    s->nl_groups = nladdr->nl_groups;
    return 0;
}

static int sock_getsockname(int fd, struct sockaddr *addr,
                            unsigned int *addrlen)
{
    struct nl_sock *s = nl_from_fd(fd);
    struct sockaddr_nl out;
    unsigned int cap;

    if (!s || !addr || !addrlen)
        return -EINVAL;
    memset(&out, 0, sizeof(out));
    out.nl_family = FAMILY_NETLINK;
    out.nl_pid = s->nl_pid;
    out.nl_groups = s->nl_groups;
    cap = *addrlen;
    if (cap > sizeof(out))
        cap = sizeof(out);
    memcpy(addr, &out, cap);
    *addrlen = sizeof(out);
    return 0;
}

static int sock_sendto(int fd, const void *buf, unsigned int len, int flags,
                       struct sockaddr *addr, unsigned int addrlen)
{
    struct nl_sock *s = nl_from_fd(fd);
    int ret;
    (void)flags; (void)addr; (void)addrlen;
    if (!s)
        return -EBADF;
    if (!buf || len < sizeof(struct nlmsghdr))
        return -EINVAL;
    if (len > NL_MSG_MAX)
        return -EMSGSIZE;
    if (s->nl_pid == 0) {
        /* Auto-bind on first use — mirrors Linux behaviour. */
        uint32_t pid = nl_alloc_pid();
        if (pid == 0)
            return -EADDRNOTAVAIL;
        s->nl_pid = pid;
    }
    ret = nl_dispatch(s, buf, len);
    if (ret < 0)
        return ret;
    return (int)len;
}

static int sock_recvfrom(int fd, void *buf, unsigned int len, int flags,
                         struct sockaddr *addr, unsigned int *addrlen)
{
    struct nl_sock *s = nl_from_fd(fd);
    struct nl_reply *r;
    unsigned int copy;
    (void)flags;

    if (!s)
        return -EBADF;
    if (!buf || len == 0)
        return -EINVAL;
    r = s->rx_head;
    if (!r)
        return -EAGAIN;

    copy = (r->len < len) ? r->len : len;
    memcpy(buf, r->data, copy);

    if (addr && addrlen && *addrlen >= sizeof(struct sockaddr_nl)) {
        struct sockaddr_nl src;
        memset(&src, 0, sizeof(src));
        src.nl_family = FAMILY_NETLINK;
        memcpy(addr, &src, sizeof(src));
        *addrlen = sizeof(src);
    } else if (addrlen) {
        *addrlen = 0;
    }

    s->rx_head = r->next;
    if (!s->rx_head)
        s->rx_tail = NULL;
    s->queued_bytes -= r->len;
    kfree(r);
    return (int)copy;
}

static int sock_sendmsg(int fd, const struct msghdr *msg, int flags)
{
    struct nl_sock *s = nl_from_fd(fd);
    uint8_t stage[NL_MSG_MAX];
    size_t total = 0;
    size_t i;

    if (!s)
        return -EBADF;
    if (!msg || !msg->msg_iov || msg->msg_iovlen < 1)
        return -EINVAL;
    for (i = 0; i < msg->msg_iovlen; i++) {
        size_t n = msg->msg_iov[i].iov_len;
        if (n > sizeof(stage) - total)
            return -EMSGSIZE;
        memcpy(stage + total, msg->msg_iov[i].iov_base, n);
        total += n;
    }
    return sock_sendto(fd, stage, (unsigned int)total, flags, msg->msg_name,
                       msg->msg_namelen);
}

static int sock_recvmsg(int fd, struct msghdr *msg, int flags)
{
    struct nl_sock *s = nl_from_fd(fd);
    struct nl_reply *r;
    size_t delivered = 0;
    size_t i;
    (void)flags;

    if (!s)
        return -EBADF;
    if (!msg || !msg->msg_iov || msg->msg_iovlen < 1)
        return -EINVAL;
    r = s->rx_head;
    if (!r)
        return -EAGAIN;

    for (i = 0; i < msg->msg_iovlen && delivered < r->len; i++) {
        size_t take = r->len - delivered;
        size_t cap = msg->msg_iov[i].iov_len;
        if (take > cap)
            take = cap;
        memcpy(msg->msg_iov[i].iov_base, r->data + delivered, take);
        delivered += take;
    }

    if (msg->msg_name && msg->msg_namelen >= sizeof(struct sockaddr_nl)) {
        struct sockaddr_nl src;
        memset(&src, 0, sizeof(src));
        src.nl_family = FAMILY_NETLINK;
        memcpy(msg->msg_name, &src, sizeof(src));
        msg->msg_namelen = sizeof(src);
    } else {
        msg->msg_namelen = 0;
    }
    msg->msg_controllen = 0;
    msg->msg_flags = 0;

    s->rx_head = r->next;
    if (!s->rx_head)
        s->rx_tail = NULL;
    s->queued_bytes -= r->len;
    kfree(r);
    return (int)delivered;
}

static int sock_poll(struct fnode *fno, uint16_t events, uint16_t *revents)
{
    struct nl_sock *s = (struct nl_sock *)fno->priv;
    uint16_t r = 0;
    if ((events & POLLIN) && s && s->rx_head)
        r |= POLLIN;
    if (events & POLLOUT)
        r |= POLLOUT;
    *revents = r;
    return r ? 1 : 0;
}

static int sock_close(struct fnode *fno)
{
    struct nl_sock *s = (struct nl_sock *)fno->priv;
    struct nl_sock **pp;
    if (!s)
        return 0;
    nl_queue_drain(s);
    for (pp = &nl_socks; *pp; pp = &((*pp)->link_next)) {
        if (*pp == s) {
            *pp = s->link_next;
            break;
        }
    }
    kfree(s->node);
    kfree(s);
    return 0;
}

static int sock_shutdown(int fd, uint16_t how)
{
    struct nl_sock *s = nl_from_fd(fd);
    (void)how;
    if (!s)
        return -EBADF;
    nl_queue_drain(s);
    return 0;
}

static int sock_not_supported(void) { return -EOPNOTSUPP; }
static int sock_connect(int fd, struct sockaddr *addr, unsigned int addrlen)
{ (void)fd; (void)addr; (void)addrlen; return -EOPNOTSUPP; }
static int sock_listen(int fd, int backlog)
{ (void)fd; (void)backlog; return -EOPNOTSUPP; }
static int sock_accept(int fd, struct sockaddr *addr, unsigned int *addrlen)
{ (void)fd; (void)addr; (void)addrlen; return -EOPNOTSUPP; }

void socket_netlink_init(void)
{
    mod_socket_nl.family = FAMILY_NETLINK;
    strcpy(mod_socket_nl.name, "netlink");
    mod_socket_nl.ops.poll       = sock_poll;
    mod_socket_nl.ops.close      = sock_close;
    mod_socket_nl.ops.socket     = sock_socket;
    mod_socket_nl.ops.bind       = sock_bind;
    mod_socket_nl.ops.sendto     = sock_sendto;
    mod_socket_nl.ops.recvfrom   = sock_recvfrom;
    mod_socket_nl.ops.sendmsg    = sock_sendmsg;
    mod_socket_nl.ops.recvmsg    = sock_recvmsg;
    mod_socket_nl.ops.shutdown   = sock_shutdown;
    mod_socket_nl.ops.connect    = sock_connect;
    mod_socket_nl.ops.listen     = sock_listen;
    mod_socket_nl.ops.accept     = sock_accept;
    mod_socket_nl.ops.getsockname = sock_getsockname;
    (void)sock_not_supported;

    register_module(&mod_socket_nl);
    register_addr_family(&mod_socket_nl, FAMILY_NETLINK);
}

#else /* !CONFIG_TCPIP */

void socket_netlink_init(void) { }

#endif /* CONFIG_TCPIP */
