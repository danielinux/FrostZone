/*
 *      KISS `ip` tool for Frostzone.
 *
 *      Talks to the kernel via AF_NETLINK / NETLINK_ROUTE. Covers:
 *          ip link show
 *          ip link set <dev> up|down
 *          ip addr show [dev <name>]
 *          ip addr add <CIDR> dev <name>
 *          ip addr del <CIDR> dev <name>
 *          ip route show
 *          ip route add <prefix> via <gw> [dev <name>]
 *          ip route del <prefix>
 *
 *      Deliberately single-file, no libnl. Uses only sendto/recvfrom so it
 *      works with the current libgloss — sendmsg/recvmsg syscalls exist in
 *      the kernel but their userspace wrappers are not shipped yet.
 */
#define _BSD_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <frosted/netlink.h>
#include <frosted/rtnetlink.h>

#ifndef htonl
#define htonl(x) __builtin_bswap32(x)
#define ntohl(x) __builtin_bswap32(x)
#define htons(x) __builtin_bswap16(x)
#define ntohs(x) __builtin_bswap16(x)
#endif

#ifndef PF_NETLINK
#define PF_NETLINK 16
#endif
#ifndef SOCK_RAW
#define SOCK_RAW 3
#endif
#ifndef AF_INET
#define AF_INET 2
#endif

/* Keep stack frames under 4 KB: a 2 KB buffer + fixed locals fits. Netlink
 * replies here are short (single link/addr/route entry each with a handful
 * of rtattrs), so 2 KB is plenty. */
#define NL_BUFSZ 2048

/* ---------- Netlink helpers ---------- */

static int nl_open(void)
{
    struct sockaddr_nl src;
    int fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0)
        return -1;
    memset(&src, 0, sizeof(src));
    src.nl_family = PF_NETLINK;
    src.nl_pid = 0; /* kernel auto-assigns */
    if (bind(fd, (struct sockaddr *)&src, sizeof(src)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int nl_send(int fd, struct nlmsghdr *nlh)
{
    struct sockaddr_nl dst;
    memset(&dst, 0, sizeof(dst));
    dst.nl_family = PF_NETLINK;
    return sendto(fd, nlh, nlh->nlmsg_len, 0,
                  (struct sockaddr *)&dst, sizeof(dst));
}

static void nla_put(struct nlmsghdr *nlh, uint16_t type,
                    const void *data, uint16_t dlen)
{
    struct rtattr *rta;
    int off = NLMSG_ALIGN(nlh->nlmsg_len);
    rta = (struct rtattr *)((char *)nlh + off);
    rta->rta_type = type;
    rta->rta_len = RTA_LENGTH(dlen);
    if (dlen && data)
        memcpy(RTA_DATA(rta), data, dlen);
    nlh->nlmsg_len = off + RTA_ALIGN(rta->rta_len);
}

static void nla_put_u32(struct nlmsghdr *nlh, uint16_t type, uint32_t v)
{
    nla_put(nlh, type, &v, sizeof(v));
}

static void nla_put_str(struct nlmsghdr *nlh, uint16_t type, const char *s)
{
    nla_put(nlh, type, s, (uint16_t)(strlen(s) + 1));
}

/* Walk rtattrs within a message body. */
static struct rtattr *rta_first(void *start, int len, int *remaining)
{
    *remaining = len;
    return (struct rtattr *)start;
}

static void *rta_find(void *start, int len, uint16_t type, int *payload_len)
{
    int rem;
    struct rtattr *r = rta_first(start, len, &rem);
    while (RTA_OK(r, rem)) {
        if (r->rta_type == type) {
            if (payload_len)
                *payload_len = RTA_PAYLOAD(r);
            return RTA_DATA(r);
        }
        r = RTA_NEXT(r, rem);
    }
    return NULL;
}

/* Receive messages until NLMSG_DONE or NLMSG_ERROR, invoking cb per object.
 * Returns: 0 on clean dump end, 0 on ack (error=0), -errno otherwise. */
typedef int (*nl_cb_t)(struct nlmsghdr *nlh, void *ctx);

static int nl_recv_dump(int fd, nl_cb_t cb, void *ctx)
{
    uint8_t buf[NL_BUFSZ];
    int n;
    struct nlmsghdr *nlh;
    for (;;) {
        n = recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return -errno;
        }
        if (n == 0)
            return 0;
        nlh = (struct nlmsghdr *)buf;
        while (NLMSG_OK(nlh, n)) {
            if (nlh->nlmsg_type == NLMSG_DONE)
                return 0;
            if (nlh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *ne = (struct nlmsgerr *)NLMSG_DATA(nlh);
                return ne->error; /* 0 = ack, negative = error */
            }
            if (cb) {
                int r = cb(nlh, ctx);
                if (r)
                    return r;
            }
            nlh = NLMSG_NEXT(nlh, n);
        }
    }
}

/* ---------- IP / CIDR parsing ---------- */

static int parse_ip(const char *s, uint32_t *out_net)
{
    unsigned int a, b, c, d;
    char dot;
    /* naive: accept "a.b.c.d" */
    int matched = 0;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &dot) == 4)
        matched = 1;
    else if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
        matched = 1;
    if (!matched || a > 255 || b > 255 || c > 255 || d > 255)
        return -1;
    *out_net = htonl((a << 24) | (b << 16) | (c << 8) | d);
    return 0;
}

static int parse_cidr(const char *s, uint32_t *out_net, uint8_t *prefix)
{
    char buf[64];
    char *slash;
    unsigned int p;
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    slash = strchr(buf, '/');
    if (!slash)
        return -1;
    *slash = '\0';
    if (sscanf(slash + 1, "%u", &p) != 1 || p > 32)
        return -1;
    *prefix = (uint8_t)p;
    return parse_ip(buf, out_net);
}

static void fmt_ip(uint32_t ip_net, char *out, size_t sz)
{
    uint32_t h = ntohl(ip_net);
    snprintf(out, sz, "%u.%u.%u.%u",
             (h >> 24) & 0xFF, (h >> 16) & 0xFF,
             (h >> 8) & 0xFF, h & 0xFF);
}

/* ---------- Resolve ifname -> ifindex ---------- */

struct name2idx_ctx {
    const char *want;
    int idx;
};

static int name2idx_cb(struct nlmsghdr *nlh, void *c)
{
    struct name2idx_ctx *ctx = (struct name2idx_ctx *)c;
    struct ifinfomsg *ifi;
    int off, body_len, plen = 0;
    char *name;
    if (nlh->nlmsg_type != RTM_NEWLINK)
        return 0;
    ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);
    off = (int)NLMSG_ALIGN(sizeof(*ifi));
    body_len = (int)nlh->nlmsg_len - NLMSG_HDRLEN - off;
    if (body_len <= 0)
        return 0;
    name = (char *)rta_find((char *)ifi + off, body_len, IFLA_IFNAME, &plen);
    if (name && !strcmp(name, ctx->want))
        ctx->idx = ifi->ifi_index;
    return 0;
}

static int ifname_to_index(int fd, const char *name)
{
    uint8_t buf[NL_BUFSZ];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct ifinfomsg *ifi;
    struct name2idx_ctx ctx = { name, 0 };
    int r;

    memset(buf, 0, sizeof(buf));
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*ifi));
    nlh->nlmsg_type = RTM_GETLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh->nlmsg_seq = 1;
    nlh->nlmsg_pid = 0;
    ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);
    ifi->ifi_family = AF_UNSPEC;
    if (nl_send(fd, nlh) < 0)
        return -1;
    r = nl_recv_dump(fd, name2idx_cb, &ctx);
    if (r != 0)
        return -1;
    return ctx.idx ? ctx.idx : -1;
}

/* ---------- `ip link show` ---------- */

static int link_show_cb(struct nlmsghdr *nlh, void *c)
{
    struct ifinfomsg *ifi;
    int off, body_len, plen = 0;
    char *name;
    uint8_t *mac;
    uint32_t *mtu;
    (void)c;
    if (nlh->nlmsg_type != RTM_NEWLINK)
        return 0;
    ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);
    off = (int)NLMSG_ALIGN(sizeof(*ifi));
    body_len = (int)nlh->nlmsg_len - NLMSG_HDRLEN - off;
    name = (char *)rta_find((char *)ifi + off, body_len, IFLA_IFNAME, &plen);
    mac  = (uint8_t *)rta_find((char *)ifi + off, body_len, IFLA_ADDRESS, &plen);
    mtu  = (uint32_t *)rta_find((char *)ifi + off, body_len, IFLA_MTU, &plen);

    printf("%d: %s: <", ifi->ifi_index, name ? name : "?");
    if (ifi->ifi_flags & 0x1) printf("UP");
    if (ifi->ifi_flags & 0x40) printf(",RUNNING");
    if (ifi->ifi_flags & 0x8) printf(",LOOPBACK");
    if (ifi->ifi_flags & 0x2) printf(",BROADCAST");
    if (ifi->ifi_flags & 0x8000) printf(",MULTICAST");
    printf("> mtu %u\n", mtu ? *mtu : 0);
    if (mac && plen >= 6) {
        printf("    link/%s %02x:%02x:%02x:%02x:%02x:%02x\n",
               (ifi->ifi_type == ARPHRD_LOOPBACK) ? "loopback" : "ether",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    return 0;
}

static int cmd_link_show(int fd)
{
    uint8_t buf[NL_BUFSZ];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct ifinfomsg *ifi;
    memset(buf, 0, sizeof(buf));
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*ifi));
    nlh->nlmsg_type = RTM_GETLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh->nlmsg_seq = 1;
    ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);
    ifi->ifi_family = AF_UNSPEC;
    if (nl_send(fd, nlh) < 0)
        return -1;
    return nl_recv_dump(fd, link_show_cb, NULL) ? -1 : 0;
}

/* ---------- `ip link set <dev> up|down` ---------- */

static int cmd_link_set(int fd, const char *dev, int up)
{
    uint8_t buf[NL_BUFSZ];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct ifinfomsg *ifi;
    int idx = ifname_to_index(fd, dev);
    int r;
    if (idx <= 0) {
        fprintf(stderr, "ip: device '%s' not found\n", dev);
        return -1;
    }
    memset(buf, 0, sizeof(buf));
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*ifi));
    nlh->nlmsg_type = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq = 2;
    ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);
    ifi->ifi_family = AF_UNSPEC;
    ifi->ifi_index = idx;
    ifi->ifi_flags = up ? 0x1 /* IFF_UP */ : 0;
    ifi->ifi_change = 0x1;
    if (nl_send(fd, nlh) < 0)
        return -1;
    r = nl_recv_dump(fd, NULL, NULL);
    if (r < 0) {
        fprintf(stderr, "ip: RTM_NEWLINK failed: %s\n", strerror(-r));
        return -1;
    }
    return 0;
}

/* ---------- `ip addr show` ---------- */

static int addr_show_cb(struct nlmsghdr *nlh, void *c)
{
    struct ifaddrmsg *ifa;
    int off, body_len, plen = 0;
    uint32_t *addr;
    char ipbuf[32];
    int *filter_idx = (int *)c;
    if (nlh->nlmsg_type != RTM_NEWADDR)
        return 0;
    ifa = (struct ifaddrmsg *)NLMSG_DATA(nlh);
    if (filter_idx && *filter_idx > 0 && ifa->ifa_index != (unsigned)*filter_idx)
        return 0;
    off = (int)NLMSG_ALIGN(sizeof(*ifa));
    body_len = (int)nlh->nlmsg_len - NLMSG_HDRLEN - off;
    addr = (uint32_t *)rta_find((char *)ifa + off, body_len, IFA_LOCAL, &plen);
    if (!addr)
        addr = (uint32_t *)rta_find((char *)ifa + off, body_len,
                                    IFA_ADDRESS, &plen);
    fmt_ip(addr ? *addr : 0, ipbuf, sizeof(ipbuf));
    printf("    inet %s/%u scope %s on if %u\n", ipbuf, ifa->ifa_prefixlen,
           (ifa->ifa_scope == RT_SCOPE_HOST) ? "host" : "global",
           ifa->ifa_index);
    return 0;
}

static int cmd_addr_show(int fd, const char *dev)
{
    uint8_t buf[NL_BUFSZ];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct ifaddrmsg *ifa;
    int idx = 0;
    int r;
    if (dev) {
        idx = ifname_to_index(fd, dev);
        if (idx <= 0) {
            fprintf(stderr, "ip: device '%s' not found\n", dev);
            return -1;
        }
    }
    memset(buf, 0, sizeof(buf));
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*ifa));
    nlh->nlmsg_type = RTM_GETADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh->nlmsg_seq = 3;
    ifa = (struct ifaddrmsg *)NLMSG_DATA(nlh);
    ifa->ifa_family = AF_INET;
    if (nl_send(fd, nlh) < 0)
        return -1;
    r = nl_recv_dump(fd, addr_show_cb, dev ? &idx : NULL);
    return r ? -1 : 0;
}

/* ---------- `ip addr add|del` ---------- */

static int cmd_addr_modify(int fd, int del, const char *cidr, const char *dev)
{
    uint8_t buf[NL_BUFSZ];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct ifaddrmsg *ifa;
    uint32_t ip_net;
    uint8_t prefix;
    int idx, r;
    uint32_t bcast_net;

    if (!dev) {
        fprintf(stderr, "ip: missing 'dev'\n");
        return -1;
    }
    if (parse_cidr(cidr, &ip_net, &prefix) < 0) {
        fprintf(stderr, "ip: bad CIDR '%s'\n", cidr);
        return -1;
    }
    idx = ifname_to_index(fd, dev);
    if (idx <= 0) {
        fprintf(stderr, "ip: device '%s' not found\n", dev);
        return -1;
    }

    memset(buf, 0, sizeof(buf));
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*ifa));
    nlh->nlmsg_type = del ? RTM_DELADDR : RTM_NEWADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK |
                       (del ? 0 : (NLM_F_CREATE | NLM_F_EXCL));
    nlh->nlmsg_seq = 4;
    ifa = (struct ifaddrmsg *)NLMSG_DATA(nlh);
    ifa->ifa_family = AF_INET;
    ifa->ifa_prefixlen = prefix;
    ifa->ifa_scope = RT_SCOPE_UNIVERSE;
    ifa->ifa_index = idx;

    nla_put(nlh, IFA_LOCAL, &ip_net, 4);
    nla_put(nlh, IFA_ADDRESS, &ip_net, 4);
    /* derive broadcast */
    {
        uint32_t mask_net = htonl(prefix ?
            ((uint32_t)0xFFFFFFFFu << (32 - prefix)) : 0);
        bcast_net = ip_net | ~mask_net;
        nla_put(nlh, IFA_BROADCAST, &bcast_net, 4);
    }
    if (nl_send(fd, nlh) < 0)
        return -1;
    r = nl_recv_dump(fd, NULL, NULL);
    if (r < 0) {
        fprintf(stderr, "ip: %s failed: %s\n",
                del ? "RTM_DELADDR" : "RTM_NEWADDR", strerror(-r));
        return -1;
    }
    return 0;
}

/* ---------- `ip route show` ---------- */

static int route_show_cb(struct nlmsghdr *nlh, void *c)
{
    struct rtmsg *rtm;
    int off, body_len, plen = 0;
    uint32_t *dst, *gw, *oif;
    char ipbuf[32];
    (void)c;
    if (nlh->nlmsg_type != RTM_NEWROUTE)
        return 0;
    rtm = (struct rtmsg *)NLMSG_DATA(nlh);
    off = (int)NLMSG_ALIGN(sizeof(*rtm));
    body_len = (int)nlh->nlmsg_len - NLMSG_HDRLEN - off;
    dst = (uint32_t *)rta_find((char *)rtm + off, body_len, RTA_DST, &plen);
    gw  = (uint32_t *)rta_find((char *)rtm + off, body_len, RTA_GATEWAY, &plen);
    oif = (uint32_t *)rta_find((char *)rtm + off, body_len, RTA_OIF, &plen);

    if (rtm->rtm_dst_len == 0 && !dst)
        printf("default");
    else {
        fmt_ip(dst ? *dst : 0, ipbuf, sizeof(ipbuf));
        printf("%s/%u", ipbuf, rtm->rtm_dst_len);
    }
    if (gw) {
        fmt_ip(*gw, ipbuf, sizeof(ipbuf));
        printf(" via %s", ipbuf);
    }
    if (oif)
        printf(" dev if%u", *oif);
    printf(" scope %s\n",
           (rtm->rtm_scope == RT_SCOPE_LINK) ? "link" :
           (rtm->rtm_scope == RT_SCOPE_HOST) ? "host" : "global");
    return 0;
}

static int cmd_route_show(int fd)
{
    uint8_t buf[NL_BUFSZ];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct rtmsg *rtm;
    memset(buf, 0, sizeof(buf));
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*rtm));
    nlh->nlmsg_type = RTM_GETROUTE;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh->nlmsg_seq = 5;
    rtm = (struct rtmsg *)NLMSG_DATA(nlh);
    rtm->rtm_family = AF_INET;
    if (nl_send(fd, nlh) < 0)
        return -1;
    return nl_recv_dump(fd, route_show_cb, NULL) ? -1 : 0;
}

/* ---------- `ip route add|del` ---------- */

static int cmd_route_modify(int fd, int del, const char *prefix,
                            const char *gw, const char *dev)
{
    uint8_t buf[NL_BUFSZ];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct rtmsg *rtm;
    uint32_t dst_net = 0, gw_net = 0;
    uint8_t dst_len = 0;
    int idx = 0, r;

    if (strcmp(prefix, "default") == 0) {
        dst_net = 0;
        dst_len = 0;
    } else if (parse_cidr(prefix, &dst_net, &dst_len) < 0) {
        /* Allow bare IP as /32 shorthand. */
        if (parse_ip(prefix, &dst_net) < 0) {
            fprintf(stderr, "ip: bad prefix '%s'\n", prefix);
            return -1;
        }
        dst_len = 32;
    }
    if (gw && parse_ip(gw, &gw_net) < 0) {
        fprintf(stderr, "ip: bad gateway '%s'\n", gw);
        return -1;
    }
    if (dev) {
        idx = ifname_to_index(fd, dev);
        if (idx <= 0) {
            fprintf(stderr, "ip: device '%s' not found\n", dev);
            return -1;
        }
    }

    memset(buf, 0, sizeof(buf));
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*rtm));
    nlh->nlmsg_type = del ? RTM_DELROUTE : RTM_NEWROUTE;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK |
                       (del ? 0 : (NLM_F_CREATE | NLM_F_EXCL));
    nlh->nlmsg_seq = 6;
    rtm = (struct rtmsg *)NLMSG_DATA(nlh);
    rtm->rtm_family = AF_INET;
    rtm->rtm_dst_len = dst_len;
    rtm->rtm_table = RT_TABLE_MAIN;
    rtm->rtm_protocol = RTPROT_BOOT;
    rtm->rtm_scope = gw ? RT_SCOPE_UNIVERSE : RT_SCOPE_LINK;
    rtm->rtm_type = RTN_UNICAST;

    if (dst_len > 0)
        nla_put(nlh, RTA_DST, &dst_net, 4);
    if (gw)
        nla_put(nlh, RTA_GATEWAY, &gw_net, 4);
    if (idx > 0)
        nla_put_u32(nlh, RTA_OIF, (uint32_t)idx);

    if (nl_send(fd, nlh) < 0)
        return -1;
    r = nl_recv_dump(fd, NULL, NULL);
    if (r < 0) {
        fprintf(stderr, "ip: %s failed: %s\n",
                del ? "RTM_DELROUTE" : "RTM_NEWROUTE", strerror(-r));
        return -1;
    }
    return 0;
}

/* ---------- CLI entry ---------- */

static void usage(void)
{
    fprintf(stderr,
        "Usage: ip OBJECT { COMMAND | help }\n"
        "  ip link show\n"
        "  ip link set DEV up|down\n"
        "  ip addr show [dev DEV]\n"
        "  ip addr add CIDR dev DEV\n"
        "  ip addr del CIDR dev DEV\n"
        "  ip route show\n"
        "  ip route add { PREFIX | default } via GW [dev DEV]\n"
        "  ip route del { PREFIX | default }\n");
}

/* Find the argument following a named token (e.g. "dev eth0" -> "eth0"). */
static const char *tok_after(int argc, char **argv, int start, const char *tok)
{
    int i;
    for (i = start; i + 1 < argc; i++) {
        if (strcmp(argv[i], tok) == 0)
            return argv[i + 1];
    }
    return NULL;
}

#ifndef APP_IP_MODULE
int main(int argc, char *argv[])
#else
int icebox_ip(int argc, char *argv[])
#endif
{
    int fd, ret = 0;
    const char *obj, *verb;

    if (argc < 2) { usage(); return 2; }
    obj = argv[1];
    verb = (argc >= 3) ? argv[2] : "show";

    fd = nl_open();
    if (fd < 0) {
        fprintf(stderr, "ip: cannot open AF_NETLINK: %s\n", strerror(errno));
        return 1;
    }

    if (strcmp(obj, "link") == 0) {
        if (strcmp(verb, "show") == 0 || strcmp(verb, "list") == 0) {
            ret = cmd_link_show(fd);
        } else if (strcmp(verb, "set") == 0 && argc >= 5) {
            ret = cmd_link_set(fd, argv[3],
                               strcmp(argv[4], "up") == 0);
        } else { usage(); ret = 2; }
    } else if (strcmp(obj, "addr") == 0 || strcmp(obj, "address") == 0 ||
               strcmp(obj, "a") == 0) {
        if (strcmp(verb, "show") == 0 || strcmp(verb, "list") == 0) {
            ret = cmd_addr_show(fd, tok_after(argc, argv, 3, "dev"));
        } else if ((strcmp(verb, "add") == 0 || strcmp(verb, "del") == 0) &&
                   argc >= 6) {
            ret = cmd_addr_modify(fd, strcmp(verb, "del") == 0,
                                  argv[3], tok_after(argc, argv, 3, "dev"));
        } else { usage(); ret = 2; }
    } else if (strcmp(obj, "route") == 0 || strcmp(obj, "r") == 0) {
        if (strcmp(verb, "show") == 0 || strcmp(verb, "list") == 0) {
            ret = cmd_route_show(fd);
        } else if (strcmp(verb, "add") == 0 && argc >= 4) {
            ret = cmd_route_modify(fd, 0, argv[3],
                                   tok_after(argc, argv, 4, "via"),
                                   tok_after(argc, argv, 4, "dev"));
        } else if (strcmp(verb, "del") == 0 && argc >= 4) {
            ret = cmd_route_modify(fd, 1, argv[3], NULL, NULL);
        } else { usage(); ret = 2; }
    } else if (strcmp(obj, "help") == 0 || strcmp(obj, "-h") == 0) {
        usage();
        ret = 0;
    } else {
        usage();
        ret = 2;
    }
    close(fd);
    return (ret < 0) ? 1 : ret;
}
