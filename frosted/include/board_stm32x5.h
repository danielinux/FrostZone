#ifndef BOARD_STM32X5_H
#define BOARD_STM32X5_H

#include "gpio.h"
#include <stdint.h>

struct stm32x5_spi_pins {
    struct gpio_config nss;
    struct gpio_config sck;
    struct gpio_config miso;
    struct gpio_config mosi;
};

struct stm32x5_i2c_pins {
    struct gpio_config scl;
    struct gpio_config sda;
};

struct stm32x5_uart_pins {
    struct gpio_config tx;
    struct gpio_config rx;
};

struct stm32x5_mikrobus {
    struct gpio_config cs;
    struct stm32x5_spi_pins spi;
    struct stm32x5_i2c_pins i2c;
    struct stm32x5_uart_pins uart;
    struct gpio_config int_pin;
    struct gpio_config pwm;
    struct gpio_config analog;
};

struct stm32x5_board_pins {
    uint8_t led_count;
    uint8_t mikrobus_count;
    struct gpio_config leds[4];
    struct gpio_config button;
    struct stm32x5_mikrobus mikrobus[2];
};

#endif /* BOARD_STM32X5_H */
