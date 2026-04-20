-include ../kconfig/.config
CFLAGS?=
CC?=arm-frosted-eabi-cc

APPS-y:=tz_guard_demo
APPS-$(APP_PHASE0_MEMFS)+=phase0_memfs
APPS-$(APP_DLOPEN_TEST)+=dlopen_test

BIN:=$(patsubst %,../out/%,$(APPS-y))
OBJ:=$(patsubst %,%.o,$(APPS-y))
DLOPEN_TEST_OBJS:=dlopen_test.o

all: $(BIN)

../out/dlopen_test: $(DLOPEN_TEST_OBJS)
	@$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

../out/%: %.o
	@$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

%.o: %.c
	@$(CC) -c -o $@ $< $(CFLAGS)

clean:
	@rm -f $(OBJ) $(DLOPEN_TEST_OBJS)
