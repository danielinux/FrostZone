#ifndef FLASH_OPS_H
#define FLASH_OPS_H

#include <stdint.h>
#include <stddef.h>

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 256
#endif

int stm32_flash_unlock(void);
void stm32_flash_lock(void);
int stm32_flash_program_range(uintptr_t dst, const uint8_t *src, size_t len);
int stm32_flash_erase_sector(uintptr_t sector_addr);
void stm32_flash_set_waitstates(unsigned int waitstates);

#endif /* FLASH_OPS_H */
