/*
 *      STM32U585 board support.
 *
 *      This module provides the minimal hardware initialisation required
 *      by frosted when running in the non-secure world on the STM32U585.
 *      It describes the board level pin-out so higher level drivers can
 *      discover available peripherals, and configures basic GPIO used by
 *      the kernel (LEDs and the front panel button).
 */

#include "board_stm32u585.h"
#include "frosted.h"
#include "sys/frosted-io.h"
#include <stddef.h>
#include <stdint.h>

#define STM32_RCC_BASE 0x46020C00UL
#include "stm32x5_board_common.h"

/* SystemCoreClock is used by some peripherals to compute delays.  Use the
 * 160 MHz value defined in the kernel configuration for this target. */
uint32_t SystemCoreClock = 160000000UL;

static const struct stm32x5_board_pins board_pins = {
    .led_count = 4,
    .mikrobus_count = 2,
    .leds = {
        {
            .base = GPIOA_BASE,
            .pin = 4,
            .mode = GPIO_MODE_OUTPUT,
            .pullupdown = IOCTL_GPIO_PUPD_NONE,
            .speed = GPIO_SPEED_MED,
            .optype = GPIO_OTYPE_PP,
            .af = 0,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "led0",
        },
        {
            .base = GPIOA_BASE,
            .pin = 0,
            .mode = GPIO_MODE_OUTPUT,
            .pullupdown = IOCTL_GPIO_PUPD_NONE,
            .speed = GPIO_SPEED_MED,
            .optype = GPIO_OTYPE_PP,
            .af = 0,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "led1",
        },
        {
            .base = GPIOA_BASE,
            .pin = 10,
            .mode = GPIO_MODE_OUTPUT,
            .pullupdown = IOCTL_GPIO_PUPD_NONE,
            .speed = GPIO_SPEED_MED,
            .optype = GPIO_OTYPE_PP,
            .af = 0,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "led2",
        },
        {
            .base = GPIOB_BASE,
            .pin = 8,
            .mode = GPIO_MODE_OUTPUT,
            .pullupdown = IOCTL_GPIO_PUPD_NONE,
            .speed = GPIO_SPEED_MED,
            .optype = GPIO_OTYPE_PP,
            .af = 0,
            .trigger = GPIO_TRIGGER_NONE,
            .name = "led3",
        },
    },
    .button = {
        .base = GPIOB_BASE,
        .pin = 3,
        .mode = GPIO_MODE_INPUT,
        .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
        .speed = GPIO_SPEED_LOW,
        .optype = GPIO_OTYPE_PP,
        .af = 0,
        .trigger = GPIO_TRIGGER_NONE,
        .name = "bt1",
    },
    .mikrobus = {
        {
            .cs = {
                .base = GPIOB_BASE,
                .pin = 4,
                .mode = GPIO_MODE_OUTPUT,
                .pullupdown = IOCTL_GPIO_PUPD_NONE,
                .speed = GPIO_SPEED_HIGH,
                .optype = GPIO_OTYPE_PP,
                .af = 0,
                .trigger = GPIO_TRIGGER_NONE,
                .name = "mb1_cs",
            },
            .spi = {
                .nss = {
                    .base = GPIOB_BASE,
                    .pin = 4,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_NONE,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 5,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb1_spi_nss",
                },
                .sck = {
                    .base = GPIOA_BASE,
                    .pin = 12,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_NONE,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 5,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb1_spi_sck",
                },
                .miso = {
                    .base = GPIOA_BASE,
                    .pin = 1,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_NONE,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 5,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb1_spi_miso",
                },
                .mosi = {
                    .base = GPIOA_BASE,
                    .pin = 11,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_NONE,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 5,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb1_spi_mosi",
                },
            },
            .i2c = {
                .scl = {
                    .base = GPIOB_BASE,
                    .pin = 6,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_OD,
                    .af = 4,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb1_i2c_scl",
                },
                .sda = {
                    .base = GPIOB_BASE,
                    .pin = 9,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_OD,
                    .af = 4,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb1_i2c_sda",
                },
            },
            .uart = {
                .tx = {
                    .base = GPIOA_BASE,
                    .pin = 1,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_NONE,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 7,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb1_uart_tx",
                },
                .rx = {
                    .base = GPIOA_BASE,
                    .pin = 3,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 7,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb1_uart_rx",
                },
            },
            .int_pin = {
                .base = GPIOB_BASE,
                .pin = 1,
                .mode = GPIO_MODE_INPUT,
                .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
                .speed = GPIO_SPEED_LOW,
                .optype = GPIO_OTYPE_PP,
                .af = 0,
                .trigger = GPIO_TRIGGER_NONE,
                .name = "mb1_int",
            },
            .pwm = {
                .base = GPIOA_BASE,
                .pin = 8,
                .mode = GPIO_MODE_AF,
                .pullupdown = IOCTL_GPIO_PUPD_NONE,
                .speed = GPIO_SPEED_HIGH,
                .optype = GPIO_OTYPE_PP,
                .af = 1,
                .trigger = GPIO_TRIGGER_NONE,
                .name = "mb1_pwm",
            },
            .analog = {
                .base = GPIOA_BASE,
                .pin = 5,
                .mode = GPIO_MODE_ANALOG,
                .pullupdown = IOCTL_GPIO_PUPD_NONE,
                .speed = GPIO_SPEED_LOW,
                .optype = GPIO_OTYPE_PP,
                .af = 0,
                .trigger = GPIO_TRIGGER_NONE,
                .name = "mb1_an",
            },
        },
        {
            .cs = {
                .base = GPIOB_BASE,
                .pin = 5,
                .mode = GPIO_MODE_OUTPUT,
                .pullupdown = IOCTL_GPIO_PUPD_NONE,
                .speed = GPIO_SPEED_HIGH,
                .optype = GPIO_OTYPE_PP,
                .af = 0,
                .trigger = GPIO_TRIGGER_NONE,
                .name = "mb2_cs",
            },
            .spi = {
                .nss = {
                    .base = GPIOB_BASE,
                    .pin = 5,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_NONE,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 5,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb2_spi_nss",
                },
                .sck = {
                    .base = GPIOB_BASE,
                    .pin = 13,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_NONE,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 5,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb2_spi_sck",
                },
                .miso = {
                    .base = GPIOB_BASE,
                    .pin = 14,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_NONE,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 5,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb2_spi_miso",
                },
                .mosi = {
                    .base = GPIOB_BASE,
                    .pin = 15,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_NONE,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 5,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb2_spi_mosi",
                },
            },
            .i2c = {
                .scl = {
                    .base = GPIOB_BASE,
                    .pin = 10,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_OD,
                    .af = 4,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb2_i2c_scl",
                },
                .sda = {
                    .base = GPIOB_BASE,
                    .pin = 7,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_OD,
                    .af = 4,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb2_i2c_sda",
                },
            },
            .uart = {
                .tx = {
                    .base = GPIOA_BASE,
                    .pin = 5,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_NONE,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 7,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb2_uart_tx",
                },
                .rx = {
                    .base = GPIOA_BASE,
                    .pin = 7,
                    .mode = GPIO_MODE_AF,
                    .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
                    .speed = GPIO_SPEED_HIGH,
                    .optype = GPIO_OTYPE_PP,
                    .af = 7,
                    .trigger = GPIO_TRIGGER_NONE,
                    .name = "mb2_uart_rx",
                },
            },
            .int_pin = {
                .base = GPIOB_BASE,
                .pin = 2,
                .mode = GPIO_MODE_INPUT,
                .pullupdown = IOCTL_GPIO_PUPD_PULLUP,
                .speed = GPIO_SPEED_LOW,
                .optype = GPIO_OTYPE_PP,
                .af = 0,
                .trigger = GPIO_TRIGGER_NONE,
                .name = "mb2_int",
            },
            .pwm = {
                .base = GPIOA_BASE,
                .pin = 9,
                .mode = GPIO_MODE_AF,
                .pullupdown = IOCTL_GPIO_PUPD_NONE,
                .speed = GPIO_SPEED_HIGH,
                .optype = GPIO_OTYPE_PP,
                .af = 1,
                .trigger = GPIO_TRIGGER_NONE,
                .name = "mb2_pwm",
            },
            .analog = {
                .base = GPIOB_BASE,
                .pin = 0,
                .mode = GPIO_MODE_ANALOG,
                .pullupdown = IOCTL_GPIO_PUPD_NONE,
                .speed = GPIO_SPEED_LOW,
                .optype = GPIO_OTYPE_PP,
                .af = 0,
                .trigger = GPIO_TRIGGER_NONE,
                .name = "mb2_an",
            },
        },
    },
};

const struct stm32x5_board_pins *stm32u5_board_pins(void)
{
    return &board_pins;
}

void usb_irq_handler(void)
{
    /* TinyUSB disabled on this target */
}

int gpio_init(void)
{
    const struct stm32x5_board_pins *pins = stm32u5_board_pins();
    size_t i;

    /* Configure LED outputs and ensure they start off. */
    for (i = 0; i < pins->led_count; i++) {
        const struct gpio_config *cfg = &pins->leds[i];
        if (!cfg->base)
            continue;
        stm32x5_rcc_enable_gpio(cfg->base);
        stm32x5_gpio_write_mode(cfg->base, (uint8_t)cfg->pin, GPIO_MODE_OUTPUT);
        stm32x5_gpio_write_otype(cfg->base, (uint8_t)cfg->pin, cfg->optype);
        stm32x5_gpio_write_speed(cfg->base, (uint8_t)cfg->pin, cfg->speed);
        stm32x5_gpio_write_pull(cfg->base, (uint8_t)cfg->pin, cfg->pullupdown);
        stm32x5_gpio_reset(cfg->base, (uint8_t)cfg->pin);
    }

    /* Configure user button as input with pull-up. */
    if (pins->button.base) {
        stm32x5_rcc_enable_gpio(pins->button.base);
        stm32x5_gpio_write_mode(pins->button.base, (uint8_t)pins->button.pin, GPIO_MODE_INPUT);
        stm32x5_gpio_write_pull(pins->button.base, (uint8_t)pins->button.pin, pins->button.pullupdown);
    }

    return 0;
}

int gpio_list_len(void)
{
    const struct stm32x5_board_pins *pins = stm32u5_board_pins();
    int count = pins->led_count;

    if (pins->button.base)
        count += 1;
    count += pins->mikrobus_count * 12;
    return count;
}

int exti_init(void)
{
    return 0;
}

int machine_init(void)
{
    return 0;
}
