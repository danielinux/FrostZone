#ifndef SYS_REBOOT_H
#define SYS_REBOOT_H
#include <stdint.h>

int reboot(void);
int standby(uint32_t interval);
int suspend(uint32_t interval);

#endif
