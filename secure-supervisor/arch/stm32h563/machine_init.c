#include "stm32h563_regs.h"

#include <stdint.h>

#include "../stm32_common/stm32_common.h"

extern void stm32h563_clock_init(void);

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

#if CONFIG_ETH
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
    gpio_config_alt(GPIOB_BASE, 15U, alt); /* TXD1 (JP6 ties PB15 to LAN8742) */
}
#endif /* CONFIG_ETH */

static void stm32h5_usb_preinit(void)
{
    /* Bring up the always-on 48 MHz source and the USB analog rail so that
     * the non-secure TinyUSB stack can start immediately. Leave the GPIO
     * muxing and controller reset sequence to the frosted side so it still
     * observes a power-on reset state.
     */
    RCC_CR |= RCC_CR_HSI48ON;
    while ((RCC_CR & RCC_CR_HSI48RDY) == 0U) {
    }

    RCC_CCIPR4 = (RCC_CCIPR4 & ~RCC_CCIPR4_USBFSSEL_MASK) | RCC_CCIPR4_USBFSSEL_HSI48;

    RCC_APB2ENR |= RCC_APB2ENR_USBFSEN;
    stm32_data_memory_barrier();
    RCC_APB2RSTR |= RCC_APB2RSTR_USBFSRST;
    stm32_data_memory_barrier();
    RCC_APB2RSTR &= ~RCC_APB2RSTR_USBFSRST;
    stm32_data_memory_barrier();

    PWR_USBSCR |= PWR_USBSCR_USB33DEN | PWR_USBSCR_USB33SV;
    while ((PWR_VMSR & PWR_VMSR_USB33RDY) == 0U) {
    }
}

#define IRQ_GPDMA1_CHANNEL1   28U

void machine_init(void)
{
    stm32h563_clock_init();
    stm32h5_usb_preinit();

#if CONFIG_ETH
    enable_gpio_clocks();
    enable_ethernet_clocks();
    select_rmii_interface();
    reset_ethernet_mac();
    configure_rmii_pins();

    ETH_MACCR &= ~(ETH_MACCR_RE | ETH_MACCR_TE);

    stm32_mark_irq_non_secure(106U);
    stm32_mark_irq_non_secure(107U);
#else
    stm32_disable_irq(IRQ_GPDMA1_CHANNEL1);
    stm32_clear_pending_irq(IRQ_GPDMA1_CHANNEL1);
#endif

    /* Route critical peripherals to the non-secure world so their ISRs run in frosted. */
    stm32_mark_irq_non_secure(IRQ_USB_DRD_FS);
    stm32_mark_irq_non_secure(60U);
}
