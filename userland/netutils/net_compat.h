#ifndef NET_COMPAT_H
#define NET_COMPAT_H

#include <arpa/inet.h>

/* frosted libc provides inet_aton — no shim needed */
#define HAVE_INET_ATON 1

#endif /* NET_COMPAT_H */
