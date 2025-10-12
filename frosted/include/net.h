#ifndef NET_H_INCLUDED
#define NET_H_INCLUDED
#include "wolfip.h"
#include "errno.h"

struct netdev_driver {
    const char *name;
    int (*is_present)(void);
    int (*attach)(struct wolfIP *stack, struct wolfIP_ll_dev *ll, unsigned int if_idx);
};

void socket_in_init(void);
extern struct wolfIP *IPStack; /* Defined in socket_in.c, set by single device modules */

#if CONFIG_TCPIP
int netdev_register(struct netdev_driver *driver);
#else
static inline int netdev_register(struct netdev_driver *driver)
{
    (void)driver;
    return -ENOSYS;
}
#endif
#endif
