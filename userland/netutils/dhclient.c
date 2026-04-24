/*
 *      This file is part of frosted.
 *
 *      frosted is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the Free Software Foundation.
 *
 *      A minimal DHCP client that speaks DORA over AF_PACKET so it
 *      can negotiate a lease before an IP address is assigned.
 *      Once an ACK is received the interface is configured via
 *      SIOCSIFADDR/NETMASK, a default route is installed, and
 *      /etc/resolv.conf is updated with the DNS server option.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/frosted-io.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>

#ifndef AF_PACKET
#define AF_PACKET 17
#endif

#ifndef ETH_P_IP
#define ETH_P_IP 0x0800
#endif

#ifndef htons
#define htons(x) __builtin_bswap16(x)
#define htonl(x) __builtin_bswap32(x)
#define ntohs(x) __builtin_bswap16(x)
#define ntohl(x) __builtin_bswap32(x)
#endif

struct sockaddr_ll_compat {
    unsigned short sll_family;
    unsigned short sll_protocol;
    int            sll_ifindex;
    unsigned short sll_hatype;
    unsigned char  sll_pkttype;
    unsigned char  sll_halen;
    unsigned char  sll_addr[8];
};

/* --- DHCP / BOOTP layout --- */

struct dhcp_msg {
    uint8_t  op;        /* 1 = req, 2 = reply */
    uint8_t  htype;     /* 1 = Ethernet */
    uint8_t  hlen;      /* 6 */
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[312];
} __attribute__((packed));

#define DHCP_MAGIC_COOKIE 0x63825363U

#define BOOTP_OP_REQUEST  1
#define BOOTP_OP_REPLY    2

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPDECLINE  4
#define DHCPACK      5
#define DHCPNAK      6
#define DHCPRELEASE  7
#define DHCPINFORM   8

/* DHCP options */
#define OPT_PAD        0
#define OPT_SUBNET     1
#define OPT_ROUTER     3
#define OPT_DNS        6
#define OPT_HOSTNAME   12
#define OPT_REQ_IP     50
#define OPT_LEASE_TIME 51
#define OPT_MSG_TYPE   53
#define OPT_SERVER_ID  54
#define OPT_PARAM_LIST 55
#define OPT_END        255

/* --- Link-layer / IP headers (all big-endian on the wire) --- */

struct eth_hdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type;
} __attribute__((packed));

struct ip_hdr {
    uint8_t  vhl;       /* version<<4 | IHL */
    uint8_t  tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t check;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed));

struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t check;
} __attribute__((packed));

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

static uint16_t ip_checksum(const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint32_t)p[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static int dhcp_iface_index(const char *ifname, uint8_t mac[6])
{
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        close(fd);
        return -1;
    }
    /* frosted's SIOCGIFHWADDR stores MAC in ifr_dstaddr.sa_data. */
    memcpy(mac, ((struct sockaddr *)&ifr.ifr_dstaddr)->sa_data, 6);
    close(fd);
    /* The stack only has one Ethernet interface today, so ifindex 1
     * is fine for a binding hint. */
    return 1;
}

static int dhcp_bring_up(const char *ifname)
{
    int fd;
    struct ifreq ifr;
    int ret = -1;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_flags = IFF_UP;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr) == 0)
        ret = 0;
    close(fd);
    return ret;
}

static int dhcp_apply_lease(const char *ifname, uint32_t ip, uint32_t netmask,
                            uint32_t gateway)
{
    int fd;
    struct ifreq ifr;
    struct sockaddr_in *sin;
    int ret = -1;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = ip;
    if (ioctl(fd, SIOCSIFADDR, &ifr) < 0) {
        fprintf(stderr, "dhclient: SIOCSIFADDR failed\r\n");
        goto out;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = netmask;
    if (ioctl(fd, SIOCSIFNETMASK, &ifr) < 0) {
        fprintf(stderr, "dhclient: SIOCSIFNETMASK failed\r\n");
        goto out;
    }

    if (gateway) {
        struct rtentry rte;
        struct sockaddr_in *gw = (struct sockaddr_in *)&rte.rt_gateway;
        memset(&rte, 0, sizeof(rte));
        gw->sin_family = AF_INET;
        gw->sin_addr.s_addr = gateway;
        rte.rt_flags = RTF_UP | RTF_GATEWAY;
        rte.rt_dev = (char *)ifname;
        if (ioctl(fd, SIOCADDRT, &rte) < 0)
            fprintf(stderr, "dhclient: SIOCADDRT failed (non-fatal)\r\n");
    }
    ret = 0;
out:
    close(fd);
    return ret;
}

static void dhcp_write_resolvconf(uint32_t dns_be)
{
    FILE *f;
    struct in_addr a;

    if (!dns_be)
        return;
    f = fopen("/etc/resolv.conf", "w");
    if (!f)
        return;
    a.s_addr = dns_be;
    fprintf(f, "nameserver %s\n", inet_ntoa(a));
    fclose(f);
}

/* --- DHCP message assembly --- */

static size_t dhcp_build(struct dhcp_msg *msg, uint8_t msg_type,
                         const uint8_t mac[6], uint32_t xid,
                         uint32_t req_ip, uint32_t server_id)
{
    uint8_t *opt;
    size_t opt_off = 0;

    memset(msg, 0, sizeof(*msg));
    msg->op = BOOTP_OP_REQUEST;
    msg->htype = 1;
    msg->hlen = 6;
    msg->xid = xid;
    msg->flags = htons(0x8000); /* request broadcast reply */
    memcpy(msg->chaddr, mac, 6);
    msg->magic = htonl(DHCP_MAGIC_COOKIE);

    opt = msg->options;
    opt[opt_off++] = OPT_MSG_TYPE;
    opt[opt_off++] = 1;
    opt[opt_off++] = msg_type;

    if (msg_type == DHCPREQUEST && req_ip) {
        opt[opt_off++] = OPT_REQ_IP;
        opt[opt_off++] = 4;
        memcpy(opt + opt_off, &req_ip, 4);
        opt_off += 4;
    }
    if (msg_type == DHCPREQUEST && server_id) {
        opt[opt_off++] = OPT_SERVER_ID;
        opt[opt_off++] = 4;
        memcpy(opt + opt_off, &server_id, 4);
        opt_off += 4;
    }

    opt[opt_off++] = OPT_PARAM_LIST;
    opt[opt_off++] = 4;
    opt[opt_off++] = OPT_SUBNET;
    opt[opt_off++] = OPT_ROUTER;
    opt[opt_off++] = OPT_DNS;
    opt[opt_off++] = OPT_LEASE_TIME;

    opt[opt_off++] = OPT_END;
    /* Pad to BOOTP minimum 300 option bytes. */
    while (opt_off < 60)
        opt[opt_off++] = OPT_PAD;

    /* Fixed header (240) + options. */
    return 240 + opt_off;
}

static size_t frame_build(uint8_t *frame, size_t framecap,
                          const uint8_t mac[6], const struct dhcp_msg *dhcp,
                          size_t dhcp_len)
{
    struct eth_hdr *eth;
    struct ip_hdr *ip;
    struct udp_hdr *udp;
    uint8_t *payload;
    size_t total_ip, udp_len;
    uint8_t pseudo[12];

    udp_len = sizeof(*udp) + dhcp_len;
    total_ip = sizeof(*ip) + udp_len;
    if (sizeof(*eth) + total_ip > framecap)
        return 0;

    eth = (struct eth_hdr *)frame;
    memset(eth->dst, 0xFF, 6);
    memcpy(eth->src, mac, 6);
    eth->type = htons(ETH_P_IP);

    ip = (struct ip_hdr *)(frame + sizeof(*eth));
    ip->vhl = 0x45;
    ip->tos = 0;
    ip->tot_len = htons((uint16_t)total_ip);
    ip->id = 0;
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->proto = 17; /* UDP */
    ip->check = 0;
    ip->src = 0;
    ip->dst = 0xFFFFFFFFU;
    ip->check = htons(ip_checksum(ip, sizeof(*ip)));

    udp = (struct udp_hdr *)(frame + sizeof(*eth) + sizeof(*ip));
    udp->src_port = htons(DHCP_CLIENT_PORT);
    udp->dst_port = htons(DHCP_SERVER_PORT);
    udp->len = htons((uint16_t)udp_len);
    udp->check = 0;

    payload = frame + sizeof(*eth) + sizeof(*ip) + sizeof(*udp);
    memcpy(payload, dhcp, dhcp_len);

    /* UDP checksum over pseudo header + udp + payload. */
    memcpy(pseudo + 0, &ip->src, 4);
    memcpy(pseudo + 4, &ip->dst, 4);
    pseudo[8] = 0;
    pseudo[9] = 17;
    pseudo[10] = (uint8_t)(udp_len >> 8);
    pseudo[11] = (uint8_t)(udp_len & 0xFFU);
    {
        uint32_t sum = 0;
        const uint8_t *p;
        size_t i;
        for (i = 0; i + 1 < sizeof(pseudo); i += 2)
            sum += ((uint32_t)pseudo[i] << 8) | pseudo[i + 1];
        p = (const uint8_t *)udp;
        for (i = 0; i + 1 < udp_len; i += 2)
            sum += ((uint32_t)p[i] << 8) | p[i + 1];
        if (udp_len & 1)
            sum += (uint32_t)p[udp_len - 1] << 8;
        while (sum >> 16)
            sum = (sum & 0xFFFFU) + (sum >> 16);
        udp->check = htons((uint16_t)~sum);
        if (udp->check == 0)
            udp->check = 0xFFFFU;
    }

    return sizeof(*eth) + total_ip;
}

/* --- DHCP reply parsing --- */

struct dhcp_info {
    uint8_t  type;
    uint32_t yiaddr;
    uint32_t server_id;
    uint32_t netmask;
    uint32_t router;
    uint32_t dns;
    uint32_t lease;
};

static int dhcp_parse_reply(const uint8_t *frame, size_t frame_len,
                            uint32_t xid_want, struct dhcp_info *out)
{
    const struct eth_hdr *eth;
    const struct ip_hdr *ip;
    const struct udp_hdr *udp;
    const struct dhcp_msg *dhcp;
    const uint8_t *opt, *opt_end;
    size_t ip_hlen;

    if (frame_len < sizeof(*eth) + sizeof(*ip) + sizeof(*udp) + 240)
        return -1;
    eth = (const struct eth_hdr *)frame;
    if (ntohs(eth->type) != ETH_P_IP)
        return -1;
    ip = (const struct ip_hdr *)(frame + sizeof(*eth));
    if ((ip->vhl >> 4) != 4)
        return -1;
    ip_hlen = (size_t)(ip->vhl & 0x0FU) * 4U;
    if (ip->proto != 17)
        return -1;
    if (sizeof(*eth) + ip_hlen + sizeof(*udp) > frame_len)
        return -1;

    udp = (const struct udp_hdr *)(frame + sizeof(*eth) + ip_hlen);
    if (ntohs(udp->dst_port) != DHCP_CLIENT_PORT)
        return -1;

    dhcp = (const struct dhcp_msg *)(frame + sizeof(*eth) + ip_hlen + sizeof(*udp));
    if ((const uint8_t *)dhcp + 240 > frame + frame_len)
        return -1;
    if (dhcp->op != BOOTP_OP_REPLY)
        return -1;
    if (dhcp->xid != xid_want)
        return -1;
    if (ntohl(dhcp->magic) != DHCP_MAGIC_COOKIE)
        return -1;

    memset(out, 0, sizeof(*out));
    out->yiaddr = dhcp->yiaddr;

    opt = dhcp->options;
    opt_end = frame + frame_len;
    while (opt < opt_end) {
        uint8_t t = *opt++;
        uint8_t l;
        if (t == OPT_END)
            break;
        if (t == OPT_PAD)
            continue;
        if (opt >= opt_end)
            break;
        l = *opt++;
        if (opt + l > opt_end)
            break;
        switch (t) {
        case OPT_MSG_TYPE:
            if (l >= 1)
                out->type = opt[0];
            break;
        case OPT_SERVER_ID:
            if (l >= 4)
                memcpy(&out->server_id, opt, 4);
            break;
        case OPT_SUBNET:
            if (l >= 4)
                memcpy(&out->netmask, opt, 4);
            break;
        case OPT_ROUTER:
            if (l >= 4)
                memcpy(&out->router, opt, 4);
            break;
        case OPT_DNS:
            if (l >= 4)
                memcpy(&out->dns, opt, 4);
            break;
        case OPT_LEASE_TIME:
            if (l >= 4)
                out->lease = ntohl(*(const uint32_t *)opt);
            break;
        default:
            break;
        }
        opt += l;
    }
    return 0;
}

static void ipstr(uint32_t be, char *out)
{
    struct in_addr a;
    a.s_addr = be;
    strncpy(out, inet_ntoa(a), 15);
    out[15] = '\0';
}

/* --- DORA loop --- */

#define DHCP_TIMEOUT_MS 8000
#define DHCP_MAX_TRIES  3

static int dhcp_wait_reply(int sock, uint8_t *buf, size_t buflen,
                           uint32_t xid, uint8_t want_type,
                           struct dhcp_info *info, int timeout_ms)
{
    struct pollfd pfd;
    int r;
    int elapsed = 0;

    while (elapsed < timeout_ms) {
        int remain = timeout_ms - elapsed;
        pfd.fd = sock;
        pfd.events = POLLIN;
        pfd.revents = 0;
        r = poll(&pfd, 1, remain);
        if (r <= 0)
            break;
        if (!(pfd.revents & POLLIN))
            break;
        r = recv(sock, buf, buflen, 0);
        if (r <= 0) {
            elapsed += 100;
            continue;
        }
        if (dhcp_parse_reply(buf, (size_t)r, xid, info) == 0) {
            if (info->type == want_type)
                return 0;
            if (info->type == DHCPNAK)
                return -2;
        }
        elapsed += 50;
    }
    return -1;
}

#ifndef APP_DHCLIENT_MODULE
int main(int argc, char *argv[])
#else
int icebox_dhclient(int argc, char *argv[])
#endif
{
    const char *ifname = NULL;
    int sock;
    uint8_t mac[6];
    int ifindex;
    struct sockaddr_ll_compat sll;
    struct dhcp_msg dhcp;
    struct dhcp_info info;
    uint8_t frame[1514];
    size_t dhcp_len, frame_len;
    uint32_t xid;
    int i;
    char ipbuf[16];
    int tries;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <ifname>\r\n", argv[0]);
        return 2;
    }
    ifname = argv[1];

    ifindex = dhcp_iface_index(ifname, mac);
    if (ifindex < 0) {
        fprintf(stderr, "dhclient: %s: no such interface\r\n", ifname);
        return 1;
    }
    dhcp_bring_up(ifname);

    sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
    if (sock < 0) {
        fprintf(stderr, "dhclient: socket(AF_PACKET) failed: %s\r\n",
                strerror(errno));
        return 1;
    }

    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_IP);
    sll.sll_ifindex = ifindex;
    sll.sll_halen = 6;
    memcpy(sll.sll_addr, mac, 6);
    (void)bind(sock, (struct sockaddr *)&sll, sizeof(sll));

    {
        struct timeval tv;
        uint32_t seed = 0;
        if (gettimeofday(&tv, NULL) == 0)
            seed = (uint32_t)tv.tv_sec ^ (uint32_t)tv.tv_usec;
        xid = seed ^ ((uint32_t)mac[2] << 24) ^ ((uint32_t)mac[3] << 16) ^
              ((uint32_t)mac[4] << 8) ^ mac[5];
    }

    /* --- DISCOVER -> OFFER --- */
    for (tries = 0; tries < DHCP_MAX_TRIES; tries++) {
        dhcp_len = dhcp_build(&dhcp, DHCPDISCOVER, mac, xid, 0, 0);
        frame_len = frame_build(frame, sizeof(frame), mac, &dhcp, dhcp_len);
        if (frame_len == 0) {
            close(sock);
            return 1;
        }
        if (sendto(sock, frame, frame_len, 0,
                   (struct sockaddr *)&sll, sizeof(sll)) < 0) {
            fprintf(stderr, "dhclient: send DISCOVER failed: %s\r\n",
                    strerror(errno));
            close(sock);
            return 1;
        }
        if (dhcp_wait_reply(sock, frame, sizeof(frame), xid, DHCPOFFER,
                            &info, DHCP_TIMEOUT_MS) == 0)
            break;
    }
    if (tries >= DHCP_MAX_TRIES) {
        fprintf(stderr, "dhclient: no DHCPOFFER received\r\n");
        close(sock);
        return 1;
    }

    ipstr(info.yiaddr, ipbuf);
    fprintf(stdout, "dhclient: offer %s", ipbuf);
    if (info.server_id) {
        ipstr(info.server_id, ipbuf);
        fprintf(stdout, " from %s", ipbuf);
    }
    fprintf(stdout, "\r\n");

    /* --- REQUEST -> ACK --- */
    for (tries = 0; tries < DHCP_MAX_TRIES; tries++) {
        struct dhcp_info ack;
        dhcp_len = dhcp_build(&dhcp, DHCPREQUEST, mac, xid,
                              info.yiaddr, info.server_id);
        frame_len = frame_build(frame, sizeof(frame), mac, &dhcp, dhcp_len);
        if (sendto(sock, frame, frame_len, 0,
                   (struct sockaddr *)&sll, sizeof(sll)) < 0) {
            close(sock);
            return 1;
        }
        i = dhcp_wait_reply(sock, frame, sizeof(frame), xid, DHCPACK,
                            &ack, DHCP_TIMEOUT_MS);
        if (i == 0) {
            /* merge yiaddr from ACK, keep options from OFFER if missing. */
            if (ack.netmask)
                info.netmask = ack.netmask;
            if (ack.router)
                info.router = ack.router;
            if (ack.dns)
                info.dns = ack.dns;
            if (ack.lease)
                info.lease = ack.lease;
            info.yiaddr = ack.yiaddr;
            break;
        }
        if (i == -2) {
            fprintf(stderr, "dhclient: server NAK\r\n");
            close(sock);
            return 1;
        }
    }
    if (tries >= DHCP_MAX_TRIES) {
        fprintf(stderr, "dhclient: no DHCPACK received\r\n");
        close(sock);
        return 1;
    }

    close(sock);

    if (dhcp_apply_lease(ifname, info.yiaddr, info.netmask, info.router) < 0)
        return 1;
    dhcp_write_resolvconf(info.dns);

    ipstr(info.yiaddr, ipbuf);
    fprintf(stdout, "dhclient: bound %s", ipbuf);
    if (info.netmask) {
        ipstr(info.netmask, ipbuf);
        fprintf(stdout, " mask %s", ipbuf);
    }
    if (info.router) {
        ipstr(info.router, ipbuf);
        fprintf(stdout, " gw %s", ipbuf);
    }
    if (info.dns) {
        ipstr(info.dns, ipbuf);
        fprintf(stdout, " dns %s", ipbuf);
    }
    if (info.lease)
        fprintf(stdout, " lease %us", (unsigned)info.lease);
    fprintf(stdout, "\r\n");
    return 0;
}
