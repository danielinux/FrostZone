/*
 *      This file is part of frostzone.
 *
 *      frostzone is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 3, as
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


#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "limits.h"
#include "stm32h563.h"
#include "task.h"
#include "taskmem.h"

#ifndef CONFIG_MEMPOOL_SIZE
#define CONFIG_MEMPOOL_SIZE (0x68000)   /* 416 KB — see stm32h563.ld */
#endif
#define MEMPOOL_SIZE        CONFIG_MEMPOOL_SIZE
#define MAX_MEMPOOL_BLOCKS  512         /* Max blocks in tracking table */
#define ALIGNMENT           8
#define ALIGN_UP(x)         (((x) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#ifndef CONFIG_TASK_STACK_SIZE
#define CONFIG_TASK_STACK_SIZE 8192
#endif

#define MMAP_NEWPAGE (1 << 1)

extern uint8_t __mempool_start__;



static __attribute__((section(".mempool"))) uint8_t pool[8];
static uint8_t *mempool_pool = NULL;

static mempool_block_t mempool_blocks[MAX_MEMPOOL_BLOCKS];

static void memzero(void *ptr, size_t len) {
    volatile uint8_t *p = ptr;
    while(len--) {
        *p++ = 0;
    }
}

static void *ns_ram_rw_range_check(const void *ptr, size_t len)
{
    uintptr_t start;
    uintptr_t end;

    if ((ptr == NULL) || (len == 0u))
        return NULL;

    start = (uintptr_t)ptr;
    if ((start < SAU_RAM_NS_START) || (start > SAU_RAM_NS_END))
        return NULL;
    if (start > (UINTPTR_MAX - (len - 1u)))
        return NULL;
    end = start + len - 1u;
    if ((end < SAU_RAM_NS_START) || (end > SAU_RAM_NS_END))
        return NULL;

    return (void *)ptr;
}

void mempool_init(void) {
    if (mempool_pool != NULL)
        return;
    mempool_pool = &__mempool_start__;
    memzero(mempool_pool, MEMPOOL_SIZE);
    memzero(mempool_blocks, sizeof(mempool_blocks));
    mempool_blocks[0].base = mempool_pool;
    mempool_blocks[0].size = MEMPOOL_SIZE;
}

static inline void mempool_limits_add(secure_task_t *task, uint32_t size)
{
    if (!task || size == 0)
        return;
    if (UINT32_MAX - task->limits.mem_used < size)
        task->limits.mem_used = UINT32_MAX;
    else
        task->limits.mem_used += size;
}

static inline void mempool_limits_sub(secure_task_t *task, uint32_t size)
{
    if (!task || size == 0)
        return;
    if (task->limits.mem_used >= size)
        task->limits.mem_used -= size;
    else
        task->limits.mem_used = 0;
}

static int mempool_consume_block(int index, uint32_t size, uint8_t **base_out)
{
    mempool_block_t *block;

    if ((index < 0) || (index >= MAX_MEMPOOL_BLOCKS))
        return -1;

    block = &mempool_blocks[index];
    if ((block->base == NULL) || (block->size < size))
        return -1;

    if (base_out)
        *base_out = block->base;

    if (block->size == size) {
        block->base = NULL;
        block->size = 0;
    } else {
        block->base += size;
        block->size -= size;
    }

    return 0;
}

/* Release a previously allocated area.
 */
void mempool_unmap(void *ptr, uint16_t task_id) {
    int i, j;
    secure_task_t *task = get_secure_task(task_id);
    mempool_block_t tmp = {0,0};
    if (!task)
        return; /* Invalid task ID */
    if ((task->mempool_count == 0) &&
        (task->main_segment.base == NULL) &&
        (task->stack_segment.base == NULL))
        return; /* No allocated segments */
    if (!ptr)
        return; /* Null pointer cannot be freed */


    /* Remove the memory from the task */
    if (task->main_segment.base == ptr)
    {
        tmp.base = ptr;
        tmp.size = task->main_segment.size;
        task->main_segment.base = NULL;
        task->main_segment.size = 0;
        mempool_limits_sub(task, tmp.size);
    }
    else if (task->stack_segment.base == ptr)
    {
        tmp.base = ptr;
        tmp.size = task->stack_segment.size;
        task->stack_segment.base = NULL;
        task->stack_segment.size = 0;
        mempool_limits_sub(task, tmp.size);
    } else {
        for (i = 0; i < CONFIG_MEMPOOL_SEGMENTS_PER_TASK; i++)
        {
            if (task->mempool[i].base == ptr)
            {
                tmp.base = ptr;
                tmp.size = task->mempool[i].size;
                mempool_limits_sub(task, task->mempool[i].size);
                task->mempool_count--;
                for (j = i; j < task->mempool_count; j++)
                    task->mempool[j] = task->mempool[j + 1];
                break;
            }
        }
    }
    /* Put the memory back to the mempool */
    if ((tmp.base != NULL) && tmp.size != 0) {
        int merged = -1;

        /* Forward merge: freed block comes right after an existing free block */
        for (i = 0; i < MAX_MEMPOOL_BLOCKS; i++) {
            if (mempool_blocks[i].base &&
                mempool_blocks[i].base + mempool_blocks[i].size == tmp.base) {
                mempool_blocks[i].size += tmp.size;
                merged = i;
                break;
            }
        }

        /* Backward merge: freed block sits right before an existing free block */
        for (i = 0; i < MAX_MEMPOOL_BLOCKS; i++) {
            if (mempool_blocks[i].base &&
                tmp.base + tmp.size == mempool_blocks[i].base) {
                if (merged >= 0) {
                    /* Both sides adjacent: absorb block[i] into merged block */
                    mempool_blocks[merged].size += mempool_blocks[i].size;
                    mempool_blocks[i].base = NULL;
                    mempool_blocks[i].size = 0;
                } else {
                    /* Only backward: extend block[i] downward */
                    mempool_blocks[i].base = tmp.base;
                    mempool_blocks[i].size += tmp.size;
                    merged = i;
                }
                break;
            }
        }

        if (merged >= 0)
            return;

        /* No adjacent free block found — create a new entry */
        for (i = 0; i < MAX_MEMPOOL_BLOCKS; i++) {
            if (mempool_blocks[i].base == NULL) {
                mempool_blocks[i].base = tmp.base;
                mempool_blocks[i].size = tmp.size;
                return;
            }
        }
    }
}


/* Allocate a secure area to contain the static memory for this process
 * (got base)
 */
static void *mempool_task_alloc(size_t task_size, uint16_t task_id)
{
    int i;
    size_t total_size = task_size + CONFIG_TASK_STACK_SIZE;
    secure_task_t *task = get_secure_task(task_id);
    if(!task) {
        return NULL;
    }

    if (task->main_segment.base != NULL) {
        mempool_unmap(task->main_segment.base, task_id);
    }

    /* Find first block that fits */
    for (i = 0; i < MAX_MEMPOOL_BLOCKS; i++) {
        if (mempool_blocks[i].size >= total_size) {
            task->main_segment.base = mempool_blocks[i].base;
            task->main_segment.size = total_size;
            /* Reduce size of this available block */
            mempool_blocks[i].base += total_size;
            mempool_blocks[i].size -= total_size;
            return (void*)(task->main_segment.base);
        }
    }
    return NULL; /* No memory available to create this task. */
}


#define MMAP_ALIGN 64

/* Pool memory allocator for processes.
 * Called by the kernel upon mmap() syscall, when a process requests
 * a new memory area to manage the heap allocations.
 *
 */
void *mempool_mmap(size_t size, uint16_t task_id, uint32_t flags)
{
    int i, j;
    int best_adjacent_block = -1;
    int best_adjacent_segment = -1;
    uint32_t best_adjacent_size = UINT32_MAX;
    int best_block = -1;
    uint32_t best_block_size = UINT32_MAX;
    uint32_t size_alignment = size % MMAP_ALIGN;

    if (size == 0)
        return NULL;
    if (size_alignment) {
        size += MMAP_ALIGN - size_alignment;
    }

    secure_task_t *task = get_secure_task(task_id);
    if (!task || !(task->caps & CAP_MALLOC)) {
        return NULL; /* no capability */
    }
    if (task->limits.mem_used + size > task->limits.mem_max) {
        return NULL; /* exceeds limit */
    }
    /* Among available blocks, first look for blocks adjacent to the current
     * ones in the same task.
     */
    if (task->mempool_count > 0) {
        for (i = 0; i < MAX_MEMPOOL_BLOCKS; i++) {
            for (j = 0; j < task->mempool_count; j++) {
                if ((task->mempool[j].base +
                     task->mempool[j].size == mempool_blocks[i].base) &&
                     (mempool_blocks[i].size >= size))
                {
                    if (((flags & MMAP_NEWPAGE) == 0) &&
                        (mempool_blocks[i].size < best_adjacent_size)) {
                        best_adjacent_block = i;
                        best_adjacent_segment = j;
                        best_adjacent_size = mempool_blocks[i].size;
                    }
                }
            }
        }

        if (best_adjacent_block >= 0) {
            uint32_t oldsize = task->mempool[best_adjacent_segment].size;

            if (mempool_consume_block(best_adjacent_block, size, NULL) != 0)
                return NULL;

            task->mempool[best_adjacent_segment].size += size;
            mempool_limits_add(task, size);
            return task->mempool[best_adjacent_segment].base + oldsize;
        }
    }

    /* Fail if no slots are available to allocate more non contiguous segments */
    if (task->mempool_count >= CONFIG_MEMPOOL_SEGMENTS_PER_TASK)
        return NULL; 

    /* Map a new non-contiguous segment to the task */
    for (i = 0; i < MAX_MEMPOOL_BLOCKS; i++) {
        if ((mempool_blocks[i].base != NULL) &&
            (mempool_blocks[i].size >= size) &&
            (mempool_blocks[i].size < best_block_size)) {
            best_block = i;
            best_block_size = mempool_blocks[i].size;
        }
    }

    if (best_block >= 0) {
        mempool_block_t *mem = &task->mempool[task->mempool_count];

        if (mempool_consume_block(best_block, size, &mem->base) != 0)
            return NULL;

        mem->size = size;
        task->mempool_count++;
        mempool_limits_add(task, size);
        return mem->base;
    }

    /* No free blocks available */
    return NULL;
}

void *mempool_alloc_stack(uint32_t size, uint16_t task_id) {
    secure_task_t *task = get_secure_task(task_id);
    if (!task) {
        if (register_secure_task(task_id, CAP_TASK, CONFIG_TASK_MAX_MEM) < 0)
            return NULL;
        task = get_secure_task(task_id);
    }
    if (!task)
        return NULL;
    
    /* Map a new stack segment to the task */
    for (int i = 0; i < MAX_MEMPOOL_BLOCKS; i++) {
        if (mempool_blocks[i].size >= size) {
            mempool_block_t *mem = &task->stack_segment;
            if (mem->base) {
                mempool_unmap(mem->base, task_id);
            }
            mem->base = mempool_blocks[i].base;
            mem->size = size;
            mempool_limits_add(task, size);

            /* Split remaining space,
             * if larger than needed */
            mempool_blocks[i].base += size;
            mempool_blocks[i].size -= size;
            return task->stack_segment.base;
        }
    }
    /* No free blocks available */
    return NULL;
}


/* Change ownership of a memory segment to another task */
int mempool_chown(const void *ptr, uint16_t new_owner, uint16_t caller_id) {
    int i, j;
    secure_task_t *src = get_secure_task(caller_id);
    secure_task_t *dst = get_secure_task(new_owner);
    mempool_block_t tmp = {0,0};
    if (!src)
        return -1; /* Invalid origin task ID */

    if (!dst) {
        if (register_secure_task(new_owner, CAP_TASK, CONFIG_TASK_MAX_MEM) < 0)
            return -1;
        dst = get_secure_task(new_owner);
    }


    if ((src->mempool_count == 0) &&
        (src->main_segment.base == NULL) &&
        (src->stack_segment.base == NULL))
        return -1; /* No allocated segments */
    if (!ptr)
        return -1; /* Null pointer cannot chownd */

    /* Check if changing ownership of the process main segment */
    if (src->main_segment.base == ptr) {
        if (dst->main_segment.base != NULL)
            mempool_unmap(dst->main_segment.base, new_owner);

        mempool_limits_sub(src, src->main_segment.size);
        mempool_limits_add(dst, src->main_segment.size);
        src->main_segment.base = NULL;
        dst->main_segment.base = (uint8_t *)ptr;
        dst->main_segment.size = src->main_segment.size;
        src->main_segment.size = 0;
        return 0;
    }

    /* Check if changing ownership of the process stack segment */
    if (src->stack_segment.base == ptr) {
        if (dst->stack_segment.base != NULL)
            mempool_unmap(dst->stack_segment.base, new_owner);

        mempool_limits_sub(src, src->stack_segment.size);
        mempool_limits_add(dst, src->stack_segment.size);
        src->stack_segment.base = NULL;
        dst->stack_segment.base = (uint8_t *)ptr;
        dst->stack_segment.size = src->stack_segment.size;
        src->stack_segment.size = 0;
        return 0;
    }

    /* Check generic heap segments */
    for (i = 0; i < src->mempool_count; i++) {
        if (src->mempool[i].base == ptr) {
            tmp.base = src->mempool[i].base;
            tmp.size = src->mempool[i].size;
            src->mempool[i].base = NULL;
            src->mempool[i].size = 0;

            /* Add the segment to destination task.
             * First, check if new task, and assign to main segment
             */
            if (dst->main_segment.base == NULL) {
                dst->main_segment.base = tmp.base;
                dst->main_segment.size = tmp.size;
                mempool_limits_sub(src, tmp.size);
                mempool_limits_add(dst, tmp.size);
                goto chown_successful;
            }

            /*
             * Then try to merge to an existing segment.
             */
            if (dst->mempool_count > 0) {
                for (j = 0; j < dst->mempool_count; j++) {
                    if (dst->mempool[j].base + dst->mempool[j].size == tmp.base) {
                        dst->mempool[j].size += tmp.size;
                        mempool_limits_sub(src, tmp.size);
                        mempool_limits_add(dst, tmp.size);
                        goto chown_successful;
                    }
                    if (tmp.base + tmp.size == dst->mempool[j].base) {
                        dst->mempool[j].base -= tmp.size;
                        dst->mempool[j].size += tmp.size;
                        mempool_limits_sub(src, tmp.size);
                        mempool_limits_add(dst, tmp.size);
                        goto chown_successful;
                    }
                }
            }

            /* If the above is not possible, add as a new segment */
            if (dst->mempool_count == CONFIG_MEMPOOL_SEGMENTS_PER_TASK)
                return -1;
            dst->mempool[dst->mempool_count] = tmp;
            dst->mempool_count++;
            mempool_limits_sub(src, tmp.size);
            mempool_limits_add(dst, tmp.size);
            goto chown_successful;
        }
    }
    /* Segment not found in source task. */
    return -1;

chown_successful:
        /* Remove the segment from source */
        src->mempool_count--;
        for (j = i; j < src->mempool_count; j++) {
            src->mempool[j] = src->mempool[j+1];
        }
        return 0;
}

/* CMSE wrappers */

__attribute__((cmse_nonsecure_entry))
void *secure_mmap(size_t size, uint16_t task_id, uint32_t flags)
{
    (void)flags;
    return mempool_mmap(size, task_id, flags);
}

__attribute__((cmse_nonsecure_entry))
void secure_munmap(void *ptr, uint16_t task_id)
{
    mempool_unmap(ptr, task_id);
}

__attribute__((cmse_nonsecure_entry))
int secure_mempool_chown(const void *ptr, uint16_t new_owner, uint16_t caller_id)
{
    return mempool_chown(ptr, new_owner, caller_id);
}

__attribute__((cmse_nonsecure_entry))
int secure_mempool_owner(const void *ptr, uint16_t task_id)
{
    secure_task_t *t = NULL;
    int i;
    if (task_id == 0) {
        return 0;
    }
    if (!ptr)
        return 0;

    t = get_secure_task(task_id);
    if (!t)
        return 0;
    if (t->main_segment.base == ptr)
        return 1;
    for (i = 0; i < t->mempool_count; i++) {
        if (t->mempool[i].base == ptr) {
            return 1;
        }
    }
    return 0;   // not found
}

__attribute__((cmse_nonsecure_entry))
void secure_munmap_task(uint16_t owner)
{
    secure_task_t *task = get_secure_task(owner);
    if (!task)
        return;
    while (task->mempool_count > 0)
        mempool_unmap(task->mempool[0].base, owner);
    if (task->stack_segment.base)
        mempool_unmap(task->stack_segment.base, owner);
    if (task->main_segment.base)
        mempool_unmap(task->main_segment.base, owner);
}

__attribute__((cmse_nonsecure_entry))
void *secure_mmap_stack(uint32_t size, uint16_t task_id)
{
    return mempool_alloc_stack(size, task_id);
}

__attribute__((cmse_nonsecure_entry))
int secure_swap_stack(uint16_t parent, uint16_t child)
{
    void *tmp;
    uint32_t sz;
    secure_task_t *p = get_secure_task(parent);
    secure_task_t *c = get_secure_task(child);
    if (!p || !c)
        return -1;
    tmp = p->stack_segment.base;
    sz = p->stack_segment.size;
    p->stack_segment.base = c->stack_segment.base;
    p->stack_segment.size = c->stack_segment.size;
    c->stack_segment.base = tmp;
    c->stack_segment.size = sz;
    return 0;
} 

__attribute__((cmse_nonsecure_entry))
int secure_meminfo(uint16_t task_id, void *_info)
{
    unsigned int i;
    struct task_meminfo *info;
    if (!_info)
        return -1;

    info = ns_ram_rw_range_check(_info, sizeof(*info));
    if (!info)
        return -1;

    secure_task_t *task = get_secure_task(task_id);
    if (!task)
        return -1;
    info->stack_base = (uintptr_t)task->stack_segment.base;
    info->stack_size = task->stack_segment.size;
    info->ram_base = (uintptr_t)task->main_segment.base;
    info->ram_size = task->main_segment.size;
    info->n_heap_regions = task->mempool_count;

    for (i = 0; i < task->mempool_count; i++) {
        info->heap[i].base = (uintptr_t)task->mempool[i].base;
        info->heap[i].size = task->mempool[i].size;
    }
    return 0;
}

__attribute__((cmse_nonsecure_entry))
int secure_mempool_stats(struct mempool_stats *stats)
{
    uint32_t kernel_used = 0;
    uint32_t task_used = 0;
    uint32_t total = MEMPOOL_SIZE;
    if (!stats)
        return -1;

    for (int i = 0; i < MAX_SECURE_TASKS; i++) {
        secure_task_t *task = &secure_tasks[i];
        if (task->task_id == 0xFFFF)
            continue;
        if (task->task_id == 0) {
            kernel_used = task->limits.mem_used;
        } else {
            task_used += task->limits.mem_used;
        }
    }

    stats->total = total;
    stats->kernel_reserved = kernel_used;
    stats->task_reserved = task_used;
    stats->free = (kernel_used + task_used >= total) ?
                  0 : (total - (kernel_used + task_used));

    /* Walk free list: count chunks and find largest contiguous block */
    stats->largest_free = 0;
    stats->n_free_chunks = 0;
    for (int i = 0; i < MAX_MEMPOOL_BLOCKS; i++) {
        if (mempool_blocks[i].base && mempool_blocks[i].size > 0) {
            stats->n_free_chunks++;
            if (mempool_blocks[i].size > stats->largest_free)
                stats->largest_free = mempool_blocks[i].size;
        }
    }
    return 0;
}
