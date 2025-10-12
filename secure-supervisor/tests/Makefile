PKG_CHECK_CFLAGS := $(shell pkg-config --cflags check 2>/dev/null)
PKG_CHECK_LIBS := $(shell pkg-config --libs check 2>/dev/null)

CHECK_CFLAGS := $(PKG_CHECK_CFLAGS)
CHECK_LIBS := $(PKG_CHECK_LIBS)

ifeq ($(strip $(CHECK_LIBS)),)
CHECK_LIBS := -lcheck -lm -lpthread -lrt
endif

CC ?= gcc

CFLAGS ?=
override CFLAGS += -Wall -Wextra -std=gnu11 -I.. -I../include $(CHECK_CFLAGS)

LDFLAGS ?=
LDLIBS ?=
override LDLIBS += $(CHECK_LIBS)

TARGET ?= unit_mempool

all: $(TARGET)

$(TARGET): unit_mempool.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

.PHONY: test clean

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
