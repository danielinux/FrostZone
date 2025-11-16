#include "frosted.h"
#include "socket_in.h"
#include <sys/ioctl.h>
#include <string.h>
#include "net.h"
#include "fcntl.h"
#include "config.h"
#include "net/if.h"
#include "net/route.h"
#include "wolfip.h"

#if WOLFIP_ENABLE_LOOPBACK
#define FROSTED_WOLFIP_LOOPBACK_IF_IDX 0U
#define FROSTED_WOLFIP_PRIMARY_IF_IDX 1U
#define FROSTED_WOLFIP_LOOPBACK_IP 0x7F000001U
#define FROSTED_WOLFIP_LOOPBACK_MASK 0xFF000000U
#else
#define FROSTED_WOLFIP_LOOPBACK_IF_IDX 0U
#define FROSTED_WOLFIP_PRIMARY_IF_IDX 0U
#define FROSTED_WOLFIP_LOOPBACK_IP 0x7F000001U
#define FROSTED_WOLFIP_LOOPBACK_MASK 0xFF000000U
#endif


static struct module mod_socket_in;

struct wolfIP *IPStack;
int ipstack_timer = -1;
static int socket_in_ready;

#if WOLFIP_MAX_INTERFACES > 1
#define MAX_NETDEV_DRIVERS (WOLFIP_MAX_INTERFACES - 1U)
#else
#define MAX_NETDEV_DRIVERS 1U
#endif

struct netdev_entry {
    struct netdev_driver *driver;
    unsigned int if_idx;
    uint8_t attached;
};

static struct netdev_entry netdev_entries[MAX_NETDEV_DRIVERS];
static size_t netdev_entry_count;
static unsigned int netdev_next_if;

static size_t wolfip_ifname_len(const char *name)
{
    size_t len = 0;
    if (!name)
        return 0;
    while (len < IFNAMSIZ && name[len])
        len++;
    return len;
}

static struct wolfIP_ll_dev *wolfip_ll_lookup(const char *name, unsigned int *if_idx)
{
    unsigned int idx;
    struct wolfIP_ll_dev *ll = NULL;

    if (!IPStack)
        return NULL;

    if (name && name[0]) {
        for (idx = 0; idx < WOLFIP_MAX_INTERFACES; idx++) {
            ll = wolfIP_getdev_ex(IPStack, idx);
            if (!ll || ll->ifname[0] == '\0')
                continue;
            if (strncmp(name, ll->ifname, IFNAMSIZ) == 0) {
                if (if_idx)
                    *if_idx = idx;
                return ll;
            }
        }
    }

    idx = (FROSTED_WOLFIP_PRIMARY_IF_IDX < WOLFIP_MAX_INTERFACES) ? FROSTED_WOLFIP_PRIMARY_IF_IDX : 0U;
    ll = wolfIP_getdev_ex(IPStack, idx);
    if ((!ll || ll->ifname[0] == '\0') && idx != FROSTED_WOLFIP_LOOPBACK_IF_IDX) {
        idx = FROSTED_WOLFIP_LOOPBACK_IF_IDX;
        ll = wolfIP_getdev_ex(IPStack, idx);
    }
    if (if_idx)
        *if_idx = idx;
    return ll;
}

static struct wolfIP_ll_dev *wolfip_ll_from_ifr(struct ifreq *ifr, unsigned int *if_idx)
{
    char name_buf[IFNAMSIZ + 1];

    if (!ifr)
        return wolfip_ll_lookup(NULL, if_idx);

    memcpy(name_buf, ifr->ifr_name, IFNAMSIZ);
    name_buf[IFNAMSIZ] = '\0';
    return wolfip_ll_lookup(name_buf, if_idx);
}

static void netdev_reset_next_if(void)
{
#if defined(WOLFIP_ENABLE_LOOPBACK) && (WOLFIP_ENABLE_LOOPBACK != 0)
    netdev_next_if = 1U;
#else
    netdev_next_if = 0U;
#endif
}

static int netdev_attach_entry(size_t idx)
{
    struct netdev_entry *entry;
    struct wolfIP_ll_dev *ll;
    int ret;

    if (!IPStack || idx >= netdev_entry_count)
        return -ENODEV;

    entry = &netdev_entries[idx];
    if (!entry->driver || entry->attached)
        return 0;
    if (entry->driver->is_present && !entry->driver->is_present())
        return -ENODEV;
    if (netdev_next_if >= WOLFIP_MAX_INTERFACES)
        return -ENOSPC;

    ll = wolfIP_getdev_ex(IPStack, netdev_next_if);
    if (!ll)
        return -ENODEV;

    memset(ll, 0, sizeof(*ll));
    ret = entry->driver->attach(IPStack, ll, netdev_next_if);
    if (ret == 0) {
        entry->attached = 1;
        entry->if_idx = netdev_next_if;
        netdev_next_if++;
    }
    return ret;
}

static void netdev_attach_registered(void)
{
    size_t i;
    for (i = 0; i < netdev_entry_count; i++)
        (void)netdev_attach_entry(i);
}


struct frosted_inet_socket {
    struct fnode *node;
    int fd;
    struct task *task;
    uint16_t events;
    uint16_t revents;
    int bytes;
    int sock_fd;
};

struct frosted_inet_socket tcp_socket[MAX_TCPSOCKETS];
struct frosted_inet_socket udp_socket[MAX_UDPSOCKETS];

static struct frosted_inet_socket *fd_inet(int fd)
{
    struct fnode *fno;
    struct frosted_inet_socket *s;
    fno = task_filedesc_get(fd);
    if (!fno)
        return NULL;
    if (fno->owner != &mod_socket_in)
        return NULL;
    s = (struct frosted_inet_socket *)fno->priv;
    return s;
}


static struct frosted_inet_socket *sockfd_inet(int fd)
{
    if ((fd & MARK_TCP_SOCKET) == MARK_TCP_SOCKET) {
        fd &= ~MARK_TCP_SOCKET;
        if (fd < MAX_TCPSOCKETS)
            return &tcp_socket[fd];
    }
    else if ((fd & MARK_UDP_SOCKET) == MARK_UDP_SOCKET) {
        fd &= ~MARK_UDP_SOCKET;
        if (fd < MAX_UDPSOCKETS)
            return &udp_socket[fd];
    }
    return NULL;
} 

#define SOCK_BLOCKING(s) (((s->node->flags & O_NONBLOCK) == 0))

static int sock_poll(struct fnode *f, uint16_t events, uint16_t *revents)
{
    struct frosted_inet_socket *s;
    s = (struct frosted_inet_socket *)f->priv;

    if (s->revents & CB_EVENT_CLOSED)
        *revents |= POLLHUP;

    if (s->revents & CB_EVENT_TIMEOUT)
        *revents |= POLLERR;

    if (s->revents & CB_EVENT_READABLE)
        *revents |= POLLIN;

    if (s->revents & CB_EVENT_WRITABLE)
        *revents |= POLLOUT;

    if (((*revents) & (POLLHUP | POLLERR)) != 0) {
        return 1;
    }
    if ((events & *revents) != 0)
        return 1;

    s->events |= events;
    s->task = this_task();
    return 0;
}

static void wolfip_socket_event(int sockfd, uint16_t events, void *arg)
{
    struct frosted_inet_socket *s = sockfd_inet(sockfd);
    if (!s)
        return;
    s->revents |= events;
    if (((s->revents & s->events) != 0) && s->task) {
        task_resume(s->task);
        s->events = 0;
    }
}

static int sock_close(struct fnode *fno)
{
    struct frosted_inet_socket *s;
    int ret = -1;

    if (!fno) {
        ret = -1;
        goto out;
    }
    s = (struct frosted_inet_socket *)fno->priv;
    if (!s) {
        ret = -1;
        goto out;
    }

    wolfIP_sock_close(IPStack, s->sock_fd);
    kfree((struct fnode *)s->node);
    ret = 0;
out:
    
    return ret;
}


static int sock_socket(int domain, int type_flags, int protocol)
{
    struct frosted_inet_socket *s;
    int sock_fd;
    sock_fd = wolfIP_sock_socket(IPStack, domain, type_flags, protocol);
    if (sock_fd < 0) {
        return sock_fd;
    }
    s = sockfd_inet(sock_fd);
    if (!s)
        return -ENOMEM;
    s->node = kcalloc(sizeof(struct fnode), 1);
    if (!s->node)
        return -ENOMEM;
    s->node->owner = &mod_socket_in;
    s->node->flags = FL_RDWR;
    s->node->priv = s;
    s->sock_fd = sock_fd;
    wolfIP_register_callback(IPStack, s->sock_fd, wolfip_socket_event, NULL);
    s->fd = task_filedesc_add(s->node);
    if (s->fd >= 0)
        task_fd_setmask(s->fd, O_RDWR);
    return s->fd;
}

static int sock_recvfrom(int fd, void *buf, unsigned int len, int flags, struct sockaddr *addr, unsigned int *addrlen)
{
    struct frosted_inet_socket *s;
    int ret;
    struct wolfIP_sockaddr_in paddr;
    socklen_t sockaddr_len = sizeof(struct wolfIP_sockaddr_in);
    s = fd_inet(fd);
    if (!s)
        return -EINVAL;
    while (s->bytes < len) {
        if ((addr) && ((*addrlen) > 0)) {
            ret = wolfIP_sock_recvfrom(IPStack, s->sock_fd, buf + s->bytes, len - s->bytes, flags, (struct wolfIP_sockaddr *)&paddr, &sockaddr_len);
        } else {
            ret = wolfIP_sock_read(IPStack, s->sock_fd, buf + s->bytes, len - s->bytes);
        }

        if ((ret < 0) && (ret != (-11))) {
            goto out;
        }

        if ((ret == 0) || (ret == -11)) {
            s->revents &= (~CB_EVENT_READABLE);
            if (SOCK_BLOCKING(s))  {
                s->events = CB_EVENT_READABLE;
                s->task = this_task();
                task_suspend();
                ret = SYS_CALL_AGAIN;
                goto out;
            }
        }
        s->bytes += ret;
        if (s->bytes > 0)
            break;
    }
    ret = s->bytes;
    s->bytes = 0;
    s->events  &= (~CB_EVENT_READABLE);
    s->revents &= (~CB_EVENT_READABLE);
    if ((ret == 0) && !SOCK_BLOCKING(s)) {
        ret = -EAGAIN;
    }
out:
    
    return ret;
}

static int sock_sendto(int fd, const void *buf, unsigned int len, int flags, struct sockaddr *addr, unsigned int addrlen)
{
    struct frosted_inet_socket *s;
    int ret;
    s = fd_inet(fd);
    if (!s) {
        ret = -EINVAL;
        goto out;
    }


    while (len > s->bytes) {
        if ((addr) && (addrlen >0))
        {
            ret = wolfIP_sock_sendto(IPStack, s->sock_fd, buf + s->bytes, len - s->bytes, flags, (struct wolfIP_sockaddr*)addr, addrlen);
        } else {
            ret = wolfIP_sock_write(IPStack, s->sock_fd, buf + s->bytes, len - s->bytes);
        }
        if (ret == 0) {
            s->revents &= (~CB_EVENT_WRITABLE);
            if (SOCK_BLOCKING(s)) {
                s->events = CB_EVENT_WRITABLE;
                s->task = this_task();
                task_suspend();
                ret = SYS_CALL_AGAIN;
                goto out;
            }
        }
        if (ret < 0) {
            goto out;
        }

        s->bytes += ret;
        if (((s->sock_fd & MARK_UDP_SOCKET) == MARK_UDP_SOCKET) && (s->bytes > 0))
            break;
    }
    ret = s->bytes;
    s->bytes = 0;
    s->events  &= (~CB_EVENT_WRITABLE);
    if ((ret == 0) && !SOCK_BLOCKING(s)) {
        ret = -EAGAIN;
    }
out:
    
    return ret;
}

static int sock_bind(int fd, struct sockaddr *addr, unsigned int addrlen)
{
    struct frosted_inet_socket *s;
    int ret;
    s = fd_inet(fd);
    if (!s) {
        return -EINVAL;
    }
    ret = wolfIP_sock_bind(IPStack, s->sock_fd, (struct wolfIP_sockaddr *)addr, addrlen);
    return ret;
}

static int sock_accept(int fd, struct sockaddr *addr, unsigned int *addrlen)
{
    struct frosted_inet_socket *l, *s;
    int sock_fd;
    int ret = -1;

    l = fd_inet(fd);
    if (!l) {
        return -EINVAL;
    }
    l->events = CB_EVENT_READABLE;

    sock_fd = wolfIP_sock_accept(IPStack, l->sock_fd, (struct wolfIP_sockaddr *)addr, addrlen);
    if ((sock_fd < 0) && (sock_fd != -11))
        return sock_fd;
    if (sock_fd == -11) {
        if (SOCK_BLOCKING(l)) {
            l->task = this_task();
            task_suspend();
        }
        return SYS_CALL_AGAIN;
    }
    
    l->revents &= (~CB_EVENT_READABLE);
    if (sock_fd >= 0) {
        s = sockfd_inet(sock_fd);
        if (!s) {
            wolfIP_sock_close(IPStack, s->sock_fd);
            return -ENOMEM;
        }
        s->sock_fd = sock_fd;
        s->node = kcalloc(sizeof(struct fnode), 1);
        if (!s->node)
            return -ENOMEM;
        s->node->owner = &mod_socket_in;
        s->node->flags = FL_RDWR;
        s->node->priv = s;
        wolfIP_register_callback(IPStack, s->sock_fd, wolfip_socket_event, NULL);
        s->fd = task_filedesc_add(s->node);
        if (s->fd >= 0)
            task_fd_setmask(s->fd, O_RDWR);
        ret = s->fd;
    }
    return ret;
}

static int sock_connect(int fd, struct sockaddr *addr, unsigned int addrlen)
{
    struct frosted_inet_socket *s;
    int ret = -1;
    s = fd_inet(fd);
    if (!s) {
        return -EINVAL;
    }

    s->events = CB_EVENT_READABLE;
    if ((s->revents & CB_EVENT_READABLE) == 0) {
        ret = wolfIP_sock_connect(IPStack, s->sock_fd, (struct wolfIP_sockaddr *)addr, addrlen);
        if (SOCK_BLOCKING(s)) {
            s->task = this_task();
            task_suspend();
            ret = SYS_CALL_AGAIN;
        } else {
            ret = -EAGAIN;
        }
    }
    /* CB_EVENT_READABLE received. Successfully connected. */
    ret = 0;
    s->events  &= ~(CB_EVENT_READABLE);
    s->revents &= ~(CB_EVENT_READABLE);
    return ret;
}

static int sock_listen(int fd, int backlog)
{
    struct frosted_inet_socket *s;
    int ret;
    s = fd_inet(fd);
    if (!s) {
        ret = -EINVAL;
        goto out;
    }
    ret = wolfIP_sock_listen(IPStack, s->sock_fd, backlog);
    s->events |= CB_EVENT_READABLE;
out:
    
    return ret;
}

static int sock_shutdown(int fd, uint16_t how)
{
    /* Not implemented. */
    return 0;
}


static int sock_io_setflags(struct ifreq *ifr)
{
    unsigned int flags = ifr ? ifr->ifr_flags : 0;
    unsigned int if_idx = 0;
    ip4 ip, nm, gw;
    struct wolfIP_ll_dev *lnk = wolfip_ll_from_ifr(ifr, &if_idx);

    if (!lnk)
        return -ENODEV;

    if (if_idx == FROSTED_WOLFIP_LOOPBACK_IF_IDX)
        return 0;

    wolfIP_ipconfig_get_ex(IPStack, if_idx, &ip, &nm, &gw);
    if ((flags & IFF_UP) == 0) {
        wolfIP_ipconfig_set_ex(IPStack, if_idx, 0, 0, 0);
    } else if (ip == 0 && nm == 0 && gw == 0) {
        /* bring interface up without address keeps config untouched */
        wolfIP_ipconfig_set_ex(IPStack, if_idx, 0, 0, 0);
    }

    return 0;
}

static int sock_io_setaddr(struct ifreq *ifr)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *if_addr = ((struct sockaddr_in *) &ifr->ifr_addr);
    unsigned int if_idx = 0;
    struct wolfIP_ll_dev *lnk = wolfip_ll_from_ifr(ifr, &if_idx);

    if (!lnk)
        return -ENODEV;

    if (if_idx == FROSTED_WOLFIP_LOOPBACK_IF_IDX)
        return -EOPNOTSUPP;

    wolfIP_ipconfig_get_ex(IPStack, if_idx, &ip, &nm, &gw);
    ip = if_addr->sin_addr.s_addr;
    wolfIP_ipconfig_set_ex(IPStack, if_idx, ip, nm, gw);
    return 0;
}

static int sock_io_setnetmask(struct ifreq *ifr)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *if_addr = ((struct sockaddr_in *) &ifr->ifr_addr);
    unsigned int if_idx = 0;
    struct wolfIP_ll_dev *lnk = wolfip_ll_from_ifr(ifr, &if_idx);

    if (!lnk)
        return -ENODEV;

    if (if_idx == FROSTED_WOLFIP_LOOPBACK_IF_IDX)
        return -EOPNOTSUPP;

    wolfIP_ipconfig_get_ex(IPStack, if_idx, &ip, &nm, &gw);
    nm = if_addr->sin_addr.s_addr;
    wolfIP_ipconfig_set_ex(IPStack, if_idx, ip, nm, gw);
    return 0;
}

static int sock_io_getflags(struct ifreq *ifr)
{
    unsigned int if_idx = 0;
    struct wolfIP_ll_dev *lnk = wolfip_ll_from_ifr(ifr, &if_idx);

    if (!lnk)
        return -ENODEV;

    memset(ifr, 0, sizeof(struct ifreq));
    strncpy(ifr->ifr_name, lnk->ifname, IFNAMSIZ);
    ifr->ifr_flags = IFF_UP;
    if (if_idx == FROSTED_WOLFIP_LOOPBACK_IF_IDX) {
        ifr->ifr_flags |= IFF_LOOPBACK;
    } else {
        ifr->ifr_flags |= IFF_RUNNING | IFF_MULTICAST | IFF_BROADCAST;
    }
	return 0;
}

static int sock_io_getaddr(struct ifreq *ifr)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *if_addr = ((struct sockaddr_in *) &ifr->ifr_addr);
    unsigned int if_idx = 0;
    struct wolfIP_ll_dev *lnk = wolfip_ll_from_ifr(ifr, &if_idx);

    if (!lnk)
        return -ENODEV;

    wolfIP_ipconfig_get_ex(IPStack, if_idx, &ip, &nm, &gw);
    if_addr->sin_addr.s_addr = ee32(ip);
    strncpy(ifr->ifr_name, lnk->ifname, IFNAMSIZ);
    return 0;
}

static int sock_io_gethwaddr(struct ifreq *eth)
{
    unsigned int if_idx = 0;
    struct wolfIP_ll_dev *lnk = wolfip_ll_from_ifr(eth, &if_idx);
    uint8_t zero_mac[6] = {0};

    if (!lnk)
        return -ENODEV;

    if (if_idx == FROSTED_WOLFIP_LOOPBACK_IF_IDX)
        memcpy(&eth->ifr_dstaddr, zero_mac, sizeof(zero_mac));
    else
        memcpy(&eth->ifr_dstaddr, lnk->mac, sizeof(lnk->mac));
    strncpy(eth->ifr_name, lnk->ifname, IFNAMSIZ);
	return 0;
}

static int sock_io_getbcast(struct ifreq *ifr)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *if_addr = ((struct sockaddr_in *) &ifr->ifr_addr);
    unsigned int if_idx = 0;
    struct wolfIP_ll_dev *lnk = wolfip_ll_from_ifr(ifr, &if_idx);

    if (!lnk)
        return -ENODEV;

    wolfIP_ipconfig_get_ex(IPStack, if_idx, &ip, &nm, &gw);
    if (if_idx == FROSTED_WOLFIP_LOOPBACK_IF_IDX)
        if_addr->sin_addr.s_addr = ee32(ip);
    else
        if_addr->sin_addr.s_addr = ee32(ip | (~nm));
    strncpy(ifr->ifr_name, lnk->ifname, IFNAMSIZ);
    return 0;
}

static int sock_io_getnmask(struct ifreq *ifr)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *if_addr = ((struct sockaddr_in *) &ifr->ifr_addr);
    unsigned int if_idx = 0;
    struct wolfIP_ll_dev *lnk = wolfip_ll_from_ifr(ifr, &if_idx);

    if (!lnk)
        return -ENODEV;

    wolfIP_ipconfig_get_ex(IPStack, if_idx, &ip, &nm, &gw);
    if_addr->sin_addr.s_addr = ee32(nm);
    strncpy(ifr->ifr_name, lnk->ifname, IFNAMSIZ);
    return 0;
}

static int sock_io_addroute(struct rtentry *rte)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *gw_addr = ((struct sockaddr_in *) &rte->rt_gateway);
    unsigned int if_idx = FROSTED_WOLFIP_PRIMARY_IF_IDX;
    struct wolfIP_ll_dev *lnk = NULL;

    if (rte->rt_dev)
        lnk = wolfip_ll_lookup(rte->rt_dev, &if_idx);
    else
        lnk = wolfip_ll_lookup(NULL, &if_idx);

    if (!lnk)
        return -ENODEV;

    if (if_idx == FROSTED_WOLFIP_LOOPBACK_IF_IDX)
        return 0;

    wolfIP_ipconfig_get_ex(IPStack, if_idx, &ip, &nm, &gw);
    gw = gw_addr->sin_addr.s_addr;
    wolfIP_ipconfig_set_ex(IPStack, if_idx, ip, nm, gw);
    return 0;
}

static int sock_io_delroute(struct rtentry *rte)
{
  return 0;
}


static int sock_io_ethtool(struct ifreq *ifr)
{
	return 0;
}

static int sock_io_getifconf(struct ifconf *ifc)
{
    size_t total = 0;
    size_t copied = 0;
    size_t max_entries = 0;
    unsigned int idx;
    struct ifreq *req;
    size_t required_bytes;

    if (!ifc)
        return -EINVAL;

    for (idx = 0; idx < WOLFIP_MAX_INTERFACES; idx++) {
        struct wolfIP_ll_dev *ll = wolfIP_getdev_ex(IPStack, idx);
        if (!ll || ll->ifname[0] == '\0')
            continue;
        total++;
    }

    required_bytes = total * sizeof(struct ifreq);

    if (!ifc->ifc_req) {
        ifc->ifc_len = (int)required_bytes;
        return 0;
    }

    if ((size_t)ifc->ifc_len < required_bytes) {
        ifc->ifc_len = (int)required_bytes;
        return -EFAULT;
    }

    max_entries = total;
    req = ifc->ifc_req;
    memset(req, 0, required_bytes);

    for (idx = 0; idx < WOLFIP_MAX_INTERFACES && copied < max_entries; idx++) {
        struct wolfIP_ll_dev *ll = wolfIP_getdev_ex(IPStack, idx);
        struct ifreq *cur;
        struct sockaddr_in *addr;
        ip4 ip, nm, gw;

        if (!ll || ll->ifname[0] == '\0')
            continue;

        cur = &req[copied];
        strncpy(cur->ifr_name, ll->ifname, IFNAMSIZ);
        addr = (struct sockaddr_in *)&cur->ifr_addr;
        addr->sin_family = AF_INET;
        wolfIP_ipconfig_get_ex(IPStack, idx, &ip, &nm, &gw);
        addr->sin_addr.s_addr = ee32(ip);
        copied++;
    }

    ifc->ifc_len = (int)(copied * sizeof(struct ifreq));
    return 0;
}

static int sock_ioctl(struct fnode *fno, const uint32_t cmd, void *arg)
{

    struct ifreq *ifr;
    int ret = -ENOENT;

    if (!fno || !arg)
        return -EINVAL;

    /* Check for route commands */
    if (cmd == SIOCADDRT)
    {
        struct rtentry *rte = (struct rtentry *)arg;
        ret = sock_io_addroute(rte);
    }
    else if (cmd == SIOCDELRT)
    {
        struct rtentry *rte = (struct rtentry *)arg;
        ret =  sock_io_delroute(rte);
    } else {

        switch(cmd) {
            case SIOCGIFCONF:
                ret = sock_io_getifconf((struct ifconf *)arg);
                break;
            case SIOCSIFFLAGS:
                ifr = (struct ifreq *)arg;
                ret = sock_io_setflags(ifr);
                break;
            case SIOCSIFADDR:
                ifr = (struct ifreq *)arg;
                ret = sock_io_setaddr(ifr);
                break;
            case SIOCSIFNETMASK:
                ifr = (struct ifreq *)arg;
                ret = sock_io_setnetmask(ifr);
                break;
            case SIOCGIFFLAGS:
                ifr = (struct ifreq *)arg;
                ret = sock_io_getflags(ifr);
                break;
            case SIOCGIFADDR:
                ifr = (struct ifreq *)arg;
                ret = sock_io_getaddr(ifr);
                break;
            case SIOCGIFHWADDR:
                ifr = (struct ifreq *)arg;
                ret = sock_io_gethwaddr(ifr);
                break;
            case SIOCGIFBRDADDR:
                ifr = (struct ifreq *)arg;
                ret = sock_io_getbcast(ifr);
                break;
            case SIOCGIFNETMASK:
                ifr = (struct ifreq *)arg;
                ret = sock_io_getnmask(ifr);
                break;
            case SIOCETHTOOL:
                ifr = (struct ifreq *)arg;
                ret = sock_io_ethtool(ifr);
                break;
            default:
                ret = -ENOSYS;
        }
    }
    
    return ret;
}


/* /sys/net hooks */
#define MAX_DEVNET_BUF ((IFNAMSIZ + 4) * WOLFIP_MAX_INTERFACES + 32)
static int sysfs_net_dev_read(struct sysfs_fnode *sfs, void *buf, int len)
{
    char *res = (char *)buf;
    struct fnode *fno = sfs->fnode;
    static int off;
    static char *txt;
    const char iface_banner[] = "Interface | \r\n";
    uint32_t cur_off = task_fd_get_off(fno);
    sysfs_lock();
    if (cur_off == 0) {
        size_t i;
        size_t buf_sz = MAX_DEVNET_BUF;
        txt = kcalloc(buf_sz, 1);
        off = 0;
        if (!txt) {
            len = -1;
            goto out;
        }
        strcpy(txt, iface_banner);
        off += strlen(iface_banner);
        for (i = 0; i < WOLFIP_MAX_INTERFACES; i++) {
            struct wolfIP_ll_dev *lnk = wolfIP_getdev_ex(IPStack, (unsigned int)i);
            size_t name_len;
            if (!lnk || lnk->ifname[0] == '\0')
                continue;
            name_len = wolfip_ifname_len(lnk->ifname);
            if ((size_t)off + name_len + 3 >= buf_sz)
                break;
            memcpy(txt + off, lnk->ifname, name_len);
            off += (int)name_len;
            txt[off++] = '\r';
            txt[off++] = '\n';
        }
    }
    cur_off = task_fd_get_off(fno);
    if (off == cur_off) {
        kfree(txt);
        txt = NULL;
        len = -1;
        goto out;
    }
    if (len > (off - cur_off)) {
       len = off - cur_off;
    }
    memcpy(res, txt + cur_off, len);
    cur_off += len;
    task_fd_set_off(fno, cur_off);
out:
    sysfs_unlock();
    return len;
}

#define MAX_SYSFS_BUFFER 1024
int sysfs_net_route_list(struct sysfs_fnode *sfs, void *buf, int len)
{
    char *res = (char *)buf;
    struct fnode *fno = sfs->fnode;
    static char *mem_txt;
    struct wolfIP_ll_dev *lnk;
    ip4 ip, nm, gw;
    char dest[16];
    char mask[16];
    char gateway[16];
    static int off;
    int i;
    uint32_t cur_off = task_fd_get_off(fno);

    sysfs_lock();

    if (cur_off == 0) {
        const char route_banner[] = "Kernel IP routing table\r\nDestination     Gateway         Genmask         Flags   Metric  Iface \r\n";
        mem_txt = kalloc(MAX_SYSFS_BUFFER);
        if (!mem_txt) {
            sysfs_unlock();
            return -1;
        }
        off = 0;
        strcpy(mem_txt + off, route_banner);
        off += strlen(route_banner);
        for (i = 0; i < WOLFIP_MAX_INTERFACES && off < MAX_SYSFS_BUFFER; i++) {
            lnk = wolfIP_getdev_ex(IPStack, (unsigned int)i);
            if (!lnk || lnk->ifname[0] == '\0')
                continue;
            wolfIP_ipconfig_get_ex(IPStack, (unsigned int)i, &ip, &nm, &gw);
            if (ip == 0 && nm == 0 && gw == 0)
                continue;
            iptoa(ip & nm, dest);
            iptoa(nm, mask);
            strcpy(mem_txt + off, dest);
            off += strlen(dest);
            mem_txt[off++] = '\t';
            if (strlen(dest) < 8)
                mem_txt[off++] = '\t';

            mem_txt[off++] = '*';
            mem_txt[off++] = '\t';
            mem_txt[off++] = '\t';

            strcpy(mem_txt + off, mask);
            off += strlen(mask);
            mem_txt[off++] = '\t';

            strcpy(mem_txt + off, "U");
            off += strlen("U");
            mem_txt[off++] = '\t';
            mem_txt[off++] = '0';
            mem_txt[off++] = '\t';

            strcpy(mem_txt + off, lnk->ifname);
            off += strlen(lnk->ifname);
            mem_txt[off++] = '\r';
            mem_txt[off++] = '\n';

            if (gw != 0 && off < MAX_SYSFS_BUFFER) {
                iptoa(gw, gateway);
                strcpy(mem_txt + off, "0.0.0.0\t\t");
                off += strlen("0.0.0.0\t\t");

                strcpy(mem_txt + off, gateway);
                off += strlen(gateway);
                mem_txt[off++] = '\t';

                strcpy(mem_txt + off, "0.0.0.0\t\t");
                off += strlen("0.0.0.0\t\t");

                mem_txt[off++] = 'U';
                mem_txt[off++] = 'G';
                mem_txt[off++] = '\t';
                mem_txt[off++] = '0';
                mem_txt[off++] = '\t';

                strcpy(mem_txt + off, lnk->ifname);
                off += strlen(lnk->ifname);
                mem_txt[off++] = '\r';
                mem_txt[off++] = '\n';
            }
            mem_txt[off++] = '\r';
            mem_txt[off++] = '\n';
        }
    }
    if (len > (off - cur_off)) {
       len = off - cur_off;
    }
    memcpy(res, mem_txt + cur_off, len);
    cur_off += len;
    task_fd_set_off(fno,cur_off);
    if (cur_off == (uint32_t)off) {
        kfree(mem_txt);
        mem_txt = NULL;
        off = 0;
        len = -1;
    }
    sysfs_unlock();
    return len;
}

static int sock_getsockopt(int sd, int level, int optname, void *optval, unsigned int *optlen)
{
    return -EIO;
}

static int sock_setsockopt(int sd, int level, int optname, void *optval, unsigned int optlen)
{
    return -EIO;
}

static int sock_getsockname(int sd, struct sockaddr *addr, unsigned int *addrlen)
{
    struct frosted_inet_socket *s;
    s = fd_inet(sd);
    if ((!s) || (!addr))
        return -EINVAL;

    if (*addrlen < sizeof(struct sockaddr_in))
        return -ENOBUFS;

    return wolfIP_sock_getsockname(IPStack, s->sock_fd, (struct wolfIP_sockaddr *)addr, addrlen);
}

static int sock_getpeername(int sd, struct sockaddr *addr, unsigned int *addrlen)
{
    struct frosted_inet_socket *s;
    s = fd_inet(sd);
    if ((!s) || (!addr))
        return -EINVAL;

    if (*addrlen < sizeof(struct sockaddr_in))
        return -ENOBUFS;
    return wolfIP_sock_getpeername(IPStack, s->sock_fd, (struct wolfIP_sockaddr *)addr, addrlen);
}


#define IPTIMER_STACK_INTERVAL_MS 5
typedef void (*timer_cb)(unsigned int,  void *);

static void ipstack_timer_cb(unsigned int ms, void *arg)
{
    if (IPStack) {
        tcpip_lock();
        wolfIP_poll(IPStack, jiffies);
        tcpip_unlock();
    }
    ktimer_add(IPTIMER_STACK_INTERVAL_MS, (timer_cb)ipstack_timer_cb, NULL);
}

int netdev_register(struct netdev_driver *driver)
{
    size_t idx;

    if (!driver || !driver->attach)
        return -EINVAL;
    if (netdev_entry_count >= MAX_NETDEV_DRIVERS)
        return -ENOSPC;

    idx = netdev_entry_count++;
    netdev_entries[idx].driver = driver;
    netdev_entries[idx].attached = 0;
    netdev_entries[idx].if_idx = 0;

    if (IPStack) {
        int ret = netdev_attach_entry(idx);
        if (ret != 0)
            return ret;
    }

    return 0;
}


void socket_in_init(void)
{
    if (socket_in_ready)
        return;

    mod_socket_in.family = FAMILY_INET;
    tcpip_lock_init();

    if (!IPStack) {
        size_t stack_size = wolfIP_instance_size();
        struct wolfIP *stack_mem;

        if (stack_size == 0U)
            return;

        stack_mem = kalloc((uint32_t)stack_size);
        if (!stack_mem)
            return;

        IPStack = stack_mem;
        wolfIP_init(IPStack);
    }

    netdev_reset_next_if();
    strcpy(mod_socket_in.name,"tcp_ip");
    mod_socket_in.ops.poll = sock_poll;
    mod_socket_in.ops.close = sock_close;
    mod_socket_in.ops.socket     = sock_socket;
    mod_socket_in.ops.connect    = sock_connect;
    mod_socket_in.ops.accept     = sock_accept;
    mod_socket_in.ops.bind       = sock_bind;
    mod_socket_in.ops.listen     = sock_listen;
    mod_socket_in.ops.recvfrom   = sock_recvfrom;
    mod_socket_in.ops.sendto     = sock_sendto;
    mod_socket_in.ops.shutdown   = sock_shutdown;
    mod_socket_in.ops.ioctl      = sock_ioctl;
    mod_socket_in.ops.getsockopt   = sock_getsockopt;
    mod_socket_in.ops.setsockopt   = sock_setsockopt;
    mod_socket_in.ops.getsockname   = sock_getsockname;
    mod_socket_in.ops.getpeername   = sock_getpeername;

    register_module(&mod_socket_in);
    register_addr_family(&mod_socket_in, FAMILY_INET);

    netdev_attach_registered();

    /* Register /sys/net/dev */
    sysfs_register("dev", "/sys/net", sysfs_net_dev_read, sysfs_no_write);

    /* Register /sys/net/route */
    sysfs_register("route", "/sys/net", sysfs_net_route_list, sysfs_no_write);

    /* Start TCP/IP timer */
    ipstack_timer = ktimer_add(IPTIMER_STACK_INTERVAL_MS, ipstack_timer_cb, NULL);

    socket_in_ready = 1;
}

int secure_getrandom(void *buf, unsigned size);
uint32_t wolfIP_getrandom(void)
{
    uint32_t r;
    secure_getrandom(&r, sizeof(r));
    return r;
}
