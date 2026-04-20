#include "frosted.h"
#include "device.h"
#include "locks.h"
#include "spi.h"
#include "stm32h5xx.h"
#define STM32_RCC_BASE RCC_BASE
#include "stm32x5_board_common.h"

#if defined(TARGET_stm32h563)
#define STM32_RCC_CFGR2      (*(volatile uint32_t *)(STM32_RCC_BASE + 0x020UL))
#define RCC_CFGR2_PPRE1_Pos  4U
#define RCC_CFGR2_PPRE2_Pos  8U
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

extern uint32_t SystemCoreClock;

#define SPI_CR1(base)      (*(volatile uint32_t *)((base) + 0x00U))
#define SPI_CR2(base)      (*(volatile uint32_t *)((base) + 0x04U))
#define SPI_CFG1(base)     (*(volatile uint32_t *)((base) + 0x08U))
#define SPI_CFG2(base)     (*(volatile uint32_t *)((base) + 0x0CU))
#define SPI_SR(base)       (*(volatile uint32_t *)((base) + 0x14U))
#define SPI_IFCR(base)     (*(volatile uint32_t *)((base) + 0x18U))
#define SPI_TXDR(base)     (*(volatile uint32_t *)((base) + 0x20U))
#define SPI_RXDR(base)     (*(volatile uint32_t *)((base) + 0x30U))

#define SPI_CR1_SPE        (1U << 0)
#define SPI_CR1_CSTART     (1U << 9)
#define SPI_CR1_SSI        (1U << 12)
#define SPI_CR1_IOLOCK     (1U << 16)
#define SPI_CR2_TSIZE_SHIFT 0U
#define SPI_CR2_TSIZE_MASK (0xFFFFU << SPI_CR2_TSIZE_SHIFT)

#define SPI_CFG1_DSIZE_SHIFT 0U
#define SPI_CFG1_DSIZE_MASK  (0x1FU << SPI_CFG1_DSIZE_SHIFT)
#define SPI_CFG1_FTHLV_SHIFT 5U
#define SPI_CFG1_FTHLV_MASK  (0xFU << SPI_CFG1_FTHLV_SHIFT)
#define SPI_CFG1_CRCSIZE_SHIFT 16U
#define SPI_CFG1_CRCSIZE_MASK  (0x1FU << SPI_CFG1_CRCSIZE_SHIFT)
#define SPI_CFG1_BAUDRATE_SHIFT 28U
#define SPI_CFG1_BAUDRATE_MASK  0x7U
#define SPI_CFG1_MBR_SHIFT   28U
#define SPI_CFG1_MBR_MASK    (0x7U << SPI_CFG1_MBR_SHIFT)

#define SPI_CFG2_MASTER    (1U << 22)
#define SPI_CFG2_LSBFRST   (1U << 23)
#define SPI_CFG2_CPHA      (1U << 24)
#define SPI_CFG2_CPOL      (1U << 25)
#define SPI_CFG2_SSM       (1U << 26)
#define SPI_CFG2_SSIOP     (1U << 28)
#define SPI_CFG2_SSOE      (1U << 29)
#define SPI_CFG2_AFCNTR    (1U << 31)

#define SPI_SR_RXP         (1U << 0)
#define SPI_SR_TXP         (1U << 1)
#define SPI_SR_EOT         (1U << 3)
#define SPI_SR_TXTF        (1U << 4)
#define SPI_SR_MODF        (1U << 9)
#define SPI_SR_TXC         (1U << 12)
#define SPI_SR_RXWNE       (1U << 15)

#define SPI_IFCR_EOTC      (1U << 3)
#define SPI_IFCR_TXTFC     (1U << 4)
#define SPI_IFCR_UDRC      (1U << 5)
#define SPI_IFCR_OVRC      (1U << 6)
#define SPI_IFCR_CRCEC     (1U << 7)
#define SPI_IFCR_TIFREC    (1U << 8)
#define SPI_IFCR_MODFC     (1U << 9)
#define SPI_IFCR_SUSPC     (1U << 11)

#define SPI3_NS_BASE       0x40003C00UL
#define SPI_WAIT_RETRIES   1000000U

#define MAX_SPIS 2

struct dev_spi {
    struct device *dev;
    struct spi_slave file_slave;
    uint32_t base;
    uint8_t idx;
    mutex_t *mutex;
    struct spi_config config;
};

static struct dev_spi *DEV_SPI[MAX_SPIS];
static int spi_dev_write(struct fnode *fno, const void *buf, unsigned int len);
static struct module mod_devspi;

static void stm32_spi_disable(uint32_t base)
{
    uint32_t cr1 = SPI_CR1(base);

    if (cr1 & SPI_CR1_SPE) {
        SPI_CR1(base) = cr1 & ~SPI_CR1_SPE;
        while (SPI_CR1(base) & SPI_CR1_SPE)
            ;
    }
}

static void stm32_spi_enable_clock(const struct spi_config *conf)
{
    (void)conf;
}

static int spi_dev_write(struct fnode *fno, const void *buf, unsigned int len)
{
    struct dev_spi *spi = (struct dev_spi *)FNO_MOD_PRIV(fno, &mod_devspi);
    if (!spi)
        return -ENODEV;
    return devspi_xfer(&spi->file_slave, buf, NULL, len);
}

static struct module mod_devspi = {
    .family = FAMILY_DEV,
    .name = "spi",
    .ops.open = device_open,
    .ops.write = spi_dev_write,
};

#if defined(TARGET_stm32h563)
static uint32_t stm32_spi_bus_clock(uint32_t base)
{
    static const uint8_t apb_presc_table[8] = { 1U, 1U, 1U, 1U, 2U, 4U, 8U, 16U };
    uint32_t cfgr2 = STM32_RCC_CFGR2;
    uint32_t presc_idx;

    if ((base >= 0x40010000UL) && (base < 0x40020000UL)) {
        presc_idx = (cfgr2 >> RCC_CFGR2_PPRE2_Pos) & 0x7U;
    } else {
        presc_idx = (cfgr2 >> RCC_CFGR2_PPRE1_Pos) & 0x7U;
    }

    return SystemCoreClock / apb_presc_table[presc_idx];
}
#else
static uint32_t stm32_spi_bus_clock(uint32_t base)
{
    (void)base;
    return SystemCoreClock;
}
#endif

static uint32_t stm32_spi_compute_mbr(uint32_t base, uint32_t baudrate)
{
    static const uint32_t divisors[] = { 2U, 4U, 8U, 16U, 32U, 64U, 128U, 256U };
    uint32_t i;
    uint32_t spi_clk = stm32_spi_bus_clock(base);

    if (baudrate == 0U)
        baudrate = 8000000U;
    if (spi_clk == 0U)
        return ARRAY_SIZE(divisors) - 1U;

    for (i = 0; i < ARRAY_SIZE(divisors); i++) {
        if ((spi_clk / divisors[i]) <= baudrate)
            return i;
    }

    return ARRAY_SIZE(divisors) - 1U;
}

/* Debug hooks: capture the last SPI3 transfer settings for GDB inspection. */
struct spi_debug_state {
    uint32_t begin_count;
    uint32_t last_tsize;
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cfg1;
    uint32_t cfg2;
    uint32_t sr;
    uint32_t last_base;
    uint32_t last_count;
};

volatile struct spi_debug_state spi3_dbg;

static void stm32_spi_config_pins(const struct spi_config *conf)
{
    if (conf->pio_sck.base)
        stm32x5_gpio_config_alt(&conf->pio_sck);
    if (conf->pio_mosi.base)
        stm32x5_gpio_config_alt(&conf->pio_mosi);
    if (conf->pio_miso.base)
        stm32x5_gpio_config_alt(&conf->pio_miso);
    if (conf->pio_nss.base)
        stm32x5_gpio_config_alt(&conf->pio_nss);
}

static void stm32_spi_clear_flags(uint32_t base)
{
    (void)SPI_SR(base);
    SPI_IFCR(base) = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC | SPI_IFCR_MODFC;
}

static void stm32_spi_program_hw(struct dev_spi *spi)
{
    uint32_t cfg1 = 0;
    uint32_t cfg2;
    uint32_t base = spi->base;

    /* Start from a clean state and drop any accidental IOLOCK. */
    stm32_spi_disable(base);
    SPI_CR1(base) &= ~SPI_CR1_IOLOCK;
    SPI_IFCR(base) = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC | SPI_IFCR_MODFC | SPI_IFCR_TIFREC | SPI_IFCR_UDRC | SPI_IFCR_SUSPC | SPI_IFCR_CRCEC;
    SPI_CR2(base) = 0;

    /* wolfBoot-style static configuration */
    cfg1 |= (2U << SPI_CFG1_BAUDRATE_SHIFT); /* hclk/32 (~3 MHz from 100 MHz kernel) */
    cfg1 |= (7U << SPI_CFG1_CRCSIZE_SHIFT);  /* 8-bit CRC size */
    cfg1 |= (0U << SPI_CFG1_FTHLV_SHIFT);    /* FIFO threshold = 1 */
    cfg1 |= (7U << SPI_CFG1_DSIZE_SHIFT);    /* 8-bit data */
    SPI_CFG1(base) = cfg1;

    cfg2 = SPI_CFG2_MASTER | SPI_CFG2_SSM; /* Mode 0 (CPOL=0, CPHA=0), software NSS */
    SPI_CFG2(base) = cfg2;

    /* Enable, then start the transfer engine. */
    SPI_CR1(base) = SPI_CR1_SPE | SPI_CR1_SSI;
    SPI_CR1(base) |= SPI_CR1_CSTART;
}

static void stm32_spi_begin_transfer(uint32_t base, uint32_t count)
{
    uint32_t cr1 = SPI_CR1(base);

    stm32_spi_clear_flags(base);
    SPI_CR2(base) = (SPI_CR2(base) & ~SPI_CR2_TSIZE_MASK) |
        ((count << SPI_CR2_TSIZE_SHIFT) & SPI_CR2_TSIZE_MASK);
    SPI_CR1(base) = cr1 | SPI_CR1_CSTART;

    /* Always capture debug snapshots so GDB can see what was attempted. */
    spi3_dbg.begin_count++;
    spi3_dbg.last_base = base;
    spi3_dbg.last_count = count;
    spi3_dbg.last_tsize = SPI_CR2(base) & SPI_CR2_TSIZE_MASK;
    spi3_dbg.cr1 = SPI_CR1(base);
    spi3_dbg.cr2 = SPI_CR2(base);
    spi3_dbg.cfg1 = SPI_CFG1(base);
    spi3_dbg.cfg2 = SPI_CFG2(base);
    spi3_dbg.sr = SPI_SR(base);
}

static void stm32_spi_wait_eot(uint32_t base)
{
    uint32_t retries = SPI_WAIT_RETRIES;

    /* In continuous-start mode, wait for the last word to leave the shifter. */
    while (!(SPI_SR(base) & SPI_SR_TXC) && retries--)
        ;
    if (retries == 0U)
        return;
    stm32_spi_clear_flags(base);
    (void)SPI_SR(base);
}

int devspi_create(const struct spi_config *conf)
{
    struct dev_spi *spi;
    struct fnode *devfs;

    if (!conf)
        return -EINVAL;
    if ((conf->idx < 0) || (conf->idx >= MAX_SPIS))
        return -EINVAL;
    if (conf->base == 0U)
        return -EINVAL;
    if (DEV_SPI[conf->idx])
        return -EEXIST;

    spi = kalloc(sizeof(*spi));
    if (!spi)
        return -ENOMEM;
    memset(spi, 0, sizeof(*spi));

    spi->idx = (uint8_t)conf->idx;
    spi->base = conf->base;
    spi->mutex = mutex_init();
    spi->file_slave.bus = spi->idx;
    spi->file_slave.priv = spi;
    memcpy(&spi->config, conf, sizeof(*conf));

    stm32_spi_enable_clock(conf);
    stm32_spi_config_pins(conf);
    stm32_spi_program_hw(spi);

    DEV_SPI[conf->idx] = spi;

    devfs = fno_search("/dev");
    if (devfs) {
        char name[] = "spi0";
        name[3] = (char)('1' + spi->idx);
        spi->dev = device_fno_init(&mod_devspi, name, devfs, 0, spi);
    }

    return 0;
}

int devspi_xfer(struct spi_slave *sl, const char *obuf, char *ibuf, unsigned int len)
{
    struct dev_spi *spi;
    const uint8_t *tx = (const uint8_t *)obuf;
    uint8_t *rx = (uint8_t *)ibuf;
    unsigned int remaining = len;

    if (!sl || sl->bus >= MAX_SPIS)
        return -EINVAL;

    spi = DEV_SPI[sl->bus];
    if (!spi || !spi->mutex)
        return -ENODEV;

    if (len == 0U)
        return 0;

    mutex_lock(spi->mutex);
    while (remaining > 0U) {
        uint32_t chunk = remaining > 0xFFFFU ? 0xFFFFU : remaining;
        uint32_t i;

        stm32_spi_begin_transfer(spi->base, chunk);
        for (i = 0; i < chunk; i++) {
            uint8_t value = tx ? tx[i] : 0xFFU;
            uint32_t tx_retries = SPI_WAIT_RETRIES;
            uint32_t rx_retries = SPI_WAIT_RETRIES;

            while (!(SPI_SR(spi->base) & SPI_SR_TXP) && tx_retries--)
                ;
            if (tx_retries == 0U) {
                kprintf("[devspi] TX timeout bus=%u base=0x%08lx i=%lu chunk=%lu sr=0x%08lx cr1=0x%08lx cr2=0x%08lx\n",
                        (unsigned)sl->bus,
                        (unsigned long)spi->base,
                        (unsigned long)i,
                        (unsigned long)chunk,
                        (unsigned long)SPI_SR(spi->base),
                        (unsigned long)SPI_CR1(spi->base),
                        (unsigned long)SPI_CR2(spi->base));
                mutex_unlock(spi->mutex);
                return -ETIMEDOUT;
            }
            SPI_TXDR(spi->base) = value;
            /* Always clock and consume RX; store if caller provided a buffer. */
            while (!(SPI_SR(spi->base) & SPI_SR_RXP) && rx_retries--)
                ;
            if (rx_retries == 0U) {
                kprintf("[devspi] RX timeout bus=%u base=0x%08lx i=%lu chunk=%lu sr=0x%08lx cr1=0x%08lx cr2=0x%08lx\n",
                        (unsigned)sl->bus,
                        (unsigned long)spi->base,
                        (unsigned long)i,
                        (unsigned long)chunk,
                        (unsigned long)SPI_SR(spi->base),
                        (unsigned long)SPI_CR1(spi->base),
                        (unsigned long)SPI_CR2(spi->base));
                mutex_unlock(spi->mutex);
                return -ETIMEDOUT;
            }
            if (rx)
                rx[i] = (uint8_t)SPI_RXDR(spi->base);
            else
                (void)SPI_RXDR(spi->base);
        }
        stm32_spi_wait_eot(spi->base);
        if (SPI_SR(spi->base) & (SPI_SR_RXP | SPI_SR_RXWNE))
            (void)SPI_RXDR(spi->base);

        if (tx)
            tx += chunk;
        if (rx)
            rx += chunk;
        remaining -= chunk;
    }
    mutex_unlock(spi->mutex);

    return (int)len;
}

int spi_bus_init(void)
{
#if defined(TARGET_stm32h563)
    static bool initialized;
    static const struct spi_config spi3_cfg = {
        .idx = 0,
        .base = 0x40003C00UL,
        .irq = 57,
        .rcc = RCC_APB1LENR_SPI3EN,
        .baudrate = 10000000,
        .polarity = 0,
        .phase = 0,
        .rx_only = 0,
        .bidir_mode = 0,
        .dff_16 = 0,
        .enable_software_slave_management = 1,
        .send_msb_first = 1,
        .pio_sck = {
            .base = GPIOC_BASE,
            .pin = 10,
            .mode = GPIO_MODE_AF,
            .pullupdown = IOCTL_GPIO_PUPD_NONE,
            .speed = GPIO_SPEED_VERY,
            .optype = GPIO_OTYPE_PP,
            .af = 6,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "spi3_sck",
        },
        .pio_miso = {
            .base = 0,
        },
        .pio_mosi = {
            .base = GPIOC_BASE,
            .pin = 12,
            .mode = GPIO_MODE_AF,
            .pullupdown = IOCTL_GPIO_PUPD_NONE,
            .speed = GPIO_SPEED_VERY,
            .optype = GPIO_OTYPE_PP,
            .af = 6,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "spi3_mosi",
        },
        .pio_nss = {
            .base = 0,
        },
    };

    int ret;

    if (initialized)
        return 0;

    ret = devspi_create(&spi3_cfg);
    if (ret == -EEXIST)
        ret = 0;
    if (ret == 0)
        initialized = true;
    return ret;
#else
    return -ENODEV;
#endif
}
