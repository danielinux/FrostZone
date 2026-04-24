/*
 * Frosted: routing / link / address messages over NETLINK_ROUTE.
 *
 * Structure names and numeric values match the Linux rtnetlink ABI so
 * the bit and byte layouts of on-the-wire messages are interchangeable.
 */
#ifndef FROSTED_RTNETLINK_H
#define FROSTED_RTNETLINK_H

#include <stdint.h>
#include <frosted/netlink.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Message types carried in nlmsghdr.nlmsg_type. */
#define RTM_NEWLINK   16
#define RTM_DELLINK   17
#define RTM_GETLINK   18
#define RTM_SETLINK   19
#define RTM_NEWADDR   20
#define RTM_DELADDR   21
#define RTM_GETADDR   22
#define RTM_NEWROUTE  24
#define RTM_DELROUTE  25
#define RTM_GETROUTE  26

/* Link message. One per interface. */
struct ifinfomsg {
    unsigned char   ifi_family;   /* AF_UNSPEC */
    unsigned char   __ifi_pad;
    unsigned short  ifi_type;     /* ARPHRD_* */
    int             ifi_index;    /* interface index */
    unsigned int    ifi_flags;    /* IFF_* */
    unsigned int    ifi_change;   /* change mask */
};

/* Address message. One per address on an interface. */
struct ifaddrmsg {
    unsigned char   ifa_family;   /* AF_INET / AF_INET6 */
    unsigned char   ifa_prefixlen;
    unsigned char   ifa_flags;
    unsigned char   ifa_scope;
    unsigned int    ifa_index;
};

/* Route message. */
struct rtmsg {
    unsigned char   rtm_family;
    unsigned char   rtm_dst_len;
    unsigned char   rtm_src_len;
    unsigned char   rtm_tos;
    unsigned char   rtm_table;    /* routing table id */
    unsigned char   rtm_protocol; /* RTPROT_* */
    unsigned char   rtm_scope;    /* RT_SCOPE_* */
    unsigned char   rtm_type;     /* RTN_* */
    unsigned int    rtm_flags;
};

/* Generic TLV attribute header. Follows every fixed-size *msg. */
struct rtattr {
    unsigned short  rta_len;
    unsigned short  rta_type;
};

/* Attribute alignment helpers (mirror Linux). */
#define RTA_ALIGNTO     4U
#define RTA_ALIGN(len)  (((len) + RTA_ALIGNTO - 1) & ~(RTA_ALIGNTO - 1))
#define RTA_OK(rta, len) \
    ((len) >= (int)sizeof(struct rtattr) && \
     (rta)->rta_len >= sizeof(struct rtattr) && \
     (int)(rta)->rta_len <= (len))
#define RTA_NEXT(rta, attrlen) \
    ((attrlen) -= RTA_ALIGN((rta)->rta_len), \
     (struct rtattr *)(((char *)(rta)) + RTA_ALIGN((rta)->rta_len)))
#define RTA_LENGTH(len) (RTA_ALIGN(sizeof(struct rtattr)) + (len))
#define RTA_SPACE(len)  RTA_ALIGN(RTA_LENGTH(len))
#define RTA_DATA(rta)   ((void *)(((char *)(rta)) + RTA_LENGTH(0)))
#define RTA_PAYLOAD(rta) ((int)((rta)->rta_len) - RTA_LENGTH(0))

/* IFLA_* attribute types for RTM_*LINK. */
#define IFLA_UNSPEC    0
#define IFLA_ADDRESS   1
#define IFLA_BROADCAST 2
#define IFLA_IFNAME    3
#define IFLA_MTU       4
#define IFLA_LINK      5
#define IFLA_QDISC     6
#define IFLA_STATS     7

/* IFA_* attribute types for RTM_*ADDR. */
#define IFA_UNSPEC    0
#define IFA_ADDRESS   1
#define IFA_LOCAL     2
#define IFA_LABEL     3
#define IFA_BROADCAST 4
#define IFA_ANYCAST   5
#define IFA_CACHEINFO 6

/* RTA_* attribute types for RTM_*ROUTE. */
#define RTA_UNSPEC    0
#define RTA_DST       1
#define RTA_SRC       2
#define RTA_IIF       3
#define RTA_OIF       4
#define RTA_GATEWAY   5
#define RTA_PRIORITY  6
#define RTA_PREFSRC   7
#define RTA_METRICS   8
#define RTA_MULTIPATH 9
#define RTA_FLOW      11
#define RTA_CACHEINFO 12
#define RTA_TABLE     15

/* Scopes. */
#define RT_SCOPE_UNIVERSE  0
#define RT_SCOPE_SITE    200
#define RT_SCOPE_LINK    253
#define RT_SCOPE_HOST    254
#define RT_SCOPE_NOWHERE 255

/* Route types. */
#define RTN_UNSPEC      0
#define RTN_UNICAST     1
#define RTN_LOCAL       2
#define RTN_BROADCAST   3
#define RTN_ANYCAST     4
#define RTN_MULTICAST   5
#define RTN_BLACKHOLE   6
#define RTN_UNREACHABLE 7

/* Route protocols (who installed the route). */
#define RTPROT_UNSPEC   0
#define RTPROT_REDIRECT 1
#define RTPROT_KERNEL   2
#define RTPROT_BOOT     3
#define RTPROT_STATIC   4

/* Built-in routing tables. */
#define RT_TABLE_UNSPEC  0
#define RT_TABLE_DEFAULT 253
#define RT_TABLE_MAIN    254
#define RT_TABLE_LOCAL   255

/* ARP hardware types (subset used by RTM_*LINK). */
#ifndef ARPHRD_ETHER
#define ARPHRD_ETHER   1
#endif
#ifndef ARPHRD_LOOPBACK
#define ARPHRD_LOOPBACK 772
#endif

#ifdef __cplusplus
}
#endif

#endif /* FROSTED_RTNETLINK_H */
