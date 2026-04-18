# this makefile is to compile apps as a separate bFLT executable
#
#
# PATH CONFIG
FROSTED?=..
PREFIX:=$(PWD)/build

KERNEL_CONFIG ?= ../frosted/.config
APP_CONFIG ?= kconfig/.config

ifneq ($(filter menuconfig,$(MAKECMDGOALS)),menuconfig)
ifeq ($(wildcard $(KERNEL_CONFIG)),)
$(error Kernel configuration missing ($(KERNEL_CONFIG)). Run 'make menuconfig' inside frosted/.)
endif
include $(KERNEL_CONFIG)

ifeq ($(wildcard $(APP_CONFIG)),)
$(error Userland configuration missing ($(APP_CONFIG)). Run 'make menuconfig' inside userland/.)
endif
include $(APP_CONFIG)

ifndef SYS_CLOCK
$(error SYS_CLOCK not defined in $(KERNEL_CONFIG))
endif
endif
# TOOLCHAIN CONFIG
CROSS_COMPILE?=arm-frosted-eabi-
CC:=$(CROSS_COMPILE)gcc
AS:=$(CROSS_COMPILE)as
AR:=$(CROSS_COMPILE)ar

ifneq ($(V),1)
   Q:=@
   #Do not print "Entering directory ...".
   MAKEFLAGS += --no-print-directory
endif


#Applications selection
DIR-y+=lib binutils hw-utils netutils games tests
DIR-$(APP_PYTHON)+=micropython
DIR-$(APP_BUSYBOX)+=busybox

MICROPYTHON_DIR:=micropython
MICROPYTHON_PORT_DIR:=$(MICROPYTHON_DIR)/ports/unix
MICROPYTHON_VARIANT:=frosted
MICROPYTHON_BIN:=$(MICROPYTHON_PORT_DIR)/build-$(MICROPYTHON_VARIANT)/micropython
MICROPYTHON_OUT:=out/python
MICROPYTHON_TOOLCHAIN_DIR?=/opt/toolchains/arm-frosted-eabi/bin
MICROPYTHON_CC:=$(CC)
ifneq ("$(wildcard $(MICROPYTHON_TOOLCHAIN_DIR)/$(notdir $(CC)))","")
MICROPYTHON_CC:=$(MICROPYTHON_TOOLCHAIN_DIR)/$(notdir $(CC))
endif

# COMPILER FLAGS -- Target CPU
CFLAGS+=-mthumb -mlittle-endian -mthumb-interwork -ffunction-sections -D__Frosted__ -Wno-implicit-int -Wno-implicit-function-declaration
ifeq ($(ARCH),)
	CFLAGS+=-mcpu=cortex-m33
else
	CFLAGS+=-mcpu=$(ARCH)
endif
CFLAGS+=-DCORE_M33 -D__frosted__ -DHAVE_MMAP=1 -O0
# COMPILER FLAGS -- No gcc libraries
CFLAGS+=-nostartfiles
# COMPILER FLAGS -- GOT/PIC
CFLAGS+=-fPIC -mlong-calls -fno-common -msingle-pic-base -mno-pic-data-is-text-relative -Wstack-usage=4096 -DSYS_CLOCK=$(SYS_CLOCK)
# Debugging
CFLAGS+=-ggdb
CFLAGS+=-I../lib/include -I../lib
CFLAGS+=-Wdeclaration-after-statement -Werror
CFLAGS+=$(CFLAGS-y)

# LINKER FLAGS
LDFLAGS+=-fPIC -mlong-calls -fno-common -Wl,-elf2flt -Wl,--start-group,-lc,-lgloss,--end-group

all: userspace.bin
	cp userspace.bin $(FROSTED)/

xipfstool: xipfs
	make -C $^
	mv $^/xipfstool .

userspace.bin: $(APPS-y) $(DIR-y) sh xipfstool lnk
	mv out/*.gdb gdb/ 2>/dev/null || true
	./xipfstool $@ \
		$(wildcard out/*.so) \
		$(APPS-y) \
		$(filter-out %.map %.gdb %.so,$(wildcard out/*))

binutils: FORCE
	mkdir -p out
	mkdir -p gdb
	rm -rf lnk
	mkdir -p lnk
	cd binutils && ./gen_makefile.py
	make -C $@ LDFLAGS="$(LDFLAGS)" CFLAGS="$(CFLAGS)" CC=$(CC)

lib: FORCE
	mkdir -p out
	mkdir -p gdb
	make -C $@ LDFLAGS="$(LDFLAGS)" CFLAGS="$(CFLAGS)" CC=$(CC) APPS_ORIGIN=$(APPS_ORIGIN)

hw-utils: FORCE
	mkdir -p out
	mkdir -p gdb
	make -C $@ LDFLAGS="$(LDFLAGS)" CFLAGS="$(CFLAGS)" CC=$(CC)

netutils: FORCE
	mkdir -p out
	mkdir -p gdb
	make -C $@ LDFLAGS="$(LDFLAGS)" CFLAGS="$(CFLAGS) -D__USE_MISC" CC=$(CC)

games: FORCE
	mkdir -p out
	mkdir -p gdb
	make -C $@ LDFLAGS="$(LDFLAGS)" CFLAGS="$(CFLAGS)" CC=$(CC)

micropython: FORCE
	mkdir -p out
	make -C $(MICROPYTHON_DIR)/mpy-cross CC=$(MICROPYTHON_CC)
	make -C $(MICROPYTHON_PORT_DIR) submodules
	make -C $(MICROPYTHON_PORT_DIR) VARIANT=$(MICROPYTHON_VARIANT) CC=$(MICROPYTHON_CC)
	install -m 755 $(MICROPYTHON_BIN) $(MICROPYTHON_OUT)

sh: FORCE
	mkdir -p out
	cp -a $@/* out/ || true

lnk: FORCE
	mkdir -p out
	cp -a $@/* out/ || true

lib/librt.a:
	echo "!<arch>" >$@

busybox-config: lib/librt.a FORCE
	make -C busybox LDFLAGS="$(LDFLAGS)" CFLAGS="$(CFLAGS)" CC=$(CC) menuconfig

tests: FORCE
	mkdir -p out
	mkdir -p gdb
	make -C $@ LDFLAGS="$(LDFLAGS)" CFLAGS="$(CFLAGS)" CC=$(CC)


FORCE:

menuconfig:
	@$(MAKE) -C kconfig/ menuconfig -f Makefile.frosted

clean:
	$(foreach d,$(DIR-y),make -C $(d) clean &>/dev/null || true;) 
	@make -C micropython/mpy-cross clean
	@make -C micropython/ports/unix clean
	@rm -f *.img
	@rm -f *.o
	@rm -rf gdb out lnk ice
	@make -C xipfs clean
	@rm -f xipfstool
