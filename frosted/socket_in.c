#include "frosted.h"
#include "socket_in.h"
#include <sys/ioctl.h>
#include <string.h>
#include "net.h"
#include "fcntl.h"
#include "config.h"
#include "net/if.h"
#include "net/route.h"
#include "locks.h"
#include "wolfip.h"


static struct module mod_socket_in;

volatile struct wolfIP *IPStack;
static mutex_t *ipstack_mutex;
int ipstack_timer = -1;

void ipstack_lock(void)
{
    if (ipstack_mutex)
        mutex_lock(ipstack_mutex);
}

void ipstack_unlock(void)
{
    if (ipstack_mutex)
        mutex_unlock(ipstack_mutex);
}

int ipstack_trylock(void)
{
    if (ipstack_mutex)
        return mutex_trylock(ipstack_mutex);
    return 0;
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

    if ((*revents) & (POLLHUP | POLLERR) != 0) {
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

    wolfIP_sock_close(IPStack, s->fd);
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
    uint16_t port;
    struct wolfIP_sockaddr_in paddr;
    socklen_t sockaddr_len = sizeof(struct wolfIP_sockaddr_in);
    s = fd_inet(fd);
    if (!s)
        return -EINVAL;
    while (s->bytes < len) {
        if ((addr) && ((*addrlen) > 0)) {
            ret = wolfIP_sock_recvfrom(IPStack, s->sock_fd, buf + s->bytes, len - s->bytes, flags, &paddr, &sockaddr_len);
        } else {
            ret = wolfIP_sock_read(IPStack, s->sock_fd, buf + s->bytes, len - s->bytes);
        }

        if (ret < 0) {
            goto out;
        }

        if (ret == 0) {
            s->revents &= (~CB_EVENT_READABLE);
            if (SOCK_BLOCKING(s))  {
                s->events = CB_EVENT_READABLE;
                s->task = this_task();
                task_suspend();
                ret = SYS_CALL_AGAIN;
                goto out;
            }
            break;
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
            ret = wolfIP_sock_sendto(IPStack, s->sock_fd, buf + s->bytes, len - s->bytes, flags, addr, addrlen);
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
    ret = wolfIP_sock_bind(IPStack, s->sock_fd, addr, addrlen);
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
    l->events = CB_EVENT_WRITABLE;

    sock_fd = wolfIP_sock_accept(IPStack, l->sock_fd, addr, addrlen);
    if ((sock_fd < 0) && (sock_fd != -11))
        return sock_fd;
    if (sock_fd == -11)
        return SYS_CALL_AGAIN;
    
    l->revents &= (~CB_EVENT_WRITABLE);
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

    s->events = CB_EVENT_WRITABLE;
    if ((s->revents & CB_EVENT_WRITABLE) == 0) {
        ret = wolfIP_sock_connect(IPStack, s->sock_fd, addr, addrlen);
        if (SOCK_BLOCKING(s)) {
            s->task = this_task();
            ret = SYS_CALL_AGAIN;
        } else {
            ret = -EAGAIN;
        }
    }
    /* CB_EVENT_WRITABLE received. Successfully connected. */
    ret = 0;
    s->events  &= ~(CB_EVENT_WRITABLE);
    s->revents &= ~(CB_EVENT_WRITABLE);
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
    s->events |= CB_EVENT_WRITABLE;
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
    unsigned int flags = ifr->ifr_flags;
    if ((flags & IFF_UP) == 0)
        wolfIP_ipconfig_set(IPStack, 0, 0, 0);
	return 0;
}

static int sock_io_setaddr(struct ifreq *ifr)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *if_addr = ((struct sockaddr_in *) &ifr->ifr_addr);
    wolfIP_ipconfig_get(IPStack, &ip, &nm, &gw);
    ip = if_addr->sin_addr.s_addr;
    wolfIP_ipconfig_set(IPStack, ip, nm, gw);
    return 0;
}

static int sock_io_setnetmask(struct ifreq *ifr)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *if_addr = ((struct sockaddr_in *) &ifr->ifr_addr);
    int ret;

    wolfIP_ipconfig_get(IPStack, &ip, &nm, &gw);
    nm = if_addr->sin_addr.s_addr;
    wolfIP_ipconfig_set(IPStack, ip, nm, gw);
    return 0;
}

static int sock_io_getflags(struct ifreq *ifr)
{
    struct ll *lnk;
    lnk = wolfIP_getdev(IPStack);
    if (!lnk)
        return -1;
    memset(ifr, 0, sizeof(struct ifreq));
    strncpy(ifr->ifr_name, lnk->ifname, IFNAMSIZ);
    ifr->ifr_flags = IFF_UP|IFF_RUNNING|IFF_MULTICAST|IFF_BROADCAST;
	return 0;
}

static int sock_io_getaddr(struct ifreq *ifr)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *if_addr = ((struct sockaddr_in *) &ifr->ifr_addr);
    struct ll *lnk;
    lnk = wolfIP_getdev(IPStack);
    if (!lnk)
        return -1;
    wolfIP_ipconfig_get(IPStack, &ip, &nm, &gw);
    if_addr->sin_addr.s_addr = ee32(ip);
    strncpy(ifr->ifr_name, lnk->ifname, IFNAMSIZ);
    return 0;
}

static int sock_io_gethwaddr(struct ifreq *eth)
{
    struct ll *lnk;
    lnk = wolfIP_getdev(IPStack);
    if (!lnk)
        return -1;
    memcpy(&eth->ifr_dstaddr, lnk->mac, 6);
    strncpy(eth->ifr_name, lnk->ifname, IFNAMSIZ);
	return 0;
}

static int sock_io_getbcast(struct ifreq *ifr)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *if_addr = ((struct sockaddr_in *) &ifr->ifr_addr);
    struct ll *lnk;
    lnk = wolfIP_getdev(IPStack);
    if (!lnk)
        return -1;
    wolfIP_ipconfig_get(IPStack, &ip, &nm, &gw);
    if_addr->sin_addr.s_addr = ee32(ip | (~nm));
    strncpy(ifr->ifr_name, lnk->ifname, IFNAMSIZ);
    return 0;
}

static int sock_io_getnmask(struct ifreq *ifr)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *if_addr = ((struct sockaddr_in *) &ifr->ifr_addr);
    struct ll *lnk;
    lnk = wolfIP_getdev(IPStack);
    if (!lnk)
        return -1;
    wolfIP_ipconfig_get(IPStack, &ip, &nm, &gw);
    if_addr->sin_addr.s_addr = ee32(nm);
    strncpy(ifr->ifr_name, lnk->ifname, IFNAMSIZ);
    return 0;
}

static int sock_io_addroute(struct rtentry *rte)
{
    ip4 ip, nm, gw;
    struct sockaddr_in *gw_addr = ((struct sockaddr_in *) &rte->rt_gateway);

    wolfIP_ipconfig_get(IPStack, &ip, &nm, &gw);
    gw = gw_addr->sin_addr.s_addr;
    wolfIP_ipconfig_set(IPStack, ip, nm, gw);
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

static int sock_ioctl(struct fnode *fno, const uint32_t cmd, void *arg)
{

    struct frosted_inet_socket *s;
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

        /* Check for interface-related ioctl */
        ifr = (struct ifreq *)arg;

        switch(cmd) {
            case SIOCSIFFLAGS:
                ret = sock_io_setflags(ifr);
                break;
            case SIOCSIFADDR:
                ret = sock_io_setaddr(ifr);
                break;
            case SIOCSIFNETMASK:
                ret = sock_io_setnetmask(ifr);
                break;
            case SIOCGIFFLAGS:
                ret = sock_io_getflags(ifr);
                break;
            case SIOCGIFADDR:
                ret = sock_io_getaddr(ifr);
                break;
            case SIOCGIFHWADDR:
                ret = sock_io_gethwaddr(ifr);
                break;
            case SIOCGIFBRDADDR:
                ret = sock_io_getbcast(ifr);
                break;
            case SIOCGIFNETMASK:
                ret = sock_io_getnmask(ifr);
                break;
            case SIOCETHTOOL:
                ret = sock_io_ethtool(ifr);
                break;
            default:
                ret = -ENOSYS;
        }
    }
    
    return ret;
}


/* /sys/net hooks */
#define MAX_DEVNET_BUF 64
static int sysfs_net_dev_read(struct sysfs_fnode *sfs, void *buf, int len)
{
    char *res = (char *)buf;
    struct fnode *fno = sfs->fnode;
    static int off;
    static char *txt;
    int i;
    struct ll *lnk;
    const char iface_banner[] = "Interface | \r\n";
    uint32_t cur_off = task_fd_get_off(fno);
    sysfs_lock();
    if (cur_off == 0) {
        lnk = wolfIP_getdev(IPStack);
        txt = kcalloc(MAX_DEVNET_BUF, 1);
        off = 0;
        if (!txt) {
            len = -1;
            goto out;
        }
        strcpy(txt, iface_banner);
        off += strlen(iface_banner);
        if (lnk) {
            strcat(txt, lnk->ifname);
            off += strlen(lnk->ifname);
            txt[off++] = '\r';
            txt[off++] = '\n';
        }
    }
    cur_off = task_fd_get_off(fno);
    if (off == cur_off) {
        kfree(txt);
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

#define MAX_SYSFS_BUFFER 512
int sysfs_net_route_list(struct sysfs_fnode *sfs, void *buf, int len)
{
    char *res = (char *)buf;
    struct fnode *fno = sfs->fnode;
    static char *mem_txt;
    struct ll *lnk;
    char dest[16];
    char mask[16];
    char gateway[16];
    char metric[5];
    static int off;
    int i;
    uint32_t cur_off = task_fd_get_off(fno);

    if (cur_off == 0) {
        const char route_banner[] = "Kernel IP routing table\r\nDestination     Gateway         Genmask         Flags   Metric  Iface \r\n";
        lnk = wolfIP_getdev(IPStack);
        sysfs_lock();
        mem_txt = kalloc(MAX_SYSFS_BUFFER);
        if (!mem_txt)
            return -1;
        off = 0;
        strcpy(mem_txt + off, route_banner);
        off += strlen(route_banner);
        if (lnk) {
            ip4 ip, nm, gw;
            wolfIP_ipconfig_get(IPStack, &ip, &nm, &gw);
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

            if (gw != 0) {
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
    int ret = -1;
    s = fd_inet(sd);
    if ((!s) || (!addr))
        return -EINVAL;

    if (*addrlen < sizeof(struct sockaddr_in))
        return -ENOBUFS;

    return wolfIP_sock_getsockname(IPStack, s->sock_fd, addr, addrlen);
}

static int sock_getpeername(int sd, struct sockaddr *addr, unsigned int *addrlen)
{
    struct frosted_inet_socket *s;
    int ret = -1;
    s = fd_inet(sd);
    if ((!s) || (!addr))
        return -EINVAL;

    if (*addrlen < sizeof(struct sockaddr_in))
        return -ENOBUFS;
    return wolfIP_sock_getpeername(IPStack, s->sock_fd, addr, addrlen);
}


static int sysfs_no_op(struct sysfs_fnode *sfs, void *buf, int len)
{
    return -1;
}


#define IPTIMER_STACK_INTERVAL_MS 5
static void ipstack_timer_cb(void *arg)
{
    if (IPStack)
        wolfIP_poll(IPStack, jiffies);
    ktimer_add(IPTIMER_STACK_INTERVAL_MS, ipstack_timer_cb, NULL);
}


void socket_in_init(void)
{
    mod_socket_in.family = FAMILY_INET;
    if (!ipstack_mutex) 
        ipstack_mutex = mutex_init();
    wolfIP_init_static(&IPStack);
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

    /* Register /sys/net/dev */
    sysfs_register("dev", "/sys/net", sysfs_net_dev_read, sysfs_no_write);

    /* Register /sys/net/route */
    sysfs_register("route", "/sys/net", sysfs_net_route_list, sysfs_no_write);

    /* Start TCP/IP timer */
    ipstack_timer = ktimer_add(IPTIMER_STACK_INTERVAL_MS, ipstack_timer_cb, NULL);
}
