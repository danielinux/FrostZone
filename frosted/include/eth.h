#ifndef INC_ETH
#define INC_ETH
#include "frosted.h"
#include "gpio.h"

struct eth_config {
    const struct gpio_config *pio_mii;
    const unsigned int n_pio_mii;
    const struct gpio_config pio_phy_reset;
    const int has_phy_reset;
};

#if CONFIG_ETH
int ethernet_init(const struct eth_config *conf);
void stm32_eth_enable_loopback(int enable);
#else
#  define ethernet_init(x) ((int)(-2))
#  define stm32_eth_enable_loopback(x) ((void)0)
#endif

#endif
