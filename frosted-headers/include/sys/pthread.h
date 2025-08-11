#ifndef _FROSTED_PTHREAD_H
#define _FROSTED_PTHREAD_H
#include <stdint.h>
#include <sys/types.h>
typedef uint32_t pthread_t;
typedef uint32_t pthread_attr_t;
typedef void *pthread_mutex_t;
typedef uint32_t pthread_mutexattr_t;
typedef uint32_t pthread_key_t;

#define PTHREAD_CREATE_JOINABLE (0)
#define PTHREAD_CREATE_DETACHED (1)
#define PTHREAD_CANCELED (2)
#define PTHREAD_CANCEL_ENABLE (3)
#define PTHREAD_CANCEL_DISABLE (4)
#define PTHREAD_CANCEL_DEFERRED (5)
#define PTHREAD_CANCEL_ASYNCHRONOUS (6)

#define PTHREAD_MUTEX_INITIALIZER NULL

#endif
