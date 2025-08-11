#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <stdint.h>
#include <stddef.h>

#define MMAP_NEWPAGE (1 << 1)


/* Declarations of NSC-callable secure memory APIs */
void *secure_mmap(size_t size, uint16_t task_id, uint32_t flags);
int   secure_munmap(void *addr, uint16_t task_id);
int   secure_mempool_chown(const void *ptr, uint16_t new_owner, uint16_t caller_id);
int   secure_mempool_owner(const void *ptr, uint16_t owner_id);
void *secure_mmap_stack(uint32_t size, uint16_t task_id);
int   secure_munmap_task(uint16_t task_id);
int   secure_swap_stack(uint16_t parent, uint16_t child);


#endif
