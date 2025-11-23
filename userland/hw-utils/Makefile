-include ../kconfig/.config

CC:=arm-frosted-eabi-gcc
CROSS_COMPILE?=arm-frosted-eabi-
LDFLAGS?=-Wl,-elf2flt -Wl,--start-group,-lc,-lgloss,--end-group

ifndef SYS_CLOCK
SYS_CLOCK:=250000000
endif

# Mirror the top-level frosted userland flags so standalone builds work.
CFLAGS+=-mthumb -mlittle-endian -mthumb-interwork -ffunction-sections -D__Frosted__ -Wno-implicit-int -Wno-implicit-function-declaration
CFLAGS+=-mcpu=cortex-m33 -DCORE_M33 -D__frosted__ -DHAVE_MMAP=1 -O0
CFLAGS+=-nostartfiles -fPIC -mlong-calls -fno-common -msingle-pic-base -mno-pic-data-is-text-relative -Wstack-usage=4096
CFLAGS+=-I../lib/include -I../lib
CFLAGS+=-ggdb -Wdeclaration-after-statement -Werror
CFLAGS+=-DSYS_CLOCK=$(SYS_CLOCK)
APPS-y:=

APPS-$(APP_RANDOM)+=random
APPS-$(APP_MORSE)+=morse
APPS-$(APP_BUSYLOOP)+=busyloop
APPS-$(APP_GPIOCTL)+=gpioctl
APPS-$(APP_LATENCY)+=latency
APPS-$(APP_ACC)+=acc
APPS-$(APP_FBCTL)+=fbctl
APPS-$(APP_LCD)+=lcd
APPS-$(APP_FBBLUE)+=fb_blue
BIN:=$(patsubst %,../out/%,$(APPS-y))
OBJ:=$(patsubst %,%.o,$(APPS-y))

all: $(BIN)

../out/%: %.o
	@$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

%.o: %.c
	$(CC) -c -o $@ $^ $(CFLAGS)

clean:
	@rm -f $(OBJ)
