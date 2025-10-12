#ifndef NET_H_INCLUDED
#define NET_H_INCLUDED
#include "wolfip.h"

void socket_in_init(void);
extern struct wolfIP *IPStack; /* Defined in socket_in.c, set by single device modules */
#endif
