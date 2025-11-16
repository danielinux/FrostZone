#ifndef SECURE_TASK_H
#define SECURE_TASK_H

#include <stdint.h>
#include "limits.h"
#define MAX_SECURE_TASKS 256



void secure_task_table_init(void);

int register_secure_task(uint16_t task_id, uint32_t caps, uint32_t mem_limit);

int unregister_secure_task(uint16_t task_id);

secure_task_t *get_secure_task(uint16_t task_id);

secure_task_t *get_secure_task_by_addr(uint32_t addr);

extern secure_task_t secure_tasks[MAX_SECURE_TASKS];

#endif
