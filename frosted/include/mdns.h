/*
 * In-kernel multicast DNS responder and resolver.
 */
#ifndef MDNS_H
#define MDNS_H

#include <stdint.h>
#include "config.h"

#if CONFIG_MDNS
void mdns_init(void);
int  mdns_lookup(const char *name, uint32_t *addr);
int  mdns_is_local_name(const char *name);
#else
static inline void mdns_init(void) { }
static inline int  mdns_lookup(const char *name, uint32_t *addr) { (void)name; (void)addr; return -1; }
static inline int  mdns_is_local_name(const char *name) { (void)name; return 0; }
#endif

#endif
