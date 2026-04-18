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

# Flash-resident base address for the library's .text.
# Libraries are packed FIRST in xipfs. The first file starts at
#   APPS_ORIGIN + sizeof(xipfs_fat)(16) + sizeof(xipfs_fhdr)(64) = APPS_ORIGIN + 80
# The bFLT header adds another 64, so .text begins at APPS_ORIGIN + 144.
APPS_ORIGIN ?= 0x08040000
SHLIB_TEXT_BASE ?= $(shell printf '0x%x' $$(( $(APPS_ORIGIN) + 144 )) )

# Custom elf2flt linker script with .text at the fixed flash address and
# rodata merged into .text (equivalent of ld-elf2flt's -move-rodata).
# Generated from the toolchain's default by:
#   1. Patching .text base address
#   2. Deleting R_RODAT: lines (keep only W_RODAT: — rodata in .text)
#   3. Stripping W_RODAT: / TOR: / SINGLE_LINK: / RISCV_GP: prefixes
ELF2FLT_LD := $(shell $(CC) -print-file-name=elf2flt.ld)
shlib-elf2flt.ld: $(ELF2FLT_LD)
	@echo "  GEN     $@ (.text @ $(SHLIB_TEXT_BASE))"
	$(Q)sed -e 's|\.text 0x0 :|.text $(SHLIB_TEXT_BASE) :|' \
	    -e '/^R_RODAT:/d' \
	    -e '/^TOR:/d' \
	    -e 's|^W_RODAT:||' \
	    -e 's|^SINGLE_LINK:||' \
	    -e 's|^RISCV_GP:||' \
	    $< > $@

# wolfSSL CFLAGS — include our HAL shim headers.
# Compile wolfCrypt sources with -Os (override the parent's -O0) so the
# shared library is much smaller. Also suppress the stack-usage=4096
# error — SW ECC paths have big frames that WOLFSSL_SMALL_STACK thins
# out, but the compiler still flags them before constant folding.
WOLFSSL_CFLAGS := $(CFLAGS) \
	-Os \
	-Wno-error=stack-usage= \
	-DWOLFSSL_USER_SETTINGS \
	-I. -Iwolfssl -Iwolfssl/wolfssl

WOLFSSL_SRCS := \
	wolfssl/wolfcrypt/src/sha256.c \
	wolfssl/wolfcrypt/src/aes.c \
	wolfssl/wolfcrypt/src/ecc.c \
	wolfssl/wolfcrypt/src/hmac.c \
	wolfssl/wolfcrypt/src/kdf.c \
	wolfssl/wolfcrypt/src/signature.c \
	wolfssl/wolfcrypt/src/sp_c32.c \
	wolfssl/wolfcrypt/src/sp_int.c \
	wolfssl/wolfcrypt/src/asn.c \
	wolfssl/wolfcrypt/src/hash.c \
	wolfssl/wolfcrypt/src/coding.c \
	wolfssl/wolfcrypt/src/wolfmath.c \
	wolfssl/wolfcrypt/src/random.c \
	wolfssl/wolfcrypt/src/logging.c \
	wolfssl/wolfcrypt/src/wc_port.c \
	wolfssl/wolfcrypt/src/memory.c \
	wolfssl/wolfcrypt/src/port/st/stm32.c \
	frosted_crypto_wrappers.c \
	stm32h5_hal.c \
	frosted_rng.c

WOLFSSL_OBJS := $(WOLFSSL_SRCS:.c=.o)

# Build wolfSSL as a shared library bFLT
wolfssl/wolfcrypt/src/%.o: wolfssl/wolfcrypt/src/%.c
	@echo "  CC      $<"
	$(Q)$(CC) -c -o $@ $< $(WOLFSSL_CFLAGS)

wolfssl/wolfcrypt/src/port/st/%.o: wolfssl/wolfcrypt/src/port/st/%.c
	@echo "  CC      $<"
	$(Q)$(CC) -c -o $@ $< $(WOLFSSL_CFLAGS)

frosted_crypto_wrappers.o: frosted_crypto_wrappers.c
	@echo "  CC      $<"
	$(Q)$(CC) -c -o $@ $< $(WOLFSSL_CFLAGS)

stm32h5_hal.o: stm32h5_hal.c
	@echo "  CC      $<"
	$(Q)$(CC) -c -o $@ $< $(WOLFSSL_CFLAGS)

frosted_rng.o: frosted_rng.c
	@echo "  CC      $<"
	$(Q)$(CC) -c -o $@ $< $(WOLFSSL_CFLAGS)

../out/libwolfssl.so: $(WOLFSSL_OBJS) shlib-elf2flt.ld
	@echo "  SHLIB   libwolfssl.so (lib_id=1, .text=$(SHLIB_TEXT_BASE))"
	$(Q)FLTFLAGS="-S -L1 -E$(CURDIR)/wolfssl.exports" \
		$(CC) -nostdlib -o $@ $(WOLFSSL_OBJS) $(CFLAGS) \
		-fPIC -mlong-calls -fno-common \
		-Wl,-elf2flt -Wl,-T,$(CURDIR)/shlib-elf2flt.ld \
		-Wl,--start-group -lc -lgloss -lgcc -Wl,--end-group -Wl,-e,0
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
	rm -f *.a *.o *.map *.ld
	rm -f wolfssl/wolfcrypt/src/*.o
	rm -f wolfssl/wolfcrypt/src/port/st/*.o
	rm -f ../out/libwolfssl.so

distclean: clean
	rm -rf wolfssl wolfssh
