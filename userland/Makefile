# this makefile is to compile apps as a separate bFLT executable
#
#
# PATH CONFIG
FROSTED?=..
PREFIX:=$(PWD)/build

-include kconfig/.config
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
#DIR-$(APP_PYTHON)+=micropython
DIR-$(APP_BUSYBOX)+=busybox

# COMPILER FLAGS -- Target CPU
CFLAGS+=-mthumb -mlittle-endian -mthumb-interwork -ffunction-sections -D__Frosted__
ifeq ($(ARCH),)
	CFLAGS+=-mcpu=cortex-m33
else
	CFLAGS+=-mcpu=$(ARCH)
endif
CFLAGS+=-DCORE_M33 -D__frosted__ -DHAVE_MMAP=1
# COMPILER FLAGS -- No gcc libraries
CFLAGS+=-nostartfiles
# COMPILER FLAGS -- GOT/PIC
CFLAGS+=-fPIC -mlong-calls -fno-common -msingle-pic-base -mno-pic-data-is-text-relative -Wstack-usage=1024 -DSYS_CLOCK=$(SYS_CLOCK)
# Debugging
CFLAGS+=-ggdb
CFLAGS+=-I../lib/include -I../lib
CFLAGS+=$(CFLAGS-y)

# LINKER FLAGS
LDFLAGS+=-fPIC -mlong-calls -fno-common -Wl,-elf2flt -lc -lgloss

all: userspace.bin
	cp userspace.bin $(FROSTED)/

xipfstool: xipfs
	make -C $^
	mv $^/xipfstool .

userspace.bin: $(APPS-y) $(DIR-y) sh xipfstool lnk
	mv out/*.gdb gdb/ 2>/dev/null || true
	./xipfstool $@ $(APPS-y) out/*

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
	make -C $@ LDFLAGS="$(LDFLAGS)" CFLAGS="$(CFLAGS)" CC=$(CC)

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
	@rm -f *.img
	@rm -f *.o
	@rm -rf gdb out lnk ice
	@make -C xipfs clean
	@rm -f xipfstool
