CC      = arm-none-eabi-gcc
LD      = arm-none-eabi-ld
OBJCOPY = arm-none-eabi-objcopy
CFLAGS  = -mcpu=cortex-m33 -mcmse -mthumb -Os -Wall -ffreestanding -nostdlib
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -Wl,--cmse-implib -Wl,--out-implib=../nonsecure_kernel/nsc_kernel.o
CFLAGS += -Iinclude
CFLAGS += -ggdb
LDFLAGS = -Ttarget.ld -Wl,-gc-sections


SRCS = ivt.c startup_secure.c secure_main.c sg.c
OBJS = $(SRCS:.c=.o)

all: secure.elf secure.hex

secure.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

secure.hex: secure.elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -f *.o *.elf *.hex
