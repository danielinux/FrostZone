#ifndef SEM_INC
#define SEM_INC

typedef struct __frosted_libc_semaphore { int __reserved[5] } sem_t;
typedef sem_t mutex_t;

#include <sys/time.h>

mutex_t *mutex_init(void);
int mutex_unlock(mutex_t *s);
int mutex_lock(mutex_t *s);
int mutex_destroy(mutex_t *s);

int sem_init(sem_t *s, int pshared, int val);
int sem_post(sem_t *s);
int sem_wait(sem_t *s);
int sem_timedwait(sem_t *s, struct timespec *t);
int sem_trywait(sem_t *s);
int sem_destroy(sem_t *s);

#endif

