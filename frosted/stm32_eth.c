#include "frosted.h"
#include "eth.h"
#include "net.h"
#include "socket_in.h"
#include "wolfip.h"
#include "locks.h"
#include <string.h>
#include <stdint.h>

#define ETH_BASE            0x40028000UL
#define ETH_REG(offset)     (*(volatile uint32_t *)(ETH_BASE + (offset)))

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

/* MTL bits */
#define ETH_MTLTXQOMR_TSF   (1U << 1)
#define ETH_MTLTXQOMR_TQS_SHIFT 16
#define ETH_MTLRXQOMR_RSF   (1U << 5)
#define ETH_MTLRXQOMR_FEP   (1U << 4)

/* DMA descriptor format (Synopsys DWMAC4, normal descriptor) */
struct stm32_eth_dma_desc {
    uint32_t des0;
    uint32_t des1;
    uint32_t des2;
    uint32_t des3;
};

#define STM32_ETH_TDES3_OWN      (1U << 31)
#define STM32_ETH_TDES3_FD       (1U << 29)
#define STM32_ETH_TDES3_LD       (1U << 28)
#define STM32_ETH_TDES3_BUF1V    (1U << 24)
#define STM32_ETH_TDES3_FL_MASK  (0x3FFFU)

#define STM32_ETH_RDES1_OWN      (1U << 31)
#define STM32_ETH_RDES1_BUF1V    (1U << 24)
#define STM32_ETH_RDES3_FS       (1U << 29)
#define STM32_ETH_RDES3_LS       (1U << 28)
#define STM32_ETH_RDES3_PL_MASK  (0x3FFFU)

#define STM32_ETH_RX_DESC_COUNT  8U
#define STM32_ETH_TX_DESC_COUNT  4U
#define STM32_ETH_RX_BUF_SIZE    LINK_MTU
#define STM32_ETH_TX_BUF_SIZE    LINK_MTU

static struct stm32_eth_dma_desc rx_ring[STM32_ETH_RX_DESC_COUNT] __attribute__((aligned(16)));
static struct stm32_eth_dma_desc tx_ring[STM32_ETH_TX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t rx_buffers[STM32_ETH_RX_DESC_COUNT][STM32_ETH_RX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t tx_buffers[STM32_ETH_TX_DESC_COUNT][STM32_ETH_TX_BUF_SIZE] __attribute__((aligned(4)));

static uint32_t rx_idx;
static uint32_t tx_idx;
static mutex_t *tx_lock;
static int eth_initialized;

extern struct wolfIP *IPStack;

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

static void stm32_eth_config_mac(const uint8_t mac[6])
{
    ETH_MACCR = ETH_MACCR_DM | ETH_MACCR_FES;
    ETH_MACPFR = 0; /* Disable promiscuous, rely on perfect filtering */

    ETH_MACA0HR = ((uint32_t)mac[5] << 8) | (uint32_t)mac[4];
    ETH_MACA0LR = ((uint32_t)mac[3] << 24) |
                  ((uint32_t)mac[2] << 16) |
                  ((uint32_t)mac[1] << 8) |
                   (uint32_t)mac[0];
}

static void stm32_eth_config_mtl(void)
{
    ETH_MTLTXQOMR = ETH_MTLTXQOMR_TSF | (7U << ETH_MTLTXQOMR_TQS_SHIFT);
    ETH_MTLRXQOMR = ETH_MTLRXQOMR_RSF | ETH_MTLRXQOMR_FEP;
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
        rx_ring[i].des1 = STM32_ETH_RDES1_OWN | STM32_ETH_RDES1_BUF1V;
        rx_ring[i].des2 = 0;
        rx_ring[i].des3 = 0;
    }

    rx_idx = 0;
    tx_idx = 0;

    ETH_DMACTXDLAR = (uint32_t)&tx_ring[0];
    ETH_DMACRXDLAR = (uint32_t)&rx_ring[0];

    ETH_DMACTXRLR = STM32_ETH_TX_DESC_COUNT - 1U;
    ETH_DMACRXRLR = STM32_ETH_RX_DESC_COUNT - 1U;

    ETH_DMACTXDTPR = (uint32_t)&tx_ring[STM32_ETH_TX_DESC_COUNT - 1U];
    ETH_DMACRXDTPR = (uint32_t)&rx_ring[STM32_ETH_RX_DESC_COUNT - 1U];
}

static void stm32_eth_config_dma(void)
{
    ETH_DMASBMR = ETH_DMASBMR_AAL | ETH_DMASBMR_FB;

    ETH_DMACRXCR = ((STM32_ETH_RX_BUF_SIZE & STM32_ETH_RDES3_PL_MASK) << ETH_DMACRXCR_RBSZ_SHIFT) |
                   ETH_DMACRXCR_SR;

    ETH_DMACTXCR = ETH_DMACTXCR_OSF | (4U << 16) | ETH_DMACTXCR_ST;
}

static void stm32_eth_start(void)
{
    ETH_MACCR |= ETH_MACCR_RE | ETH_MACCR_TE;
}

static void stm32_eth_stop(void)
{
    ETH_MACCR &= ~(ETH_MACCR_RE | ETH_MACCR_TE);
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
    desc->des1 = STM32_ETH_RDES1_OWN | STM32_ETH_RDES1_BUF1V;
    desc->des3 = 0;
    __asm volatile ("dmb" ::: "memory");
    ETH_DMACRXDTPR = (uint32_t)desc;
}

static int stm32_eth_poll(struct ll *dev, void *frame, uint32_t len)
{
    struct stm32_eth_dma_desc *desc;
    uint32_t status;
    uint32_t frame_len = 0;

    (void)dev;

    desc = &rx_ring[rx_idx];

    if (desc->des1 & STM32_ETH_RDES1_OWN)
        return 0;

    status = desc->des3;

    if ((status & (STM32_ETH_RDES3_FS | STM32_ETH_RDES3_LS)) ==
            (STM32_ETH_RDES3_FS | STM32_ETH_RDES3_LS)) {
        frame_len = status & STM32_ETH_RDES3_PL_MASK;
        if (frame_len > len)
            frame_len = len;
        memcpy(frame, rx_buffers[rx_idx], frame_len);
    }

    stm32_eth_release_rx_desc(desc);
    rx_idx = (rx_idx + 1U) % STM32_ETH_RX_DESC_COUNT;

    return (int)frame_len;
}

static int stm32_eth_send(struct ll *dev, void *frame, uint32_t len)
{
    struct stm32_eth_dma_desc *desc;

    (void)dev;

    if (len == 0 || len > STM32_ETH_TX_BUF_SIZE)
        return -EMSGSIZE;

    if (tx_lock)
        mutex_lock(tx_lock);

    desc = &tx_ring[tx_idx];

    if (desc->des3 & STM32_ETH_TDES3_OWN) {
        if (tx_lock)
            mutex_unlock(tx_lock);
        return 0;
    }

    memcpy(tx_buffers[tx_idx], frame, len);

    desc->des0 = (uint32_t)tx_buffers[tx_idx];
    desc->des1 = 0;
    desc->des2 = 0;
    desc->des3 = (len & STM32_ETH_TDES3_FL_MASK) |
                 STM32_ETH_TDES3_FD |
                 STM32_ETH_TDES3_LD |
                 STM32_ETH_TDES3_BUF1V |
                 STM32_ETH_TDES3_OWN;

    __asm volatile ("dmb" ::: "memory");

    ETH_DMACTXDTPR = (uint32_t)desc;
    tx_idx = (tx_idx + 1U) % STM32_ETH_TX_DESC_COUNT;

    if (tx_lock)
        mutex_unlock(tx_lock);

    return (int)len;
}

static void stm32_eth_ack_status(void)
{
    uint32_t status = ETH_DMACSR;

    if (status != 0)
        ETH_DMACSR = status;
}

int ethernet_init(const struct eth_config *conf)
{
    struct ll *ll_dev;
    uint8_t mac[6];

    (void)conf;

    if (eth_initialized)
        return 0;

    stm32_eth_generate_mac(mac);

    socket_in_init();

    ll_dev = wolfIP_getdev(IPStack);
    memcpy(ll_dev->mac, mac, sizeof(mac));
    strcpy(ll_dev->ifname, "eth0");
    ll_dev->poll = stm32_eth_poll;
    ll_dev->send = stm32_eth_send;

    if (!tx_lock)
        tx_lock = mutex_init();

    stm32_eth_stop();
    stm32_eth_hw_reset();
    stm32_eth_config_mac(mac);
    stm32_eth_config_mtl();
    stm32_eth_init_desc();
    stm32_eth_config_dma();
    stm32_eth_ack_status();
    stm32_eth_start();

    wolfIP_ipconfig_set(IPStack, 0, 0, 0);
    dhcp_client_init(IPStack);

    eth_initialized = 1;
    return 0;
}

