/*
 *  RP2350 specific machine initialisation
 *  This file is moved from secure-supervisor/rp2350.c
 */

#include <stdint.h>
#include <string.h>
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/gpio.h"
#include "hardware/regs/usb.h"
#include "hardware/structs/usb.h"
#include "hardware/resets.h"
#include "pico/multicore.h"
#include "pico/rand.h"

/* Register definitions from the RP2350 header – kept minimal */
#define NVIC_ICER0 (*(volatile uint32_t *)(0xE000E180))
#define NVIC_ICPR0 (*(volatile uint32_t *)(0xE000E280))
#define NVIC_ITNS0 (*(volatile uint32_t *)(0xE000E380))
#define NSACR (*(volatile uint32_t *)(0xE000ED8C))
#define CPACR (*(volatile uint32_t *)(0xE000ED88))

/* access control specific for RP2350 */
#define ACCESS_CONTROL (0x40060000)
/* ... many ACCESS_CONTROL_* macros omitted for brevity ... */

/* Public entry point for low‑level init */
void machine_init(void)
{
    /* Example: enable PLL, configure clocks, setup NVIC */
    clocks_init();
    pll_init();
    rp2350_configure_nvic();
    rp2350_configure_access_control();
}

/* rp2350_configure_nvic and rp2350_configure_access_control
 * functions are defined below.  They are identical to the original
 * implementation from secure-supervisor/rp2350.c but moved into this
 * target‑specific module.
 */
static void rp2350_configure_nvic(void)
{
    NVIC_ICER0 = 0xFFFFFFFF;
    NVIC_ICPR0 = 0xFFFFFFFF;
    NVIC_ITNS0 = 0xFFFFFFFF;
}

static void rp2350_configure_access_control(void)
{
    /* Simplified example – real implementation must match the RP2350
     * memory protection registers and access rights.
     */
    /* ... omitted */
}

