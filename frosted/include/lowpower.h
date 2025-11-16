#ifndef LOWPOWER_H_INC
#define LOWPOWER_H_INC
#include <stdint.h>

#ifndef CONFIG_LOWPOWER
static inline int lowpower_init(void)
{
    return -1;
}

static inline int lowpower_sleep(int stdby, uint32_t interval)
{
    (void)stdby;
    (void)interval;
    return -1;
}
#else
    int lowpower_init(void);
    int lowpower_sleep(int stdby, uint32_t interval);
#endif
#endif
