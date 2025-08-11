/*
 *      This file is part of frostzone.
 *
 *      frostzone is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 3, as
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

#include "stdint.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/gpio.h"

#define NVIC_ICER0 (*(volatile uint32_t *)(0xE000E180))
#define NVIC_ICPR0 (*(volatile uint32_t *)(0xE000E280))
#define NVIC_ITNS0 (*(volatile uint32_t *)(0xE000EF00))

#define NSACR (*(volatile uint32_t *)(0xE000ED8C))
#define CPACR (*(volatile uint32_t *)(0xE000ED88))

#define ACCESS_BITS_DBG (1 << 7)
#define ACCESS_BITS_DMA (1 << 6)
#define ACCESS_BITS_CORE1 (1 << 5)
#define ACCESS_BITS_CORE0 (1 << 4)
#define ACCESS_BITS_SP    (1 << 3)
#define ACCESS_BITS_SU    (1 << 2)
#define ACCESS_BITS_NSP   (1 << 1)
#define ACCESS_BITS_NSU   (1 << 0)
#define ACCESS_MAGIC (0xACCE0000)


#define ACCESS_CONTROL (0x40060000)
#define ACCESS_CONTROL_LOCK             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0000))
#define ACCESS_CONTROL_FORCE_CORE_NS    (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0004))
#define ACCESS_CONTROL_CFGRESET         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0008))
#define ACCESS_CONTROL_GPIOMASK0       (*(volatile uint32_t *)(ACCESS_CONTROL + 0x000C))
#define ACCESS_CONTROL_GPIOMASK1       (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0010))
#define ACCESS_CONTROL_ROM              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0014))
#define ACCESS_CONTROL_XIP_MAIN         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0018))
#define ACCESS_CONTROL_SRAM(block)      (*(volatile uint32_t *)(ACCESS_CONTROL + 0x001C + (block) * 4))  /* block = 0..9 */
#define ACCESS_CONTROL_DMA              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0044))
#define ACCESS_CONTROL_USBCTRL          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0048))
#define ACCESS_CONTROL_PIO0             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x004C))
#define ACCESS_CONTROL_PIO1             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0050))
#define ACCESS_CONTROL_PIO2             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0054))
#define ACCESS_CONTROL_CORESIGHT_TRACE  (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0058))
#define ACCESS_CONTROL_CORESIGHT_PERIPH (*(volatile uint32_t *)(ACCESS_CONTROL + 0x005C))
#define ACCESS_CONTROL_SYSINFO          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0060))
#define ACCESS_CONTROL_RESETS           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0064))
#define ACCESS_CONTROL_IO_BANK0         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0068))
#define ACCESS_CONTROL_IO_BANK1         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x006C))
#define ACCESS_CONTROL_PADS_BANK0       (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0070))
#define ACCESS_CONTROL_PADS_QSPI        (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0074))
#define ACCESS_CONTROL_BUSCTRL          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0078))
#define ACCESS_CONTROL_ADC              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x007C))
#define ACCESS_CONTROL_HSTX             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0080))
#define ACCESS_CONTROL_I2C0             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0084))
#define ACCESS_CONTROL_I2C1             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0088))
#define ACCESS_CONTROL_PWM              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x008C))
#define ACCESS_CONTROL_SPI0             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0090))
#define ACCESS_CONTROL_SPI1             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0094))
#define ACCESS_CONTROL_TIMER0           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x0098))
#define ACCESS_CONTROL_TIMER1           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x009C))
#define ACCESS_CONTROL_UART0            (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00A0))
#define ACCESS_CONTROL_UART1            (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00A4))
#define ACCESS_CONTROL_OTP              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00A8))
#define ACCESS_CONTROL_TBMAN            (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00AC))
#define ACCESS_CONTROL_POWMAN           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00B0))
#define ACCESS_CONTROL_TRNG             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00B4))
#define ACCESS_CONTROL_SHA256           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00B8))
#define ACCESS_CONTROL_SYSCFG           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00BC))
#define ACCESS_CONTROL_CLOCKS           (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00C0))
#define ACCESS_CONTROL_XOSC             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00C4))
#define ACCESS_CONTROL_ROSC             (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00C8))
#define ACCESS_CONTROL_PLL_SYS          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00CC))
#define ACCESS_CONTROL_PLL_USB          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00D0))
#define ACCESS_CONTROL_TICKS            (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00D4))
#define ACCESS_CONTROL_WATCHDOG         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00D8))
#define ACCESS_CONTROL_PSM              (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00DC))
#define ACCESS_CONTROL_XIP_CTRL         (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00E0))
#define ACCESS_CONTROL_XIP_QMI          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00E4))
#define ACCESS_CONTROL_XIP_AUX          (*(volatile uint32_t *)(ACCESS_CONTROL + 0x00E8))


static void rp2350_configure_nvic(void)
{
    /* Disable all interrupts */
    NVIC_ICER0 = 0xFFFFFFFF;
    NVIC_ICPR0 = 0xFFFFFFFF;

    /* Set all interrupts to non-secure */
    NVIC_ITNS0 = 0xFFFFFFFF;
}

static void rp2350_configure_access_control(void)
{
    int i;
    const uint32_t secure_fl = (ACCESS_BITS_SU | ACCESS_BITS_SP | ACCESS_BITS_DMA | ACCESS_BITS_DBG | ACCESS_BITS_CORE0) | ACCESS_MAGIC;
    const uint32_t non_secure_fl = (ACCESS_BITS_NSU | ACCESS_BITS_NSP | ACCESS_BITS_DMA | ACCESS_BITS_DBG | ACCESS_BITS_CORE0 | ACCESS_BITS_CORE1) | ACCESS_MAGIC;


    /* Set access control to Non-secure for upper RAM (0x20040000 - 0x20081FFF) */
    for (i = 0; i < 8; i++)
        ACCESS_CONTROL_SRAM(i) = non_secure_fl | secure_fl;
    for (i = 8; i < 10; i++)
        ACCESS_CONTROL_SRAM(i) = secure_fl;

    /* Set access control for peripherals */
    ACCESS_CONTROL_ROM = secure_fl | non_secure_fl;
    ACCESS_CONTROL_XIP_MAIN = non_secure_fl | secure_fl;
    ACCESS_CONTROL_DMA = non_secure_fl;
    ACCESS_CONTROL_TRNG = secure_fl;
    ACCESS_CONTROL_SYSCFG = secure_fl;
    ACCESS_CONTROL_SHA256 = secure_fl;
    ACCESS_CONTROL_IO_BANK0 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_IO_BANK1 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_PADS_BANK0 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_PIO0 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_PIO1 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_PIO2 = non_secure_fl | secure_fl;

    ACCESS_CONTROL_I2C0   = non_secure_fl |secure_fl;
    ACCESS_CONTROL_I2C1   = non_secure_fl | secure_fl;
    ACCESS_CONTROL_PWM    = non_secure_fl | secure_fl;
    ACCESS_CONTROL_SPI0   = non_secure_fl | secure_fl;
    ACCESS_CONTROL_SPI1   = non_secure_fl | secure_fl;
    ACCESS_CONTROL_TIMER0 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_TIMER1 = non_secure_fl | secure_fl;
    ACCESS_CONTROL_UART0  = non_secure_fl | secure_fl;
    ACCESS_CONTROL_UART1  = non_secure_fl | secure_fl;
    ACCESS_CONTROL_ADC    = non_secure_fl | secure_fl;
    ACCESS_CONTROL_RESETS = non_secure_fl | secure_fl;
    ACCESS_CONTROL_USBCTRL= non_secure_fl | secure_fl;
    /* Force core 1 to non-secure */
    ACCESS_CONTROL_FORCE_CORE_NS = (1 << 1) | ACCESS_MAGIC;

    /* GPIO masks: Each bit represents "NS allowed" for a GPIO pin */
    ACCESS_CONTROL_GPIOMASK0 = 0xFFFFFFFF;
    ACCESS_CONTROL_GPIOMASK1 = 0xFFFFFFFF;

    CPACR |= 0x000000FF; /* Enable access to coprocessors CP0-CP7 */
    NSACR |= 0x000000FF; /* Enable non-secure access to coprocessors CP0-CP7 */

    /* Lock access control */
    ACCESS_CONTROL_LOCK = non_secure_fl | secure_fl;
}

void machine_init(void)
{
    set_sys_clock_khz(120000, true);
    gpio_set_function(24, GPIO_FUNC_USB);          // DM
    gpio_set_function(25, GPIO_FUNC_USB);          // DP
    gpio_disable_pulls(24);
    gpio_disable_pulls(25);

    pll_init(pll_usb, 1, 480 * MHZ, 5, 2);  // VCO=480MHz -> 480/(5*2)=48MHz

    clock_configure(clk_usb,
            0,  // no glitchless mux for clk_usb
            CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
            48 * MHZ,   // src_freq (PLL_USB out)
            48 * MHZ);  // target freq

    rp2350_configure_nvic();
    rp2350_configure_access_control();
}
