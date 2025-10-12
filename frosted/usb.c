/*
 *      This file is part of frostzone.
 *
 *      frostzone is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the Free Software Foundation.
 *
 *
 *      frostzone is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frostzone.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Authors: Daniele Lacamera
 *
 */

#if CONFIG_TUD_ENABLED
#include "tusb.h"
#include "frosted.h"
#include "device.h"
#include "poll.h"
#include "cirbuf.h"
#include "locks.h"
#include "nvic.h"
#include "sys/frosted-io.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if defined(TARGET_rp2350)
#include "pico.h"
#endif
#if defined(TARGET_stm32h563)
#include "stm32h5xx.h"
#define STM32_RCC_BASE RCC_BASE
#include "stm32x5_board_common.h"
#endif

int ttyusb_init(void);
#define MAX_TTYUSB_DEV 2
struct dev_ttyusb {
    struct device *dev;
    uint8_t itf;
    struct cirbuf *inbuf;
    struct cirbuf *outbuf;
    uint16_t sid;
    uint8_t *w_start;
    uint8_t *w_end;
};



static struct dev_ttyusb DEV_TTYUSB[2];
static volatile int usb_connected = 0;

static int ttyusb_read(struct fnode *fno, void *buf, size_t len);
static int ttyusb_write(struct fnode *fno, const void *buf, size_t len);
static int ttyusb_poll(struct fnode *fno, uint16_t events, uint16_t *revents);
static void ttyusb_tty_attach(struct fnode *fno, int pid);

static sem_t sem_usb;

#if defined(TARGET_stm32h563)
#define USB_KEEPALIVE_INTERVAL_MS 2U
static void usb_keepalive_timer_cb(uint32_t now, void *arg);

#define USB_IRQ_TRACE_LEN 16
struct usb_irq_record {
    uint32_t jif;
    uint32_t istr_before;
    uint32_t istr_after;
    uint32_t cntr;
    uint32_t chep0;
    uint32_t daddr;
};
static struct usb_irq_record usb_irq_trace[USB_IRQ_TRACE_LEN];
static volatile uint32_t usb_irq_trace_wr;
#endif

static struct module mod_devttyusb = {
    .family = FAMILY_FILE,
    .name = "usb_tty",
    .ops.open = device_open,
    .ops.read = ttyusb_read,
    .ops.poll = ttyusb_poll,
    .ops.write = ttyusb_write,
    .ops.tty_attach = ttyusb_tty_attach,
};


#if CONFIG_USB_NET && CONFIG_TCPIP
#include "net.h"

static struct module mod_devusbnet = {
    .family = FAMILY_NETDEV,
    .name = "usb_net",
};
#endif

uint32_t tusb_time_millis_api(void) {
    return jiffies;
}

static void ttyusb_send_break(void *arg)
{
    int *pid = (int *)(arg);
    if (pid)
        task_kill(*pid, 2);
}

static void cdc_task(void) {
    uint8_t itf;
    for (itf = 0; itf < CFG_TUD_CDC; itf++) {
        // connected() check for DTR bit
        // Most but not all terminal client set this when making connection
        // if ( tud_cdc_n_connected(itf) )
        {
            if (tud_cdc_n_available(itf)) {
                uint8_t buf[64];
                uint32_t count = tud_cdc_n_read(itf, buf, sizeof(buf));
                int i;
                struct dev_ttyusb *u = &DEV_TTYUSB[itf];
                if (count == 0)
                    continue;
                for (i = 0; i < count; i++) {
                    char b = ((char *)buf)[i];
                    /* Intercept ^C */
                    if (b == 3) {
                        if (u->sid > 1) {
                            tasklet_add(ttyusb_send_break, &u->sid);
                        }
                        continue;
                    }
                    mutex_lock(u->dev->mutex);
                    /* read data into circular buffer */
                    cirbuf_writebyte(u->inbuf, b);
                    mutex_unlock(u->dev->mutex);
                    /* If a process is attached, resume the process */
                    if (u->dev->task != NULL)
                        task_resume(u->dev->task);
                }
            }
        }
    }
}

void usb_tasklet(void *arg) {
    static uint32_t last_poll = 0;
    (void)arg;
    if (!tusb_inited())
        return;
    while(sem_trywait(&sem_usb) == 0) {
        tud_task(); // tinyusb device task
        tud_cdc_write_flush();
        cdc_task();
        last_poll = jiffies;
    }
    if (jiffies > last_poll + 500) {
        uint32_t disconn_time = jiffies;
        tud_task();
        tud_disconnect();
        while (jiffies < disconn_time + 20) {
            schedule();
        }
        tud_connect();
    }
    last_poll = jiffies;
}

#if defined(TARGET_stm32h563)
static void usb_keepalive_timer_cb(uint32_t now, void *arg)
{
    (void)now;
    (void)arg;

    if (tusb_inited()) {
        tusb_int_handler(0, false);
        sem_post(&sem_usb);
        tasklet_add(usb_tasklet, NULL);
    }

    ktimer_add(USB_KEEPALIVE_INTERVAL_MS, usb_keepalive_timer_cb, NULL);
}
#endif
#if defined(TARGET_rp2350)
#define USBCTRL_BASE 0x50110000
#define USB_MAIN *((volatile uint32_t *)(USBCTRL_BASE + 0x40))
#define USB_SIE_CTRL *((volatile uint32_t *)(USBCTRL_BASE + 0x4C))
#define USB_SIE_STATUS *((volatile uint32_t *)(USBCTRL_BASE + 0x50))
#define USB_INTE *((volatile uint32_t *)(USBCTRL_BASE + 0x90))
#define USB_INTS *((volatile uint32_t *)(USBCTRL_BASE + 0x98))

#define USB_MAIN_PHY_ISO (1 << 2)
#define USB_MAIN_EN (1 << 0)
#define USB_INTE_TRANS_COMPLETE (1 << 3)
#define USB_SIE_STATUS_TRANS_COMPLETE (1 << 18)
#define USB_SIE_CTRL_EP0_INT_1BUF (1 << 29)
#define USB_SIE_CTRL_PU_EN (1 << 16)

void rp2040_usb_init(void);
#endif
#if defined(TARGET_stm32h563)
#define STM32H5_TYPEC_ATTACH_TIMEOUT_MS 250U

static inline void stm32h5_typec_configure_analog(uint32_t base, uint8_t pin)
{
    stm32x5_gpio_write_mode(base, pin, GPIO_MODE_ANALOG);
    stm32x5_gpio_write_pull(base, pin, IOCTL_GPIO_PUPD_NONE);
    stm32x5_gpio_write_speed(base, pin, GPIO_SPEED_LOW);
}

static void stm32h5_typec_gpio_init(void)
{
    stm32h5_typec_configure_analog(GPIOB_BASE, 13U); /* CC1 */
    stm32h5_typec_configure_analog(GPIOB_BASE, 14U); /* CC2 */
    stm32h5_typec_configure_analog(GPIOA_BASE, 9U);  /* Dead-battery pin */
    stm32h5_typec_configure_analog(GPIOA_BASE, 4U);  /* VBUS sense */

    /* Fault indication pin idles high – keep a defined level while unused. */
    stm32x5_gpio_write_mode(GPIOG_BASE, 7U, GPIO_MODE_INPUT);
    stm32x5_gpio_write_pull(GPIOG_BASE, 7U, IOCTL_GPIO_PUPD_PULLUP);
    stm32x5_gpio_write_speed(GPIOG_BASE, 7U, GPIO_SPEED_LOW);
}

static int stm32h5_typec_wait_for_attach(uint32_t timeout_ms)
{
    uint32_t deadline = jiffies + timeout_ms;
    uint32_t last_sr = 0U;

    UCPD1->IMR = UCPD_IMR_TYPECEVT1IE | UCPD_IMR_TYPECEVT2IE;

    while (1) {
        uint32_t sr = UCPD1->SR;
        uint32_t v_cc1 = (sr & UCPD_SR_TYPEC_VSTATE_CC1_Msk) >> UCPD_SR_TYPEC_VSTATE_CC1_Pos;
        uint32_t v_cc2 = (sr & UCPD_SR_TYPEC_VSTATE_CC2_Msk) >> UCPD_SR_TYPEC_VSTATE_CC2_Pos;
        bool cc1_evt = (sr & UCPD_SR_TYPECEVT1) && (v_cc1 == 3U);
        bool cc2_evt = (sr & UCPD_SR_TYPECEVT2) && (v_cc2 == 3U);

        if (cc1_evt || cc2_evt) {
            uint32_t cr = UCPD1->CR;
            cr &= ~(UCPD_CR_PHYCCSEL | UCPD_CR_CCENABLE_BOTH);
            cr |= UCPD_CR_PHYRXEN;

            if (cc2_evt) {
                cr |= UCPD_CR_PHYCCSEL | UCPD_CR_CCENABLE_1;
            } else {
                cr |= UCPD_CR_CCENABLE_0;
            }

            UCPD1->CR = cr;
            UCPD1->ICR = UCPD_ICR_TYPECEVT1CF | UCPD_ICR_TYPECEVT2CF;
            return 1;
        }

        if ((int32_t)(deadline - jiffies) <= 0)
            return 0;

        if (sr != last_sr) {
            last_sr = sr;
            UCPD1->ICR = UCPD_ICR_TYPECEVT1CF | UCPD_ICR_TYPECEVT2CF;
        }

        schedule();
    }
}

static void stm32h5_typec_sink_enable(void)
{
    uint32_t cfg1;

    stm32h5_typec_gpio_init();

    RCC_APB1HENR |= RCC_APB1HENR_UCPDEN;
    RCC_APB1HRSTR |= RCC_APB1HRSTR_UCPDRST;
    RCC_APB1HRSTR &= ~RCC_APB1HRSTR_UCPDRST;

    cfg1 = (13U << UCPD_CFGR1_HBITCLKDIV_Pos) |
           (0x10U << UCPD_CFGR1_IFRGAP_Pos) |
           (0x07U << UCPD_CFGR1_TRANSWIN_Pos) |
           (0x01U << UCPD_CFGR1_PSC_Pos) |
           UCPD_CFGR1_RXORDSETEN_Msk;
    UCPD1->CFGR1 = cfg1 | UCPD_CFGR1_UCPDEN;

    /* Advertise Rd on both CC pins so the port-protection IC can request VBUS. */
    UCPD1->CR = UCPD_CR_ANAMODE | UCPD_CR_CCENABLE_BOTH;

    /* Hand over the CC resistors to the UCPD once it is configured. */
    PWR_UCPDR |= PWR_UCPDR_DBDIS;

    (void)stm32h5_typec_wait_for_attach(STM32H5_TYPEC_ATTACH_TIMEOUT_MS);
}

static void stm32h5_usb_hw_init(void)
{
    const struct gpio_config usb_pins[] = {
        {
            .base = GPIOA_BASE,
            .pin = 11,
            .mode = GPIO_MODE_AF,
            .pullupdown = IOCTL_GPIO_PUPD_NONE,
            .speed = GPIO_SPEED_HIGH,
            .optype = GPIO_OTYPE_PP,
            .af = 10,
        },
        {
            .base = GPIOA_BASE,
            .pin = 12,
            .mode = GPIO_MODE_AF,
            .pullupdown = IOCTL_GPIO_PUPD_NONE,
            .speed = GPIO_SPEED_HIGH,
            .optype = GPIO_OTYPE_PP,
            .af = 10,
        },
    };

    stm32h5_typec_sink_enable();

    RCC_CR |= RCC_CR_HSI48ON;
    while ((RCC_CR & RCC_CR_HSI48RDY) == 0)
        ;

    RCC_CCIPR4 = (RCC_CCIPR4 & ~RCC_CCIPR4_USBFSSEL_Msk) | RCC_CCIPR4_USBFSSEL_HSI48;

    /* Clock Recovery System keeps HSI48 in spec for USB without an external crystal. */
    RCC_APB1LENR |= RCC_APB1LENR_CRSEN;
    CRS_CR = 0;
    CRS_CFGR = ((47999U << CRS_CFGR_RELOAD_Pos) & CRS_CFGR_RELOAD_Msk) |
               ((34U << CRS_CFGR_FELIM_Pos) & CRS_CFGR_FELIM_Msk) |
               CRS_CFGR_SYNCSRC_USB;
    CRS_ICR = 0xFFFFFFFFU;
    CRS_CR = CRS_CR_AUTOTRIMEN | CRS_CR_CEN | (32U << CRS_CR_TRIM_Pos);

    PWR_USBSCR |= PWR_USBSCR_USB33DEN | PWR_USBSCR_USB33SV;
    while ((PWR_VMSR & PWR_VMSR_USB33RDY) == 0)
        ;

    RCC_APB2ENR |= RCC_APB2ENR_USBFSEN;
    RCC_APB2RSTR |= RCC_APB2RSTR_USBFSRST;
    RCC_APB2RSTR &= ~RCC_APB2RSTR_USBFSRST;

    stm32x5_gpio_config_alt(&usb_pins[0]);
    stm32x5_gpio_config_alt(&usb_pins[1]);
}
#endif

void frosted_usbdev_init(void)
{
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    sem_init(&sem_usb, 0);

#if defined(TARGET_rp2350)
    uint32_t now = jiffies;
    reset_block(RESETS_RESET_USBCTRL_BITS);
    while (jiffies < now + 10)
        schedule();
    unreset_block_wait(RESETS_RESET_USBCTRL_BITS);
    rp2040_usb_init();
#elif defined(TARGET_stm32h563)
    stm32h5_usb_hw_init();
#else
#error "USB device init not defined for this target"
#endif

    tusb_init(BOARD_TUD_RHPORT, &dev_init);
    ttyusb_init();
    tud_connect();

    /* Prime control endpoint before the host issues the first SETUP. */
    tusb_int_handler(BOARD_TUD_RHPORT, false);
    tud_task();

#ifndef CONFIG_USB_POLLING
#if defined(TARGET_rp2350)
    nvic_set_pending(USBCTRL_IRQ);
    nvic_set_priority(USBCTRL_IRQ, 1 << 5);
    nvic_enable_irq(USBCTRL_IRQ);
#elif defined(TARGET_stm32h563)
    nvic_set_priority(USB_DRD_FS_IRQn, 1 << 5);
    nvic_enable_irq(USB_DRD_FS_IRQn);
#endif
#endif

#if defined(TARGET_stm32h563)
    ktimer_add(USB_KEEPALIVE_INTERVAL_MS, usb_keepalive_timer_cb, NULL);
#endif
}

void tud_mount_cb(void) {
}

void tud_umount_cb(void) {
}

// Invoked when cdc when line state changed e.g connected/disconnected
// Use to reset to DFU when disconnect with 1200 bps
void tud_cdc_line_state_cb(uint8_t instance, bool dtr, bool rts) {
  (void)rts;
  if (!dtr)
      usb_connected = 0;
  else
      usb_connected = 1;
}

static void ttyusb_tty_attach(struct fnode *fno, int pid)
{
    struct dev_ttyusb *ttyusb = (struct dev_ttyusb *)FNO_MOD_PRIV(fno, &mod_devttyusb);
    if (ttyusb->sid != pid) {
        //kprintf("/dev/%s active job pid: %d\r\n", fno->fname, pid);
        ttyusb->sid = pid;
    }
}

static inline int usb_tx_ready(struct dev_ttyusb *u)
{
    /* Connected and has FIFO room? (TinyUSB returns #bytes free) */
    return tud_cdc_n_connected(u->itf) && tud_cdc_n_write_available(u->itf) > 0;
}

static void ttyusb_tx_drain(struct dev_ttyusb *u)
{
    uint32_t avail;
    if (!tud_cdc_n_connected(u->itf)) {
        tud_connect();
        return;
    }

    avail = tud_cdc_n_write_available(u->itf);
    while (avail && cirbuf_bytesinuse(u->outbuf)) {
        uint32_t chunk, wrote;
        uint8_t tmp[64];                 /* bounded copy is fine; loop if larger */

        mutex_lock(u->dev->mutex);
        chunk = cirbuf_bytesinuse(u->outbuf);   /* contiguous bytes available to read */
        if (chunk > avail)
            chunk = avail;

        /* temporary pointer to the linear span */
        if (chunk > sizeof(tmp)) 
            chunk = sizeof(tmp);
        cirbuf_readbytes(u->outbuf, tmp, chunk);
        mutex_unlock(u->dev->mutex);

        wrote = tud_cdc_n_write(u->itf, tmp, (uint32_t)chunk);
        (void)wrote; /* TinyUSB's CDC write is all-or-less, but we handle loop */
        avail = tud_cdc_n_write_available(u->itf);
    }
    sem_post(&sem_usb);
    tud_cdc_n_write_flush(u->itf);
}

void tud_cdc_tx_complete_cb(uint8_t itf)
{
    struct dev_ttyusb *u = &DEV_TTYUSB[itf];
    if (!u->dev)
        return;
    ttyusb_tx_drain(u);
}


static int ttyusb_write(struct fnode *fno, const void *buf, unsigned int len)
{
    struct dev_ttyusb *u;
    const uint8_t *p = buf;
    uint32_t written = 0;
    uint32_t pushed;
    if (!usb_connected) {
        tud_connect();
        return len;
    }
    u = (struct dev_ttyusb *)FNO_MOD_PRIV(fno, &mod_devttyusb);
    if (!u)
        return -1;
    mutex_lock(u->dev->mutex);
    /* Fast path: if ready and no backlog, try direct */
    if (usb_tx_ready(u) && cirbuf_bytesinuse(u->outbuf) == 0) {
        uint32_t avail = tud_cdc_n_write_available(u->itf);
        uint32_t n = (len < avail) ? len : avail;
        if (n) {
            uint32_t w = tud_cdc_n_write(u->itf, p, (uint32_t)n);
            (void)w;
            tud_cdc_n_write_flush(u->itf);
            p += n; written += n; len -= n;
        }
    }

    /* Enqueue the rest into the ring */
    pushed = cirbuf_writebytes(u->outbuf, p, len);
    written += pushed;

    /* Try to kick out what we just queued (if the bus is ready) */
    if (usb_tx_ready(u))
        ttyusb_tx_drain(u);
    mutex_unlock(u->dev->mutex);
    return (int)written;
}

static int ttyusb_read(struct fnode *fno, void *buf, unsigned int len)
{
    int out;
    volatile int len_available;
    char *ptr = (char *)buf;
    struct dev_ttyusb *ttyusb;

    if (len <= 0)
        return len;

    ttyusb = (struct dev_ttyusb *)FNO_MOD_PRIV(fno, &mod_devttyusb);
    if (!ttyusb)
        return -1;

    mutex_lock(ttyusb->dev->mutex);
    len_available =  cirbuf_bytesinuse(ttyusb->inbuf);
    if (len_available <= 0) {
        ttyusb->dev->task = this_task();
        task_suspend();
        out = SYS_CALL_AGAIN;
        goto again;
    }

    if (len_available < len)
        len = len_available;

    for(out = 0; out < len; out++) {
        /* read data */
        if (cirbuf_readbyte(ttyusb->inbuf, ptr) != 0)
            break;
        ptr++;
    }

again:
    mutex_unlock(ttyusb->dev->mutex);
    return out;
}

static int ttyusb_poll(struct fnode *fno, uint16_t events, uint16_t *revents)
{
    int ret = 0;
    struct dev_ttyusb *ttyusb;

    ttyusb = (struct dev_ttyusb *)FNO_MOD_PRIV(fno, &mod_devttyusb);
    if (!ttyusb)
        return -1;

    ttyusb->dev->task = this_task();
    mutex_lock(ttyusb->dev->mutex);
    if ((events & POLLOUT) && (tud_cdc_n_write_available(ttyusb->itf) > 0)) {
        *revents |= POLLOUT;
        ret = 1;
    }
    if ((events == POLLIN) && (cirbuf_bytesinuse(ttyusb->inbuf) > 0)) {
        *revents |= POLLIN;
        ret = 1;
    }
    mutex_unlock(ttyusb->dev->mutex);
    return ret;
}

static int ttyusb_fno_init(uint8_t itf)
{
    static int num_ttys = 0;
    char name[8] = "ttyUSB";
    struct fnode *devfs = fno_search("/dev");
    struct dev_ttyusb *u;

    if (itf >= MAX_TTYUSB_DEV)
        return -ENODEV;

    u = &DEV_TTYUSB[itf];
    if (!devfs)
        return -ENOENT;
    memset(u, 0, sizeof(struct dev_ttyusb));
    name[6] =  '0' + itf;
    u->itf = itf;
    u->dev = device_fno_init(&mod_devttyusb, name, devfs, FL_TTY, u);
    u->inbuf = cirbuf_create(256);
    u->outbuf = cirbuf_create(512);
    u->dev->task = NULL;

    return 0;
}

void usb_irq_handler(void)
{
#if defined(TARGET_stm32h563)
    uint32_t sticky_istr;
    uint32_t trace_idx = usb_irq_trace_wr;
    if (trace_idx < USB_IRQ_TRACE_LEN) {
        usb_irq_trace[trace_idx].jif = jiffies;
        usb_irq_trace[trace_idx].istr_before = USB_DRD_FS->ISTR;
        usb_irq_trace[trace_idx].cntr = USB_DRD_FS->CNTR;
        usb_irq_trace[trace_idx].chep0 = USB_DRD_FS->CHEP[0];
        usb_irq_trace[trace_idx].daddr = USB_DRD_FS->DADDR;
    }
#endif
    //dcd_int_handler(0);
    tusb_int_handler(0, true);
#if defined(TARGET_stm32h563)
    if (trace_idx < USB_IRQ_TRACE_LEN) {
        usb_irq_trace[trace_idx].istr_after = USB_DRD_FS->ISTR;
        usb_irq_trace_wr = trace_idx + 1;
    }
    /* TinyUSB ignores SOF/ESOF when no listeners are registered.
     * Ack them here so the IRQ line deasserts and higher-priority
     * USB events can keep flowing.
     */
    sticky_istr = USB_DRD_FS->ISTR & (USB_ISTR_SOF | USB_ISTR_ESOF);
    if (sticky_istr) {
        USB_DRD_FS->ISTR = (uint32_t)~sticky_istr;
    }
#endif
    sem_post(&sem_usb);
    tasklet_add(usb_tasklet, NULL);
    //asm volatile("sev");
}


int ttyusb_init(void)
{

    register_module(&mod_devttyusb);
    ttyusb_fno_init(0);
    ttyusb_fno_init(1);
    return 0;
}


#if CONFIG_USB_NET && CONFIG_TCPIP
#include "wolfip.h"

/* Two static buffers for RX frames from USB host */
__attribute__((section(".usb_tud"))) uint8_t tusb_net_rxbuf[LINK_MTU][2];
uint8_t tusb_net_rxbuf_used[2] =  {0, 0};

/* Two static buffers for TX frames to USB host */
__attribute__((section(".usb_tud"))) uint8_t tusb_net_txbuf[LINK_MTU][4];
uint16_t tusb_net_txbuf_sz[4] = {0, 0, 0, 0};

static int ll_usb_send(struct wolfIP_ll_dev *dev, void *frame, uint32_t sz);
int ll_usb_poll(struct wolfIP_ll_dev *dev, void *frame, uint32_t sz);

static int usb_netdev_attach(struct wolfIP *stack, struct wolfIP_ll_dev *ll, unsigned int if_idx)
{
    const uint8_t usb_macaddr[6] = { 0x02, 0x02, 0x84, 0x6A, 0x96, 0x07 };

    memcpy(ll->mac, usb_macaddr, sizeof(usb_macaddr));
    strncpy(ll->ifname, "usb", sizeof(ll->ifname) - 1);
    ll->ifname[sizeof(ll->ifname) - 1] = '\0';
    ll->poll = ll_usb_poll;
    ll->send = ll_usb_send;

    wolfIP_ipconfig_set_ex(stack, if_idx,
                           atoip4("192.168.7.2"),
                           atoip4("255.255.255.0"),
                           atoip4("192.168.7.1"));
    return 0;
}

static struct netdev_driver usb_net_driver = {
    .name = "usb_net",
    .is_present = NULL,
    .attach = usb_netdev_attach,
};


static int ll_usb_send(struct wolfIP_ll_dev *dev, void *frame, uint32_t sz) {
    uint16_t sz16 = (uint16_t)sz;
    uint32_t i;
    (void) dev;
    for (;;) {
        if (!tud_ready()) {
            sem_post(&sem_usb);
            return 0;
        }
        if (tud_network_can_xmit(sz16)) {
            for (i = 0; i < 4; i++) {
                if (tusb_net_txbuf_sz[i] == 0) {
                    memcpy(tusb_net_txbuf[i], frame, sz16);
                    tusb_net_txbuf_sz[i] = sz16;
                    tud_network_xmit(tusb_net_txbuf[i], tusb_net_txbuf_sz[i]);
                    return (int)sz16;
                }
            }
        }
        tusb_int_handler(0, false);
        tud_task();
    }
}

/* This is the callback that TinyUSB calls when it is ready to send a frame.
 * This is where the write operation is finalized.
 */
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    uint16_t ret = arg;
    (void) ref;
    (void) arg;
    memcpy(dst, ref, arg);
    if (ref == tusb_net_rxbuf[0])
        tusb_net_txbuf_sz[0] = 0;
    else if (ref == tusb_net_txbuf[1])
        tusb_net_txbuf_sz[1] = 0;
    else if (ref == tusb_net_txbuf[2])
        tusb_net_txbuf_sz[2] = 0;
    else if (ref == tusb_net_txbuf[3])
        tusb_net_txbuf_sz[3] = 0;
    sem_post(&sem_usb);
    return ret;
}

/* This is the callback that TinyUSB calls when it is ready to receive a frame.
 * This is where the read operation is initiated, the frame is copied to the
 * static buffer, and the buffer is marked as used.
 */

static void tusb_net_push_rx(const uint8_t *src, uint16_t size) {
    uint8_t *dst = NULL;
    int i;
    for (i = 0; i < 2; i++) {
        if (!tusb_net_rxbuf_used[i]) {
            dst = tusb_net_rxbuf[i];
            break;
        }
    }
    if (dst) {
        memcpy(dst, src, size);
        tusb_net_rxbuf_used[i] = 1;
    }
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    tusb_net_push_rx(src, size);
    tud_network_recv_renew();
    return true;
}

/* This is the poll function of the wolfIP device driver.
 * It is called by the wolfIP stack when it is ready to receive a frame.
 * It will return the number of bytes received, or 0 if no frame is available.
 *
 * Frames copied in tusb_net_push_rx are processed here and sent to the stack.
 */
int  ll_usb_poll(struct wolfIP_ll_dev *dev, void *frame, uint32_t sz) {
    int i;
    (void) dev;
    if (sz < 64)
        return 0;
    for (i = 0; i < 2; i++) {
        if (tusb_net_rxbuf_used[i]) {
            memcpy(frame, tusb_net_rxbuf[i], sz);
            tusb_net_rxbuf_used[i] = 0;
            return (int)sz;
        }
    }
    return 0;
}


int netusb_init(void) {
    register_module(&mod_devusbnet);
    return netdev_register(&usb_net_driver);
}

#else
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    return arg;
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    return true;
}



#endif

void tud_network_init_cb(void)
{
}


int secure_getrandom(void *buf, unsigned size);
uint32_t wolfIP_getrandom(void);

#endif /* CONFIG_TUD_ENABLED */
