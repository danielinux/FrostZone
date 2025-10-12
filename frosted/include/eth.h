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

#ifdef CONFIG_ETH
int ethernet_init(const struct eth_config *conf);
#else
#  define ethernet_init(x) ((int)(-2))
#endif

#endif
