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
#include "task.h"

#define MEMPOOL_SIZE        (0x50000) /* 20030000 ~ 2006FFFF */
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

void mempool_init(void) {
    if (mempool_pool != NULL)
        return;
    mempool_pool = &__mempool_start__;
    memset(mempool_pool, 0, MEMPOOL_SIZE);
    memset(mempool_blocks, 0, sizeof(mempool_blocks));
    mempool_blocks[0].base = mempool_pool;
    mempool_blocks[0].size = MEMPOOL_SIZE;
}

/* Release a previously allocated area.
 */
void mempool_unmap(void *ptr, uint16_t task_id) {
    int i, j;
    secure_task_t *task = get_secure_task(task_id);
    mempool_block_t tmp = {0,0};
    if (!task)
        return; /* Invalid task ID */
    if ((task->mempool_count == 0) && (task->main_segment.base == NULL))
        return; /* No allocated segments */
    if (!ptr)
        return; /* Null pointer cannot be freed */


    /* Remove the memory from the task */
    if (task->main_segment.base == ptr)
    {
        tmp.base = ptr;
        tmp.size = task->main_segment.size;
        task->main_segment.base = NULL;
    } else {
        for (i = 0; i < CONFIG_MEMPOOL_SEGMENTS_PER_TASK; i++)
        {
            if (task->mempool[i].base == ptr)
            {
                tmp.base = ptr;
                tmp.size = task->mempool[i].size;
                task->limits.mem_used -= task->mempool[i].size;
                task->mempool_count--;
                for (j = i; j < task->mempool_count; j++)
                    task->mempool[j] = task->mempool[j + 1];
                break;
            }
        }
    }
    /* Put the memory back to the mempool */
    if ((tmp.base != NULL) && tmp.size != 0) {
        /* First, try to find a contiguous block to extend */
        for (i = 0; i < MAX_MEMPOOL_BLOCKS; i++) {
            if (mempool_blocks[i].base + mempool_blocks[i].size == tmp.base) {
                mempool_blocks[i].size += tmp.size;
                /* Check if we can merge other blocks after extending */
                for (j = i; j < MAX_MEMPOOL_BLOCKS; j++) {
                    if (mempool_blocks[j].base == mempool_blocks[i].base +
                            mempool_blocks[j].size) {
                        mempool_blocks[i].size += mempool_blocks[j].size;
                        mempool_blocks[j].base = NULL;
                        mempool_blocks[j].size = 0;
                    }
                }
                return;
            }
        }
        /* If extending existing blocks is not possible, create a new block in
         * the first available slot
         */
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
    int i, j;
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
            mempool_blocks[i].size = total_size;
            return (void*)(task->main_segment.base);
        }
    }
    return NULL; /* No memory available to create this task. */
}

/* Pool memory allocator for processes.
 * Called by the kernel upon mmap() syscall, when a process requests
 * a new memory area to manage the heap allocations.
 *
 */
void *mempool_mmap(size_t size, uint16_t task_id, uint32_t flags)
{

    int i, j;
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
                    if ((flags & MMAP_NEWPAGE) == 0) {
                        /* The new block is contiguous to the currently used one. 
                         * merge them.
                         */
                        uint32_t oldsize = task->mempool[j].size;
                        task->mempool[j].size += size;

                        /* The old block is moved and resized. */
                        mempool_blocks[i].base += size;
                        mempool_blocks[i].size -= size;
                        task->limits.mem_used += size;
                        return task->mempool[j].base + oldsize;
                    }
                }
            }
        }
    }

    /* Fail if no slots are available to allocate more non contiguous segments */
    if (task->mempool_count >= CONFIG_MEMPOOL_SEGMENTS_PER_TASK)
        return NULL; 

    /* Map a new non-contiguous segment to the task */
    for (i = 0; i < MAX_MEMPOOL_BLOCKS; i++) {
        if (mempool_blocks[i].size >= size) {
            mempool_block_t *mem = &task->mempool[task->mempool_count];
            mem->base = mempool_blocks[i].base;
            mem->size = size;
            task->mempool_count++;
            task->limits.mem_used += size;

            /* Split remaining space, if larger than needed */
            mempool_blocks[i].base += size;
            mempool_blocks[i].size -= size;
            return task->mempool[task->mempool_count - 1].base;
        }
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
            task->limits.mem_used += size;

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
int mempool_chown(void *ptr, uint16_t new_owner, uint16_t caller_id) {
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


    if ((src->mempool_count == 0) && (src->main_segment.base == NULL))
        return -1; /* No allocated segments */
    if (!ptr)
        return -1; /* Null pointer cannot chownd */

    /* Check if changing ownership of the process main segment */
    if (src->main_segment.base == ptr) {
        if (dst->main_segment.base != NULL)
            mempool_unmap(dst->main_segment.base, new_owner);

        src->main_segment.base = NULL;
        dst->main_segment.base = ptr;
        dst->main_segment.size = src->main_segment.size;
        src->main_segment.size = 0;
        return 0;
    }

    /* Check if changing ownership of the process stack segment */
    if (src->stack_segment.base == ptr) {
        if (dst->stack_segment.base != NULL)
            mempool_unmap(dst->stack_segment.base, new_owner);

        src->stack_segment.base = NULL;
        dst->stack_segment.base = ptr;
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
                goto chown_successful;
            }

            /*
             * Then try to merge to an existing segment.
             */
            if (dst->mempool_count > 0) {
                for (j = 0; j < dst->mempool_count -1; j++) {
                    if (dst->mempool[j].base + dst->mempool[j].size == tmp.base) {
                        dst->mempool[j].size += tmp.size;
                        goto chown_successful;
                    } else if (dst->mempool[j+1].base == tmp.base + tmp.size) {
                        dst->mempool[j+1].base -= tmp.size;
                        dst->mempool[j+1].size += tmp.size;
                        goto chown_successful;
                    }
                }
            }

            /* If the above is not possible, add as a new segment */
            if (dst->mempool_count == CONFIG_MEMPOOL_SEGMENTS_PER_TASK)
                return -1;
            dst->mempool[dst->mempool_count] = tmp;
            dst->mempool_count++;
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
int secure_mempool_chown(void *ptr, uint16_t new_owner, uint16_t caller_id)
{
    return mempool_chown(ptr, new_owner, caller_id);
}

__attribute__((cmse_nonsecure_entry))
int secure_mempool_owner(void *ptr, uint16_t task_id)
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
    int i;
    secure_task_t *task = get_secure_task(owner);
    if (!task)
        return;
    while (task->mempool_count > 0)
        mempool_unmap(task->mempool[0].base, owner);
    mempool_unmap(&task->main_segment, owner);
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

