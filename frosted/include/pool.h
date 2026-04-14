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
 *      Authors: Daniele Lacamera
 *
 */

#ifndef POOL_H_INCLUDED
#define POOL_H_INCLUDED

#include <stdint.h>
#include <stddef.h>

struct pool {
    void *base;
    uint32_t elem_size;
    uint32_t capacity;
    uint32_t used;
    uint32_t *freemap; /* bitmap: bit=1 means slot is free */
};

/*
 * POOL_DEFINE(name, type, cap)
 *
 * Declare a static pool of `cap` elements of `type`.
 * The pool struct, backing storage, and freemap bitmap are all
 * placed in .bss (zero-initialized).  pool_init() must be called
 * before first use to set the freemap bits to 1 (all free).
 */
#define POOL_DEFINE(name, type, cap)                                       \
    static type _pool_##name##_storage[(cap)]                              \
        __attribute__((aligned(8)));                                        \
    static uint32_t _pool_##name##_freemap[((cap) + 31u) / 32u];          \
    static struct pool name = {                                            \
        .base = _pool_##name##_storage,                                    \
        .elem_size = sizeof(type),                                         \
        .capacity = (cap),                                                 \
        .used = 0,                                                         \
        .freemap = _pool_##name##_freemap                                  \
    }

/* Initialize the pool: mark all slots as free.
 * Must be called once before pool_alloc/pool_free. */
void pool_init(struct pool *p);

/* Allocate one element.  Returns NULL if pool is exhausted.
 * The returned memory is zeroed. */
void *pool_alloc(struct pool *p);

/* Free a previously allocated element back to the pool. */
void pool_free(struct pool *p, void *ptr);

/* Return the number of free slots remaining. */
static inline uint32_t pool_available(const struct pool *p)
{
    return p->capacity - p->used;
}

#endif /* POOL_H_INCLUDED */
