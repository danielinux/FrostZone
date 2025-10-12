#ifndef STM32X5_BOARD_COMMON_H
#define STM32X5_BOARD_COMMON_H

#include <stdint.h>
#include "gpio.h"

#ifndef STM32_RCC_BASE
#error "STM32_RCC_BASE must be defined before including stm32x5_board_common.h"
#endif

#define STM32_RCC_AHB2RSTR  (*(volatile uint32_t *)(STM32_RCC_BASE + 0x044UL))
#define STM32_RCC_AHB2ENR   (*(volatile uint32_t *)(STM32_RCC_BASE + 0x08CUL))
#define STM32_RCC_APB1LRSTR (*(volatile uint32_t *)(STM32_RCC_BASE + 0x074UL))
#define STM32_RCC_APB1LENR  (*(volatile uint32_t *)(STM32_RCC_BASE + 0x09CUL))
#define STM32_RCC_APB2RSTR  (*(volatile uint32_t *)(STM32_RCC_BASE + 0x0A0UL))
#define STM32_RCC_APB2ENR   (*(volatile uint32_t *)(STM32_RCC_BASE + 0x0A4UL))

#if defined(TARGET_stm32h563)
#define GPIOA_BASE 0x42020000UL
#define GPIOB_BASE 0x42020400UL
#define GPIOC_BASE 0x42020800UL
#define GPIOD_BASE 0x42020C00UL
#define GPIOE_BASE 0x42021000UL
#define GPIOF_BASE 0x42021400UL
#define GPIOG_BASE 0x42021800UL
#define GPIOH_BASE 0x42021C00UL
#define GPIOI_BASE 0x42022000UL
#else
#define GPIOA_BASE 0x40020000UL
#define GPIOB_BASE 0x40020400UL
#define GPIOC_BASE 0x40020800UL
#define GPIOD_BASE 0x40020C00UL
#define GPIOE_BASE 0x40021000UL
#define GPIOF_BASE 0x40021400UL
#define GPIOG_BASE 0x40021800UL
#define GPIOH_BASE 0x40021C00UL
#define GPIOI_BASE 0x40022000UL
#endif

#define GPIO_MODE_INPUT   0x0U
#define GPIO_MODE_OUTPUT  0x1U
#define GPIO_MODE_AF      0x2U
#define GPIO_MODE_ANALOG  0x3U

#define GPIO_OTYPE_PP     0x0U
#define GPIO_OTYPE_OD     0x1U

#define GPIO_SPEED_LOW    0x0U
#define GPIO_SPEED_MED    0x1U
#define GPIO_SPEED_HIGH   0x2U
#define GPIO_SPEED_VERY   0x3U

struct stm32_gpio {
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t LCKR;
    volatile uint32_t AFR[2];
    volatile uint32_t BRR;
};

static inline void stm32x5_rcc_enable_gpio(uint32_t base)
{
#if defined(TARGET_stm32h563)
    (void)base;
    return;
#else
    uint32_t bit;

    switch (base) {
    case GPIOA_BASE:
        bit = 0U;
        break;
    case GPIOB_BASE:
        bit = 1U;
        break;
    case GPIOC_BASE:
        bit = 2U;
        break;
    case GPIOD_BASE:
        bit = 3U;
        break;
    case GPIOE_BASE:
        bit = 4U;
        break;
    case GPIOF_BASE:
        bit = 5U;
        break;
    case GPIOG_BASE:
        bit = 6U;
        break;
    case GPIOH_BASE:
        bit = 7U;
        break;
    case GPIOI_BASE:
        bit = 8U;
        break;
    default:
        return;
    }

    STM32_RCC_AHB2ENR |= (1U << bit);
#endif
}

static inline void stm32x5_gpio_write_mode(uint32_t base, uint8_t pin, uint32_t mode)
{
    struct stm32_gpio *gpio = (struct stm32_gpio *)(uintptr_t)base;
    uint32_t shift = (uint32_t)pin * 2U;
    uint32_t mask = 3U << shift;

    gpio->MODER = (gpio->MODER & ~mask) | ((mode & 0x3U) << shift);
}

static inline void stm32x5_gpio_write_otype(uint32_t base, uint8_t pin, uint32_t type)
{
    struct stm32_gpio *gpio = (struct stm32_gpio *)(uintptr_t)base;

    if (type == GPIO_OTYPE_OD)
        gpio->OTYPER |= (1U << pin);
    else
        gpio->OTYPER &= ~(1U << pin);
}

static inline void stm32x5_gpio_write_speed(uint32_t base, uint8_t pin, uint32_t speed)
{
    struct stm32_gpio *gpio = (struct stm32_gpio *)(uintptr_t)base;
    uint32_t shift = (uint32_t)pin * 2U;
    uint32_t mask = 3U << shift;

    gpio->OSPEEDR = (gpio->OSPEEDR & ~mask) | ((speed & 0x3U) << shift);
}

static inline void stm32x5_gpio_write_pull(uint32_t base, uint8_t pin, uint8_t pullupdown)
{
    struct stm32_gpio *gpio = (struct stm32_gpio *)(uintptr_t)base;
    uint32_t shift = (uint32_t)pin * 2U;
    uint32_t mask = 3U << shift;
    uint32_t value = 0U;

    switch (pullupdown) {
    case IOCTL_GPIO_PUPD_PULLUP:
        value = 1U;
        break;
    case IOCTL_GPIO_PUPD_PULLDOWN:
        value = 2U;
        break;
    default:
        value = 0U;
        break;
    }

    gpio->PUPDR = (gpio->PUPDR & ~mask) | (value << shift);
}

static inline void stm32x5_gpio_reset(uint32_t base, uint8_t pin)
{
    struct stm32_gpio *gpio = (struct stm32_gpio *)(uintptr_t)base;
    gpio->BSRR = (1U << (pin + 16U));
}

static inline void stm32x5_gpio_set(uint32_t base, uint8_t pin)
{
    struct stm32_gpio *gpio = (struct stm32_gpio *)(uintptr_t)base;
    gpio->BSRR = (1U << pin);
}

static inline void stm32x5_gpio_config_alt(const struct gpio_config *cfg)
{
    struct stm32_gpio *gpio;
    uint32_t shift;
    uint32_t mask;

    if (!cfg || !cfg->base)
        return;

    stm32x5_rcc_enable_gpio(cfg->base);
    gpio = (struct stm32_gpio *)(uintptr_t)cfg->base;
    shift = (uint32_t)cfg->pin * 2U;
    mask = 3U << shift;

    gpio->MODER = (gpio->MODER & ~mask) | ((uint32_t)GPIO_MODE_AF << shift);
    stm32x5_gpio_write_otype(cfg->base, (uint8_t)cfg->pin, cfg->optype);
    stm32x5_gpio_write_speed(cfg->base, (uint8_t)cfg->pin, cfg->speed ? cfg->speed : GPIO_SPEED_HIGH);
    stm32x5_gpio_write_pull(cfg->base, (uint8_t)cfg->pin, cfg->pullupdown);

    if (cfg->pin < 8U) {
        uint32_t af_shift = (uint32_t)cfg->pin * 4U;
        gpio->AFR[0] = (gpio->AFR[0] & ~(0xFU << af_shift)) | ((uint32_t)cfg->af << af_shift);
    } else {
        uint32_t af_shift = ((uint32_t)cfg->pin - 8U) * 4U;
        gpio->AFR[1] = (gpio->AFR[1] & ~(0xFU << af_shift)) | ((uint32_t)cfg->af << af_shift);
    }
}

#endif /* STM32X5_BOARD_COMMON_H */
