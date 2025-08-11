/*
 *      This file is part of frostzone.
 *
 *      frostzone is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the Free Software Foundation.
 *
 *
 *      frostzone is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frostzone.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Authors: Daniele Lacamera
 *
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "frosted.h"
#include "mempool.h"

#define PAGE_SIZE 4096
#define ALIGNMENT 8
#define MAX_PAGES 32
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define BLOCK_FREE 0x80000000
#define BLOCK_SIZE_MASK 0x7FFFFFFF

struct __attribute__((__packed__)) page {
    void *base;
    uint16_t offset;
    uint16_t capacity;
};

static struct page pages[MAX_PAGES];
static uint32_t page_count = 0;

void *kalloc(uint32_t size);
void *kcalloc(uint32_t nmemb, uint32_t size);
void *krealloc(void *ptr, uint32_t size);
void kfree(void *ptr);

static void *alloc_from_pages(uint32_t size);
static void try_merge_blocks(uint8_t *base, uint16_t *offset, uint32_t capacity);

static int add_new_page(uint32_t page_size)
{
    void *mem;
    if (page_count >= MAX_PAGES)
        return -1;
    mem = secure_mmap(page_size, 0, 0);
    if (!mem)
        return -1;
    pages[page_count].base = mem;
    pages[page_count].offset = 0;
    pages[page_count].capacity = page_size;
    page_count++;
    return 0;
}

void *kalloc(uint32_t size)
{
    uint32_t total = ALIGN_UP(size + sizeof(uint32_t), ALIGNMENT);
    void *ptr = alloc_from_pages(total);
    if (ptr == 0) {
        uint32_t page_size = ALIGN_UP(total, PAGE_SIZE);
        if (add_new_page(page_size) != 0) {
            return 0;
        }
        ptr = alloc_from_pages(total);
    }
    if (ptr) {
        *((uint32_t *)ptr) = total;
        return (uint8_t *)ptr + sizeof(uint32_t);
    }
    return 0;
}

void *kcalloc(uint32_t nmemb, uint32_t size)
{
    void *mem = kalloc(size * nmemb);
    if (mem) {
        memset(mem, 0, size * nmemb);
    }
    return mem;
}

void *krealloc(void *ptr, uint32_t size)
{
    if (ptr == 0) return kalloc(size);
    if (size == 0) {
        kfree(ptr);
        return 0;
    }
    uint8_t *block = (uint8_t *)ptr - sizeof(uint32_t);
    uint32_t old_size = *((uint32_t *)block) & BLOCK_SIZE_MASK;
    void *newptr = kalloc(size);
    if (!newptr) return 0;
    memcpy(newptr, ptr, (size < old_size - sizeof(uint32_t)) ? size : (old_size - sizeof(uint32_t)));
    kfree(ptr);
    return newptr;
}

void kfree(void *ptr)
{
    if (!ptr) return;
    uint8_t *block = (uint8_t *)ptr - sizeof(uint32_t);
    uint32_t size = *((uint32_t *)block) & BLOCK_SIZE_MASK;
    *((uint32_t *)block) = size | BLOCK_FREE;
    for (uint32_t i = 0; i < page_count; i++) {
        uint8_t *base = (uint8_t *)pages[i].base;
        if (block >= base && block < base + pages[i].capacity) {
            try_merge_blocks(base, &pages[i].offset, pages[i].capacity);
            return;
        }
    }
}

static void *alloc_from_pages(uint32_t size)
{
    for (uint32_t i = 0; i < page_count; i++) {
        struct page *pg = &pages[i];
        uint8_t *ptr = (uint8_t *)pg->base;
        uint16_t off = 0;
        while (off + sizeof(uint32_t) <= pg->offset) {
            uint32_t *hdr = (uint32_t *)(ptr + off);
            uint32_t blk_size = *hdr & BLOCK_SIZE_MASK;
            if ((*hdr & BLOCK_FREE) && blk_size >= size) {
                *hdr = size;
                return (void *)hdr;
            }
            off += blk_size + sizeof(uint32_t);
        }
        if ((uint32_t)(pg->capacity - pg->offset) >= size) {
            void *p = ptr + pg->offset;
            *((uint32_t *)p) = size;
            pg->offset += size;
            return (uint8_t *)p;
        }
    }
    return 0;
}


static void try_merge_blocks(uint8_t *base, uint16_t *offset, uint32_t capacity)
{
    uint16_t off = 0, max_used = 0;
    while (off + sizeof(uint32_t) <= *offset) {
        uint32_t *hdr = (uint32_t *)(base + off);
        uint32_t size = *hdr & BLOCK_SIZE_MASK;
        if (!(*hdr & BLOCK_FREE)) {
            max_used = off + size;
        } else {
            uint32_t *next = (uint32_t *)(base + off + size);
            if ((uint8_t *)next < base + *offset && (*next & BLOCK_FREE)) {
                uint32_t next_size = *next & BLOCK_SIZE_MASK;
                *hdr = (size + next_size) | BLOCK_FREE;
                continue;
            }
        }
        off += size;
    }
    *offset = max_used;
}

void *sys_mmap_hdlr(uint32_t len, uint16_t pid, uint32_t flags)
{
    pid = this_task_getpid();
    flags = 0;
    return secure_mmap(PAGE_SIZE, pid, flags);
}

int sys_munmap_hdlr(void *addr, uint16_t pid)
{
    return secure_munmap(addr, pid);
}


