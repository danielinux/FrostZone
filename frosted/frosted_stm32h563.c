#include "board_stm32h563.h"
#include "frosted.h"
#include "sys/frosted-io.h"
#include "nvic.h"
#include "stm32h5xx.h"
#include "spi.h"
#include <stddef.h>
#include <stdint.h>

#define STM32_RCC_BASE 0x44020C00UL
#include "stm32x5_board_common.h"

/* The secure supervisor configures PLL1 to provide a 250 MHz system clock. */
uint32_t SystemCoreClock = 250000000UL;

static const struct stm32x5_board_pins board_pins = {
    .led_count = 3,
    .mikrobus_count = 0,
    .leds =
        {
            {
                .base = GPIOB_BASE,
                .pin = 0,
                .mode = GPIO_MODE_OUTPUT,
                .pullupdown = IOCTL_GPIO_PUPD_NONE,
                .speed = GPIO_SPEED_HIGH,
                .optype = GPIO_OTYPE_PP,
                .af = 0,
                .trigger = GPIO_TRIGGER_NONE,
                .name = "led_green",
            },
            {
                .base = GPIOG_BASE,
                .pin = 4,
                .mode = GPIO_MODE_OUTPUT,
                .pullupdown = IOCTL_GPIO_PUPD_NONE,
                .speed = GPIO_SPEED_HIGH,
                .optype = GPIO_OTYPE_PP,
                .af = 0,
                .trigger = GPIO_TRIGGER_NONE,
                .name = "led_red",
            },
            {
                .base = GPIOF_BASE,
                .pin = 4,
                .mode = GPIO_MODE_OUTPUT,
                .pullupdown = IOCTL_GPIO_PUPD_NONE,
                .speed = GPIO_SPEED_HIGH,
                .optype = GPIO_OTYPE_PP,
                .af = 0,
                .trigger = GPIO_TRIGGER_NONE,
                .name = "led_orange",
            },
            { 0 },
        },
    .button = {
        .base = GPIOC_BASE,
        .pin = 13,
        .mode = GPIO_MODE_INPUT,
        .pullupdown = IOCTL_GPIO_PUPD_PULLDOWN,
        .speed = GPIO_SPEED_LOW,
        .optype = GPIO_OTYPE_PP,
        .af = 0,
        .trigger = GPIO_TRIGGER_NONE,
        .name = "user_btn",
    },
};

const struct stm32x5_board_pins *stm32h5_board_pins(void)
{
    return &board_pins;
}

#if !CONFIG_DEVUSB
void usb_irq_handler(void)
{
    /* USB not enabled on this target */
}
#endif

int gpio_init(void)
{
    const struct stm32x5_board_pins *pins = stm32h5_board_pins();
    size_t i;
    const struct gpio_config *green_led = NULL;

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
        if (cfg->base == GPIOB_BASE && cfg->pin == 0)
            green_led = cfg;
    }

    if (green_led) {
        /* Drive the green status LED (PB0) high once GPIOs are configured. */
        stm32x5_gpio_set(green_led->base, (uint8_t)green_led->pin);
    }

    if (pins->button.base) {
        stm32x5_rcc_enable_gpio(pins->button.base);
        stm32x5_gpio_write_mode(pins->button.base, (uint8_t)pins->button.pin, GPIO_MODE_INPUT);
        stm32x5_gpio_write_pull(pins->button.base, (uint8_t)pins->button.pin, pins->button.pullupdown);
    }

    return 0;
}

int gpio_list_len(void)
{
    const struct stm32x5_board_pins *pins = stm32h5_board_pins();
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
#if !CONFIG_ETH
    nvic_disable_irq(GPDMA1_Channel1_IRQn);
    nvic_clear_pending(GPDMA1_Channel1_IRQn);
#endif
#if CONFIG_SPI1
    spi_bus_init();
#endif
    return 0;
}
