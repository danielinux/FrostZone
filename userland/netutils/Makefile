-include ../kconfig/.config
CFLAGS?=
CC?=arm-frosted-eabi-gcc
APPS-$(APP_IFCONFIG)+=ifconfig
APPS-$(APP_ROUTE)+=route
APPS-$(APP_TELNETD)+=telnetd
APPS-$(APP_NETCAT)+=nc
APPS-$(APP_HOST)+=host
#APPS-$(APP_HTTPD)+=https
APPS-$(APP_STARWARS)+=starwars
APPS-$(APP_NTPC)+=ntpc
APPS-$(APP_SSHD)+=sshd
APPS-$(APP_PING)+=ping


BIN:=$(patsubst %,../out/%,$(APPS-y))
OBJ:=$(patsubst %,%.o,$(APPS-y))

all: $(BIN)


clean:
	@rm -f $(OBJ)




