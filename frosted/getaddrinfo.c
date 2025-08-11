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
#include "socket_in.h"


#ifndef CONFIG_DNS_CLIENT
int sys_getaddrinfo_hdlr(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    return -ENOSYS;
}

int sys_freeaddrinfo_hdlr(uint32_t arg1)
{
    return -ENOSYS;
}
#else


int sys_getaddrinfo_hdlr(const char *node, const char *service, 
        const struct addrinfo *hints, struct addrinfo **res)
{
    if (!node || !res)
        return -EINVAL;
    if (task_ptr_valid(node) || task_ptr_valid(service) || task_ptr_valid(hints) || task_ptr_valid(res))
        return -EACCES;
    return dns_getaddrinfo(node, service, hints, res);
}

int sys_freeaddrinfo_hdlr(struct addrinfo *res)
{
    if (!res)
        return -EINVAL;
    if (task_ptr_valid(res))
        return -EACCES;
    return dns_freeaddrinfo(res);
}

#endif
