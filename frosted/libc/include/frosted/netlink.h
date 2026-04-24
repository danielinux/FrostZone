/*
 * Frosted: AF_NETLINK protocol family.
 *
 * Structures and macros follow the Linux netlink ABI so application code
 * can be ported with minimal churn. The header namespace is <frosted/...>
 * rather than <linux/...> to advertise that this is Frosted's own
 * implementation, not a Linux clone.
 */
#ifndef FROSTED_NETLINK_H
#define FROSTED_NETLINK_H

#include <stdint.h>

/* Used outside the kernel: pull in sockaddr base types. Inside the kernel
 * (KERNEL is set by frosted.h before any other include) we define the
 * minimum locally so the file stays self-contained. */
#ifndef KERNEL
#include <sys/types.h>
#include <sys/socket.h>
#else
#ifndef _SA_FAMILY_T_DECLARED
#define _SA_FAMILY_T_DECLARED
typedef uint16_t sa_family_t;
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol families on PF_NETLINK sockets. */
#define NETLINK_ROUTE           0
#define NETLINK_USERSOCK        2
#define NETLINK_KOBJECT_UEVENT 15

/* sockaddr_nl: address of a netlink endpoint. */
struct sockaddr_nl {
    sa_family_t     nl_family;  /* AF_NETLINK */
    unsigned short  nl_pad;     /* zero */
    uint32_t        nl_pid;     /* port id; 0 = kernel */
    uint32_t        nl_groups;  /* multicast groups mask */
};

/* Fixed header sitting at the front of every netlink message. */
struct nlmsghdr {
    uint32_t  nlmsg_len;        /* length including this header */
    uint16_t  nlmsg_type;       /* RTM_*, NLMSG_* */
    uint16_t  nlmsg_flags;      /* NLM_F_* */
    uint32_t  nlmsg_seq;        /* sender sequence number */
    uint32_t  nlmsg_pid;        /* sender port id */
};

/* nlmsg_flags bits. */
#define NLM_F_REQUEST   0x0001  /* a request message */
#define NLM_F_MULTI     0x0002  /* multipart; terminated by NLMSG_DONE */
#define NLM_F_ACK       0x0004  /* reply with NLMSG_ERROR(error=0) */
#define NLM_F_ECHO      0x0008
#define NLM_F_DUMP_INTR 0x0010

/* GET request modifiers. */
#define NLM_F_ROOT      0x0100  /* return whole table */
#define NLM_F_MATCH     0x0200  /* return by supplied key */
#define NLM_F_ATOMIC    0x0400
#define NLM_F_DUMP      (NLM_F_ROOT | NLM_F_MATCH)

/* NEW request modifiers. */
#define NLM_F_REPLACE   0x0100
#define NLM_F_EXCL      0x0200
#define NLM_F_CREATE    0x0400
#define NLM_F_APPEND    0x0800

/* Well-known message types (types >= NLMSG_MIN_TYPE are protocol-specific). */
#define NLMSG_NOOP      0x0001
#define NLMSG_ERROR     0x0002
#define NLMSG_DONE      0x0003
#define NLMSG_OVERRUN   0x0004
#define NLMSG_MIN_TYPE  0x0010

/* Alignment: every nlmsghdr and every payload starts on a 4-byte boundary. */
#define NLMSG_ALIGNTO    4U
#define NLMSG_ALIGN(len) (((len) + NLMSG_ALIGNTO - 1) & ~(NLMSG_ALIGNTO - 1))
#define NLMSG_HDRLEN     ((int)NLMSG_ALIGN(sizeof(struct nlmsghdr)))
#define NLMSG_LENGTH(len) ((len) + NLMSG_HDRLEN)
#define NLMSG_SPACE(len) NLMSG_ALIGN(NLMSG_LENGTH(len))
#define NLMSG_DATA(nlh)  ((void *)(((char *)(nlh)) + NLMSG_HDRLEN))
#define NLMSG_NEXT(nlh, len) \
    ((len) -= NLMSG_ALIGN((nlh)->nlmsg_len), \
     (struct nlmsghdr *)(((char *)(nlh)) + NLMSG_ALIGN((nlh)->nlmsg_len)))
#define NLMSG_OK(nlh, len) \
    ((len) >= (int)sizeof(struct nlmsghdr) && \
     (nlh)->nlmsg_len >= sizeof(struct nlmsghdr) && \
     (int)(nlh)->nlmsg_len <= (len))
#define NLMSG_PAYLOAD(nlh, len) ((nlh)->nlmsg_len - NLMSG_SPACE((len)))

/* Body of NLMSG_ERROR: negated errno + original request header. */
struct nlmsgerr {
    int              error;     /* 0 = ack, -errno otherwise */
    struct nlmsghdr  msg;
};

#ifdef __cplusplus
}
#endif

#endif /* FROSTED_NETLINK_H */
