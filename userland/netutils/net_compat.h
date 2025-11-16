#ifndef NET_COMPAT_H
#define NET_COMPAT_H

#include <arpa/inet.h>

#ifndef HAVE_INET_ATON
static inline int frosted_inet_aton(const char *cp, struct in_addr *inp)
{
    return inet_pton(AF_INET, cp, &inp->s_addr) == 1;
}
#define inet_aton(_cp, _inp) frosted_inet_aton((_cp), (_inp))
#endif

#endif /* NET_COMPAT_H */
