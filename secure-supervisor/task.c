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
/* secure_task_table.c - Static task table for secure task lookup */

#include "task.h"
#include "limits.h"
#include <string.h>


secure_task_t secure_tasks[MAX_SECURE_TASKS];

/* Initialize secure task table */
void secure_task_table_init(void) {
    memset(secure_tasks, 0, sizeof(secure_tasks));
    for (int i = 0; i < MAX_SECURE_TASKS; ++i) {
        secure_tasks[i].task_id = 0xFFFF; /* Invalid ID */
    }
    /* Entry '0' for frosted kernel */
    secure_tasks[0].task_id = 0;
    secure_tasks[0].caps = CAP_KERNEL;
    secure_tasks[0].limits.mem_max = UINT32_MAX;
    secure_tasks[0].limits.mem_used = 0;
}

/* Add or update a task entry */
int register_secure_task(uint16_t task_id, uint32_t caps, uint32_t mem_limit) {
    for (int i = 0; i < MAX_SECURE_TASKS; ++i) {
        if (secure_tasks[i].task_id == task_id || secure_tasks[i].task_id == 0xFFFF) {
            secure_tasks[i].task_id = task_id;
            secure_tasks[i].caps = caps;
            secure_tasks[i].limits.mem_max = mem_limit;
            secure_tasks[i].limits.mem_used = 0;
            return 0;
        }
    }
    return -1;
}

/* Remove a task entry */
int unregister_secure_task(uint16_t task_id) {
    for (int i = 0; i < MAX_SECURE_TASKS; ++i) {
        if (secure_tasks[i].task_id == task_id) {
            secure_tasks[i].task_id = 0xFFFF;
            secure_tasks[i].caps = 0;
            secure_tasks[i].limits.mem_max = 0;
            secure_tasks[i].limits.mem_used = 0;
            return 0;
        }
    }
    return -1;
}

/* Lookup a task by ID */
secure_task_t *get_secure_task(uint16_t task_id) {
    for (int i = 0; i < MAX_SECURE_TASKS; ++i) {
        if (secure_tasks[i].task_id == task_id) {
            return &secure_tasks[i];
        }
    }
    return NULL;
}

