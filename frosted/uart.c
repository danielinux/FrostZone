#include "uart.h"

#ifdef CONFIG_DEVUART

#include "cirbuf.h"
#include "device.h"
#include "errno.h"
#include "fcntl.h"
#include "locks.h"
#include "nvic.h"
#include "poll.h"
#include "string.h"
#include "sys/frosted-io.h"

#define USART1_BASE 0x40013800UL
#define USART2_BASE 0x40004400UL
#define USART3_BASE 0x40004800UL
#define UART4_BASE  0x40004C00UL
#define UART5_BASE  0x40005000UL

#if defined(TARGET_stm32h563)
#define STM32_RCC_BASE 0x44020C00UL
#elif defined(TARGET_stm32u585)
#define STM32_RCC_BASE 0x46020C00UL
#else
#define STM32_RCC_BASE 0x46020C00UL
#endif

#include "stm32x5_board_common.h"

#define USART_CR1_UE        (1U << 0)
#define USART_CR1_RE        (1U << 2)
#define USART_CR1_TE        (1U << 3)
#define USART_CR1_IDLEIE    (1U << 4)
#define USART_CR1_RXFNEIE   (1U << 5)
#define USART_CR1_TXFNFIE   (1U << 7)
#define USART_CR1_FIFOEN    (1U << 29)

#define USART_CR3_OVRDIS    (1U << 12)

#define USART_ISR_PE        (1U << 0)
#define USART_ISR_FE        (1U << 1)
#define USART_ISR_NE        (1U << 2)
#define USART_ISR_ORE       (1U << 3)
#define USART_ISR_IDLE      (1U << 4)
#define USART_ISR_RXFNE     (1U << 5)
#define USART_ISR_TC        (1U << 6)
#define USART_ISR_TXFNF     (1U << 7)

#define USART_ICR_PECF      (1U << 0)
#define USART_ICR_FECF      (1U << 1)
#define USART_ICR_NECF      (1U << 2)
#define USART_ICR_ORECF     (1U << 3)
#define USART_ICR_IDLECF    (1U << 4)
#define USART_ICR_TCCF      (1U << 6)

#define UART_RX_BUFFER_SIZE 256

typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t BRR;
    volatile uint32_t GTPR;
    volatile uint32_t RTOR;
    volatile uint32_t RQR;
    volatile uint32_t ISR;
    volatile uint32_t ICR;
    volatile uint32_t RDR;
    volatile uint32_t TDR;
    volatile uint32_t PRESC;
} stm32_usart_t;

#define MAX_UART_PORTS 4

struct stm32_uart_port {
    stm32_usart_t *regs;
    struct device *dev;
    struct cirbuf *rxbuf;
    uint8_t irq;
    int8_t sid;
};

static struct stm32_uart_port uart_ports[MAX_UART_PORTS];
static void stm32_uart_break_tasklet(void *arg);

extern uint32_t SystemCoreClock;

static struct module mod_devuart = {
    .family = FAMILY_DEV,
    .name = "uart",
};

static inline void stm32_gpio_config_alt(const struct gpio_config *cfg)
{
    stm32x5_gpio_config_alt(cfg);
}

static inline void stm32_uart_disable_rx_irq(struct stm32_uart_port *port)
{
    port->regs->CR1 &= ~USART_CR1_RXFNEIE;
}

static inline void stm32_uart_enable_rx_irq(struct stm32_uart_port *port)
{
    port->regs->CR1 |= USART_CR1_RXFNEIE;
}

static void stm32_uart_clear_errors(stm32_usart_t *regs, uint32_t isr)
{
    uint32_t icr = 0;

    if (isr & USART_ISR_IDLE)
        icr |= USART_ICR_IDLECF;
    if (isr & USART_ISR_ORE)
        icr |= USART_ICR_ORECF;
    if (isr & USART_ISR_NE)
        icr |= USART_ICR_NECF;
    if (isr & USART_ISR_FE)
        icr |= USART_ICR_FECF;
    if (isr & USART_ISR_PE)
        icr |= USART_ICR_PECF;
    if (isr & USART_ISR_TC)
        icr |= USART_ICR_TCCF;

    if (icr)
        regs->ICR = icr;
}

static void stm32_uart_irq_handler(struct stm32_uart_port *port)
{
    uint32_t isr;
    struct task *waiting;

    if (!port || !port->regs)
        return;

    isr = port->regs->ISR;

    if (isr & USART_ISR_RXFNE) {
        uint8_t data = (uint8_t)port->regs->RDR;

        if (cirbuf_writebyte(port->rxbuf, data) == 0) {
            waiting = port->dev->task;
            if (waiting) {
                port->dev->task = NULL;
                task_resume(waiting);
            }
            if (data == 0x03 && port->sid > 1) {
                tasklet_add(stm32_uart_break_tasklet, port);
            }
        }
    } else {
        stm32_uart_clear_errors(port->regs, isr);
    }
}

void usart2_irq_handler(void);

// IRQ handlers for USART peripherals follow camelCase naming as per coding style.
void usart2_irq_handler(void)
{
    stm32_uart_irq_handler(&uart_ports[0]);
}

void usart3_irq_handler(void)
{
    stm32_uart_irq_handler(&uart_ports[0]);
}

static void stm32_uart_enable_clock(uint32_t base)
{
#if defined(TARGET_stm32h563)
    (void)base;
    return;
#endif
    switch (base) {
    case USART1_BASE:
        STM32_RCC_APB2RSTR |= (1U << 14);
        STM32_RCC_APB2RSTR &= ~(1U << 14);
        STM32_RCC_APB2ENR |= (1U << 14);
        break;
    case USART2_BASE:
        STM32_RCC_APB1LRSTR |= (1U << 17);
        STM32_RCC_APB1LRSTR &= ~(1U << 17);
        STM32_RCC_APB1LENR |= (1U << 17);
        break;
    case USART3_BASE:
        STM32_RCC_APB1LRSTR |= (1U << 18);
        STM32_RCC_APB1LRSTR &= ~(1U << 18);
        STM32_RCC_APB1LENR |= (1U << 18);
        break;
    case UART4_BASE:
        STM32_RCC_APB1LRSTR |= (1U << 19);
        STM32_RCC_APB1LRSTR &= ~(1U << 19);
        STM32_RCC_APB1LENR |= (1U << 19);
        break;
    case UART5_BASE:
        STM32_RCC_APB1LRSTR |= (1U << 20);
        STM32_RCC_APB1LRSTR &= ~(1U << 20);
        STM32_RCC_APB1LENR |= (1U << 20);
        break;
    default:
        break;
    }
}

static uint32_t stm32_uart_brr(uint32_t baudrate)
{
    if (!baudrate)
        return 0;
    return (SystemCoreClock + (baudrate / 2U)) / baudrate;
}

static int stm32_uart_open(const char *path, int flags)
{
    return device_open(path, flags);
}

static int stm32_uart_read(struct fnode *fno, void *buf, unsigned int len)
{
    struct stm32_uart_port *port;
    int len_available;
    uint8_t *ptr = (uint8_t *)buf;
    int out = 0;

    if (!buf || len == 0)
        return 0;

    port = (struct stm32_uart_port *)FNO_MOD_PRIV(fno, &mod_devuart);
    if (!port)
        return -ENODEV;
    
    mutex_lock(port->dev->mutex);
    len_available =  cirbuf_bytesinuse(port->rxbuf);
    if (len_available <= 0) {
        port->dev->task = this_task();
        task_suspend();
        out = SYS_CALL_AGAIN;
        goto again;
    }

    if (len_available < len)
        len = len_available;

    for(out = 0; out < len; out++) {
        /* read data */
        if (cirbuf_readbyte(port->rxbuf, ptr) != 0)
            break;
        ptr++;
    }

again:
    mutex_unlock(port->dev->mutex);
    return out;

}

static int stm32_uart_poll(struct fnode *fno, uint16_t events, uint16_t *revents)
{
    struct stm32_uart_port *port;
    int ready = 0;

    port = (struct stm32_uart_port *)FNO_MOD_PRIV(fno, &mod_devuart);
    if (!port)
        return -ENODEV;

    mutex_lock(port->dev->mutex);
    port->dev->task = this_task();
    if ((events & POLLIN) && cirbuf_bytesinuse(port->rxbuf) > 0) {
        *revents |= POLLIN;
        ready = 1;
    }
    if ((events & POLLOUT) && (port->regs->ISR & USART_ISR_TXFNF)) {
        *revents |= POLLOUT;
        ready = 1;
    }
    mutex_unlock(port->dev->mutex);
    return ready;
}

static void stm32_uart_flush_tx(stm32_usart_t *regs)
{
    while ((regs->ISR & USART_ISR_TC) == 0)
        ;
}

static int stm32_uart_write(struct fnode *fno, const void *buf, unsigned int len)
{
    struct stm32_uart_port *port;
    const uint8_t *data = buf;
    unsigned int i;

    if (!buf || len == 0)
        return 0;

    port = (struct stm32_uart_port *)FNO_MOD_PRIV(fno, &mod_devuart);
    if (!port)
        return -ENODEV;

    for (i = 0; i < len; i++) {
        while ((port->regs->ISR & USART_ISR_TXFNF) == 0)
            ;
        port->regs->TDR = data[i];
    }

    stm32_uart_flush_tx(port->regs);
    return (int)len;
}

static int stm32_uart_close(struct fnode *fno)
{
    struct stm32_uart_port *port;

    port = (struct stm32_uart_port *)FNO_MOD_PRIV(fno, &mod_devuart);
    if (!port)
        return -ENODEV;

    mutex_lock(port->dev->mutex);
    port->dev->task = NULL;
    mutex_unlock(port->dev->mutex);
    return 0;
}

static void stm32_uart_tty_attach(struct fnode *fno, int pid)
{
    struct stm32_uart_port *port;

    port = (struct stm32_uart_port *)FNO_MOD_PRIV(fno, &mod_devuart);
    if (!port)
        return;
    port->sid = (int8_t)pid;
}

static void stm32_uart_setup_module(void)
{
    if (mod_devuart.ops.open)
        return;

    mod_devuart.ops.open = stm32_uart_open;
    mod_devuart.ops.read = stm32_uart_read;
    mod_devuart.ops.poll = stm32_uart_poll;
    mod_devuart.ops.write = stm32_uart_write;
    mod_devuart.ops.close = stm32_uart_close;
    mod_devuart.ops.tty_attach = stm32_uart_tty_attach;
    register_module(&mod_devuart);
}

static struct stm32_uart_port *stm32_uart_port_from_cfg(const struct uart_config *cfg)
{
    uint8_t idx = cfg->devidx;

    if (idx >= MAX_UART_PORTS)
        return NULL;
    return &uart_ports[idx];
}

int uart_create(const struct uart_config *cfg)
{
    struct stm32_uart_port *port;
    struct fnode *devfs;
    char name[8] = "ttyS0";

    if (!cfg)
        return -EINVAL;

    stm32_uart_setup_module();

    port = stm32_uart_port_from_cfg(cfg);
    if (!port)
        return -ENODEV;

    memset(port, 0, sizeof(*port));
    port->regs = (stm32_usart_t *)(uintptr_t)cfg->base;
    port->irq = (uint8_t)cfg->irq;
    port->sid = -1;
    port->rxbuf = cirbuf_create(UART_RX_BUFFER_SIZE);
    if (!port->rxbuf)
        return -ENOMEM;

    stm32_uart_enable_clock(cfg->base);
    stm32_gpio_config_alt(&cfg->pio_tx);
    stm32_gpio_config_alt(&cfg->pio_rx);

    port->regs->CR1 = 0;
    port->regs->CR2 = 0;
    port->regs->CR3 = USART_CR3_OVRDIS;
    port->regs->PRESC = 0;
    port->regs->BRR = stm32_uart_brr(cfg->baudrate ? cfg->baudrate : 115200U);
    port->regs->ICR = 0xFFFFFFFFU;
    port->regs->RQR = 0;
    port->regs->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE | USART_CR1_RXFNEIE;

    nvic_set_priority(port->irq, 1U << 5);
    nvic_clear_pending(port->irq);
    nvic_enable_irq(port->irq);

    devfs = fno_search("/dev");
    if (!devfs) {
        kfree(port->rxbuf);
        port->rxbuf = NULL;
        return -ENOENT;
    }

    name[4] = '0' + cfg->devidx;
    port->dev = device_fno_init(&mod_devuart, name, devfs, FL_TTY, port);
    if (!port->dev) {
        kfree(port->rxbuf);
        port->rxbuf = NULL;
        return -ENOMEM;
    }

    return 0;
}

int uart_init(void)
{
#if defined(TARGET_stm32h563)
    static const struct uart_config default_uart = {
        .devidx = 0,
        .base = USART3_BASE,
        .irq = 60,
        .baudrate = 115200,
        .stop_bits = 1,
        .data_bits = 8,
        .parity = 0,
        .flow = 0,
        .pio_rx = {
            .base = GPIOD_BASE,
            .pin = 9,
            .mode = GPIO_MODE_AF,
            .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
            .speed = GPIO_SPEED_HIGH,
            .optype = GPIO_OTYPE_PP,
            .af = 7,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "uart3_rx",
        },
        .pio_tx = {
            .base = GPIOD_BASE,
            .pin = 8,
            .mode = GPIO_MODE_AF,
            .pullupdown = IOCTL_GPIO_PUPD_NONE,
            .speed = GPIO_SPEED_HIGH,
            .optype = GPIO_OTYPE_PP,
            .af = 7,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "uart3_tx",
        },
    };
#elif defined(TARGET_STM32U585)
    static const struct uart_config default_uart = {
        .devidx = 0,
        .base = USART2_BASE,
        .irq = 62,
        .baudrate = 115200,
        .stop_bits = 1,
        .data_bits = 8,
        .parity = 0,
        .flow = 0,
        .pio_rx = {
            .base = GPIOA_BASE,
            .pin = 3,
            .mode = GPIO_MODE_AF,
            .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
            .speed = GPIO_SPEED_HIGH,
            .optype = GPIO_OTYPE_PP,
            .af = 7,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "uart2_rx",
        },
        .pio_tx = {
            .base = GPIOA_BASE,
            .pin = 1,
            .mode = GPIO_MODE_AF,
            .pullupdown = IOCTL_GPIO_PUPD_NONE,
            .speed = GPIO_SPEED_HIGH,
            .optype = GPIO_OTYPE_PP,
            .af = 7,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "uart2_tx",
        },
    };
#else
    static const struct uart_config default_uart = {
        .devidx = 0,
        .base = USART2_BASE,
        .irq = 62,
        .baudrate = 115200,
        .stop_bits = 1,
        .data_bits = 8,
        .parity = 0,
        .flow = 0,
        .pio_rx = {
            .base = GPIOA_BASE,
            .pin = 3,
            .mode = GPIO_MODE_AF,
            .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
            .speed = GPIO_SPEED_HIGH,
            .optype = GPIO_OTYPE_PP,
            .af = 7,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "uart2_rx",
        },
        .pio_tx = {
            .base = GPIOA_BASE,
            .pin = 1,
            .mode = GPIO_MODE_AF,
            .pullupdown = IOCTL_GPIO_PUPD_NONE,
            .speed = GPIO_SPEED_HIGH,
            .optype = GPIO_OTYPE_PP,
            .af = 7,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "uart2_tx",
        },
    };
#endif

    return uart_create(&default_uart);
}

static void stm32_uart_break_tasklet(void *arg)
{
    struct stm32_uart_port *port = arg;
    if (!port || port->sid <= 1)
        return;
    task_kill(port->sid, 2);
}

#endif /* CONFIG_DEVUART */
