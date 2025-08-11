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
#include "tusb.h"
#include "frosted.h"
#include "device.h"
#include "pico.h"
#include "poll.h"
#include "cirbuf.h"
#include "locks.h"

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


static struct module mod_devttyusb = {
    .family = FAMILY_FILE,
    .name = "ttyUSB",
    .ops.open = device_open,
    .ops.read = ttyusb_read,
    .ops.poll = ttyusb_poll,
    .ops.write = ttyusb_write,
    .ops.tty_attach = ttyusb_tty_attach,
};

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
                    /* read data into circular buffer */
                    cirbuf_writebyte(u->inbuf, b);
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
    if (jiffies > last_poll + 1) {

#ifdef CONFIG_USB_POLLING
        tusb_int_handler(0, false);
#endif
        tud_task(); // tinyusb device task
        cdc_task();
        last_poll = jiffies;
    }
    tasklet_add(usb_tasklet, NULL);
}

void frosted_usbdev_init(void)
{
    reset_block(RESETS_RESET_USBCTRL_BITS);
    unreset_block_wait(RESETS_RESET_USBCTRL_BITS);
    // init device stack on configured roothub port
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
    ttyusb_init();
    tasklet_add(usb_tasklet, NULL);
}

// Invoked when device is mounted
void tud_mount_cb(void) {
    usb_connected = 1;
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
    usb_connected = 0;
}


// Invoked when cdc when line state changed e.g connected/disconnected
// Use to reset to DFU when disconnect with 1200 bps
void tud_cdc_line_state_cb(uint8_t instance, bool dtr, bool rts) {
  (void)rts;

#if 0
  // DTR = false is counted as disconnected
  if (!dtr) {
    // touch1200 only with first CDC instance (Serial)
    if (instance == 0) {
      cdc_line_coding_t coding;
      tud_cdc_get_line_coding(&coding);
      if (coding.bit_rate == 1200) {
        if (board_reset_to_bootloader) {
          board_reset_to_bootloader();
        }
      }
    }
  }
#endif
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
    if (!tud_cdc_n_connected(u->itf)) return;


    avail = tud_cdc_n_write_available(u->itf);
    while (avail && cirbuf_bytesinuse(u->outbuf)) {
        size_t chunk = cirbuf_bytesinuse(u->outbuf);   /* contiguous bytes available to read */
        if (chunk > avail) chunk = avail;

        /* temporary pointer to the linear span */
        uint8_t tmp[64];                 /* bounded copy is fine; loop if larger */
        if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
        cirbuf_readbytes(u->outbuf, tmp, chunk);

        uint32_t wrote = tud_cdc_n_write(u->itf, tmp, (uint32_t)chunk);
        (void)wrote; /* TinyUSB's CDC write is all-or-less, but we handle loop */
        avail = tud_cdc_n_write_available(u->itf);
    }

    tud_cdc_n_write_flush(u->itf);

}

void tud_cdc_tx_complete_cb(uint8_t itf)
{
    struct dev_ttyusb *u = &DEV_TTYUSB[itf];
    if (!u->dev) return;
    ttyusb_tx_drain(u);
}


static int ttyusb_write(struct fnode *fno, const void *buf, unsigned int len)
{
    struct dev_ttyusb *u;
    const uint8_t *p = buf;
    uint32_t written = 0;
    uint32_t pushed;

    if (!usb_connected)
        return len;

    u = (struct dev_ttyusb *)FNO_MOD_PRIV(fno, &mod_devttyusb);
    if (!u)
        return -1;

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
        mutex_unlock(ttyusb->dev->mutex);
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

int ttyusb_init(void)
{
    register_module(&mod_devttyusb);
    ttyusb_fno_init(0);
    ttyusb_fno_init(1);
    return 0;
}

