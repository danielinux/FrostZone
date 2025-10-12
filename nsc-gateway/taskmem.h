#ifndef TASKMEM_H
#define TASKMEM_H
#include <stdint.h>
#include <stddef.h>

/* Structure used to exchange information about a task's memory layout
 *
 * Shared between supervisor and kernel.
 *
 */
struct task_meminfo {
    uintptr_t xip_base;
    uint32_t xip_size;
    uintptr_t ram_base;
    uint32_t ram_size;
    uintptr_t stack_base;
    uint32_t stack_size;
    uint32_t n_heap_regions;
    struct {
        uintptr_t base;
        uint32_t size;
    } heap[3];
};

struct mempool_stats {
    uint32_t total;
    uint32_t kernel_reserved;
    uint32_t task_reserved;
    uint32_t free;
};

#define MMAP_NEWPAGE (1 << 1)


/* Declarations of NSC-callable secure memory APIs */
void *secure_mmap(size_t size, uint16_t task_id, uint32_t flags);
void  secure_munmap(void *addr, uint16_t task_id);
int   secure_mempool_chown(const void *ptr, uint16_t new_owner, uint16_t caller_id);
int   secure_mempool_owner(const void *ptr, uint16_t owner_id);
void *secure_mmap_stack(uint32_t size, uint16_t task_id);
void  secure_munmap_task(uint16_t task_id);
int   secure_swap_stack(uint16_t parent, uint16_t child);
int   secure_meminfo(uint16_t task_id, void *_info);
int   secure_mempool_stats(struct mempool_stats *stats);

#endif
