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
#include <stdint.h>

#include "armv8m_tz.h"
#include "task.h"
#include "limits.h"
#include "mempool.h"
#include "../frosted/include/nvic.h"

/* Include target‑specific headers
 *  - Pico SDK for rp2350 builds
 *  - ST HAL for stm32u585 builds
 */
#if defined(TARGET_STM32U585)
#   include "stm32u5xx-hal-driver/Inc/stm32u5xx_hal.h"
#   include "stm32u5xx-hal-driver/Inc/stm32u5xx_hal_cortex.h"
#   include "stm32u5xx-hal-driver/Inc/stm32u5xx_hal_gpio.h"
#elif defined(TARGET_RP2350)
#   include "pico/stdlib.h"
#   include "pico/stdio.h"
#elif defined(TARGET_STM32H563)
#   include "stm32h563.h"
#endif

#include "random.h"

static void sau_init(void)
{

#if defined(TARGET_STM32H563) || defined(TARGET_STM32U585)
    /* Secure supervisor flash */
    sau_init_region(0, SAU_FLASH_SECURE_START, SAU_FLASH_SECURE_END, 1<<1);

    /* NSC area: supervisor flash callables */
    sau_init_region(1, SAU_FLASH_NSC_START, SAU_FLASH_NSC_END, 1<<1);

    /* Non-secure frosted scheduler + Userland (contiguous) */
    sau_init_region(2, SAU_FLASH_NS_START, SAU_FLASH_NS_END, 0);

    /* Secure RAM */
    sau_init_region(4, SAU_RAM_SECURE_START, SAU_RAM_SECURE_END, 1<<1);

    /* Non-secure RAM (mempool / kernel data) */
    sau_init_region(5, SAU_RAM_NS_START, SAU_RAM_NS_END, 0);

    /* Non-secure peripheral space */
    sau_init_region(6, SAU_PERIPH_START, SAU_PERIPH_END, 0);

#else
    /* Secure supervisor flash */
    sau_init_region(0, SAU_FLASH_SECURE_START, SAU_FLASH_SECURE_END, 1);

    /* NSC area: supervisor flash callables */
    sau_init_region(1, SAU_FLASH_NSC_START, SAU_FLASH_NSC_END, 1);

    /* Non-secure frosted scheduler */
    sau_init_region(2, SAU_FLASH_NS_START, SAU_FLASH_NS_END, 0);

    /* XIPfs */
    sau_init_region(3, SAU_FLASH_XIP_START, SAU_FLASH_XIP_END, 0);

    /* RAM: secure area stack */
    sau_init_region(4, SAU_RAM_SECURE_START, SAU_RAM_SECURE_END, 1);

    /* RAM: non-secure area (managed via MPU) */
    sau_init_region(5, SAU_RAM_NS_START, SAU_RAM_NS_END, 0);

    /* RAM: secure area for supervisor's stack */
    sau_init_region(6, SAU_RAM_SECURE_START, SAU_RAM_SECURE_START + 0x1FFFU, 1);

    /* USB DRAM: NS */
    sau_init_region(7, SAU_USB_RAM_START, SAU_USB_RAM_END, 0);
#endif

    SAU_CTRL = SAU_INIT_CTRL_ENABLE;

    /* Enable SecureFault, MemFault, BusFault and UsageFault in Secure mode */
    SCB_SHCSR |= SCB_SHCSR_SECUREFAULT_EN;

    /* Enable faults in Non-secure mode */
    SCB_SHCSR_NS |= SCB_SHCSR_MEMFAULT_EN | SCB_SHCSR_BUSFAULT_EN | SCB_SHCSR_USGFAULT_EN;

    /* Add flag to trap misaligned accesses */
    //*((volatile uint32_t *)0xE000ED14) |= 0x00000008;
}

__attribute__((weak))
void machine_init(void)
{
}

#define USB_MAIN *((volatile uint32_t *)(USBCTRL_BASE + 0x40))
#define USB_MAIN_PHY_ISO (1 << 2)
#define USB_MAIN_CONTROLLER_EN (1 << 0)

void main(void) {
    /* Machine‑specific code */
    machine_init();

#ifdef CONFIG_FPU
    /* FPU: enable */
    fpu_enable();
#endif

    /* Initialize shared SRAM before reprogramming TZ attributes. */
    mempool_init();

#if defined(TARGET_STM32H563)
    stm32h5_gtzc_setup();
    stm32h5_configure_gpio_security();
#endif

    /* SAU */
    sau_init();


    /* Secure tasks table init */
    secure_task_table_init();

    /* TRNG */
    trng_init();

    /* Configure Non-Secure vector table */
    SCB_VTOR_NS = NS_START_ADDR;

    #ifdef TARGET_RP2350
    /* detach USB (rp2350) */
    stdio_flush();
    stdio_deinit_all();
    USB_MAIN |= USB_MAIN_PHY_ISO; // PHY isolation
    USB_MAIN &= (~USB_MAIN_CONTROLLER_EN); // Disable controller

    gpio_set_function(24, GPIO_FUNC_USB);          // DM
    gpio_set_function(25, GPIO_FUNC_USB);          // DP
    gpio_disable_pulls(24);
    gpio_disable_pulls(25);
    #endif

    /* Set Non-Secure MSP */
    asm volatile ( "msr msp_ns, %0\n" : : "r" (*((volatile uint32_t *) NS_START_ADDR)));

    /* Transition to Non-Secure domain kernel */
    asm volatile( "bxns %0\n" : : "r"(*((volatile uint32_t *)(NS_START_ADDR + 4)) + 1) );

    /* Idle loop - Never reached */
    while (1) {
        __asm__("wfi");
    }
}
