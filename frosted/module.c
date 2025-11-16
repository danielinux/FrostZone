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

#if CONFIG_TCPIP
#define TCPIP_LOCK() tcpip_lock()
#define TCPIP_UNLOCK() tcpip_unlock()
#else
#define TCPIP_LOCK() do {} while (0)
#define TCPIP_UNLOCK() do {} while (0)
#endif

struct address_family {
    struct module *mod;
    uint16_t family;
    struct address_family *next;
};

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
   af = kalloc(sizeof(struct address_family));
   if (!af)
       return -1;
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
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!se || task_ptr_valid(se)) {
        ret = -EACCES;
        goto out;
    }

    fno = task_filedesc_get(sd);

    if (fno && fno->owner && fno->owner->ops.bind)
        ret = fno->owner->ops.bind(sd, se->se_addr, se->se_len);

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
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!se || task_ptr_valid(se)) {
        ret = -EACCES;
        goto out;
    }

    fno = task_filedesc_get(sd);

    if (fno && fno->owner && fno->owner->ops.connect)
        ret = fno->owner->ops.connect(sd, se->se_addr, se->se_len);

out:
    TCPIP_UNLOCK();
    return ret;
}

int sys_accept_hdlr(int sd, struct sockaddr_env *se)
{
    struct fnode *fno;
    int ret = -EINVAL;

    TCPIP_LOCK();
    fno = task_filedesc_get(sd);
    if (task_ptr_valid(se)) {
        ret = -EACCES;
        goto out;
    }
    if (fno && fno->owner && fno->owner->ops.accept) {
        if (se)
            ret = fno->owner->ops.accept(sd, se->se_addr, &(se->se_len));
        else
            ret = fno->owner->ops.accept(sd, NULL, NULL);
    }
out:
    TCPIP_UNLOCK();
    return ret;
}


int sys_recvfrom_hdlr(int sd, void *buf, int len, int flags, struct sockaddr_env *se)
{
    struct fnode *fno;
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
            ret = fno->owner->ops.recvfrom(sd, buf, len, flags, se->se_addr, &(se->se_len));
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
            ret = fno->owner->ops.sendto(sd, buf, len, flags, se->se_addr, se->se_len);
            goto out;
        }
        ret = fno->owner->ops.sendto(sd, buf, len, flags, NULL, 0);
    }

out:
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
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!se)
        goto out;

    if (task_ptr_valid(se)) {
        ret = -EACCES;
        goto out;
    }

    fno = task_filedesc_get(sd);
    if (fno && fno->owner && fno->owner->ops.getsockname)
        ret = fno->owner->ops.getsockname(sd, se->se_addr, &(se->se_len));

out:
    TCPIP_UNLOCK();
    return ret;
}

int sys_getpeername_hdlr(int sd, struct sockaddr_env *se)
{
    struct fnode *fno;
    int ret = -EINVAL;

    TCPIP_LOCK();
    if (!se)
        goto out;

    if (task_ptr_valid(se)) {
        ret = -EACCES;
        goto out;
    }

    fno = task_filedesc_get(sd);
    if (fno && fno->owner && fno->owner->ops.getpeername)
        ret = fno->owner->ops.getpeername(sd, se->se_addr, &(se->se_len));

out:
    TCPIP_UNLOCK();
    return ret;
}
