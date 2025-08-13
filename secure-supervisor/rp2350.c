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
#include "string.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/gpio.h"

#include "hardware/regs/usb.h"
#include "hardware/structs/usb.h"
#include "hardware/resets.h"
#include "pico/multicore.h"

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
    const uint32_t secure_fl = (ACCESS_BITS_SU | ACCESS_BITS_SP | ACCESS_BITS_DMA | ACCESS_BITS_DBG | ACCESS_BITS_CORE0 | ACCESS_BITS_CORE1) | ACCESS_MAGIC;
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
    // ACCESS_CONTROL_FORCE_CORE_NS = (1 << 1) | ACCESS_MAGIC;

    /* GPIO masks: Each bit represents "NS allowed" for a GPIO pin */
    ACCESS_CONTROL_GPIOMASK0 = 0xFFFFFFFF;
    ACCESS_CONTROL_GPIOMASK1 = 0xFFFFFFFF;

    CPACR |= 0x000000FF; /* Enable access to coprocessors CP0-CP7 */
    NSACR |= 0x000000FF; /* Enable non-secure access to coprocessors CP0-CP7 */

    /* Lock access control */
    ACCESS_CONTROL_LOCK = non_secure_fl | secure_fl;
}


typedef struct {
  uint32_t vtor_ns;   // NS vector table base
  uint32_t msp_ns;    // NS stack pointer value (top-of-stack)
  uint32_t entry_ns;  // NS entry point (Thumb bit will be OR'ed)
} core1_ns_boot_t;



volatile core1_ns_boot_t core1_ns_boot;


extern unsigned long __core1_kickstart;
extern unsigned long __core1_ivt;
extern unsigned long __core1_stack_top;

__attribute__((used, aligned(128), section(".core1_ivt")))
void * __core1_sec_vtor[] = {
    (void *)&__core1_stack_top,                    // SP
    (void *)((uintptr_t)(&__core1_kickstart)),             // Reset (Thumb)
    0,0,0,0,0,0,0,0,                                   // keep tiny
};

// Secure stub that runs first on core1, then switches to NS
#define SCB_NS_VTOR (*(volatile uint32_t *)0xE002ED08u)   // Secure alias of VTOR_NS
static inline void set_msp_ns(uint32_t v) { __asm volatile ("msr MSP_NS, %0" :: "r"(v)); }

__attribute__((noreturn, section(".core1_kickstart")))
void core1_secure_entry(void) {
    // Program Non-secure context from the values provided by NS
    SCB_NS_VTOR = core1_ns_boot.vtor_ns;
    set_msp_ns(core1_ns_boot.msp_ns);
    asm volatile ("dsb");
    asm volatile ("isb");

    uint32_t pc_ns = core1_ns_boot.entry_ns | 1u;  // ensure Thumb
    __asm volatile ("bxns %0" :: "r"(pc_ns) : "memory");
    __builtin_unreachable();
}

// NS-callable veneer: stash NS context, then use SDK to launch core1
__attribute__((cmse_nonsecure_entry))
void secure_core1_start(uint32_t vtor_ns, uint32_t sp_ns, uint32_t entry_ns) {
    core1_ns_boot.vtor_ns  = vtor_ns;
    core1_ns_boot.msp_ns   = sp_ns;
    core1_ns_boot.entry_ns = entry_ns;

    // Reset core1 (optional but tidy), then launch with explicit SP & VTOR
    multicore_reset_core1();
    extern void * __core1_sec_vtor[];                // secure VTOR we defined
    extern uint32_t __core1_stack_top;
    multicore_launch_core1_raw(core1_secure_entry,
                               (uint32_t *)&__core1_stack_top,
                               (uint32_t)__core1_sec_vtor);
}

void machine_init(void)
{
    set_sys_clock_khz(120000, true);

    rp2350_configure_access_control();
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
                        //
    // Reset -> unreset USBCTRL
    reset_block(RESETS_RESET_USBCTRL_BITS);
    unreset_block_wait(RESETS_RESET_USBCTRL_BITS);

    // Clear DPRAM
    memset(usb_dpram, 0, sizeof(*usb_dpram));

    // Route controller to on-chip PHY, force VBUS present
    usb_hw->muxing = USB_USB_MUXING_TO_PHY_BITS | USB_USB_MUXING_SOFTCON_BITS;
    usb_hw->pwr    = USB_USB_PWR_VBUS_DETECT_BITS | USB_USB_PWR_VBUS_DETECT_OVERRIDE_EN_BITS;

    // Clear PHY isolation (RP2350) and enable controller
    hw_clear_bits(&usb_hw->main_ctrl, USB_MAIN_CTRL_PHY_ISO_BITS);
    hw_set_bits(&usb_hw->main_ctrl, USB_MAIN_CTRL_CONTROLLER_EN_BITS);

    // Optional: per-transaction EP0 IRQ (kept disabled until NS takes over)
#ifdef USB_SIE_CTRL_EP0_INT_1BUF_BITS
    hw_set_bits(&usb_hw->sie_ctrl, USB_SIE_CTRL_EP0_INT_1BUF_BITS);
#endif

    // DO NOT enable device pull-up yet; DO NOT enable block interrupts yet
    hw_clear_bits(&usb_hw->sie_ctrl, USB_SIE_CTRL_PULLUP_EN_BITS);
    usb_hw->inte = 0;

    rp2350_configure_nvic();

}
