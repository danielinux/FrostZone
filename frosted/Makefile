TARGET?=stm32h563

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
CFLAGS  = -mcpu=cortex-m33 -mthumb -O0 -Wall -ggdb -ffreestanding -nostdlib -Iinclude -I../frosted-headers/include
CFLAGS += -Ilibc/include
CFLAGS += -mlong-calls -fno-lto -fno-common -fno-toplevel-reorder
CFLAGS += -nostartfiles -nostdinc
LINKER_SCRIPT = $(TARGET).ld
MAP_FILE = kernel.map
LDFLAGS = -T$(LINKER_SCRIPT) -Wl,--gc-sections -Wl,-Map=$(MAP_FILE)


# Default Configuration: STM32U585. TODO: make ?= configurable
CONFIG_SYS_CLOCK=160000000
CONFIG_APPS_ORIGIN=0x08030000
CONFIG_FLASH_ORIGIN=0x08010000
CONFIG_DEVUSB=0
CFG_TUD_ENABLED=0
CONFIG_USB_NET=0
CONFIG_SIGNALS=1
CONFIG_PTY_UNIX=1
CONFIG_EXTENDED_MEMFAULT=1
CONFIG_RELOCATE_VECTORS_TO_RAM=0
LINK_MTU?=1514
CONFIG_ETH?=1

ifeq ($(TARGET),stm32h563)
CONFIG_SYS_CLOCK=250000000
CONFIG_APPS_ORIGIN=0x08030000
CONFIG_FLASH_ORIGIN=0x08010000
LINKER_SCRIPT=stm32h563.ld
endif

CFLAGS += -DTARGET_$(TARGET)
CFLAGS += -DCONFIG_SYS_CLOCK=$(CONFIG_SYS_CLOCK)
CFLAGS += -DCONFIG_APPS_ORIGIN=$(CONFIG_APPS_ORIGIN)
CFLAGS += -DCONFIG_FLASH_ORIGIN=$(CONFIG_FLASH_ORIGIN)
CFLAGS += -DCONFIG_DEVUSB=$(CONFIG_DEVUSB)
CFLAGS += -DCFG_TUD_ENABLED=$(CFG_TUD_ENABLED)
CFLAGS += -DCONFIG_USB_NET=$(CONFIG_USB_NET)
CFLAGS += -DCONFIG_SIGNALS=$(CONFIG_SIGNALS)
CFLAGS += -DCONFIG_PTY_UNIX=$(CONFIG_PTY_UNIX)
CFLAGS += -DCONFIG_EXTENDED_MEMFAULT=$(CONFIG_EXTENDED_MEMFAULT)
CFLAGS += -DCONFIG_RELOCATE_VECTORS_TO_RAM=$(CONFIG_RELOCATE_VECTORS_TO_RAM)
CFLAGS += -DCONFIG_DEVUART=1
CFLAGS += -DLINK_MTU=$(LINK_MTU)
CFLAGS += -DCONFIG_ETH=$(CONFIG_ETH)

CFLAGS += -DSEMAPHORES



SRCS = startup_ns.c ns_main.c \
	    bflt.c \
		cirbuf.c \
		device.c \
		fpb.c \
		frosted.c \
		getaddrinfo.c \
		locks.c \
		memfs.c \
		module.c \
		null.c \
		pipe.c \
		privileged_alloc.c \
		pty.c \
		scheduler.c \
		socket_in.c \
		socket_un.c \
		string.c \
		sys.c \
		syscall_table.c \
		sysfs.c \
		systick.c \
		tasklet.c \
		term.c \
		tty_console.c \
		uart.c \
		usb.c \
		usb_descriptors.c \
		vfs.c \
		wolfip.c \
		semaphore.S \
		mutex.S \
		xipfs.c 

ifeq ($(CONFIG_ETH),1)
SRCS += \
	stm32_eth.c
endif

SRCS += frosted_$(TARGET).c
OBJS = $(SRCS:.c=.o) ../secure-supervisor/nsc_kernel.o



all: kernel.elf kernel.bin

kernel.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin *.map
