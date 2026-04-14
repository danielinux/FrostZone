#include "frosted.h"
#ifndef LOCKS_H
#define LOCKS_H

#define SEM_MAX_LISTENERS 8

/* Structures */
struct semaphore {
    int value;
    uint32_t signature;
    int listeners;
    int last;
    struct task *listener[SEM_MAX_LISTENERS];
};


int suspend_on_sem_wait(sem_t *s);
int suspend_on_mutex_lock(mutex_t *s);

mutex_t *mutex_init(void);
int mutex_lock(mutex_t *s);
int mutex_unlock(mutex_t *s);
#endif
