-include ../kconfig/.config
CFLAGS?=
CC?=arm-frosted-eabi-cc

APPS-y:=tz_guard_demo

BIN:=$(patsubst %,../out/%,$(APPS-y))
OBJ:=$(patsubst %,%.o,$(APPS-y))

all: $(BIN)

../out/%: %.o
	@$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

%.o: %.c
	@$(CC) -c -o $@ $< $(CFLAGS)

clean:
	@rm -f $(OBJ)
