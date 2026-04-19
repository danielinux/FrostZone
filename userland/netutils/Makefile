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
APPS-$(APP_HTTPD)+=httpd
CFLAGS-$(APP_HTTPD)+=-DAPP_HTTPD_STANDALONE
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

# wolfSSL is a shared library — everything links via stub archive + import map.
CFLAGS-$(LIB_WOLFSSL)+=-I../lib/include/

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
	rm -f ../binutils/icebox.o
	$(MAKE) -C ../binutils ICEBOX_EXTRA_OBJS="$(ICEBOX_NET_OBJS)" ICEBOX_EXTRA_CFLAGS="$(CFLAGS-ICEBOX-m)" icebox-refresh
	@mkdir -p ../lnk
	@for app in $(ICEBOX_LINKS); do ln -sf ../out/icebox ../lnk/$$app; done
endif
endif

# ---------------------------------------------------------------
# sshd - statically bundles wolfSSH, uses wolfCrypt via libwolfssl.so
# ---------------------------------------------------------------
WOLFSSH_ROOT    := ../lib/wolfssh
# misc.c is included inline by internal.c when NO_INLINE is unset, so
# don't compile it separately.
WOLFSSH_SRCS    := \
	$(WOLFSSH_ROOT)/src/ssh.c \
	$(WOLFSSH_ROOT)/src/internal.c \
	$(WOLFSSH_ROOT)/src/io.c \
	$(WOLFSSH_ROOT)/src/log.c \
	$(WOLFSSH_ROOT)/src/port.c \
	$(WOLFSSH_ROOT)/src/keygen.c
WOLFSSH_OBJS    := $(WOLFSSH_SRCS:.c=.o)

# wolfSSH uses wolfCrypt via libwolfssl.so (stub archive + import map).
# Share the wolfSSL user_settings so wolfSSH sees the same wolfCrypt
# configuration it's expecting at compile time. -Os overrides the
# parent's -O0 and keeps code size down; WOLFSSL_SMALL_STACK propagates
# via user_settings.h.
SSHD_CFLAGS  := $(CFLAGS) $(CFLAGS-y) \
	-Os \
	-DWOLFSSL_USER_SETTINGS \
	-DDEBUG_WOLFSSH \
	-I../lib -I../lib/wolfssl -I../lib/wolfssl/wolfssl \
	-I$(WOLFSSH_ROOT) \
	-include $(CURDIR)/sshd_wolfssh_settings.h \
	-Wno-error=stack-usage= -Wno-error=unused-function \
	-Wno-error=unused-variable -Wno-error=unused-but-set-variable

$(WOLFSSH_ROOT)/src/%.o: $(WOLFSSH_ROOT)/src/%.c
	$(CC) -c -o $@ $< $(SSHD_CFLAGS)

sshd.o: sshd.c sshd_hostkey.h sshd_wolfssh_settings.h
	$(CC) -c -o $@ sshd.c $(SSHD_CFLAGS)

../out/sshd: sshd.o $(WOLFSSH_OBJS) ../lib/libwolfssl_stubs.a
	@FLTFLAGS="-I$(CURDIR)/../out/libwolfssl.map" \
		arm-frosted-eabi-gcc -o $@ $^ \
		$(CFLAGS) $(CFLAGS-y) $(LDFLAGS) -lm

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
