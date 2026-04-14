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

#include "pool.h"
#include <string.h>

void pool_init(struct pool *p)
{
    uint32_t full_words = p->capacity / 32u;
    uint32_t remaining = p->capacity % 32u;
    uint32_t i;

    for (i = 0; i < full_words; i++)
        p->freemap[i] = 0xFFFFFFFFu;

    if (remaining > 0)
        p->freemap[full_words] = (1u << remaining) - 1u;

    p->used = 0;
}

void *pool_alloc(struct pool *p)
{
    uint32_t words = (p->capacity + 31u) / 32u;
    uint32_t i;
    uint32_t bit;
    uint32_t index;
    uint8_t *slot;

    for (i = 0; i < words; i++) {
        if (p->freemap[i] == 0)
            continue;
        bit = (uint32_t)__builtin_ctz(p->freemap[i]);
        index = i * 32u + bit;
        if (index >= p->capacity)
            return (void *)0;
        p->freemap[i] &= ~(1u << bit);
        p->used++;
        slot = (uint8_t *)p->base + (index * p->elem_size);
        memset(slot, 0, p->elem_size);
        return slot;
    }
    return (void *)0;
}

void pool_free(struct pool *p, void *ptr)
{
    uint32_t offset;
    uint32_t index;
    uint32_t word;
    uint32_t bit;

    if (!ptr)
        return;

    offset = (uint32_t)((uint8_t *)ptr - (uint8_t *)p->base);
    index = offset / p->elem_size;

    if (index >= p->capacity)
        return;
    if (offset != index * p->elem_size)
        return; /* misaligned pointer */

    word = index / 32u;
    bit = index % 32u;

    if (p->freemap[word] & (1u << bit))
        return; /* double free */

    p->freemap[word] |= (1u << bit);
    p->used--;
}
