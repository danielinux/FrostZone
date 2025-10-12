#ifndef _STDDEF_H
#define _STDDEF_H

/* Define size_t as unsigned int (for 32-bit platforms) */
typedef unsigned int size_t;

/* Define ssize_t as signed int (non-standard but commonly used) */
typedef int ssize_t;

#define NULL ((void *)0)

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#endif /* _STDDEF_H */
