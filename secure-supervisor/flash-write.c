#include <stdint.h>

#include "armv8m_tz.h"
#include "task.h"
#include "limits.h"
#include "mempool.h"

#include "pico/stdlib.h"
#include "hardware/flash.h"

#define RAM_BASE 0x20000000
#define RAM_MAX  0x20080000 

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 256
#endif 

#define FLASH_PART_OFF (0x1F0000U)
#define FLASH_PART_SIZE (0x10000U)
#define FLASH_PART_BASE_ADDR (0x10000000U + FLASH_PART_OFF)
#define ADDR_IN_RAM(x) (((uint32_t)x) >= RAM_BASE && ((uint32_t)x) < RAM_MAX)

#define PARTITION_SIZE (1024 * 64)
#define SECTOR_SIZE (4096)

uint8_t sector_cache[SECTOR_SIZE];

__attribute__((cmse_nonsecure_entry))
int secure_flash_write_page(uint32_t off, uint8_t *page)
{
    int i;
    if (off & 0xFF)
        return -1; /* off must be aligned to 256 bytes */
    if (off >= PARTITION_SIZE)
        return -1;  /* Out of range */
    if (!ADDR_IN_RAM(page))
        return -1; /* Page must be in RAM */
    
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        volatile uint32_t *orig = ((uint32_t *)(FLASH_PART_BASE_ADDR + off)) + i;
        volatile uint32_t *new = ((uint32_t *)page) + i;
        if ((*orig & *new) != *new) {
            uint32_t sector_n = off / SECTOR_SIZE;
            uint32_t page_n_in_sector = (off & (SECTOR_SIZE - 1)) / FLASH_PAGE_SIZE;
            uint32_t j;
            memcpy(sector_cache, (void *)((uintptr_t)orig & ~(SECTOR_SIZE - 1)), SECTOR_SIZE);
            flash_range_erase(FLASH_PART_OFF + off, FLASH_PAGE_SIZE);
            for (j = 0; j < page_n_in_sector; j++) {
                flash_range_program(FLASH_PART_OFF + SECTOR_SIZE * sector_n +
                        j * FLASH_PAGE_SIZE, sector_cache + j * FLASH_PAGE_SIZE,
                        FLASH_PAGE_SIZE);
            }
            flash_range_program(FLASH_PART_OFF + off, page, FLASH_PAGE_SIZE);
            for (j = page_n_in_sector + 1; j < SECTOR_SIZE / FLASH_PAGE_SIZE; j++) {
                flash_range_program(FLASH_PART_OFF + SECTOR_SIZE * sector_n +
                        j * FLASH_PAGE_SIZE, sector_cache + j * FLASH_PAGE_SIZE,
                        FLASH_PAGE_SIZE);
            }
            return 0; /* Done erasing sector + writing page + restoring old pages
                         in sector */
        } 
    }
    flash_range_program(FLASH_PART_OFF + off, page, FLASH_PAGE_SIZE);
    return 0; /* Done writing just by clearing bits */
}
