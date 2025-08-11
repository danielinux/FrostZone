CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
CFLAGS  = -mcpu=cortex-m33 -mthumb -Os -Wall -ffreestanding -nostdlib
LDFLAGS = -Tkernel.ld

SRCS = startup_ns.c ns_main.c
OBJS = $(SRCS:.c=.o) nsc_kernel.o

all: kernel.elf kernel.hex

kernel.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

kernel.hex: kernel.elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -f *.o *.elf *.hex
