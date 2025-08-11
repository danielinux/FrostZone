#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/pthread.h>

/* 1:1 syscall mapping */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);
void pthread_exit(void *retval);
int pthread_detach(pthread_t thread);
int pthread_join(pthread_t thread, void **retval);
int pthread_cancel(pthread_t thread);
int pthread_setcancelstate(int state, int *oldstate);
pthread_t pthread_self(void);
int pthread_mutex_init(pthread_mutex_t *restrict mutex,
                       const pthread_mutexattr_t *restrict attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_key_create(pthread_key_t *key, void (*destructor)(void*));
int pthread_setspecific(pthread_key_t key, const void *value);

/* proxy to sys_pthread_getspecific */
void *pthread_getspecific(pthread_key_t key);

/* Pure libC functions */
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);
int pthread_equal(pthread_t t1, pthread_t t2);
int pthread_setcanceltype(int type, int *oldtype);
int pthread_yield(void);
int pthread_kill(pthread_t thread, int sig);


#endif /* _FROSTED_POLL_H */
