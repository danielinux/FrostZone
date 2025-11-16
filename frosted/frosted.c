/*
 *      This file is part of frosted.
 *
 *      frosted is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the free Software Foundation.
 *
 *
 *      frosted is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frosted.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Authors: Daniele Lacamera, Maxime Vincent
 *
 */
#include "frosted.h"
#include "eth.h"
#include "kprintf.h"
#include "bflt.h"
#include "null.h"
#include "xipfs.h"
#include "gpio.h"
#include "uart.h"
#include "rng.h"
#include "sdram.h"
#if CONFIG_TCPIP
#include "socket_in.h"
#endif
#include "fatfs.h"
#include "framebuffer.h"
#include "tft.h"
#include "fbcon.h"
#include "eth.h"
#include "exti.h"
#include "pty.h"
#include "lowpower.h"
#include "tty_console.h"
#include "systick.h"
//#include "tusb.h"
#include "nvic.h"
#include "string.h"
#include "flashfs.h"
#ifdef CONFIG_PIPE
void sys_pipe_init(void);
#endif

#if CONFIG_USB && CONFIG_USB_NET
int netusb_init(void);
#endif



#define IDLE() while(1){do{}while(0);}

static int tcpip_timer_pending = 0;

#if CONFIG_TCPIP
static mutex_t *tcpip_mutex;

static void tcpip_lock_ensure(void)
{
    if (!tcpip_mutex)
        tcpip_mutex = mutex_init();
}

void tcpip_lock_init(void)
{
    tcpip_lock_ensure();
}

void tcpip_lock(void)
{
    tcpip_lock_ensure();
    mutex_lock(tcpip_mutex);
}

void tcpip_unlock(void)
{
    if (tcpip_mutex)
        mutex_unlock(tcpip_mutex);
}

int tcpip_trylock(void)
{
    tcpip_lock_ensure();
    return mutex_trylock(tcpip_mutex);
}
#endif

#define ARM_CFSR (*(volatile uint32_t *)(0xE000ED28))
#define ARM_MMFAR_REG ((volatile uint32_t *)(0xE000ED34))
#define ARM_BFAR_REG ((volatile uint32_t *)(0xE000ED38))
#define SCB_CFSR (*(volatile uint32_t *)(0xE000ED28))
#define SCB_HFSR (*(volatile uint32_t *)(0xE000ED2C))

#define SCB_HFSR_FORCED (1u << 30)

/* The following needs to be defined by
 * the application code
 */
static const char *const xipfs_image = (const char *)CONFIG_APPS_ORIGIN;

void simple_hard_fault_handler(void)
{
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
    volatile uint32_t hfsr = SCB_HFSR;
    //volatile uint32_t bfsr = SCB_BFSR;
    volatile uint32_t afsr = SCB_AFSR;
    volatile uint32_t bfar = SCB_BFAR;
    //volatile uint32_t ufsr = SCB_UFSR;
    volatile uint32_t mmfar = SCB_MMFAR;
#endif
    while(1);
}

#ifdef CONFIG_HARDFAULT_DBG

volatile unsigned long stacked_r0 ;
volatile unsigned long stacked_r1 ;
volatile unsigned long stacked_r2 ;
volatile unsigned long stacked_r3 ;
volatile unsigned long stacked_r12 ;
volatile unsigned long stacked_lr ;
volatile unsigned long stacked_pc ;
volatile unsigned long stacked_psr ;
volatile unsigned long _CFSR ;
volatile unsigned long _HFSR ;
volatile unsigned long _DFSR ;
volatile unsigned long _AFSR ;
volatile unsigned long _BFAR ;
volatile unsigned long _MMAR ;

void hardfault_handler_dbg(unsigned long *hardfault_args){
    stacked_r0 = ((unsigned long)hardfault_args[0]) ;
    stacked_r1 = ((unsigned long)hardfault_args[1]) ;
    stacked_r2 = ((unsigned long)hardfault_args[2]) ;
    stacked_r3 = ((unsigned long)hardfault_args[3]) ;
    stacked_r12 = ((unsigned long)hardfault_args[4]) ;
    stacked_lr = ((unsigned long)hardfault_args[5]) ;
    stacked_pc = ((unsigned long)hardfault_args[6]) ;
    stacked_psr = ((unsigned long)hardfault_args[7]) ;

    // Configurable Fault Status Register
    // Consists of MMSR, BFSR and UFSR
    _CFSR = (*((volatile unsigned long *)(0xE000ED28))) ;



    // Hard Fault Status Register
    _HFSR = (*((volatile unsigned long *)(0xE000ED2C))) ;

    // Debug Fault Status Register
    _DFSR = (*((volatile unsigned long *)(0xE000ED30))) ;

    // Auxiliary Fault Status Register
    _AFSR = (*((volatile unsigned long *)(0xE000ED3C))) ;

    // Read the Fault Address Registers. These may not contain valid values.
    // Check BFARVALID/MMARVALID to see if they are valid values
    // MemManage Fault Address Register
    _MMAR = (*((volatile unsigned long *)(0xE000ED34))) ;
    // Bus Fault Address Register
    _BFAR = (*((volatile unsigned long *)(0xE000ED38))) ;
    __asm("BKPT #0") ; // Break into the debugger
}
#else
void hardfault_handler_dbg(unsigned long *sp)
{
    __asm("BKPT #0") ; // Break into the debugger
}

#endif

static void process_memory_fault(uint32_t fault_type)
{
    if (task_segfault(fault_type) < 0) {
        while(1)
            ;
    }
}


void hard_fault_handler(void)
{
    while(1)
        ;
}

void mem_manage_handler(void)
{
    process_memory_fault(FAULT_TYPE_MPU);
    asm volatile("isb");
}

void bus_fault_handler(void)
{
    process_memory_fault(FAULT_TYPE_BUS);
    asm volatile("isb");
}

void usage_fault_handler(void)
{
    process_memory_fault(FAULT_TYPE_USAGE);
    asm volatile("isb");
}

void secure_violation_handler(void)
{
    process_memory_fault(FAULT_TYPE_SECURE);
    asm volatile("isb");
}


#if (CONFIG_RELOCATE_VECTORS_TO_RAM)
extern uintptr_t *_ram_vectors;
extern uintptr_t *_flash_vectors;
void relocate_vectors(void)
{
    uintptr_t flash_iv = &_flash_vectors;
    uintptr_t ram_iv = &_ram_vectors;
    memcpy(ram_iv, flash_iv, 0x200);
    SCB_VTOR = ram_iv;
    asm volatile("dsb");
}
#endif

static void hw_init(void)
{
    gpio_init();
    exti_init();
    uart_init();
    ptmx_init();
    rng_init();
    sdram_init();
    machine_init();
    lowpower_init();
    frosted_systick_config(CONFIG_SYS_CLOCK);
}

void frosted_usbdev_init(void);
int vfs_mount(char *source, char *target, char *module, uint32_t flags, void *args);
int frosted_init(void)
{
    int xip_mounted;

    nvic_enable_memfault();
    nvic_enable_busfault();
    nvic_enable_secure_violation();


    /* Disable FPU */
    #define NS_FPU_FPCCR   (*(volatile uint32_t *)0xE000EF34UL)  /* FPCCR @ +0xEF34 */
    #define FPCCR_ASPEN    (1u << 31)
    #define FPCCR_LSPEN    (1u << 30)
    NS_FPU_FPCCR &= ~(FPCCR_ASPEN | FPCCR_LSPEN);

#if (CONFIG_RELOCATE_VECTORS_TO_RAM)
    relocate_vectors();
#endif
    /* ktimers must be enabled before systick */
    ktimer_init();
    kernel_task_init();
    

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
    fpb_init();
#endif

    vfs_init();
    devnull_init(fno_search("/dev"));
    
    hw_init();

    /* Set up system */

    syscalls_init();
#ifdef CONFIG_PIPE
    sys_pipe_init();
#endif

    memfs_init();
    xipfs_init();
    sysfs_init();
    fatfs_init();
#if defined(CONFIG_FLASHFS)
    flashfs_init();
#endif

    tft_init();
    fbcon_init(320, 240);
    tty_console_init();

    vfs_mount(NULL, "/tmp", "memfs", 0, NULL);
    xip_mounted = vfs_mount((char *)xipfs_image, "/bin", "xipfs", 0, NULL);
    vfs_mount(NULL, "/sys", "sysfs", 0, NULL);
#if defined(CONFIG_FLASHFS)
    vfs_mount(NULL, "/var", "flashfs", 0, NULL);
#endif

    klog_init();

#if CONFIG_TCPIP
    socket_in_init();
#endif


#ifdef UNIX
    socket_un_init();
#endif

#ifdef CONFIG_USB
    frosted_usbdev_init();
#endif

#ifdef CONFIG_USB
#if CONFIG_USB_NET
    netusb_init();
#endif
#endif

#if CONFIG_ETH
    /* Initialize Ethernet if enabled.  The driver ignores the config
     * parameter and only requires the function call to start the
     * device and IP stack. */
    ethernet_init(NULL);
#endif

    return xip_mounted;
}

static const char init_path[] = "/bin/init";
static const char *const init_args[2] = { init_path, NULL };

#ifdef CONFIG_USBHOST
struct usbh_host *USBHost = NULL;
#endif

void frosted_kernel(int xipfs_mounted)
{
    struct task_exec_info info = {};

    if (xipfs_mounted == 0)
    {
        struct fnode *fno = fno_search(init_path);
        void * memptr;
        size_t mem_size;
        size_t stack_size;
        uint32_t got_loc;
        if (!fno) {
            /* PANIC: Unable to find /bin/init */
            while(1 < 2);
        }

        if (fno->owner && fno->owner->ops.exe) {
            if (fno->owner->ops.exe(fno, (void *)init_args, &info) != 0)
            {
                /* PANIC: Unable to execute /bin/init */
                while(1 < 2);
            }
            task_create(&info, (void *)init_args, NICE_DEFAULT);
        }
    } else {
        IDLE();
    }

    frosted_scheduler_on();

    while(1) {
        check_tasklets();
        asm volatile ("wfe");
    }
}

/* OS entry point */
void frosted_main(void)
{
    int xipfs_mounted;
    xipfs_mounted = frosted_init();
    mpu_init();
    frosted_kernel(xipfs_mounted); /* never returns */
}
