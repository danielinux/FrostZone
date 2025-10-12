#include "stm32h563_regs.h"

#include <stdint.h>

#include "../stm32_common/stm32_common.h"

extern void stm32h563_clock_init(void);

#define STM32H563_MDIO_CLOCK_RANGE 0x4U
#define STM32H563_MDIO_TIMEOUT     100000U

#define PHY_REG_BCR 0x00U
#define PHY_REG_BSR 0x01U
#define PHY_REG_ID1 0x02U

#define PHY_BCR_RESET            BIT(15)
#define PHY_BCR_SPEED_100        BIT(13)
#define PHY_BCR_AUTONEG_ENABLE   BIT(12)
#define PHY_BCR_POWER_DOWN       BIT(11)
#define PHY_BCR_ISOLATE          BIT(10)
#define PHY_BCR_RESTART_AUTONEG  BIT(9)
#define PHY_BCR_FULL_DUPLEX      BIT(8)

static void gpio_config_alt(uint32_t base, uint32_t pin, uint32_t alternate)
{
    volatile uint32_t *moder = (volatile uint32_t *)(base + GPIO_MODER_OFFSET);
    volatile uint32_t *otyper = (volatile uint32_t *)(base + GPIO_OTYPER_OFFSET);
    volatile uint32_t *ospeedr = (volatile uint32_t *)(base + GPIO_OSPEEDR_OFFSET);
    volatile uint32_t *pupdr = (volatile uint32_t *)(base + GPIO_PUPDR_OFFSET);
    volatile uint32_t *afr;
    uint32_t shift = pin * 2U;
    uint32_t afr_shift = (pin % 8U) * 4U;

    *moder &= ~(0x3U << shift);
    *moder |= (0x2U << shift);

    *otyper &= ~BIT(pin);

    *ospeedr &= ~(0x3U << shift);
    *ospeedr |= (0x3U << shift);

    *pupdr &= ~(0x3U << shift);

    if (pin < 8U) {
        afr = (volatile uint32_t *)(base + GPIO_AFRL_OFFSET);
    } else {
        afr = (volatile uint32_t *)(base + GPIO_AFRH_OFFSET);
    }

    *afr &= ~(0xFU << afr_shift);
    *afr |= ((alternate & 0xFU) << afr_shift);
}

static void enable_gpio_clocks(void)
{
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN | RCC_AHB2ENR_GPIOGEN;
    stm32_data_memory_barrier();
}

static void enable_ethernet_clocks(void)
{
    RCC_APB3ENR |= RCC_APB3ENR_SBSEN;
    RCC_AHB1ENR |= RCC_AHB1ENR_ETHEN | RCC_AHB1ENR_ETHTXEN | RCC_AHB1ENR_ETHRXEN;
    stm32_data_memory_barrier();
}

static void select_rmii_interface(void)
{
    uint32_t reg = SBS_PMCR;
    reg &= ~SBS_PMCR_ETH_SEL_PHY_MASK;
    reg |= SBS_PMCR_ETH_SEL_PHY_RMII;
    SBS_PMCR = reg;
    stm32_data_memory_barrier();
}

static void reset_ethernet_mac(void)
{
    RCC_AHB1RSTR |= RCC_AHB1RSTR_ETHRST;
    stm32_data_memory_barrier();
    RCC_AHB1RSTR &= ~RCC_AHB1RSTR_ETHRST;
    stm32_data_memory_barrier();
}

static void configure_rmii_pins(void)
{
    const uint32_t alt = 11U;

    gpio_config_alt(GPIOA_BASE, 1U, alt); /* REF_CLK */
    gpio_config_alt(GPIOA_BASE, 2U, alt); /* MDIO */
    gpio_config_alt(GPIOA_BASE, 7U, alt); /* CRS_DV */

    gpio_config_alt(GPIOC_BASE, 1U, alt); /* MDC */
    gpio_config_alt(GPIOC_BASE, 4U, alt); /* RXD0 */
    gpio_config_alt(GPIOC_BASE, 5U, alt); /* RXD1 */

    gpio_config_alt(GPIOG_BASE, 11U, alt); /* TX_EN */
    gpio_config_alt(GPIOG_BASE, 13U, alt); /* TXD0 */
    gpio_config_alt(GPIOG_BASE, 14U, alt); /* TXD1 */
}

static void mdio_wait_ready(void)
{
    uint32_t timeout = STM32H563_MDIO_TIMEOUT;

    while ((ETH_MACMDIOAR & ETH_MACMDIOAR_MB) != 0U && timeout != 0U) {
        timeout--;
    }
}

static void mdio_write(uint32_t phy, uint32_t reg, uint16_t value)
{
    mdio_wait_ready();

    ETH_MACMDIODR = (uint32_t)value;
    uint32_t cfg = (STM32H563_MDIO_CLOCK_RANGE << ETH_MACMDIOAR_CR_SHIFT)
        | (reg << ETH_MACMDIOAR_RDA_SHIFT)
        | (phy << ETH_MACMDIOAR_PA_SHIFT)
        | (ETH_MACMDIOAR_GOC_WRITE << ETH_MACMDIOAR_GOC_SHIFT);

    ETH_MACMDIOAR = cfg | ETH_MACMDIOAR_MB;
    mdio_wait_ready();
}

static uint16_t mdio_read(uint32_t phy, uint32_t reg)
{
    mdio_wait_ready();

    uint32_t cfg = (STM32H563_MDIO_CLOCK_RANGE << ETH_MACMDIOAR_CR_SHIFT)
        | (reg << ETH_MACMDIOAR_RDA_SHIFT)
        | (phy << ETH_MACMDIOAR_PA_SHIFT)
        | (ETH_MACMDIOAR_GOC_READ << ETH_MACMDIOAR_GOC_SHIFT);

    ETH_MACMDIOAR = cfg | ETH_MACMDIOAR_MB;
    mdio_wait_ready();

    return (uint16_t)(ETH_MACMDIODR & 0xFFFFU);
}

static int32_t detect_phy_address(void)
{
    for (uint32_t addr = 0U; addr < 32U; addr++) {
        uint16_t id1 = mdio_read(addr, PHY_REG_ID1);
        if (id1 != 0xFFFFU && id1 != 0x0000U) {
            return (int32_t)addr;
        }
    }

    return -1;
}

static void ethernet_phy_init(void)
{
    int32_t phy_addr = detect_phy_address();

    if (phy_addr < 0) {
        phy_addr = 0;
    }

    mdio_write((uint32_t)phy_addr, PHY_REG_BCR, PHY_BCR_RESET);

    uint32_t timeout = STM32H563_MDIO_TIMEOUT;
    while (timeout-- != 0U) {
        uint16_t bcr = mdio_read((uint32_t)phy_addr, PHY_REG_BCR);
        if ((bcr & PHY_BCR_RESET) == 0U) {
            break;
        }
    }

    uint16_t ctrl = mdio_read((uint32_t)phy_addr, PHY_REG_BCR);
    ctrl &= ~(PHY_BCR_POWER_DOWN | PHY_BCR_ISOLATE);
    ctrl |= PHY_BCR_AUTONEG_ENABLE | PHY_BCR_RESTART_AUTONEG | PHY_BCR_FULL_DUPLEX | PHY_BCR_SPEED_100;
    mdio_write((uint32_t)phy_addr, PHY_REG_BCR, ctrl);
}

void machine_init(void)
{
    stm32h563_clock_init();

    enable_gpio_clocks();
    enable_ethernet_clocks();
    select_rmii_interface();
    reset_ethernet_mac();
    configure_rmii_pins();

    ethernet_phy_init();

    ETH_MACCR &= ~(ETH_MACCR_RE | ETH_MACCR_TE);

    stm32_mark_irq_non_secure(106U);
    stm32_mark_irq_non_secure(107U);
}
