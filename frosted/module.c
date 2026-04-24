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
#include "string.h"

/* Kernel-local mirror of <sys/socket.h>'s msghdr/iovec. Must match bit-by-bit:
 * userland passes pointers into the handler. */
#ifndef _STRUCT_IOVEC_DECLARED
#define _STRUCT_IOVEC_DECLARED
struct iovec {
    void  *iov_base;
    uint32_t iov_len;
};
#endif
struct msghdr {
    void        *msg_name;
    uint32_t     msg_namelen;
    struct iovec *msg_iov;
    int          msg_iovlen;
    void        *msg_control;
    uint32_t     msg_controllen;
    int          msg_flags;
};

#if CONFIG_TCPIP
#define TCPIP_LOCK() tcpip_lock()
#define TCPIP_UNLOCK() tcpip_unlock()
#else
#define TCPIP_LOCK() do {} while (0)
#define TCPIP_UNLOCK() do {} while (0)
#endif

#define MAX_ADDRESS_FAMILIES 8

struct address_family {
    struct module *mod;
    uint16_t family;
    struct address_family *next;
};

static struct address_family af_table[MAX_ADDRESS_FAMILIES];
static int af_count = 0;
struct address_family *AF = NULL;

int register_module(struct module *m)
{
    m->next = MODS;
    MODS = m;
    return 0;
}

int unregister_module(struct module *m)
{
    struct module *cur = MODS;
    while (cur) {
        /*XXX*/
        cur = cur->next;
    }
}

struct module *module_search(char *name)
{
    struct module *m = MODS;
    while(m) {
        if (strcmp(m->name, name) == 0)
            return m;
        m = m->next;
    }
    return NULL;
}

static struct module *af_to_module(uint16_t family)
{
    struct address_family *af = AF;
    while (af) {
        if (af->family == family)
            return af->mod;
        af = af->next;
    }
    return NULL;
}

int register_addr_family(struct module *m, uint16_t family)
{
   struct address_family *af;
   if (af_to_module(family))
       return -1; /* Another module already claimed this AF */
   if (af_count >= MAX_ADDRESS_FAMILIES)
       return -1;
   af = &af_table[af_count++];
   af->family = family;
   af->mod = m;
   af->next = AF;
   AF = af;
   return 0;
}

int sys_read_hdlr(int fd, void *buf, int len)
{
    struct fnode *fno;

    if (!buf)
        return -EIO;

    if (task_ptr_valid(buf))
        return -EACCES;

    if (!task_fd_readable(fd))
        return -EPERM;

    fno = task_filedesc_get(fd);
    if (fno && fno->owner->ops.read) {
        task_set_cur_fd(fd);
        return fno->owner->ops.read(fno, buf, len);
    } else if (fno->owner && fno->owner->ops.recvfrom) {
        return fno->owner->ops.recvfrom(fd, buf, len, 0, NULL, NULL);
    }

    return -ENOENT;
}

int sys_write_hdlr(int fd, void *buf, int len)
{
    struct fnode *fno;

    if (!buf)
        return -EIO;

    if (task_ptr_valid(buf))
        return -EACCES;

    if (!task_fd_writable(fd))
        return -EPERM;

    fno = task_filedesc_get(fd);
    if (!fno)
        return -ENOENT;

    if (fno->owner && fno->owner->ops.write) {
        task_set_cur_fd(fd);
        return fno->owner->ops.write(fno, buf, len);
    } else if (fno->owner && fno->owner->ops.sendto) {
        return fno->owner->ops.sendto(fd, buf, len, 0, NULL, 0);
    }

    return -EOPNOTSUPP;
}

int sys_socket_hdlr(int family, int type, int proto)
{
    struct module *m;
    int ret;

    TCPIP_LOCK();
    m = af_to_module(family);
    if(!m || !(m->ops.socket)) {
        ret = -EOPNOTSUPP;
        goto out;
    }
    ret = m->ops.socket(family, type, proto);
out:
    TCPIP_UNLOCK();
    return ret;
}

int sys_bind_hdlr(int sd, struct sockaddr_env *se)
{
    struct fnode *fno;
    union { struct sockaddr sa; uint8_t raw[32]; } kaddr;
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!se || task_ptr_valid(se)) {
        ret = -EACCES;
        goto out;
    }

    if (!se->se_addr || task_ptr_valid(se->se_addr) ||
        se->se_len > sizeof(kaddr)) {
        ret = -EACCES;
        goto out;
    }
    memcpy(&kaddr.sa, se->se_addr, se->se_len);

    fno = task_filedesc_get(sd);

    if (fno && fno->owner && fno->owner->ops.bind)
        ret = fno->owner->ops.bind(sd, &kaddr.sa, se->se_len);

out:
    TCPIP_UNLOCK();
    return ret;
}

int sys_listen_hdlr(int sd, unsigned int backlog)
{
    struct fnode *fno;
    int ret = -EINVAL;

    TCPIP_LOCK();
    fno = task_filedesc_get(sd);
    if (fno && fno->owner && fno->owner->ops.listen)
        ret = fno->owner->ops.listen(sd, backlog);
    TCPIP_UNLOCK();
    return ret;
}

int sys_connect_hdlr(int sd, struct sockaddr_env *se)
{
    struct fnode *fno;
    union { struct sockaddr sa; uint8_t raw[32]; } kaddr;
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!se || task_ptr_valid(se)) {
        ret = -EACCES;
        goto out;
    }

    if (!se->se_addr || task_ptr_valid(se->se_addr) ||
        se->se_len > sizeof(kaddr)) {
        ret = -EACCES;
        goto out;
    }
    memcpy(&kaddr.sa, se->se_addr, se->se_len);

    fno = task_filedesc_get(sd);

    if (fno && fno->owner && fno->owner->ops.connect)
        ret = fno->owner->ops.connect(sd, &kaddr.sa, se->se_len);

out:
    TCPIP_UNLOCK();
    return ret;
}

int sys_accept_hdlr(int sd, struct sockaddr_env *se)
{
    struct fnode *fno;
    union { struct sockaddr sa; uint8_t raw[32]; } kaddr;
    unsigned int kaddrlen;
    int ret = -EINVAL;

    TCPIP_LOCK();
    fno = task_filedesc_get(sd);
    if (se && task_ptr_valid(se)) {
        ret = -EACCES;
        goto out;
    }
    if (fno && fno->owner && fno->owner->ops.accept) {
        if (se) {
            if (!se->se_addr || task_ptr_valid(se->se_addr)) {
                ret = -EACCES;
                goto out;
            }
            kaddrlen = sizeof(kaddr);
            ret = fno->owner->ops.accept(sd, &kaddr.sa, &kaddrlen);
            if (ret >= 0) {
                if (kaddrlen > se->se_len)
                    kaddrlen = se->se_len;
                memcpy(se->se_addr, &kaddr.sa, kaddrlen);
                se->se_len = kaddrlen;
            }
        } else {
            ret = fno->owner->ops.accept(sd, NULL, NULL);
        }
    }
out:
    TCPIP_UNLOCK();
    return ret;
}


int sys_recvfrom_hdlr(int sd, void *buf, int len, int flags, struct sockaddr_env *se)
{
    struct fnode *fno;
    union { struct sockaddr sa; uint8_t raw[32]; } kaddr;
    unsigned int kaddrlen;
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!buf || task_ptr_valid(buf)) {
        ret = -EACCES;
        goto out;
    }

    fno = task_filedesc_get(sd);
    if (fno && fno->owner && fno->owner->ops.recvfrom) {
        if (se) {
            if (task_ptr_valid(se)) {
                ret = -EACCES;
                goto out;
            }
            if (!se->se_addr || task_ptr_valid(se->se_addr)) {
                ret = -EACCES;
                goto out;
            }
            kaddrlen = sizeof(kaddr);
            ret = fno->owner->ops.recvfrom(sd, buf, len, flags, &kaddr.sa, &kaddrlen);
            if (ret >= 0) {
                if (kaddrlen > se->se_len)
                    kaddrlen = se->se_len;
                memcpy(se->se_addr, &kaddr.sa, kaddrlen);
                se->se_len = kaddrlen;
            }
            goto out;
        }
        ret = fno->owner->ops.recvfrom(sd, buf, len, flags, NULL, NULL);
    }

out:
    TCPIP_UNLOCK();
    return ret;
}

int sys_sendto_hdlr(int sd, const void *buf, int len, int flags, struct sockaddr_env *se )
{
    struct fnode *fno;
    union { struct sockaddr sa; uint8_t raw[32]; } kaddr;
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!buf || task_ptr_valid(buf)) {
        ret = -EACCES;
        goto out;
    }

    fno = task_filedesc_get(sd);
    if (fno && fno->owner && fno->owner->ops.sendto) {
        if (se) {
            if (task_ptr_valid(se)) {
                ret = -EACCES;
                goto out;
            }
            if (!se->se_addr || task_ptr_valid(se->se_addr) ||
                se->se_len > sizeof(kaddr)) {
                ret = -EACCES;
                goto out;
            }
            memcpy(&kaddr.sa, se->se_addr, se->se_len);
            ret = fno->owner->ops.sendto(sd, buf, len, flags, &kaddr.sa, se->se_len);
            goto out;
        }
        ret = fno->owner->ops.sendto(sd, buf, len, flags, NULL, 0);
    }

out:
    TCPIP_UNLOCK();
    return ret;
}

/* Bound each call — avoids runaway allocations via userland-supplied iovecs. */
#define NL_MSG_MAX 4096

static int msghdr_total_len(const struct msghdr *msg)
{
    int i, total = 0;
    if (!msg->msg_iov)
        return 0;
    for (i = 0; i < msg->msg_iovlen; i++) {
        if (!msg->msg_iov[i].iov_base && msg->msg_iov[i].iov_len)
            return -EACCES;
        total += (int)msg->msg_iov[i].iov_len;
        if (total > NL_MSG_MAX)
            return -EMSGSIZE;
    }
    return total;
}

/* Copy iovec chain into a flat buffer (allocated by caller). */
static int msghdr_gather(const struct msghdr *msg, void *dst, int cap)
{
    int i, off = 0;
    char *d = (char *)dst;
    for (i = 0; i < msg->msg_iovlen; i++) {
        int n = (int)msg->msg_iov[i].iov_len;
        if (off + n > cap)
            return -EMSGSIZE;
        memcpy(d + off, msg->msg_iov[i].iov_base, n);
        off += n;
    }
    return off;
}

/* Scatter a flat buffer back into iovec chain. Returns bytes delivered. */
static int msghdr_scatter(struct msghdr *msg, const void *src, int len)
{
    int i, off = 0;
    const char *s = (const char *)src;
    for (i = 0; i < msg->msg_iovlen && off < len; i++) {
        int take = len - off;
        if (take > (int)msg->msg_iov[i].iov_len)
            take = (int)msg->msg_iov[i].iov_len;
        memcpy(msg->msg_iov[i].iov_base, s + off, take);
        off += take;
    }
    return off;
}

int sys_sendmsg_hdlr(int sd, const struct msghdr *msg, int flags)
{
    struct fnode *fno;
    union { struct sockaddr sa; uint8_t raw[32]; } kaddr;
    void *flat = NULL;
    int total, ret = -EINVAL;

    TCPIP_LOCK();
    if (!msg || task_ptr_valid((void *)msg)) {
        ret = -EACCES;
        goto out;
    }
    if (msg->msg_iov && task_ptr_valid(msg->msg_iov)) {
        ret = -EACCES;
        goto out;
    }
    fno = task_filedesc_get(sd);
    if (!fno || !fno->owner) {
        ret = -EBADF;
        goto out;
    }

    /* Native path: family implements sendmsg directly. */
    if (fno->owner->ops.sendmsg) {
        ret = fno->owner->ops.sendmsg(sd, msg, flags);
        goto out;
    }

    /* Fallback: linearize into a scratch buffer and call sendto. */
    if (!fno->owner->ops.sendto) {
        ret = -EOPNOTSUPP;
        goto out;
    }
    total = msghdr_total_len(msg);
    if (total < 0) {
        ret = total;
        goto out;
    }
    if (total > 0) {
        flat = kalloc(total);
        if (!flat) {
            ret = -ENOMEM;
            goto out;
        }
        ret = msghdr_gather(msg, flat, total);
        if (ret < 0)
            goto out;
    }
    if (msg->msg_name && msg->msg_namelen > 0) {
        if (msg->msg_namelen > (unsigned)sizeof(kaddr)) {
            ret = -EINVAL;
            goto out;
        }
        memcpy(&kaddr.sa, msg->msg_name, msg->msg_namelen);
        ret = fno->owner->ops.sendto(sd, flat, total, flags, &kaddr.sa, msg->msg_namelen);
    } else {
        ret = fno->owner->ops.sendto(sd, flat, total, flags, NULL, 0);
    }

out:
    if (flat)
        kfree(flat);
    TCPIP_UNLOCK();
    return ret;
}

int sys_recvmsg_hdlr(int sd, struct msghdr *msg, int flags)
{
    struct fnode *fno;
    union { struct sockaddr sa; uint8_t raw[32]; } kaddr;
    unsigned int kaddrlen;
    void *flat = NULL;
    int cap, got, ret = -EINVAL;

    TCPIP_LOCK();
    if (!msg || task_ptr_valid(msg)) {
        ret = -EACCES;
        goto out;
    }
    if (msg->msg_iov && task_ptr_valid(msg->msg_iov)) {
        ret = -EACCES;
        goto out;
    }
    fno = task_filedesc_get(sd);
    if (!fno || !fno->owner) {
        ret = -EBADF;
        goto out;
    }

    /* Native path. */
    if (fno->owner->ops.recvmsg) {
        ret = fno->owner->ops.recvmsg(sd, msg, flags);
        goto out;
    }

    /* Fallback: allocate a scratch buffer totalling the iov capacity, call
     * recvfrom, scatter back. */
    if (!fno->owner->ops.recvfrom) {
        ret = -EOPNOTSUPP;
        goto out;
    }
    cap = msghdr_total_len(msg);
    if (cap < 0) {
        ret = cap;
        goto out;
    }
    if (cap > 0) {
        flat = kalloc(cap);
        if (!flat) {
            ret = -ENOMEM;
            goto out;
        }
    }
    if (msg->msg_name && msg->msg_namelen > 0) {
        kaddrlen = sizeof(kaddr);
        got = fno->owner->ops.recvfrom(sd, flat, cap, flags, &kaddr.sa, &kaddrlen);
        if (got >= 0) {
            if (kaddrlen > msg->msg_namelen)
                kaddrlen = msg->msg_namelen;
            memcpy(msg->msg_name, &kaddr.sa, kaddrlen);
            msg->msg_namelen = kaddrlen;
        }
    } else {
        got = fno->owner->ops.recvfrom(sd, flat, cap, flags, NULL, NULL);
    }
    if (got >= 0) {
        msghdr_scatter(msg, flat, got);
        msg->msg_controllen = 0;
        msg->msg_flags = 0;
    }
    ret = got;

out:
    if (flat)
        kfree(flat);
    TCPIP_UNLOCK();
    return ret;
}

int sys_shutdown_hdlr(int sd, int how)
{
    struct fnode *fno;
    int ret = -EINVAL;

    TCPIP_LOCK();
    fno = task_filedesc_get(sd);
    if (fno && fno->owner && fno->owner->ops.shutdown)
        ret = fno->owner->ops.shutdown(sd, how);
    TCPIP_UNLOCK();
    return ret;
}

int sys_setsockopt_hdlr(int sd, int level, int optname, void *optval, unsigned int optlen)
{
    struct fnode *fno;
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!optval || task_ptr_valid(optval)) {
        ret = -EACCES;
        goto out;
    }

    fno = task_filedesc_get(sd);
    if (fno && fno->owner && fno->owner->ops.setsockopt)
        ret = fno->owner->ops.setsockopt(sd, level, optname, optval, optlen);

out:
    TCPIP_UNLOCK();
    return ret;
}

int sys_getsockopt_hdlr(int sd, int level, int optname, void *optval, unsigned int *optlen)
{
    struct fnode *fno;
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!optval || task_ptr_valid(optval)) {
        ret = -EACCES;
        goto out;
    }

    if (!optlen || task_ptr_valid(optlen)) {
        ret = -EACCES;
        goto out;
    }

    fno = task_filedesc_get(sd);
    if (fno && fno->owner && fno->owner->ops.getsockopt)
        ret = fno->owner->ops.getsockopt(sd, level, optname, optval, optlen);

out:
    TCPIP_UNLOCK();
    return ret;
}

int sys_getsockname_hdlr(int sd, struct sockaddr_env *se)
{
    struct fnode *fno;
    union { struct sockaddr sa; uint8_t raw[32]; } kaddr;
    unsigned int kaddrlen;
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!se)
        goto out;

    if (task_ptr_valid(se)) {
        ret = -EACCES;
        goto out;
    }

    if (!se->se_addr || task_ptr_valid(se->se_addr)) {
        ret = -EACCES;
        goto out;
    }

    fno = task_filedesc_get(sd);
    if (fno && fno->owner && fno->owner->ops.getsockname) {
        kaddrlen = sizeof(kaddr);
        ret = fno->owner->ops.getsockname(sd, &kaddr.sa, &kaddrlen);
        if (ret >= 0) {
            if (kaddrlen > se->se_len)
                kaddrlen = se->se_len;
            memcpy(se->se_addr, &kaddr.sa, kaddrlen);
            se->se_len = kaddrlen;
        }
    }

out:
    TCPIP_UNLOCK();
    return ret;
}

int sys_getpeername_hdlr(int sd, struct sockaddr_env *se)
{
    struct fnode *fno;
    union { struct sockaddr sa; uint8_t raw[32]; } kaddr;
    unsigned int kaddrlen;
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!se)
        goto out;

    if (task_ptr_valid(se)) {
        ret = -EACCES;
        goto out;
    }

    if (!se->se_addr || task_ptr_valid(se->se_addr)) {
        ret = -EACCES;
        goto out;
    }

    fno = task_filedesc_get(sd);
    if (fno && fno->owner && fno->owner->ops.getpeername) {
        kaddrlen = sizeof(kaddr);
        ret = fno->owner->ops.getpeername(sd, &kaddr.sa, &kaddrlen);
        if (ret >= 0) {
            if (kaddrlen > se->se_len)
                kaddrlen = se->se_len;
            memcpy(se->se_addr, &kaddr.sa, kaddrlen);
            se->se_len = kaddrlen;
        }
    }

out:
    TCPIP_UNLOCK();
    return ret;
}
