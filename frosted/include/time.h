/* time.h - Minimal time structures for frosted */

#ifndef _FROSTED_TIME_H
#define _FROSTED_TIME_H

#include <stdint.h>

#ifndef __time_t_defined
typedef int64_t time_t;
#define __time_t_defined
#endif

typedef long suseconds_t;

typedef int clockid_t;

/* Clock IDs */
#define CLOCK_REALTIME           0  /* System-wide real-time clock */
#define CLOCK_MONOTONIC          1  /* Monotonic clock, cannot be set */
#define CLOCK_PROCESS_CPUTIME_ID 2  /* High-resolution per-process timer */
#define CLOCK_THREAD_CPUTIME_ID  3  /* Thread-specific CPU-time clock */
#define CLOCK_MONOTONIC_RAW      4  /* Monotonic clock not adjusted by NTP */
#define CLOCK_REALTIME_COARSE    5  /* Lower resolution but faster */
#define CLOCK_MONOTONIC_COARSE   6  /* Lower resolution but faster */
#define CLOCK_BOOTTIME           7  /* Includes time spent in suspend */
#define CLOCK_REALTIME_ALARM     8  /* Alarm clock (real-time) */
#define CLOCK_BOOTTIME_ALARM     9  /* Alarm clock (boot-time) */

typedef struct timeval {
    time_t      tv_sec;     /* seconds */
    suseconds_t tv_usec;    /* microseconds */
} timeval;

typedef struct timespec {
    time_t tv_sec;          /* seconds */
    long   tv_nsec;         /* nanoseconds */
} timespec;
#define _TIMEVAL_DEFINED

#endif /* _FROSTED_TIME_H */
