/* Capabilities and limits */
#ifndef SECURE_TASK_CAPS_H
#define SECURE_TASK_CAPS_H

#include <stdint.h>

/* Capability flags */
#define CAP_MALLOC   (1 << 0)
#define CAP_PERIPH   (1 << 1)
#define CAP_MAPMEM   (1 << 2)

#define CAP_KERNEL (CAP_MALLOC | CAP_PERIPH | CAP_MAPMEM)
#define CAP_TASK   (CAP_MALLOC | CAP_PERIPH)

#define CONFIG_TASK_MAX_MEM (32 * 1024)

/* Task limits */
typedef struct {
    uint32_t mem_max;     /* Max allowed memory in bytes */
    uint32_t mem_used;    /* Currently used memory */
} task_limits_t;


/* Memory pool block structure */
typedef struct {
    uint8_t *base;
    uint32_t size;
} mempool_block_t;


#ifndef CONFIG_MEMPOOL_SEGMENTS_PER_TASK
#define CONFIG_MEMPOOL_SEGMENTS_PER_TASK 3
#endif /* CONFIG_MEMPOOL_SEGMENTS */

/* Task structure in secure domain */
typedef struct {
    uint16_t task_id;
    uint16_t mempool_count; /* Number of used memory pool blocks */
    uint32_t caps;
    task_limits_t limits;
    mempool_block_t main_segment; /* data/bss segment for the task (got base)*/
    mempool_block_t stack_segment; /* stack segment */
    mempool_block_t mempool[CONFIG_MEMPOOL_SEGMENTS_PER_TASK]; /* Memory pool blocks mmapped for task's heap */
} secure_task_t;

/* External task lookup hook */
secure_task_t *get_secure_task(uint16_t task_id);

#endif /* SECURE_TASK_CAPS_H */
