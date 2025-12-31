CC      = arm-none-eabi-gcc
LD      = arm-none-eabi-ld
OBJCOPY = arm-none-eabi-objcopy

CFLAGS  = -mcpu=cortex-m33 -mcmse -mthumb -Os -Wall -ffreestanding -nostdlib -ggdb
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -Wl,--cmse-implib -Wl,--out-implib=../nsc-gateway/nsc_kernel.o
CFLAGS += -Iinclude -I ../nsc-gateway -ggdb

LDFLAGS = -Wl,-gc-sections

KCONFIG_DIR := $(abspath $(CURDIR)/../userland/kconfig)
KCONFIG_FILE := $(abspath $(CURDIR)/Kconfig)
KCONFIG_CONFIG ?= $(abspath $(CURDIR)/.config)

ifneq ($(filter menuconfig,$(MAKECMDGOALS)),menuconfig)
ifeq ($(wildcard $(KCONFIG_CONFIG)),)
$(error Secure supervisor configuration missing ($(KCONFIG_CONFIG)). Run 'make menuconfig' first.)
endif

include $(KCONFIG_CONFIG)

kconfig_bool = $(strip $(if $(filter y Y 1,$(1)),1,0))

CONFIG_ETH := $(call kconfig_bool,$(ETH))
CONFIG_FLASHFS_VERIFY := $(call kconfig_bool,$(FLASHFS_VERIFY))

ifeq ($(TARGET_STM32H563),y)
TARGET := stm32h563
LINKER_SCRIPT := stm32h563.ld
TARGET_INCLUDES := -Iarch/stm32h563 -Iarch/stm32_common -DTARGET_STM32H563
TARGET_SRCS := arch/stm32h563/machine_init.c arch/stm32h563/clock.c
else ifeq ($(TARGET_STM32U585),y)
TARGET := stm32u585
LINKER_SCRIPT := stm32u585.ld
TARGET_INCLUDES := -Iarch/stm32u585 -Iarch/stm32_common -DTARGET_STM32U585
TARGET_SRCS := arch/stm32u585/machine_init.c
else ifeq ($(TARGET_RP2350),y)
TARGET := rp2350
LINKER_SCRIPT := rp2350.ld
TARGET_INCLUDES := -Iarch/rp2350 -DTARGET_RP2350
TARGET_SRCS := arch/rp2350/rp2350.c
else
$(error No target selected in $(KCONFIG_CONFIG))
endif

LDFLAGS += -T$(LINKER_SCRIPT)
CFLAGS += $(TARGET_INCLUDES)

SRCS = ivt.c startup_secure.c secure_main.c sg.c mempool.c task.c random.c flash-write.c \
	$(TARGET_SRCS)

CFLAGS += -DCONFIG_ETH=$(CONFIG_ETH)
CFLAGS += -DCONFIG_FLASHFS_VERIFY=$(CONFIG_FLASHFS_VERIFY)
endif

OBJS = $(SRCS:.c=.o)

all: secure.elf secure.bin

secure.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

secure.bin: secure.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean menuconfig

menuconfig:
	$(MAKE) -C $(KCONFIG_DIR) -f Makefile.frosted \
		KBUILD_KCONFIG=$(KCONFIG_FILE) \
		KCONFIG_CONFIG=$(KCONFIG_CONFIG) \
		menuconfig
