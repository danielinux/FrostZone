TARGET ?= stm32h563
CONFIG_MPU ?= 1

CC      = arm-none-eabi-gcc
LD      = arm-none-eabi-ld
OBJCOPY = arm-none-eabi-objcopy

CFLAGS  = -mcpu=cortex-m33 -mcmse -mthumb -Os -Wall -ffreestanding -nostdlib -ggdb
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -Wl,--cmse-implib -Wl,--out-implib=nsc_kernel.o
CFLAGS += -Iinclude -ggdb
CFLAGS += -DCONFIG_MPU=$(CONFIG_MPU)

LDFLAGS = -Wl,-gc-sections

SRCS = ivt.c startup_secure.c secure_main.c sg.c mempool.c task.c mpu.c random.c

ifeq ($(TARGET),stm32u585)
SRCS += arch/stm32u585/machine_init.c
CFLAGS += -Iarch/stm32u585 -Iarch/stm32_common -DTARGET_STM32U585
LDFLAGS += -Tstm32u585.ld
else ifeq ($(TARGET),stm32h563)
SRCS += arch/stm32h563/machine_init.c arch/stm32h563/clock.c
CFLAGS += -Iarch/stm32h563 -Iarch/stm32_common -DTARGET_STM32H563
LDFLAGS += -Tstm32h563.ld
else ifeq ($(TARGET),rp2350)
SRCS += arch/rp2350/rp2350.c
CFLAGS += -Iarch/rp2350 -DTARGET_RP2350
LDFLAGS += -Trp2350.ld
else
$(error Unsupported TARGET $(TARGET))
endif

OBJS = $(SRCS:.c=.o)

all: secure.elf secure.bin

secure.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

secure.bin: secure.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin
