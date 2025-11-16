CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
CFLAGS  = -mcpu=cortex-m33 -mthumb -O0 -Wall -ggdb -ffreestanding -nostdlib -Iinclude -I../frosted-headers/include
CFLAGS += -Ilibc/include -I../nsc-gateway
CFLAGS += -mlong-calls -fno-lto -fno-common -fno-toplevel-reorder
CFLAGS += -nostartfiles -nostdinc -Wdeclaration-after-statement
MAP_FILE = kernel.map

KCONFIG_DIR := $(abspath $(CURDIR)/../userland/kconfig)
KCONFIG_FILE := $(abspath $(CURDIR)/Kconfig)
KCONFIG_CONFIG ?= $(abspath $(CURDIR)/.config)

ifeq ($(wildcard $(KCONFIG_CONFIG)),)
$(error Frosted configuration missing ($(KCONFIG_CONFIG)). Run 'make menuconfig' first.)
endif

include $(KCONFIG_CONFIG)

kconfig_bool = $(strip $(if $(filter y Y 1,$(1)),1,0))

ifeq ($(TARGET_STM32H563),y)
TARGET := stm32h563
LINKER_SCRIPT := stm32h563.ld
else ifeq ($(TARGET_STM32U585),y)
TARGET := stm32u585
LINKER_SCRIPT := stm32u585.ld
else ifeq ($(TARGET_RP2350),y)
TARGET := rp2350
LINKER_SCRIPT := pico2350.ld
else
$(error No target selected in $(KCONFIG_CONFIG))
endif

ifndef SYS_CLOCK
$(error SYS_CLOCK is not defined in $(KCONFIG_CONFIG))
endif
CONFIG_SYS_CLOCK := $(SYS_CLOCK)

ifndef APPS_ORIGIN
$(error APPS_ORIGIN is not defined in $(KCONFIG_CONFIG))
endif
CONFIG_APPS_ORIGIN := $(APPS_ORIGIN)

ifndef FLASH_ORIGIN
$(error FLASH_ORIGIN is not defined in $(KCONFIG_CONFIG))
endif
CONFIG_FLASH_ORIGIN := $(FLASH_ORIGIN)

ifndef LINK_MTU
$(error LINK_MTU is not defined in $(KCONFIG_CONFIG))
endif

CONFIG_FLASHFS := $(call kconfig_bool,$(FLASHFS))
CONFIG_FLASHFS_VERIFY := $(call kconfig_bool,$(FLASHFS_VERIFY))
CONFIG_DEVUSB := $(call kconfig_bool,$(DEVUSB))
CFG_TUD_ENABLED := $(call kconfig_bool,$(CFG_TUD_ENABLED))
CONFIG_USB_NET := $(call kconfig_bool,$(USB_NET))
CONFIG_SIGNALS := $(call kconfig_bool,$(SIGNALS))
CONFIG_PTY_UNIX := $(call kconfig_bool,$(PTY_UNIX))
CONFIG_PIPE := $(call kconfig_bool,$(PIPE))
CONFIG_CORE_DUMP := $(call kconfig_bool,$(CORE_DUMP))
CONFIG_EXTENDED_MEMFAULT := $(call kconfig_bool,$(EXTENDED_MEMFAULT))
CONFIG_RELOCATE_VECTORS_TO_RAM := $(call kconfig_bool,$(RELOCATE_VECTORS_TO_RAM))
CONFIG_TCPIP := $(call kconfig_bool,$(TCPIP))
CONFIG_ETH := $(call kconfig_bool,$(ETH))
CONFIG_MPU := $(call kconfig_bool,$(MPU))
CONFIG_DEVFRAMEBUFFER := $(call kconfig_bool,$(DEVFRAMEBUFFER))
CONFIG_DEVFBCON := $(call kconfig_bool,$(DEVFBCON))
CONFIG_DEVTTY_CONSOLE := $(call kconfig_bool,$(DEVTTY_CONSOLE))
CONFIG_ILI9341 := $(call kconfig_bool,$(ILI9341))
CONFIG_SPI1 := $(call kconfig_bool,$(SPI1))

LDFLAGS = -T$(LINKER_SCRIPT) -Wl,--gc-sections -Wl,-Map=$(MAP_FILE)

CFLAGS += -DTARGET_$(TARGET)
CFLAGS += -DCONFIG_SYS_CLOCK=$(CONFIG_SYS_CLOCK)
CFLAGS += -DCONFIG_APPS_ORIGIN=$(CONFIG_APPS_ORIGIN)
CFLAGS += -DCONFIG_FLASH_ORIGIN=$(CONFIG_FLASH_ORIGIN)
CFLAGS += -DCONFIG_DEVUSB=$(CONFIG_DEVUSB)
CFLAGS += -DCONFIG_TUD_ENABLED=$(CFG_TUD_ENABLED)
CFLAGS += -DCFG_TUD_ENABLED=$(CFG_TUD_ENABLED)
CFLAGS += -DCONFIG_USB_NET=$(CONFIG_USB_NET)
CFLAGS += -DCFG_TUD_NCM=$(CONFIG_USB_NET)
CFLAGS += -DCONFIG_SIGNALS=$(CONFIG_SIGNALS)
CFLAGS += -DCONFIG_PTY_UNIX=$(CONFIG_PTY_UNIX)
CFLAGS += -DCONFIG_PIPE=$(CONFIG_PIPE)
CFLAGS += -DCONFIG_CORE_DUMP=$(CONFIG_CORE_DUMP)
CFLAGS += -DCONFIG_EXTENDED_MEMFAULT=$(CONFIG_EXTENDED_MEMFAULT)
CFLAGS += -DCONFIG_RELOCATE_VECTORS_TO_RAM=$(CONFIG_RELOCATE_VECTORS_TO_RAM)
CFLAGS += -DCONFIG_TCPIP=$(CONFIG_TCPIP)
CFLAGS += -DCONFIG_DEVUART=1
CFLAGS += -DLINK_MTU=$(LINK_MTU)
CFLAGS += -DCONFIG_ETH=$(CONFIG_ETH)
CFLAGS += -DCONFIG_MPU=$(CONFIG_MPU)
CFLAGS += -DCONFIG_ILI9341=$(CONFIG_ILI9341)
CFLAGS += -DCONFIG_SPI1=$(CONFIG_SPI1)
CFLAGS += -DCONFIG_DEVFRAMEBUFFER=$(CONFIG_DEVFRAMEBUFFER)
CFLAGS += -DCONFIG_DEVFBCON=$(CONFIG_DEVFBCON)
CFLAGS += -DCONFIG_DEVTTY_CONSOLE=$(CONFIG_DEVTTY_CONSOLE)
# Enable FlashFS when requested.
ifeq ($(CONFIG_FLASHFS),1)
CFLAGS += -DCONFIG_FLASHFS=1
CFLAGS += -DCONFIG_FLASHFS_VERIFY=$(CONFIG_FLASHFS_VERIFY)
endif

CFLAGS += -DSEMAPHORES

ifeq ($(CONFIG_DEVUSB),1)
CFLAGS += -DCONFIG_USB=1
CFLAGS += -DCFG_TUSB_MCU=OPT_MCU_STM32H5
CFLAGS += -Ilib/tinyusb/src -Ilib/tinyusb/hw
endif



SRCS = startup_ns.c ns_main.c \
	    bflt.c \
		cirbuf.c \
		device.c \
		fpb.c \
		mpu.c \
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
		socket_un.c \
		string.c \
		sys.c \
		syscall_table.c \
		sysfs.c \
		systick.c \
		tasklet.c \
		term.c \
		framebuffer.c \
		fbcon.c \
		tty_console.c \
		uart.c \
		usb.c \
		usb_descriptors.c \
		netd_stub.c \
		vfs.c \
		semaphore.S \
		mutex.S \
		xipfs.c \
		stm32_spi.c \
		ili9341_spi.c \
		fonts/cga_8x8.c \
		fonts/palette_256_xterm.c

ifeq ($(CONFIG_FLASHFS),1)
SRCS += flashfs.c
endif

ifeq ($(CONFIG_ETH),1)
SRCS += \
	stm32_eth.c lan8742.c
endif

ifeq ($(CONFIG_TCPIP),1)
SRCS += socket_in.c wolfip.c
endif

ifeq ($(CONFIG_DEVUSB),1)
TINYUSB_SRCS = \
	lib/tinyusb/src/tusb.c \
	lib/tinyusb/src/common/tusb_fifo.c \
	lib/tinyusb/src/device/usbd.c \
	lib/tinyusb/src/device/usbd_control.c \
	lib/tinyusb/src/class/cdc/cdc_device.c \
	lib/tinyusb/src/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
ifeq ($(CONFIG_USB_NET),1)
TINYUSB_SRCS += lib/tinyusb/src/class/net/ncm_device.c
endif
SRCS += $(TINYUSB_SRCS)
endif

SRCS += frosted_$(TARGET).c
OBJS = $(SRCS:.c=.o) ../nsc-gateway/nsc_kernel.o

stm32_spi.o ili9341_spi.o fbcon.o framebuffer.o: CFLAGS += -Os



all: kernel.elf kernel.bin

kernel.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin *.map

.PHONY: menuconfig clean

menuconfig:
	$(MAKE) -C $(KCONFIG_DIR) -f Makefile.frosted \
		KBUILD_KCONFIG=$(KCONFIG_FILE) \
		KCONFIG_CONFIG=$(KCONFIG_CONFIG) \
		menuconfig
