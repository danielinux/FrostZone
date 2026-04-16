-include ../kconfig/.config

CROSS_COMPILE ?= arm-frosted-eabi-
CC := $(CROSS_COMPILE)gcc

WOLFSSL_REPO := wolfSSL/wolfssl
WOLFSSH_REPO := wolfSSL/wolfssh

GITHUB_API := https://api.github.com/repos

# Resolve latest release tag via GitHub API
wolfssl_tag = $(shell cat wolfssl/.tag 2>/dev/null)
wolfssh_tag = $(shell cat wolfssh/.tag 2>/dev/null)

LIBS-y:=

all: fetch shlibs
	@echo "Libraries ready."

# ---- wolfSSL --------------------------------------------------------
ifeq ($(LIB_WOLFSSL),y)
LIBS-y += wolfssl

# wolfSSL source files for SHA-256 (minimal set for the demo)
WOLFSSL_CFLAGS := $(CFLAGS) \
	-DWOLFSSL_USER_SETTINGS \
	-I. -Iwolfssl -Iwolfssl/wolfssl

WOLFSSL_SRCS := \
	wolfssl/wolfcrypt/src/sha256.c \
	wolfssl/wolfcrypt/src/logging.c \
	wolfssl/wolfcrypt/src/wc_port.c \
	wolfssl/wolfcrypt/src/memory.c

WOLFSSL_OBJS := $(WOLFSSL_SRCS:.c=.o)

# Build wolfSSL as a shared library bFLT
wolfssl/wolfcrypt/src/%.o: wolfssl/wolfcrypt/src/%.c
	@echo "  CC      $<"
	$(Q)$(CC) -c -o $@ $< $(WOLFSSL_CFLAGS)

../out/libwolfssl.so: $(WOLFSSL_OBJS)
	@echo "  SHLIB   libwolfssl.so (lib_id=1)"
	$(Q)FLTFLAGS="-S -L1 -E$(CURDIR)/wolfssl.exports" \
		$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) -Wl,-e,0
	@echo "  MAP     libwolfssl.map"

# The .map file is generated as a side-effect of the .so build
libwolfssl.map: ../out/libwolfssl.so
	@test -f $@ || { echo "ERROR: libwolfssl.map not generated"; exit 1; }

# Stub archive — provides symbols for the linker so elf2flt can tag GOT entries
wolfssl_stubs.o: wolfssl_stubs.c
	$(CC) -c -o $@ $< $(CFLAGS)

libwolfssl_stubs.a: wolfssl_stubs.o
	$(AR) rcs $@ $^


wolfssl/.tag:
	@echo "  FETCH   wolfssl (latest release)"
	@mkdir -p wolfssl
	@tag=$$(curl -sf $(GITHUB_API)/$(WOLFSSL_REPO)/releases/latest \
	        | python3 -c "import sys,json; print(json.load(sys.stdin)['tag_name'])") && \
	 echo "  TAG     $$tag" && \
	 curl -sfL $(GITHUB_API)/$(WOLFSSL_REPO)/tarball/$$tag \
	        | tar xz --strip-components=1 -C wolfssl && \
	 echo "$$tag" > wolfssl/.tag
	@echo "  OK      wolfssl ($$(cat wolfssl/.tag))"
endif

# ---- wolfSSH --------------------------------------------------------
ifeq ($(LIB_WOLFSSH),y)
LIBS-y += wolfssh

wolfssh/.tag: wolfssl/.tag
	@echo "  FETCH   wolfssh (latest release)"
	@mkdir -p wolfssh
	@tag=$$(curl -sf $(GITHUB_API)/$(WOLFSSH_REPO)/releases/latest \
	        | python3 -c "import sys,json; print(json.load(sys.stdin)['tag_name'])") && \
	 echo "  TAG     $$tag" && \
	 curl -sfL $(GITHUB_API)/$(WOLFSSH_REPO)/tarball/$$tag \
	        | tar xz --strip-components=1 -C wolfssh && \
	 echo "$$tag" > wolfssh/.tag
	@echo "  OK      wolfssh ($$(cat wolfssh/.tag))"
endif

fetch: $(addsuffix /.tag,$(LIBS-y))

# Build shared libraries (only wolfSSL for now)
ifeq ($(LIB_WOLFSSL),y)
shlibs: fetch ../out/libwolfssl.so libwolfssl_stubs.a
else
shlibs:
endif

# Re-fetch: remove tag file to trigger fresh download
refetch:
	rm -f wolfssl/.tag wolfssh/.tag
	$(MAKE) fetch

clean:
	rm -f *.a *.o *.map
	rm -f wolfssl/wolfcrypt/src/*.o
	rm -f ../out/libwolfssl.so

distclean: clean
	rm -rf wolfssl wolfssh
