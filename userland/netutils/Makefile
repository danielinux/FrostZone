-include ../kconfig/.config
CROSS_COMPILE ?= arm-frosted-eabi-
CC := $(CROSS_COMPILE)gcc
CFLAGS?=
CFLAGS += -D_DEFAULT_SOURCE -D_BSD_SOURCE
APPS-$(APP_IFCONFIG)+=ifconfig
CFLAGS-$(APP_IFCONFIG)+=-DAPP_IFCONFIG_STANDALONE
CFLAGS-ICEBOX-$(APP_IFCONFIG)+=-DAPP_IFCONFIG_MODULE
APPS-$(APP_ROUTE)+=route
CFLAGS-$(APP_ROUTE)+=-DAPP_ROUTE_STANDALONE
CFLAGS-ICEBOX-$(APP_ROUTE)+=-DAPP_ROUTE_MODULE
APPS-$(APP_TELNETD)+=telnetd
CFLAGS-$(APP_TELNETD)+=-DAPP_TELNETD_STANDALONE
CFLAGS-ICEBOX-$(APP_TELNETD)+=-DAPP_TELNETD_MODULE
APPS-$(APP_NETCAT)+=nc
CFLAGS-$(APP_NETCAT)+=-DAPP_NETCAT_STANDALONE
CFLAGS-ICEBOX-$(APP_NETCAT)+=-DAPP_NETCAT_MODULE
APPS-$(APP_HOST)+=host
CFLAGS-$(APP_HOST)+=-DAPP_HOST_STANDALONE
CFLAGS-ICEBOX-$(APP_HOST)+=-DAPP_HOST_MODULE
#APPS-$(APP_HTTPD)+=https
APPS-$(APP_STARWARS)+=starwars
CFLAGS-$(APP_STARWARS)+=-DAPP_STARWARS_STANDALONE
CFLAGS-ICEBOX-$(APP_STARWARS)+=-DAPP_STARWARS_MODULE
APPS-$(APP_NTPC)+=ntpc
CFLAGS-$(APP_NTPC)+=-DAPP_NTPC_STANDALONE
CFLAGS-ICEBOX-$(APP_NTPC)+=-DAPP_NTPC_MODULE
APPS-$(APP_SSHD)+=sshd
CFLAGS-$(APP_SSHD)+=-DAPP_SSHD_STANDALONE
CFLAGS-ICEBOX-$(APP_SSHD)+=-DAPP_SSHD_MODULE
APPS-$(APP_PING)+=ping
CFLAGS-$(APP_PING)+=-DAPP_PING_STANDALONE
CFLAGS-ICEBOX-$(APP_PING)+=-DAPP_PING_MODULE

LIBS-$(LIB_MONGOOSE)+=../lib/libmongoose.a
LIBS-$(LIB_WOLFSSL)+= ../lib/libssl.a
CFLAGS-$(LIB_WOLFSSL)+=-I../lib/include/ -DDEBUG_WOLFSSH

BIN:=$(patsubst %,../out/%,$(APPS-y))
OBJ:=$(patsubst %,%.o,$(APPS-y))
MOD:=$(patsubst %,ice/%.o,$(APPS-m))
ICEBOX_NET_OBJS:=$(addprefix ../netutils/,$(MOD))
ICEBOX_LINKS:=$(APPS-m)

all: $(BIN) $(MOD)

ifeq ($(ICEBOX),y)
ifneq ($(strip $(MOD)),)
all: icebox-refresh

.PHONY: icebox-refresh
icebox-refresh: $(MOD)
	$(MAKE) -C ../binutils ICEBOX_EXTRA_OBJS="$(ICEBOX_NET_OBJS)" icebox-refresh
	@mkdir -p ../lnk
	@for app in $(ICEBOX_LINKS); do ln -sf ../out/icebox ../lnk/$$app; done
endif
endif

../out/sshd:
	make -C wolfssh -f ../Makefile.wolfssh
	cp sshd ../out
	cp sshd.gdb ../gdb

../out/https: https.o
	mkdir -p ../out
	mkdir -p ../gdb
	echo $(LDFLAGS)
	@arm-frosted-eabi-gcc -o $@ $(LDFLAGS) $(CFLAGS) $(CFLAGS-y) ../lib/wolfssl/src/*.o ../lib/wolfssl/wolfcrypt/src/*.o $^
	cp https ../out
	cp https.gdb ../gdb

ice/%.o: %.c
	@mkdir -p ice
	$(CC) -c -o $@ $^ $(CFLAGS) $(CFLAGS-ICEBOX-m)

%.o: %.c
	$(CC) -c -o $@ $^ $(CFLAGS) $(CFLAGS-y)

../out/%: %.o
	@arm-frosted-eabi-gcc -o $@ $^ $(CFLAGS) $(CFLAGS-y) $(LDFLAGS) $(LIBS-y) -lm


clean:
	@rm -f $(OBJ)
	@rm -rf ice
