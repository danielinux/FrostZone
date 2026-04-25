/*
 *   JEDEC SPI NOR flash driver for frosted
 *
 * Probes a JEDEC-compliant SPI NOR flash chip, reads its manufacturer/device
 * ID, and exposes low-level read / sector-erase / format helpers.
 *
 * FlashFS uses these helpers to mount an external flash device under /mnt/flash.
 *
 * The driver uses the existing devspi SPI framework (from stm32_spi.c) for
 * all bus transactions.  The flash is always a SPI master peripheral on the
 * non-secure side of TrustZone.
 *
 * Supported JEDEC flash command set:
 *
 *  RDID  (0x9F) -- read manufacturer / device ID
 *  WREN  (0x06) -- write enable latch
 *  WRDI  (0x04) -- write disable
 *  RDSR  (0x05) -- read status register 1
 *  WRSR  (0x01) -- write status register 1
 *  PP    (0x02)  -- page program (up to 256 bytes per page)
 *  READ  (0x03)  -- read data (3-byte address)
 *  SE    (0x20)  -- sector erase (4 KB fixed)
 *
 */

#include "frosted.h"
#include "device.h"
#include "gpio.h"
#include "spi.h"
#include "kprintf.h"
#define STM32_RCC_BASE RCC_BASE
#include "stm32h5xx.h"
#include "stm32x5_board_common.h"
#include "jedec_spi_flash.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static struct module mod_jedec_flash;
static struct device *jedec_flash_dev;
static uint8_t jedec_sector_shadow[JEDEC_SECTOR_SIZE];

#define JEDEC_VERIFY_CHUNK 256U

static bool jedec_uses_emulator_fallback(const struct jedec_spi_flash *flash)
{
    if (!flash)
        return false;
    return flash->manufacturer == 0xFF && flash->memory_type == 0xFF;
}

/* SPI bus base addresses
 * STM32H563 reference manual:
 *   SPI1  -> APB4 (0x4100_2400)
 *   SPI2  -> APB1 (0x4000_3800)
 *   SPI3  -> APB1 (0x4000_3C00)
 */
#define SPI1_NS_BASE     0x41002400UL
#define SPI2_NS_BASE     0x40003800UL
#define SPI3_NS_BASE     0x40003C00UL

/* SPI peripheral register offsets (for clock enable)
 * STM32H563: RCC_BASE = 0x4402_0C00
 *   AHB2ENR  = +0x08C (GPIO clock)
 *   APB1LENR = +0x09C (SPI2/SPI3 clock)
 *   APB4ENR  = +0x084 (SPI1 clock)
 */

#define RCC_APB4ENR (*(volatile uint32_t *)(STM32_RCC_BASE + 0x084UL))

/* GPIO bank name → base address helper */
static uint32_t jedec_spi_gpio_bank_to_base(const char *bank_name)
{
    /* Only valid on STM32; use the common board defines. */
#if defined(TARGET_stm32h563)
    if (strcmp(bank_name, "GPIOA") == 0) return GPIOA_BASE;
    if (strcmp(bank_name, "GPIOB") == 0) return GPIOB_BASE;
    if (strcmp(bank_name, "GPIOC") == 0) return GPIOC_BASE;
    if (strcmp(bank_name, "GPIOD") == 0) return GPIOD_BASE;
    if (strcmp(bank_name, "GPIOE") == 0) return GPIOE_BASE;
    if (strcmp(bank_name, "GPIOF") == 0) return GPIOF_BASE;
    if (strcmp(bank_name, "GPIOG") == 0) return GPIOG_BASE;
    if (strcmp(bank_name, "GPIOH") == 0) return GPIOH_BASE;
    /* Fall through to invalid; caller checks. */
#endif
    (void)bank_name;
    return 0;
}

/* GPIO CS setup */
static void jedec_spi_gpio_set_output(uint32_t gpio_base, uint8_t pin)
{
    /* GPIO clocks are already enabled by RCC_AHB2ENR in frosted_stm32h563.c */
    stm32x5_rcc_enable_gpio(gpio_base);
    stm32x5_gpio_write_mode(gpio_base, pin, GPIO_MODE_OUTPUT);
    stm32x5_gpio_write_otype(gpio_base, pin, GPIO_OTYPE_PP);
    stm32x5_gpio_write_speed(gpio_base, pin, GPIO_SPEED_HIGH);
    stm32x5_gpio_write_pull(gpio_base, pin, IOCTL_GPIO_PUPD_PULLUP);
    stm32x5_gpio_set(gpio_base, pin);   /* CS high = inactive */
}

/* Default pin muxing for each SPI bus (standard alt-function assignments) */
static void jedec_spi_get_default_pins(uint32_t base,
    struct gpio_config *p_sck, struct gpio_config *p_mosi,
    struct gpio_config *p_miso, struct gpio_config *p_nss)
{
    if (!p_sck || !p_mosi || !p_miso || !p_nss)
        return;

    if (base == SPI1_NS_BASE) {
        *p_sck   = (struct gpio_config){ .base = GPIOA_BASE, .pin = 5,  .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 5 };
        *p_mosi  = (struct gpio_config){ .base = GPIOA_BASE, .pin = 7,  .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 5 };
        *p_miso  = (struct gpio_config){ .base = GPIOA_BASE, .pin = 6,  .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 5 };
        *p_nss   = (struct gpio_config){ .base = GPIOA_BASE, .pin = 4,  .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 5 };
        kprintf("jedec: SPI1 (base=0x%08lx) on PA4/PA5/PA6/PA7\n", (unsigned long)base);
    } else if (base == SPI2_NS_BASE) {
        *p_sck   = (struct gpio_config){ .base = GPIOB_BASE, .pin = 13, .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 1 };
        *p_mosi  = (struct gpio_config){ .base = GPIOB_BASE, .pin = 15, .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 1 };
        *p_miso  = (struct gpio_config){ .base = GPIOB_BASE, .pin = 14, .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 1 };
        *p_nss   = (struct gpio_config){ .base = GPIOB_BASE, .pin = 12, .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 1 };
        kprintf("jedec: SPI2 (base=0x%08lx) on PB12/PB13/PB14/PB15\n", (unsigned long)base);
    } else {
        /* SPI3: standard alt-function per STM32H5 ref manual */
        *p_sck   = (struct gpio_config){ .base = GPIOC_BASE, .pin = 10, .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 6 };
        *p_mosi  = (struct gpio_config){ .base = GPIOC_BASE, .pin = 12, .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 6 };
        *p_miso  = (struct gpio_config){ .base = GPIOC_BASE, .pin = 11, .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 6 };
        *p_nss   = (struct gpio_config){ .base = GPIOC_BASE, .pin = 8,  .mode = GPIO_MODE_AF, .speed = GPIO_SPEED_HIGH, .optype = GPIO_OTYPE_PP, .af = 6 };
        kprintf("jedec: SPI3 (base=0x%08lx) on PC8/PC10/PC11/PC12\n", (unsigned long)base);
    }
}

/* JEDEC helpers: low-level SPI transactions with CS control */

static int jedec_send_cmd_then_read(struct jedec_spi_flash *flash,
    const uint8_t *cmd, int cmd_len, uint8_t *rx, uint32_t rx_len)
{
    int ret;

    stm32x5_gpio_reset(flash->gpio_base, flash->cs_pin);
    ret = devspi_xfer(&flash->spi_slave, (const char *)cmd, (char *)rx, cmd_len);
    if (ret < 0)
        goto done;

    if (rx_len > 0) {
        int rx2_ret = devspi_xfer(&flash->spi_slave, NULL, (char *)rx, rx_len);
        if (rx2_ret < 0)
            ret = rx2_ret;
    }

done:
    stm32x5_gpio_set(flash->gpio_base, flash->cs_pin);
    return ret;
}

static int jedec_poll_busy(struct jedec_spi_flash *flash)
{
    uint8_t sr = 0xFF;
    volatile uint32_t retries = 2000000;
    int ret;

    while ((sr & 0x01) && retries--) {
        uint8_t rd[1] = { 0x00 };
        ret = jedec_send_cmd_then_read(flash, (uint8_t[]){ JEDEC_CMD_RDSR }, 1, rd, 1);
        if (ret < 0)
            return -EIO;
        sr = rd[0];
    }
    return (retries == 0) ? -ETIMEDOUT : 0;
}

static int jedec_send_write_enable(struct jedec_spi_flash *flash)
{
    uint8_t cmd = JEDEC_CMD_WREN;
    return jedec_send_cmd_then_read(flash, &cmd, 1, NULL, 0);
}

static int jedec_send_write_disable(struct jedec_spi_flash *flash)
{
    uint8_t cmd = JEDEC_CMD_WRDI;
    return jedec_send_cmd_then_read(flash, &cmd, 1, NULL, 0);
}

static int jedec_send_cmd_then_write(struct jedec_spi_flash *flash,
    const uint8_t *cmd, int cmd_len, const uint8_t *tx, uint32_t tx_len)
{
    uint8_t staging[4 + JEDEC_VERIFY_CHUNK];
    int ret;
    uint32_t total_len;

    stm32x5_gpio_reset(flash->gpio_base, flash->cs_pin);
    if (tx_len > 0) {
        if ((uint32_t)cmd_len + tx_len > sizeof(staging)) {
            ret = -EINVAL;
            goto done;
        }
        memcpy(staging, cmd, cmd_len);
        memcpy(staging + cmd_len, tx, tx_len);
        total_len = (uint32_t)cmd_len + tx_len;
        ret = devspi_xfer(&flash->spi_slave, (const char *)staging, NULL, total_len);
        if (ret < 0)
            goto done;
    } else {
        ret = devspi_xfer(&flash->spi_slave, (const char *)cmd, NULL, cmd_len);
        if (ret < 0)
            goto done;
    }
    ret = 0;

done:
    stm32x5_gpio_set(flash->gpio_base, flash->cs_pin);
    return ret;
}

static int jedec_wait_program_verify(const struct jedec_spi_flash *flash,
        uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint8_t verify[JEDEC_VERIFY_CHUNK];
    uint32_t retries = 2000;
    int ret;

    if (len > sizeof(verify))
        return -EINVAL;

    while (retries--) {
        ret = jedec_spi_flash_read(flash, addr, verify, len);
        if (ret < 0)
            return ret;
        if (memcmp(verify, buf, len) == 0)
            return 0;
    }
    return -ETIMEDOUT;
}

static int jedec_wait_erase_verify(const struct jedec_spi_flash *flash, uint32_t addr,
        uint32_t len)
{
    uint8_t verify[JEDEC_VERIFY_CHUNK];
    uint32_t retries = 2000;
    uint32_t off;
    uint32_t chunk;
    int ret;

    while (retries--) {
        int all_ff = 1;

        for (off = 0; off < len; off += chunk) {
            chunk = len - off;
            if (chunk > sizeof(verify))
                chunk = sizeof(verify);
            ret = jedec_spi_flash_read(flash, addr + off, verify, chunk);
            if (ret < 0)
                return ret;
            {
                uint32_t i;

                for (i = 0; i < chunk; i++) {
                    if (verify[i] != 0xFF) {
                        all_ff = 0;
                        break;
                    }
                }
            }
            if (!all_ff)
                break;
        }
        if (all_ff)
            return 0;
    }
    return -ETIMEDOUT;
}

static int jedec_page_needs_erase(const uint8_t *existing, const uint8_t *incoming,
        uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        if ((existing[i] & incoming[i]) != incoming[i])
            return 1;
    }
    return 0;
}

static int jedec_spi_flash_program_range(const struct jedec_spi_flash *flash,
        uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t written = 0;
    int ret = 0;
    int wel_set = 0;

    while (written < len) {
        uint32_t page_off = (addr + written) % flash->page_size;
        uint32_t chunk = flash->page_size - page_off;
        uint8_t cmd[4];

        if (chunk > (len - written))
            chunk = len - written;

        ret = jedec_send_write_enable((struct jedec_spi_flash *)flash);
        if (ret < 0)
            goto out;
        wel_set = 1;

        cmd[0] = JEDEC_CMD_PP;
        cmd[1] = (uint8_t)((addr + written) >> 16);
        cmd[2] = (uint8_t)((addr + written) >> 8);
        cmd[3] = (uint8_t)(addr + written);

        ret = jedec_send_cmd_then_write((struct jedec_spi_flash *)flash, cmd, 4,
                buf + written, chunk);
        if (ret < 0)
            goto out;

        if (jedec_uses_emulator_fallback(flash))
            ret = jedec_wait_program_verify(flash, addr + written, buf + written, chunk);
        else
            ret = jedec_poll_busy((struct jedec_spi_flash *)flash);
        if (ret < 0)
            goto out;

        written += chunk;
    }

out:
    if (wel_set)
        jedec_send_write_disable((struct jedec_spi_flash *)flash);
    return ret;
}

/* JEDEC geometry defines */
#define JEDEC_DEFAULT_PAGE_SIZE       256
#define JEDEC_DEFAULT_SECTOR_SIZE    4096

/* jedec_bus_base_for_index: return the SPI peripheral base address */
static uint32_t jedec_bus_base_for_index(int spi_bus)
{
    switch (spi_bus) {
    case 0:
        return SPI1_NS_BASE;
    case 1:
        return SPI2_NS_BASE;
    case 2:
        return SPI3_NS_BASE;
    default:
        return 0;
    }
}

/* jedec_spi_flash_probe */

int jedec_spi_flash_probe(struct jedec_spi_flash *flash,
    int spi_bus, uint8_t cs_pin, const char *gpio_bank, uint32_t baud)
{
    uint8_t id[4];
    uint32_t base;
    uint32_t gpio_base;
    uint8_t devspi_bus;
    struct gpio_config sck, mosi, miso, nss;
    const struct spi_config *cfgp;
    struct spi_config cfg;
    int ret;

    if (!flash)
        return -EINVAL;

    base = jedec_bus_base_for_index(spi_bus);
    if (base == 0)
        return -EINVAL;

#if defined(TARGET_stm32h563)
    /*
     * The current non-secure STM32H563 SPI driver only instantiates SPI3.
     * Refuse SPI1/SPI2 here instead of faulting on direct register access.
     */
    if (spi_bus != 2) {
        kprintf("jedec: SPI%d is not supported on stm32h563 yet; use SPI3\n",
                spi_bus + 1);
        return -EOPNOTSUPP;
    }

    /*
     * On stm32h563, the existing SPI driver exposes the SPI3 peripheral in
     * devspi slot 0 (appearing as /dev/spi1). Reuse that slot here so JEDEC
     * probing rides the already-supported path instead of trying to allocate
     * a non-existent slot 2.
     */
    devspi_bus = 0;
#else
    devspi_bus = (uint8_t)spi_bus;
#endif

    gpio_base = jedec_spi_gpio_bank_to_base(gpio_bank);
    if (gpio_base == 0)
        return -EINVAL;

    memset(flash, 0, sizeof(*flash));
    flash->spi_bus    = (uint8_t)spi_bus;
    flash->cs_pin     = cs_pin;
    flash->gpio_base  = gpio_base;
    flash->gpio_bank  = gpio_bank;
    flash->baudrate   = baud;
    flash->page_size  = JEDEC_DEFAULT_PAGE_SIZE;
    flash->sector_size = JEDEC_DEFAULT_SECTOR_SIZE;

    jedec_spi_get_default_pins(base, &sck, &mosi, &miso, &nss);

    /*
     * The shared SPI3 bring-up path leaves MISO unconfigured; configure the
     * lines we need here before attempting JEDEC reads.
     */
    if (sck.base)
        stm32x5_gpio_config_alt(&sck);
    if (mosi.base)
        stm32x5_gpio_config_alt(&mosi);
    if (miso.base)
        stm32x5_gpio_config_alt(&miso);

    /* Configure CS as GPIO output (high = idle) */
    jedec_spi_gpio_set_output(gpio_base, cs_pin);

    /* Create SPI bus */
    cfg = (struct spi_config) {
        .idx                        = devspi_bus,
        .base                       = base,
        .baudrate                   = baud,
        .polarity                   = 0,
        .phase                      = 0,
        .rx_only                    = 0,
        .bidir_mode                 = 0,
        .dff_16                     = 0,
        .enable_software_slave_management = 1,
        .send_msb_first             = 1,
        .pio_sck                    = sck,
        .pio_mosi                   = mosi,
        .pio_miso                   = miso,
        /* CS is driven manually through GPIO; do not remux the same pin to NSS. */
        .pio_nss                    = (struct gpio_config){ 0 },
    };
    cfgp = &cfg;

    ret = devspi_create(cfgp);
    if (ret == -EEXIST)
        ret = 0;
    if (ret < 0) {
        kprintf("jedec: SPI%d init failed (%d)\n", spi_bus + 1, ret);
        return ret;
    }

    /* Set up SPI slave pointer */
    flash->spi_bus    = (uint8_t)spi_bus;
    flash->spi_slave.bus = devspi_bus;
    flash->spi_slave.priv = flash;

    /* Read JEDEC ID (RDID = 0x9F: returns 3 bytes) */
    memset(id, 0xFF, sizeof(id));
    ret = jedec_send_cmd_then_read(flash, (uint8_t[]){ JEDEC_CMD_RDID, 0xFF, 0xFF, 0xFF },
                                    1, id, 4);
    if (ret < 0)
        return ret;

    if (id[0] == 0xFF || id[1] == 0xFF) {
        /*
         * m33mu's SPI flash model does not currently provide a usable RDID
         * response, but it does back READ transactions against the attached
         * host image. Fall back to the legacy 64 KiB flashfs geometry so
         * userspace mount testing can proceed under emulation.
         */
        flash->manufacturer = 0xFF;
        flash->memory_type = 0xFF;
        flash->capacity_code = 0xF8;
        flash->jedec_id = 0xFFFF;
        flash->size_bytes = 0x800000UL;
        flash->page_count = flash->size_bytes / flash->page_size;
        flash->probed = true;
        return 0;
    }

    flash->manufacturer  = id[0];
    flash->memory_type   = id[1];
    flash->capacity_code = id[2];
    flash->jedec_id = ((uint16_t)id[0] << 8) | id[1];

    flash->size_bytes = jedec_capacity_code_to_bytes(id[2]);
    if (flash->size_bytes > 0)
        flash->page_count = flash->size_bytes / flash->page_size;

    kprintf("jedec: SPI%d flash found: MFR=0x%02X ID=0x%04X cap=%lu pages=%lu\n",
            spi_bus + 1, flash->manufacturer, flash->jedec_id,
            (unsigned long)flash->size_bytes, (unsigned long)flash->page_count);

    flash->probed = true;
    return 0;
}

int jedec_spi_flash_register_device(struct jedec_spi_flash *flash, const char *dev_path)
{
    struct fnode *devfs;
    const char *name;

    if (!flash || !flash->probed || !dev_path)
        return -EINVAL;

    if (jedec_flash_dev != NULL) {
        flash->dev_path = dev_path;
        return 0;
    }

    devfs = fno_search("/dev");
    if (!devfs)
        return -ENOENT;

    name = dev_path;
    if (strncmp(dev_path, "/dev/", 5) == 0)
        name = dev_path + 5;

    memset(&mod_jedec_flash, 0, sizeof(mod_jedec_flash));
    strncpy(mod_jedec_flash.name, "jedecflash", sizeof(mod_jedec_flash.name) - 1);
    mod_jedec_flash.name[sizeof(mod_jedec_flash.name) - 1] = '\0';
    mod_jedec_flash.family = FAMILY_FILE;
    mod_jedec_flash.ops.open = device_open;

    jedec_flash_dev = device_fno_init(&mod_jedec_flash, name, devfs, 0, flash);
    if (!jedec_flash_dev)
        return -ENOMEM;

    flash->dev_path = dev_path;
    register_module(&mod_jedec_flash);
    return 0;
}

/* jedec_spi_flash_read */

int jedec_spi_flash_read(const struct jedec_spi_flash *flash, uint32_t addr,
                         void *buf, uint32_t len)
{
    uint8_t cmd[4];
    int ret;

    if (!flash || !buf)
        return -EINVAL;
    if ((addr + len) < addr || (addr + len) > flash->size_bytes)
        return -EINVAL;

    cmd[0] = JEDEC_CMD_READ;
    cmd[1] = (uint8_t)(addr >> 16);
    cmd[2] = (uint8_t)(addr >> 8);
    cmd[3] = (uint8_t)(addr);

    stm32x5_gpio_reset(flash->gpio_base, flash->cs_pin);
    ret = devspi_xfer((struct spi_slave *)&flash->spi_slave, (const char *)cmd, NULL, 4);
    if (ret < 0)
        goto done;
    if (len > 0)
        ret = devspi_xfer((struct spi_slave *)&flash->spi_slave, NULL, (char *)buf, len);
done:
    stm32x5_gpio_set(flash->gpio_base, flash->cs_pin);
    return (ret < 0) ? ret : (int)len;
}

int jedec_spi_flash_write_page(const struct jedec_spi_flash *flash, uint32_t addr,
                         const void *buf, uint32_t len)
{
    uint32_t sector_base;
    uint32_t page_off;
    int ret;

    if (!flash || !buf || len == 0)
        return -EINVAL;
    if (addr + len > flash->size_bytes)
        return -EINVAL;

    sector_base = addr & ~(flash->sector_size - 1U);
    page_off = addr - sector_base;

    ret = jedec_spi_flash_read(flash, sector_base, jedec_sector_shadow, flash->sector_size);
    if (ret < 0)
        return ret;

    if (!jedec_page_needs_erase(jedec_sector_shadow + page_off, buf, len)) {
        ret = jedec_spi_flash_program_range(flash, addr, buf, len);
        return ret;
    }

    memcpy(jedec_sector_shadow + page_off, buf, len);
    ret = jedec_spi_flash_erase_sector(flash, sector_base);
    if (ret < 0)
        return ret;
    return jedec_spi_flash_program_range(flash, sector_base, jedec_sector_shadow,
            flash->sector_size);
}

/* jedec_spi_flash_erase_sector */

int jedec_spi_flash_erase_sector(const struct jedec_spi_flash *flash, uint32_t addr)
{
    int ret;

    /* Write enable */
    jedec_send_write_enable((struct jedec_spi_flash *)flash);

    /* Sector erase command */
    uint8_t cmd[4] = { JEDEC_CMD_SE,
                       (uint8_t)(addr >> 16),
                       (uint8_t)(addr >> 8),
                       (uint8_t)(addr) };
    ret = jedec_send_cmd_then_read((struct jedec_spi_flash *)flash, cmd, 4, NULL, 0);
    if (ret < 0)
        return ret;

    if (jedec_uses_emulator_fallback(flash))
        ret = jedec_wait_erase_verify(flash, addr, flash->sector_size);
    else
        ret = jedec_poll_busy((struct jedec_spi_flash *)flash);
    return ret;
}

/* jedec_spi_flash_erase_all */

int jedec_spi_flash_erase_all(const struct jedec_spi_flash *flash)
{
    int ret;
    volatile uint32_t n;
    int progress = -1;

    kprintf("jedec: erasing %lu sectors ... ",
            (unsigned long)(flash->size_bytes / flash->sector_size));
    for (n = 0; n < (flash->size_bytes / flash->sector_size); n += 16) {
        uint32_t end;
        for (end = n; end < n + 16 &&
                end < (flash->size_bytes / flash->sector_size); end++) {
            jedec_send_write_enable((struct jedec_spi_flash *)flash);
            uint32_t sector_addr = end * flash->sector_size;
            uint8_t cmd[4] = { JEDEC_CMD_SE,
                               (uint8_t)(sector_addr >> 16),
                               (uint8_t)(sector_addr >> 8),
                               (uint8_t)(sector_addr) };
            jedec_send_cmd_then_read((struct jedec_spi_flash *)flash, cmd, 4, NULL, 0);
            ret = jedec_poll_busy((struct jedec_spi_flash *)flash);
            if (ret != 0) {
                kprintf("\n");
                return ret;
            }
        }
        int pct = ((n + 16) * 100) / (flash->size_bytes / flash->sector_size);
        if (pct > progress) {
            kprintf("\033[2K\r%3d%%", pct > 100 ? 100 : pct);
            progress = pct;
        }
    }
    kprintf("\033[2K\r");
    kprintf("jedec: erase done, total %lu sectors\n",
            (unsigned long)(flash->size_bytes / flash->sector_size));
    return 0;
}

/* jedec_spi_flash_is_blank */

int jedec_spi_flash_is_blank(const struct jedec_spi_flash *flash)
{
    uint8_t page[256];
    unsigned int i;
    int ret;
    uint32_t sample_sz = flash->page_size < sizeof(page) ? flash->page_size : sizeof(page);

    ret = jedec_spi_flash_read(flash, 0, page, sample_sz);
    if (ret < 0)
        return 0;  /* read error */

    for (i = 0; i < sample_sz; i++) {
        if (page[i] != 0xFF)
            return 0;
    }
    return 1;
}
