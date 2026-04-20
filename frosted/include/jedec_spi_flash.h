#ifndef _JEDEC_SPI_FLASH_H
#define _JEDEC_SPI_FLASH_H

#include <stdint.h>
#include <stdbool.h>
#include "spi.h"

/* JEDEC SPI NOR flash command opcodes */
#define JEDEC_CMD_WREN       0x06
#define JEDEC_CMD_WRDI       0x04
#define JEDEC_CMD_RDSR       0x05
#define JEDEC_CMD_WRSR       0x01
#define JEDEC_CMD_PP         0x02
#define JEDEC_CMD_READ       0x03
#define JEDEC_CMD_SE         0x20
#define JEDEC_CMD_BE_64K     0xD8
#define JEDEC_CMD_RDID       0x9F
#define JEDEC_CMD_RDSR2      0x35
#define JEDEC_CMD_RDSDR      0x3B

#define JEDEC_SECTOR_SIZE    4096

/* JEDEC capacity codes to bytes.
 * Standard RDID encoding: capacity byte == log2(total_bytes). */
static uint32_t jedec_capacity_code_to_bytes(uint16_t code)
{
    if (code == 0 || code >= 32)
        return 0;
    return (1UL << code);
}

struct jedec_spi_flash {
    uint8_t spi_bus;
    uint8_t cs_pin;
    uint32_t gpio_base;
    const char *gpio_bank;
    uint8_t gpio_pin;
    uint32_t baudrate;
    const char *dev_path;
    struct spi_slave spi_slave;

    /* Probed values from RDID */
    uint16_t jedec_id;
    uint8_t manufacturer;
    uint8_t memory_type;
    uint8_t capacity_code;

    /* Derived flash geometry */
    uint32_t size_bytes;
    uint32_t page_size;
    uint32_t sector_size;
    uint32_t page_count;

    bool probed;
    bool ready;
};

/*
 * Probe an external JEDEC SPI flash:
 *  - initializes CS GPIO (output, high)
 *  - sends RDID command to identify the chip
 *  - derives size, page size, page count from the ID
 *
 * Returns 0 on success, negative errno on failure.
 */
int jedec_spi_flash_probe(struct jedec_spi_flash *flash,
    int spi_bus, uint8_t cs_pin, const char *gpio_bank, uint32_t baud);

int jedec_spi_flash_register_device(struct jedec_spi_flash *flash, const char *dev_path);

/*
 * Read 'len' bytes from flash address 'addr' into 'buf'.
 * Returns number of bytes read on success, negative errno on failure.
 */
int jedec_spi_flash_read(const struct jedec_spi_flash *flash, uint32_t addr,
                         void *buf, uint32_t len);

/*
 * Rewrite one logical flashfs page at 'addr'. The implementation preserves
 * surrounding data by rewriting the containing erase sector when needed.
 */
int jedec_spi_flash_write_page(const struct jedec_spi_flash *flash, uint32_t addr,
                               const void *buf, uint32_t len);

/*
 * Send WREN, then erase a 4KB sector at 'addr'.
 * Polls status register until BUSY bit clears.
 * Returns 0 on success, negative errno on failure.
 */
int jedec_spi_flash_erase_sector(const struct jedec_spi_flash *flash, uint32_t addr);

/*
 * Erase ALL sectors in the flash device and fill with 0xFF.
 * This is effectively a "format" for blank external flash.
 */
int jedec_spi_flash_erase_all(const struct jedec_spi_flash *flash);

/*
 * Check if the first 'len' bytes of flash are all 0xFF (blank / new).
 */
int jedec_spi_flash_is_blank(const struct jedec_spi_flash *flash);

#endif /* _JEDEC_SPI_FLASH_H */
