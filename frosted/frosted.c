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
#include "kprintf.h"
#include "bflt.h"
#include "null.h"
#include "xipfs.h"
#include "gpio.h"
#include "uart.h"
#include "rng.h"
#include "sdram.h"
#include "socket_in.h"
#include "fatfs.h"
#include "framebuffer.h"
#include "ltdc.h"
#include "fbcon.h"
#include "eth.h"
#include "exti.h"
#include "pty.h"
#include "lowpower.h"
#include "tty_console.h"
#include "systick.h"
#include "tusb.h"



#define IDLE() while(1){do{}while(0);}

static int tcpip_timer_pending = 0;

/* The following needs to be defined by
 * the application code
 */
void (*init)(void *arg) = (void (*)(void*))(CONFIG_APPS_ORIGIN);

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


__attribute__((naked)) void hard_fault_handler(void)
{
    __asm("MOVS R0, #4          \n"
          "MOVS R1, LR          \n"
          "TST R0, R1           \n"
          "BEQ _MSP             \n"
          "MRS R0, PSP          \n"
          "B hardfault_handler_dbg \n"
      "_MSP:"
          "MRS R0, MSP          \n"
          "B hardfault_handler_dbg \n"
           );
}

void mem_manage_handler(void)
{
#   define ARM_CFSR (*(volatile uint32_t *)(0xE000ED28))
#   define ARM_MMFAR (*(volatile uint32_t *)(0xE000ED34))
    volatile uint32_t address = 0xFFFFFFFF;
    volatile uint32_t instruction = 0xFFFFFFFF;
    uint32_t *top_stack;

    if ((ARM_CFSR & 0x80)!= 0) {
        address = ARM_MMFAR;
        asm volatile ("mrs %0, psp" : "=r" (top_stack));
        instruction = *(top_stack - 1);
    }

    if (task_segfault(address, instruction, MEMFAULT_ACCESS) < 0)
        while(1);
}

void bus_fault_handler(void)
{
    while(1);
}

void usage_fault_handler(void)
{
    while(1);
}

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


extern unsigned long __core1_ns_ivt;
extern unsigned long __core1_ns_stack_top;

void cpu1_main(void)
{
    while(1)
    {
        check_tasklets();
        asm volatile("wfe");
    }

}

void core1_ns_reset(void) {
    cpu1_main();
    while(1)
        ;
}

__attribute__((section(".core1_vectors")))
const void *core1_ivt[] = {
     &__core1_ns_stack_top,
     core1_ns_reset,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};


void secure_core1_start(uint32_t vtor_ns, uint32_t sp_ns, uint32_t entry_ns);
void cpu1_start(void)
{
    task_stop(get_kernel());
    secure_core1_start((uintptr_t)&__core1_ns_ivt,
                       (uintptr_t)&__core1_ns_stack_top,
                       (uintptr_t)&cpu1_main);
}

void frosted_usbdev_init(void);
int vfs_mount(char *source, char *target, char *module, uint32_t flags, void *args);
int frosted_init(void)
{
    extern void * _k__syscall__;
    int xip_mounted;
    /* ktimers must be enabled before systick */
    ktimer_init();
    

    kernel_task_init();
    

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
    fpb_init();
#endif

    vfs_init();
    devnull_init(fno_search("/dev"));
    
    hw_init();
    frosted_usbdev_init();

    /* Set up system */


    mpu_init();
    

    syscalls_init();

    memfs_init();
    xipfs_init();
    sysfs_init();
    fatfs_init();

    ltdc_init();
    fbcon_init( 480, 272);
    tty_console_init();

    vfs_mount(NULL, "/tmp", "memfs", 0, NULL);
    xip_mounted = vfs_mount((char *)init, "/bin", "xipfs", 0, NULL);
    vfs_mount(NULL, "/sys", "sysfs", 0, NULL);

    klog_init();


#ifdef UNIX
    socket_un_init();
#endif
    cpu1_start();

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
    frosted_kernel(xipfs_mounted); /* never returns */
}

