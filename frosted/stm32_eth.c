#include "frosted.h"
#include "eth.h"
#include "net.h"
#include "socket_in.h"
#include "wolfip.h"
#include "locks.h"
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include "lan8742.h"

#if CONFIG_ETH
static struct module mod_deveth = {
    .family = FAMILY_NETDEV,
    .name = "stm32_eth",
};
#endif

#ifndef __DCACHE_PRESENT
#define __DCACHE_PRESENT 0
#endif

#define STM32_ETH_DCACHE_LINE_SIZE 32U

static inline void stm32_eth_clean_dcache_range(const void *addr, size_t len)
{
#if __DCACHE_PRESENT
    uintptr_t start;
    uintptr_t end;

    if (!len)
        return;

    start = (uintptr_t)addr & ~(uintptr_t)(STM32_ETH_DCACHE_LINE_SIZE - 1U);
    end = ((uintptr_t)addr + len + (STM32_ETH_DCACHE_LINE_SIZE - 1U)) &
          ~(uintptr_t)(STM32_ETH_DCACHE_LINE_SIZE - 1U);

    for (; start < end; start += STM32_ETH_DCACHE_LINE_SIZE)
        __asm volatile ("dc cvac, %0" :: "r"(start) : "memory");

    __asm volatile ("dsb sy" ::: "memory");
#else
    (void)addr;
    (void)len;
#endif
}

static inline void stm32_eth_invalidate_dcache_range(void *addr, size_t len)
{
#if __DCACHE_PRESENT
    uintptr_t start;
    uintptr_t end;

    if (!len)
        return;

    start = (uintptr_t)addr & ~(uintptr_t)(STM32_ETH_DCACHE_LINE_SIZE - 1U);
    end = ((uintptr_t)addr + len + (STM32_ETH_DCACHE_LINE_SIZE - 1U)) &
          ~(uintptr_t)(STM32_ETH_DCACHE_LINE_SIZE - 1U);

    for (; start < end; start += STM32_ETH_DCACHE_LINE_SIZE)
        __asm volatile ("dc ivac, %0" :: "r"(start) : "memory");

    __asm volatile ("dsb sy" ::: "memory");
#else
    (void)addr;
    (void)len;
#endif
}

#define ETH_BASE            0x40028000UL
#define ETH_REG(offset)     (*(volatile uint32_t *)(ETH_BASE + (offset)))
/* Transmit Poll Demand Register – any write triggers the DMA engine */
#define ETH_TPDR            ETH_REG(0x1180U)

/* MAC registers */
#define ETH_MACCR           ETH_REG(0x0000)
#define ETH_MACPFR          ETH_REG(0x0004)
#define ETH_MACA0HR         ETH_REG(0x0300)
#define ETH_MACA0LR         ETH_REG(0x0304)

/* MTL registers */
#define ETH_MTLTXQOMR       ETH_REG(0x0D00)
#define ETH_MTLRXQOMR       ETH_REG(0x0D30)

/* DMA registers */
#define ETH_DMAMR           ETH_REG(0x1000)
#define ETH_DMASBMR         ETH_REG(0x1004)
#define ETH_DMACTXCR        ETH_REG(0x1104)
#define ETH_DMACRXCR        ETH_REG(0x1108)
#define ETH_DMACTXDLAR      ETH_REG(0x1114)
#define ETH_DMACRXDLAR      ETH_REG(0x111C)
#define ETH_DMACTXDTPR      ETH_REG(0x1120)
#define ETH_DMACRXDTPR      ETH_REG(0x1128)
#define ETH_DMACTXRLR       ETH_REG(0x112C)
#define ETH_DMACRXRLR       ETH_REG(0x1130)
#define ETH_DMACSR          ETH_REG(0x1160)
#define ETH_MACMDIOAR       ETH_REG(0x0200)
#define ETH_MACMDIODR       ETH_REG(0x0204)

/* MAC control bits */
#define ETH_MACCR_RE        (1U << 0)
#define ETH_MACCR_TE        (1U << 1)
#define ETH_MACCR_DM        (1U << 13)
#define ETH_MACCR_FES       (1U << 14)

/* DMA bits */
#define ETH_DMAMR_SWR       (1U << 0)
#define ETH_DMASBMR_FB      (1U << 0)
#define ETH_DMASBMR_AAL     (1U << 12)

#define ETH_DMACTXCR_ST     (1U << 0)
#define ETH_DMACTXCR_OSF    (1U << 4)
#define ETH_DMACRXCR_SR     (1U << 0)
#define ETH_DMACRXCR_RBSZ_SHIFT 1
#define ETH_DMACSR_TBU      (1U << 2)
#define ETH_DMACTXCR_TPBL_SHIFT 16
#define ETH_DMACTXCR_TPBL(val) (((uint32_t)(val) & 0x3FU) << ETH_DMACTXCR_TPBL_SHIFT)
#define ETH_DMACRXCR_RPBL_SHIFT 16
#define ETH_DMACRXCR_RPBL(val) (((uint32_t)(val) & 0x3FU) << ETH_DMACRXCR_RPBL_SHIFT)

/* MTL bits */
#define ETH_MTLTXQOMR_FTQ           (1U << 0)
#define ETH_MTLTXQOMR_TSF           (1U << 1)
#define ETH_MTLTXQOMR_TXQEN_SHIFT   2
#define ETH_MTLTXQOMR_TXQEN_ENABLE  (2U << ETH_MTLTXQOMR_TXQEN_SHIFT)
#define ETH_MTLTXQOMR_MASK          0x00000072U
#define ETH_MTLRXQOMR_RSF           (1U << 5)
#define ETH_MTLRXQOMR_MASK          0x0000007BU

#define ETH_MACMDIOAR_MB        (1U << 0)
#define ETH_MACMDIOAR_C45E      (1U << 1)
#define ETH_MACMDIOAR_GOC_SHIFT 2
#define ETH_MACMDIOAR_GOC_WRITE 0x1U
#define ETH_MACMDIOAR_GOC_READ  0x3U
#define ETH_MACMDIOAR_CR_SHIFT  8
#define ETH_MACMDIOAR_RDA_SHIFT 16
#define ETH_MACMDIOAR_PA_SHIFT  21

#define STM32_ETH_PHY_REG_BCR      0x00U
#define STM32_ETH_MDIO_CLOCK_RANGE 0x4U
#define STM32_ETH_MDIO_TIMEOUT     100000U
#define STM32_ETH_PHY_REG_BSR      0x01U
#define STM32_ETH_PHY_REG_ID1      0x02U
#define STM32_ETH_PHY_REG_ANAR     0x04U
#define STM32_ETH_PHY_REG_SCSR     0x1FU

#define STM32_ETH_PHY_BCR_RESET            (1U << 15)
#define STM32_ETH_PHY_BCR_SPEED_100        (1U << 13)
#define STM32_ETH_PHY_BCR_AUTONEG_ENABLE   (1U << 12)
#define STM32_ETH_PHY_BCR_POWER_DOWN       (1U << 11)
#define STM32_ETH_PHY_BCR_ISOLATE          (1U << 10)
#define STM32_ETH_PHY_BCR_RESTART_AUTONEG  (1U << 9)
#define STM32_ETH_PHY_BCR_FULL_DUPLEX      (1U << 8)

#define STM32_ETH_PHY_BSR_EXTENDED_CAP     (1U << 0)
#define STM32_ETH_PHY_BSR_LINK_STATUS      (1U << 2)
#define STM32_ETH_PHY_BSR_AUTONEG_COMPLETE (1U << 5)
#define STM32_ETH_PHY_BSR_10_HALF          (1U << 11)
#define STM32_ETH_PHY_BSR_10_FULL          (1U << 12)
#define STM32_ETH_PHY_BSR_100_HALF         (1U << 13)
#define STM32_ETH_PHY_BSR_100_FULL         (1U << 14)


#define STM32_ETH_PHY_ANAR_SELECTOR_8023   0x0001U
#define STM32_ETH_PHY_ANAR_CAP_10HD        0x0020U
#define STM32_ETH_PHY_ANAR_CAP_10FD        0x0040U
#define STM32_ETH_PHY_ANAR_CAP_100HD       0x0080U
#define STM32_ETH_PHY_ANAR_CAP_100FD       0x0100U
#define STM32_ETH_PHY_ANAR_DEFAULT (STM32_ETH_PHY_ANAR_SELECTOR_8023 | \
                                    STM32_ETH_PHY_ANAR_CAP_10HD |     \
                                    STM32_ETH_PHY_ANAR_CAP_10FD |     \
                                    STM32_ETH_PHY_ANAR_CAP_100HD |    \
                                    STM32_ETH_PHY_ANAR_CAP_100FD)

#define STM32_ETH_PHY_SCSR_10HD            (1U << 0)
#define STM32_ETH_PHY_SCSR_10FD            (1U << 1)
#define STM32_ETH_PHY_SCSR_100HD           (1U << 2)
#define STM32_ETH_PHY_SCSR_100FD           (1U << 3)

#define ETH_MACCR_LM               (1U << 12)

/* SBS (Security and Boot Services) registers needed for PHY interface selection */
#define SBS_BASE          0x44000400U
#define SBS_PMCR          (*(volatile uint32_t *)(SBS_BASE + 0x100U))
#define SBS_PMCR_ETH_SEL_PHY_MASK (0x7U << 21)
#define SBS_PMCR_ETH_SEL_PHY_RMII (0x4U << 21)

/* DMA descriptor format (Synopsys DWMAC4, normal descriptor) */
struct stm32_eth_dma_desc {
    volatile uint32_t des0;
    volatile uint32_t des1;
    volatile uint32_t des2;
    volatile uint32_t des3;
};

#define STM32_ETH_TDES3_OWN      (1U << 31)
#define STM32_ETH_TDES3_FD       (1U << 29)
#define STM32_ETH_TDES3_LD       (1U << 28)
#define STM32_ETH_TDES2_B1L_MASK (0x3FFFU)
#define STM32_ETH_TDES3_FL_MASK  (0x7FFFU)

#define STM32_ETH_RDES3_OWN      (1U << 31)
#define STM32_ETH_RDES3_BUF1V    (1U << 24)
#define STM32_ETH_RDES3_FS       (1U << 29)
#define STM32_ETH_RDES3_LS       (1U << 28)
#define STM32_ETH_RDES3_PL_MASK  (0x3FFFU)

#define STM32_ETH_RX_DESC_COUNT  4U
#define STM32_ETH_TX_DESC_COUNT  3U
#define STM32_ETH_RX_BUF_SIZE    LINK_MTU
#define STM32_ETH_TX_BUF_SIZE    LINK_MTU
#define STM32_ETH_FRAME_MIN_LEN  60U
#define STM32_ETH_DMA_TPBL       32U
#define STM32_ETH_DMA_RPBL       32U

#define STM32_ETH_USE_STATIC_IP  1
#define STM32_ETH_IP_ADDRESS     "192.168.12.11"
#define STM32_ETH_NETMASK        "255.255.255.0"
#define STM32_ETH_GATEWAY        "192.168.12.1"

static struct stm32_eth_dma_desc rx_ring[STM32_ETH_RX_DESC_COUNT] __attribute__((aligned(32),section(".eth_ring")));
static struct stm32_eth_dma_desc tx_ring[STM32_ETH_TX_DESC_COUNT] __attribute__((aligned(32),section(".eth_ring")));
static uint8_t rx_buffers[STM32_ETH_RX_DESC_COUNT][STM32_ETH_RX_BUF_SIZE] __attribute__((aligned(32),section(".eth_ring")));
static uint8_t tx_buffers[STM32_ETH_TX_DESC_COUNT][STM32_ETH_TX_BUF_SIZE] __attribute__((aligned(32),section(".eth_ring")));
/* Staging area keeps a CPU-owned copy once DMA hands ownership back. */
static uint8_t rx_staging_buffer[STM32_ETH_RX_BUF_SIZE] __attribute__((aligned(32),section(".eth_ring")));

/* Simple debug counters to probe for RX activity under GDB. */
volatile uint32_t stm32_eth_rx_debug_count;
volatile uint32_t stm32_eth_rx_debug_last_len;
volatile uint32_t stm32_eth_link_status_debug;
volatile uint16_t stm32_eth_phy_bsr_debug;
volatile uint16_t stm32_eth_phy_id1_debug;
volatile uint16_t stm32_eth_phy_id2_debug;
volatile uint16_t stm32_eth_phy_bcr_debug;
volatile uint32_t stm32_eth_tx_last_desc3_debug;
volatile uint32_t stm32_eth_tx_dma_status_debug;
volatile uint32_t stm32_eth_phy_debug40;
volatile uint32_t stm32_eth_phy_debug44;
volatile uint32_t stm32_eth_phy_debug48;

static uint32_t rx_idx;
static uint32_t tx_idx;
static mutex_t *tx_lock;
static int eth_initialized;
static int eth_driver_registered;
static int eth_module_registered;
static int32_t stm32_eth_phy_addr = -1;
static lan8742_Object_t lan8742_dev;
static int lan8742_initialized;

extern struct wolfIP *IPStack;

static uint16_t stm32_eth_mdio_read(uint32_t phy, uint32_t reg);
static void stm32_eth_mdio_write(uint32_t phy, uint32_t reg, uint16_t value);
static void stm32_eth_config_speed_duplex(void);
static void stm32_eth_phy_initialize(void);
static int32_t lan8742_io_read(uint32_t addr, uint32_t reg, uint32_t *value);
static int32_t lan8742_io_write(uint32_t addr, uint32_t reg, uint32_t value);
static int32_t lan8742_io_get_tick(void);

static void stm32_eth_delay(void)
{
    volatile uint32_t i;
    for (i = 0; i < 10000U; i++)
        __asm volatile ("nop");
}

static void stm32_eth_hw_reset(void)
{
    ETH_DMAMR |= ETH_DMAMR_SWR;
    while (ETH_DMAMR & ETH_DMAMR_SWR)
        ;
}

/* Trigger the DMA engine for the first transmitted packet.
   Writing any value to the Transmit Poll Demand Register forces the
   MAC to scan the descriptor list immediately. */
static inline void stm32_eth_trigger_tx(void)
{
    ETH_TPDR = 0U; /* value is irrelevant */
    __asm volatile ("dsb sy" ::: "memory");
}

static void stm32_eth_config_mac(const uint8_t mac[6])
{
    uint32_t maccr = ETH_MACCR;

    maccr &= ~(ETH_MACCR_DM | ETH_MACCR_FES);
    maccr |= ETH_MACCR_DM | ETH_MACCR_FES;
    ETH_MACCR = maccr;

    ETH_MACPFR = 0; /* Disable promiscuous, rely on perfect filtering */

    ETH_MACA0HR = ((uint32_t)mac[5] << 8) | (uint32_t)mac[4];
    ETH_MACA0LR = ((uint32_t)mac[3] << 24) |
                  ((uint32_t)mac[2] << 16) |
                  ((uint32_t)mac[1] << 8) |
                   (uint32_t)mac[0];
}

static void stm32_eth_config_speed_duplex(void)
{
    uint32_t maccr;
    uint16_t bsr;

    if (stm32_eth_phy_addr < 0)
        return;

    maccr = ETH_MACCR;
    maccr &= ~(ETH_MACCR_FES | ETH_MACCR_DM);

    if (lan8742_initialized) {
        int32_t state = LAN8742_GetLinkState(&lan8742_dev);
        stm32_eth_link_status_debug = (uint32_t)state;

        switch (state) {
        case LAN8742_STATUS_100MBITS_FULLDUPLEX:
            maccr |= ETH_MACCR_FES | ETH_MACCR_DM;
            break;
        case LAN8742_STATUS_100MBITS_HALFDUPLEX:
            maccr |= ETH_MACCR_FES;
            break;
        case LAN8742_STATUS_10MBITS_FULLDUPLEX:
            maccr |= ETH_MACCR_DM;
            break;
        default:
            break;
        }

        ETH_MACCR = maccr;
        stm32_eth_phy_bsr_debug =
            (uint16_t)stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, STM32_ETH_PHY_REG_BSR);
        return;
    }

    bsr = stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, STM32_ETH_PHY_REG_BSR);
    bsr |= stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, STM32_ETH_PHY_REG_BSR);

    if ((bsr & STM32_ETH_PHY_BSR_100_FULL) != 0U) {
        maccr |= ETH_MACCR_FES | ETH_MACCR_DM;
    } else if ((bsr & STM32_ETH_PHY_BSR_100_HALF) != 0U) {
        maccr |= ETH_MACCR_FES;
    } else if ((bsr & STM32_ETH_PHY_BSR_10_FULL) != 0U) {
        maccr |= ETH_MACCR_DM;
    }

    ETH_MACCR = maccr;

    stm32_eth_phy_bsr_debug = bsr;
    stm32_eth_link_status_debug = (uint32_t)bsr;
}

static void stm32_eth_config_mtl(void)
{
    uint32_t txqomr = ETH_MTLTXQOMR;
    uint32_t rxqomr = ETH_MTLRXQOMR;

    txqomr &= ~ETH_MTLTXQOMR_MASK;
    txqomr |= (ETH_MTLTXQOMR_TSF | ETH_MTLTXQOMR_TXQEN_ENABLE);
    ETH_MTLTXQOMR = txqomr;

    rxqomr &= ~ETH_MTLRXQOMR_MASK;
    rxqomr |= ETH_MTLRXQOMR_RSF;
    ETH_MTLRXQOMR = rxqomr;
}

static void stm32_eth_init_desc(void)
{
    uint32_t i;

    for (i = 0; i < STM32_ETH_TX_DESC_COUNT; i++) {
        tx_ring[i].des0 = (uint32_t)tx_buffers[i];
        tx_ring[i].des1 = 0;
        tx_ring[i].des2 = 0;
        tx_ring[i].des3 = 0;
    }

    for (i = 0; i < STM32_ETH_RX_DESC_COUNT; i++) {
        rx_ring[i].des0 = (uint32_t)rx_buffers[i];
        rx_ring[i].des1 = 0;
        rx_ring[i].des2 = 0;
        rx_ring[i].des3 = (STM32_ETH_RX_BUF_SIZE & STM32_ETH_RDES3_PL_MASK) |
                          STM32_ETH_RDES3_OWN |
                          STM32_ETH_RDES3_BUF1V;
    }

    rx_idx = 0;
    tx_idx = 0;

    stm32_eth_clean_dcache_range(tx_ring, sizeof(tx_ring));
    stm32_eth_clean_dcache_range(rx_ring, sizeof(rx_ring));
    stm32_eth_clean_dcache_range(tx_buffers, sizeof(tx_buffers));
    stm32_eth_invalidate_dcache_range(rx_buffers, sizeof(rx_buffers));

    ETH_DMACTXDLAR = (uint32_t)&tx_ring[0];
    ETH_DMACRXDLAR = (uint32_t)&rx_ring[0];

    ETH_DMACTXRLR = STM32_ETH_TX_DESC_COUNT - 1U;
    ETH_DMACRXRLR = STM32_ETH_RX_DESC_COUNT - 1U;

    ETH_DMACTXDTPR = (uint32_t)&tx_ring[0];
    ETH_DMACRXDTPR = (uint32_t)&rx_ring[STM32_ETH_RX_DESC_COUNT - 1U];
}

static void stm32_eth_config_dma(void)
{
    ETH_DMASBMR = ETH_DMASBMR_FB | ETH_DMASBMR_AAL;

    ETH_DMACRXCR = ((STM32_ETH_RX_BUF_SIZE & STM32_ETH_RDES3_PL_MASK) << ETH_DMACRXCR_RBSZ_SHIFT) |
                   ETH_DMACRXCR_RPBL(STM32_ETH_DMA_RPBL);

    ETH_DMACTXCR = ETH_DMACTXCR_OSF |
                   ETH_DMACTXCR_TPBL(STM32_ETH_DMA_TPBL);
}

static void stm32_eth_start(void)
{
    ETH_MACCR |= ETH_MACCR_TE | ETH_MACCR_RE;
    ETH_MTLTXQOMR |= ETH_MTLTXQOMR_FTQ;
    ETH_DMACTXCR |= ETH_DMACTXCR_ST;
    ETH_DMACRXCR |= ETH_DMACRXCR_SR;
}

static void stm32_eth_stop(void)
{
    ETH_DMACTXCR &= ~ETH_DMACTXCR_ST;
    ETH_DMACRXCR &= ~ETH_DMACRXCR_SR;
    ETH_MACCR &= ~ETH_MACCR_RE;
    ETH_MTLTXQOMR |= ETH_MTLTXQOMR_FTQ;
    ETH_MACCR &= ~ETH_MACCR_TE;
}

static void stm32_eth_mdio_wait_ready(void)
{
    uint32_t timeout = STM32_ETH_MDIO_TIMEOUT;

    while ((ETH_MACMDIOAR & ETH_MACMDIOAR_MB) != 0U && timeout != 0U)
        timeout--;
}

static uint16_t stm32_eth_mdio_read(uint32_t phy, uint32_t reg)
{
    uint32_t cfg;

    stm32_eth_mdio_wait_ready();

    cfg = (STM32_ETH_MDIO_CLOCK_RANGE << ETH_MACMDIOAR_CR_SHIFT) |
          (reg << ETH_MACMDIOAR_RDA_SHIFT) |
          (phy << ETH_MACMDIOAR_PA_SHIFT) |
          (ETH_MACMDIOAR_GOC_READ << ETH_MACMDIOAR_GOC_SHIFT);

    ETH_MACMDIOAR = cfg | ETH_MACMDIOAR_MB;
    stm32_eth_mdio_wait_ready();
    return (uint16_t)(ETH_MACMDIODR & 0xFFFFU);
}

static void stm32_eth_mdio_write(uint32_t phy, uint32_t reg, uint16_t value)
{
    uint32_t cfg;

    stm32_eth_mdio_wait_ready();

    ETH_MACMDIODR = (uint32_t)value;
    cfg = (STM32_ETH_MDIO_CLOCK_RANGE << ETH_MACMDIOAR_CR_SHIFT) |
          (reg << ETH_MACMDIOAR_RDA_SHIFT) |
          (phy << ETH_MACMDIOAR_PA_SHIFT) |
          (ETH_MACMDIOAR_GOC_WRITE << ETH_MACMDIOAR_GOC_SHIFT);

    ETH_MACMDIOAR = cfg | ETH_MACMDIOAR_MB;
    stm32_eth_mdio_wait_ready();
}

static int32_t lan8742_io_read(uint32_t addr, uint32_t reg, uint32_t *value)
{
    if (!value)
        return -1;
    *value = (uint32_t)stm32_eth_mdio_read(addr, reg);
    return 0;
}

static int32_t lan8742_io_write(uint32_t addr, uint32_t reg, uint32_t value)
{
    stm32_eth_mdio_write(addr, reg, (uint16_t)value);
    return 0;
}

static int32_t lan8742_io_get_tick(void)
{
    static uint32_t tick;
    return (int32_t)tick++;
}

static int32_t stm32_eth_detect_phy(void)
{
    uint32_t addr;

    for (addr = 0U; addr < 32U; addr++) {
        uint16_t id1 = stm32_eth_mdio_read(addr, STM32_ETH_PHY_REG_ID1);
        if (id1 != 0xFFFFU && id1 != 0x0000U) {
            uint16_t id2 = stm32_eth_mdio_read(addr, STM32_ETH_PHY_REG_ID1 + 1U);
            stm32_eth_phy_id1_debug = id1;
            stm32_eth_phy_id2_debug = id2;
            return (int32_t)addr;
        }
    }

    return -1;
}

static void stm32_eth_update_link_status(void)
{
    static uint32_t sample_divider;
    uint16_t status;

    if (stm32_eth_phy_addr < 0)
        return;

    sample_divider++;
    if ((sample_divider & 0xFFU) != 0U)
        return;

    status = stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, STM32_ETH_PHY_REG_BSR);
    stm32_eth_phy_bsr_debug = status;

    if (lan8742_initialized)
        stm32_eth_link_status_debug = (uint32_t)LAN8742_GetLinkState(&lan8742_dev);
    else
        stm32_eth_link_status_debug = (uint32_t)status;
}

/* Wait until the PHY reports a valid link status.
   Returns 0 on success, -ETIMEDOUT otherwise. */
static int wait_for_link(void)
{
    uint32_t timeout = 2000000U; /* Roughly 2 seconds */
    while (timeout--) {
        uint16_t bsr = stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr,
                                          STM32_ETH_PHY_REG_BSR);
        if (bsr & STM32_ETH_PHY_BSR_LINK_STATUS)
            return 0;
    }
    return -ETIMEDOUT;
}

#if 0
static void stm32_eth_generate_mac(uint8_t mac[6])
{
#define STM32_UID_BASE 0x08FFF800UL
    volatile uint32_t *uid = (volatile uint32_t *)(STM32_UID_BASE);
    uint32_t w0 = uid[0];
    uint32_t w1 = uid[1];
    uint32_t w2 = uid[2];

    mac[0] = 0x02; /* Locally administered, unicast */
    mac[1] = 0x80 | (uint8_t)(w2 >> 24);
    mac[2] = (uint8_t)(w2);
    mac[3] = (uint8_t)(w1 >> 8);
    mac[4] = (uint8_t)(w1);
    mac[5] = (uint8_t)(w0);
}
#else
static void stm32_eth_generate_mac(uint8_t mac[6])
{
    mac[0] = 0x00;
    mac[1] = 0x11;
    mac[2] = 0xAA;
    mac[3] = 0xBB;
    mac[4] = 0x22;
    mac[5] = 0x33;
}
#endif


static inline void stm32_eth_release_rx_desc(struct stm32_eth_dma_desc *desc)
{
    desc->des1 = 0;
    desc->des3 = (STM32_ETH_RX_BUF_SIZE & STM32_ETH_RDES3_PL_MASK) |
                 STM32_ETH_RDES3_OWN |
                 STM32_ETH_RDES3_BUF1V;
    stm32_eth_clean_dcache_range(desc, sizeof(*desc));
    __asm volatile ("dsb sy" ::: "memory");
    ETH_DMACRXDTPR = (uint32_t)desc;
}

static int stm32_eth_poll(struct wolfIP_ll_dev *dev, void *frame, uint32_t len)
{
    struct stm32_eth_dma_desc *desc;
    uint32_t status;
    uint32_t frame_len = 0;

    (void)dev;

    stm32_eth_update_link_status();

    desc = &rx_ring[rx_idx];
    stm32_eth_invalidate_dcache_range(desc, sizeof(*desc));

    if (desc->des3 & STM32_ETH_RDES3_OWN)
        return 0;

    status = desc->des3;

    if ((status & (STM32_ETH_RDES3_FS | STM32_ETH_RDES3_LS)) ==
            (STM32_ETH_RDES3_FS | STM32_ETH_RDES3_LS)) {
        frame_len = status & STM32_ETH_RDES3_PL_MASK;
        if (frame_len > len)
            frame_len = len;
        stm32_eth_invalidate_dcache_range(rx_buffers[rx_idx], frame_len);
        memcpy(rx_staging_buffer, rx_buffers[rx_idx], frame_len);
        memcpy(frame, rx_staging_buffer, frame_len);
        stm32_eth_rx_debug_last_len = frame_len;
        stm32_eth_rx_debug_count++;
    }

    stm32_eth_release_rx_desc(desc);
    rx_idx = (rx_idx + 1U) % STM32_ETH_RX_DESC_COUNT;

    return (int)frame_len;
}

static int stm32_eth_send(struct wolfIP_ll_dev *dev, void *frame, uint32_t len)
{
    struct stm32_eth_dma_desc *desc;
    uint32_t dma_len;
    uint32_t next_idx;
    uint32_t debug_val;

    (void)dev;

    if (len == 0 || len > STM32_ETH_TX_BUF_SIZE)
        return -EMSGSIZE;

    if (tx_lock)
        mutex_lock(tx_lock);

    desc = &tx_ring[tx_idx];
    stm32_eth_invalidate_dcache_range(desc, sizeof(*desc));

    if (desc->des3 & STM32_ETH_TDES3_OWN) {
        if (tx_lock)
            mutex_unlock(tx_lock);
        return -EAGAIN;
    }

    memcpy(tx_buffers[tx_idx], frame, len);
    dma_len = (len < STM32_ETH_FRAME_MIN_LEN) ? STM32_ETH_FRAME_MIN_LEN : len;
    if (dma_len > len)
        memset(tx_buffers[tx_idx] + len, 0, dma_len - len);
    desc->des0 = (uint32_t)tx_buffers[tx_idx];
    desc->des1 = 0;
    desc->des2 = (dma_len & STM32_ETH_TDES2_B1L_MASK);
    stm32_eth_clean_dcache_range(tx_buffers[tx_idx], dma_len);
    /* Ensure the payload is visible before we hand ownership to the DMA */
    __asm volatile ("dsb sy" ::: "memory");
    desc->des3 = (dma_len & STM32_ETH_TDES3_FL_MASK) |
                 STM32_ETH_TDES3_FD |
                 STM32_ETH_TDES3_LD |
                 STM32_ETH_TDES3_OWN;
    stm32_eth_clean_dcache_range(desc, sizeof(*desc));

    stm32_eth_tx_last_desc3_debug = desc->des3;
    stm32_eth_tx_dma_status_debug = ETH_DMACSR;

    __asm volatile ("dsb sy" ::: "memory");

    ETH_DMACSR = ETH_DMACSR_TBU;

    /* Kick the DMA on the very first transmission after init. */
    if (tx_idx == 0U)
        stm32_eth_trigger_tx();

    next_idx = (tx_idx + 1U) % STM32_ETH_TX_DESC_COUNT;
    ETH_DMACTXDTPR = (uint32_t)&tx_ring[next_idx];
    tx_idx = next_idx;

    debug_val = stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, 0x0011U);
    debug_val |= stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, 0x001BU) << 16;
    stm32_eth_phy_debug40 = debug_val;

    debug_val = stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, 0x0019U);
    debug_val |= stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, 0x001FU) << 16;
    stm32_eth_phy_debug44 = debug_val;

    debug_val = stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, 0x0010U);
    debug_val |= stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, 0x001AU) << 16;
    stm32_eth_phy_debug48 = debug_val;

    stm32_eth_tx_last_desc3_debug = desc->des3;
    stm32_eth_tx_dma_status_debug = ETH_DMACSR;

    if (tx_lock)
        mutex_unlock(tx_lock);

    return (int)len;
}

static void stm32_eth_phy_initialize(void)
{
    uint32_t timeout;
    uint16_t ctrl;
    uint16_t bsr;
    int32_t link_state;
    uint32_t id_val = 0U;
    uint32_t mcsr = 0U;

    if (!lan8742_initialized) {
        lan8742_IOCtx_t io_ctx = {
            .Init = NULL,
            .DeInit = NULL,
            .ReadReg = lan8742_io_read,
            .WriteReg = lan8742_io_write,
            .GetTick = lan8742_io_get_tick,
        };

        if ((LAN8742_RegisterBusIO(&lan8742_dev, &io_ctx) == LAN8742_STATUS_OK) &&
            (LAN8742_Init(&lan8742_dev) == LAN8742_STATUS_OK)) {
            lan8742_initialized = 1;
            stm32_eth_phy_addr = (int32_t)lan8742_dev.DevAddr;

            if (lan8742_io_read(lan8742_dev.DevAddr, LAN8742_PHYI1R, &id_val) == 0)
                stm32_eth_phy_id1_debug = (uint16_t)id_val;
            if (lan8742_io_read(lan8742_dev.DevAddr, LAN8742_PHYI2R, &id_val) == 0)
                stm32_eth_phy_id2_debug = (uint16_t)id_val;

            if (lan8742_io_read(lan8742_dev.DevAddr, LAN8742_MCSR, &mcsr) == 0) {
                if ((mcsr & LAN8742_MCSR_EDPWRDOWN) != 0U) {
                    lan8742_io_write(lan8742_dev.DevAddr, LAN8742_MCSR,
                                     mcsr & ~LAN8742_MCSR_EDPWRDOWN);
                }
            }

            LAN8742_StartAutoNego(&lan8742_dev);
        }
    }

    if (lan8742_initialized) {
        LAN8742_DisablePowerDownMode(&lan8742_dev);
        LAN8742_StartAutoNego(&lan8742_dev);

        timeout = STM32_ETH_MDIO_TIMEOUT;
        do {
            link_state = LAN8742_GetLinkState(&lan8742_dev);
            stm32_eth_link_status_debug = (uint32_t)link_state;
            if (link_state == LAN8742_STATUS_100MBITS_FULLDUPLEX ||
                link_state == LAN8742_STATUS_100MBITS_HALFDUPLEX ||
                link_state == LAN8742_STATUS_10MBITS_FULLDUPLEX ||
                link_state == LAN8742_STATUS_10MBITS_HALFDUPLEX) {
                break;
            }
        } while (--timeout != 0U);

        stm32_eth_phy_bsr_debug =
            (uint16_t)stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, STM32_ETH_PHY_REG_BSR);
        stm32_eth_phy_bcr_debug =
            (uint16_t)stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, STM32_ETH_PHY_REG_BCR);
        return;
    }

    if (stm32_eth_phy_addr < 0) {
        stm32_eth_phy_addr = stm32_eth_detect_phy();
        if (stm32_eth_phy_addr < 0)
            stm32_eth_phy_addr = 0;
    }

    if (stm32_eth_phy_addr >= 0) {
        stm32_eth_phy_id1_debug =
            stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, STM32_ETH_PHY_REG_ID1);
        stm32_eth_phy_id2_debug =
            stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, STM32_ETH_PHY_REG_ID1 + 1U);
    }

    stm32_eth_mdio_write((uint32_t)stm32_eth_phy_addr,
                         STM32_ETH_PHY_REG_BCR,
                         STM32_ETH_PHY_BCR_RESET);

    timeout = STM32_ETH_MDIO_TIMEOUT;
    do {
        ctrl = stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr,
                                   STM32_ETH_PHY_REG_BCR);
    } while ((ctrl & STM32_ETH_PHY_BCR_RESET) != 0U && --timeout != 0U);

    stm32_eth_phy_bcr_debug = ctrl;

    ctrl &= ~(STM32_ETH_PHY_BCR_POWER_DOWN |
              STM32_ETH_PHY_BCR_ISOLATE |
              STM32_ETH_PHY_BCR_SPEED_100 |
              STM32_ETH_PHY_BCR_FULL_DUPLEX);

    stm32_eth_mdio_write((uint32_t)stm32_eth_phy_addr,
                         STM32_ETH_PHY_REG_ANAR,
                         STM32_ETH_PHY_ANAR_DEFAULT);

    ctrl |= STM32_ETH_PHY_BCR_AUTONEG_ENABLE |
            STM32_ETH_PHY_BCR_RESTART_AUTONEG;
    stm32_eth_mdio_write((uint32_t)stm32_eth_phy_addr,
                         STM32_ETH_PHY_REG_BCR,
                         ctrl);

    timeout = STM32_ETH_MDIO_TIMEOUT;
    do {
        bsr = stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr,
                                  STM32_ETH_PHY_REG_BSR);
        bsr |= stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr,
                                   STM32_ETH_PHY_REG_BSR);
    } while ((bsr & STM32_ETH_PHY_BSR_AUTONEG_COMPLETE) == 0U &&
             --timeout != 0U);

    timeout = STM32_ETH_MDIO_TIMEOUT;
    do {
        bsr = stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr,
                                  STM32_ETH_PHY_REG_BSR);
        bsr |= stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr,
                                   STM32_ETH_PHY_REG_BSR);
    } while ((bsr & STM32_ETH_PHY_BSR_LINK_STATUS) == 0U &&
             --timeout != 0U);

    stm32_eth_phy_bsr_debug = bsr;
}

static void stm32_eth_ack_status(void)
{
    uint32_t status = ETH_DMACSR;

    if (status != 0)
        ETH_DMACSR = status;
}

void stm32_eth_enable_loopback(int enable)
{
    uint32_t maccr = ETH_MACCR;

    if (enable)
        maccr |= ETH_MACCR_LM | ETH_MACCR_RE | ETH_MACCR_TE;
    else
        maccr &= ~ETH_MACCR_LM;

    ETH_MACCR = maccr;
}

static int stm32_eth_attach(struct wolfIP *stack, struct wolfIP_ll_dev *ll, unsigned int if_idx)
{
    uint8_t mac[6];

    if (eth_initialized)
        return 0;

    stm32_eth_generate_mac(mac);

    memcpy(ll->mac, mac, sizeof(mac));
    strncpy(ll->ifname, "eth0", sizeof(ll->ifname) - 1);
    ll->ifname[sizeof(ll->ifname) - 1] = '\0';
    ll->poll = stm32_eth_poll;
    ll->send = stm32_eth_send;

    if (!tx_lock)
        tx_lock = mutex_init();

    stm32_eth_stop();
    /* PHY interface selection is handled by the supervisor; just reset MAC */
    stm32_eth_hw_reset();
    stm32_eth_config_mac(mac);
    stm32_eth_config_mtl();
    stm32_eth_init_desc();
    stm32_eth_config_dma();

    stm32_eth_phy_initialize();
    stm32_eth_config_speed_duplex();
    /* Wait for the PHY to report a live link before we enable TX */
    (void)wait_for_link();
    stm32_eth_ack_status();
    stm32_eth_start();
    stm32_eth_link_status_debug =
        (uint32_t)stm32_eth_mdio_read((uint32_t)stm32_eth_phy_addr, STM32_ETH_PHY_REG_BSR);

#if STM32_ETH_USE_STATIC_IP
    wolfIP_ipconfig_set_ex(stack,
                           if_idx,
                           atoip4(STM32_ETH_IP_ADDRESS),
                           atoip4(STM32_ETH_NETMASK),
                           atoip4(STM32_ETH_GATEWAY));
#else
    wolfIP_ipconfig_set_ex(stack, if_idx, 0, 0, 0);
    dhcp_client_init(stack);
#endif

    eth_initialized = 1;
    return 0;
}

static struct netdev_driver stm32_eth_driver = {
    .name = "stm32_eth",
    .is_present = NULL,
    .attach = stm32_eth_attach,
};

int ethernet_init(const struct eth_config *conf)
{
    int ret;

    (void)conf;

    if (!eth_module_registered) {
        register_module(&mod_deveth);
        eth_module_registered = 1;
    }

    if (eth_driver_registered)
        return 0;

    ret = netdev_register(&stm32_eth_driver);
    if (ret == 0)
        eth_driver_registered = 1;
    return ret;
}
