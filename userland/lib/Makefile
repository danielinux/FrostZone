-include ../kconfig/.config

CROSS_COMPILE ?= arm-frosted-eabi-
CC := $(CROSS_COMPILE)gcc

# Same helper as in ../Makefile: turn a kconfig y/Y/1/unset value
# into a literal 1 or 0 for use as a preprocessor define.
kconfig_bool = $(strip $(if $(filter y Y 1,$(1)),1,0))

WOLFSSL_REPO := wolfSSL/wolfssl
WOLFSSH_REPO := wolfSSL/wolfssh

GITHUB_API := https://api.github.com/repos

# SQLite amalgamation is fetched directly from sqlite.org as a source zip.
# The amalgamation (sqlite3.c / sqlite3.h / shell.c) is produced locally
# on the build host via `./configure && make sqlite3.c shell.c`, which
# needs tclsh available on the build host.
SQLITE_VERSION := 3530000
SQLITE_URL := https://sqlite.org/2026/sqlite-src-$(SQLITE_VERSION).zip
SQLITE_SRCDIR := sqlite-src-$(SQLITE_VERSION)
SQLITE_ZIP := sqlite-src-$(SQLITE_VERSION).zip

# Resolve latest release tag via GitHub API
wolfssl_tag = $(shell cat wolfssl/.tag 2>/dev/null)
wolfssh_tag = $(shell cat wolfssh/.tag 2>/dev/null)
sqlite_tag = $(shell cat sqlite/.tag 2>/dev/null)

LIBS-y:=
SHLIB_TARGETS:=
RUNTIME_ARCHIVES:=

all: fetch shlibs
	@echo "Libraries ready."

APPS_ORIGIN ?= 0x08040000
XIPFS_FAT_SIZE := 16
XIPFS_FHDR_SIZE := 64
BFLT_HDR_SIZE := 64

# Flash-resident base address for the first library .text.
# Libraries are packed first in xipfs:
#   APPS_ORIGIN + sizeof(xipfs_fat)(16) + sizeof(xipfs_fhdr)(64) + sizeof(flat_hdr)(64)
FIRST_SHLIB_TEXT_BASE = $(shell printf '0x%x' $$(( $(APPS_ORIGIN) + $(XIPFS_FAT_SIZE) + $(XIPFS_FHDR_SIZE) + $(BFLT_HDR_SIZE) )) )

# The next library starts after the previous xipfs file header + payload,
# rounded up to a 4-byte boundary.
define next_shlib_text_base
$(shell size=$$(stat -c %s $(1) 2>/dev/null || echo 0); \
pad=$$(( (4 - ($$size % 4)) % 4 )); \
printf '0x%x' $$(( $(2) + $$size + $(XIPFS_FHDR_SIZE) + $$pad )))
endef

# Custom elf2flt linker script with .text at the fixed flash address and
# rodata merged into .text (equivalent of ld-elf2flt's -move-rodata).
# Generated from the toolchain's default by:
#   1. Patching .text base address
#   2. Deleting R_RODAT: lines (keep only W_RODAT: — rodata in .text)
#   3. Stripping W_RODAT: / TOR: / SINGLE_LINK: / RISCV_GP: prefixes
ELF2FLT_LD := $(shell $(CC) -print-file-name=elf2flt.ld)
WOLFSSL_TEXT_BASE = $(FIRST_SHLIB_TEXT_BASE)
DLOPEN_TEST_TEXT_BASE = $(if $(filter y,$(LIB_WOLFSSL)),$(call next_shlib_text_base,../out/libwolfssl.so,$(WOLFSSL_TEXT_BASE)),$(FIRST_SHLIB_TEXT_BASE))

shlib-elf2flt-wolfssl.ld: $(ELF2FLT_LD)
	@echo "  GEN     $@ (.text @ $(WOLFSSL_TEXT_BASE))"
	$(Q)sed -e 's|\.text 0x0 :|.text $(WOLFSSL_TEXT_BASE) :|' \
	    -e '/^R_RODAT:/d' \
	    -e '/^TOR:/d' \
	    -e 's|^W_RODAT:||' \
	    -e 's|^SINGLE_LINK:||' \
	    -e 's|^RISCV_GP:||' \
	    $< > $@

shlib-elf2flt-dltest.ld: $(ELF2FLT_LD) $(if $(filter y,$(LIB_WOLFSSL)),../out/libwolfssl.so)
	@echo "  GEN     $@ (.text @ $(DLOPEN_TEST_TEXT_BASE))"
	$(Q)sed -e 's|\.text 0x0 :|.text $(DLOPEN_TEST_TEXT_BASE) :|' \
	    -e '/^R_RODAT:/d' \
	    -e '/^TOR:/d' \
	    -e 's|^W_RODAT:||' \
	    -e 's|^SINGLE_LINK:||' \
	    -e 's|^RISCV_GP:||' \
	    $< > $@

# ---- dlopen/dlsym sample library ------------------------------------
ifeq ($(APP_DLOPEN_TEST),y)
SHLIB_TARGETS += ../out/libdltest.so

DLOPEN_TEST_CFLAGS := $(CFLAGS) -Os
DLOPEN_TEST_OBJS := dltest_lib.o

dltest_lib.o: dltest_lib.c
	@echo "  CC      $<"
	$(Q)$(CC) -c -o $@ $< $(DLOPEN_TEST_CFLAGS)

../out/libdltest.so: $(DLOPEN_TEST_OBJS) shlib-elf2flt-dltest.ld
	@echo "  SHLIB   libdltest.so (lib_id=2, .text=$(DLOPEN_TEST_TEXT_BASE))"
	$(Q)FLTFLAGS="-S -L2 -E$(CURDIR)/dltest.exports" \
		$(CC) -nostdlib -o $@ $(DLOPEN_TEST_OBJS) $(CFLAGS) \
		-fPIC -mlong-calls -fno-common \
		-Wl,-elf2flt -Wl,-T,$(CURDIR)/shlib-elf2flt-dltest.ld \
		-Wl,--start-group -lc -lgloss -lgcc -Wl,--end-group -Wl,-e,0
endif

# ---- wolfSSL --------------------------------------------------------
ifeq ($(LIB_WOLFSSL),y)
LIBS-y += wolfssl
SHLIB_TARGETS += ../out/libwolfssl.so
RUNTIME_ARCHIVES += libwolfssl_runtime.a

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

../out/libwolfssl.so: $(WOLFSSL_OBJS) shlib-elf2flt-wolfssl.ld
	@echo "  SHLIB   libwolfssl.so (lib_id=1, .text=$(WOLFSSL_TEXT_BASE))"
	$(Q)FLTFLAGS="-S -L1 -E$(CURDIR)/wolfssl.exports" \
		$(CC) -nostdlib -o $@ $(WOLFSSL_OBJS) $(CFLAGS) \
		-fPIC -mlong-calls -fno-common \
		-Wl,-elf2flt -Wl,-T,$(CURDIR)/shlib-elf2flt-wolfssl.ld \
		-Wl,--start-group -lc -lgloss -lgcc -Wl,--end-group -Wl,-e,0
# Runtime resolver archive — forwards wolfCrypt calls through dlopen/dlsym.
wolfssl_runtime.o: wolfssl_runtime.c
	$(CC) -c -o $@ $< $(CFLAGS)

libwolfssl_runtime.a: wolfssl_runtime.o
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

# ---- SQLite ---------------------------------------------------------
ifeq ($(LIB_SQLITE),y)
LIBS-y += sqlite
SHLIB_TARGETS += ../out/libsqlite.so
RUNTIME_ARCHIVES += libsqlite_runtime.a

# Pick the preceding shared library (if any) so libsqlite.so's .text
# lands after the previous one in flash. Packaging order in
# userspace.bin is: libwolfssl.so, libdltest.so, then every other *.so.
ifeq ($(APP_DLOPEN_TEST),y)
SQLITE_PREV_SO  := ../out/libdltest.so
SQLITE_PREV_BASE := $(DLOPEN_TEST_TEXT_BASE)
else ifeq ($(LIB_WOLFSSL),y)
SQLITE_PREV_SO  := ../out/libwolfssl.so
SQLITE_PREV_BASE := $(WOLFSSL_TEXT_BASE)
else
SQLITE_PREV_SO  :=
SQLITE_PREV_BASE :=
endif

SQLITE_TEXT_BASE = $(if $(SQLITE_PREV_SO),$(call next_shlib_text_base,$(SQLITE_PREV_SO),$(SQLITE_PREV_BASE)),$(FIRST_SHLIB_TEXT_BASE))

shlib-elf2flt-sqlite.ld: $(ELF2FLT_LD) $(SQLITE_PREV_SO)
	@echo "  GEN     $@ (.text @ $(SQLITE_TEXT_BASE))"
	$(Q)sed -e 's|\.text 0x0 :|.text $(SQLITE_TEXT_BASE) :|' \
	    -e '/^R_RODAT:/d' \
	    -e '/^TOR:/d' \
	    -e 's|^W_RODAT:||' \
	    -e 's|^SINGLE_LINK:||' \
	    -e 's|^RISCV_GP:||' \
	    $< > $@

# Minimal-feature SQLite build. Everything non-essential is stripped;
# temp tables live in memory (SQLITE_TEMP_STORE=2) because Frosted has
# no /tmp; double-quoted string literals are disabled (DQS=0); UTF-16,
# the shared cache, authorizer, load-extension and deprecated APIs are
# all compiled out. Optional features (FTS5, RTREE, JSON1) are added
# back via menuconfig sub-options.
SQLITE_OMIT_FLAGS := \
    -DSQLITE_THREADSAFE=$(call kconfig_bool,$(LIB_SQLITE_THREADSAFE)) \
    -DSQLITE_DEFAULT_MEMSTATUS=0 \
    -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1 \
    -DSQLITE_LIKE_DOESNT_MATCH_BLOBS \
    -DSQLITE_OMIT_DEPRECATED \
    -DSQLITE_OMIT_SHARED_CACHE \
    -DSQLITE_USE_ALLOCA \
    -DSQLITE_OMIT_AUTOINIT \
    -DSQLITE_DQS=0 \
    -DSQLITE_OMIT_LOAD_EXTENSION \
    -DSQLITE_OMIT_AUTHORIZATION \
    -DSQLITE_OMIT_UTF16 \
    -DSQLITE_OMIT_EXPLAIN \
    -DSQLITE_OMIT_TRACE \
    -DSQLITE_OMIT_WAL \
    -DSQLITE_OMIT_PROGRESS_CALLBACK \
    -DSQLITE_OMIT_COMPILEOPTION_DIAGS \
    -DSQLITE_OMIT_INCRBLOB \
    -DSQLITE_OMIT_INTEGRITY_CHECK \
    -DSQLITE_OMIT_AUTOVACUUM \
    -DSQLITE_OMIT_AUTORESET \
    -DSQLITE_OMIT_BUILTIN_TEST \
    -DSQLITE_OMIT_TCL_VARIABLE \
    -DSQLITE_OMIT_GET_TABLE \
    -DSQLITE_OMIT_XFER_OPT \
    -DSQLITE_TEMP_STORE=2 \
    -DSQLITE_DEFAULT_PAGE_SIZE=1024 \
    -DSQLITE_DEFAULT_CACHE_SIZE=-32 \
    -DSQLITE_MAX_EXPR_DEPTH=100 \
    -DSQLITE_MAX_ATTACHED=4 \
    -DSQLITE_MAX_COLUMN=64 \
    -DSQLITE_MAX_MMAP_SIZE=0 \
    -DHAVE_USLEEP=1

ifeq ($(LIB_SQLITE_ENABLE_FTS5),y)
SQLITE_OMIT_FLAGS += -DSQLITE_ENABLE_FTS5
endif
ifeq ($(LIB_SQLITE_ENABLE_RTREE),y)
SQLITE_OMIT_FLAGS += -DSQLITE_ENABLE_RTREE
endif
ifeq ($(LIB_SQLITE_ENABLE_JSON1),y)
SQLITE_OMIT_FLAGS += -DSQLITE_ENABLE_JSON1
endif

# sqlite3.c is a ~240k-line amalgamation; -Os and -w keep it manageable
# and hide upstream warnings that aren't actionable from here. Drop the
# project-wide -Werror and -Wdeclaration-after-statement because SQLite's
# style does not conform, and relax -Wstack-usage which the deep VDBE
# paths occasionally exceed before constant folding.
SQLITE_CFLAGS := $(filter-out -Werror -Wdeclaration-after-statement -Wstack-usage=%,$(CFLAGS)) \
    -Os -w -Wno-error $(SQLITE_OMIT_FLAGS) -I. -Isqlite

SQLITE_OBJS := sqlite/sqlite3.o sqlite_posix_stubs.o
ifeq ($(APP_SQLITE),y)
# shell.c is pulled into libsqlite.so with main() renamed to
# sqlite_shell_main so the tiny CLI stub in binutils/sqlite.c can
# invoke it through the dlsym resolver.
SQLITE_OBJS += sqlite/shell.o
endif

sqlite/sqlite3.o: sqlite/.tag
	@echo "  CC      sqlite/sqlite3.c"
	$(Q)$(CC) -c -o $@ sqlite/sqlite3.c $(SQLITE_CFLAGS)

sqlite/shell.o: sqlite/.tag
	@echo "  CC      sqlite/shell.c (-Dmain=sqlite_shell_main)"
	$(Q)$(CC) -c -o $@ sqlite/shell.c $(SQLITE_CFLAGS) -Dmain=sqlite_shell_main

# Stubs for POSIX calls that Frosted does not implement (stat/lstat/
# access/fsync/fchmod/fchown/...). stat+lstat also prevent newlib's
# libgloss from pulling in its stat.o, which would collide with libc's
# sysfstat.o on fstat().
sqlite_posix_stubs.o: sqlite_posix_stubs.c
	@echo "  CC      $<"
	$(Q)$(CC) -c -o $@ $< $(CFLAGS) -Wno-error

../out/libsqlite.so: $(SQLITE_OBJS) shlib-elf2flt-sqlite.ld
	@echo "  SHLIB   libsqlite.so (lib_id=3, .text=$(SQLITE_TEXT_BASE))"
	$(Q)FLTFLAGS="-S -L3 -E$(CURDIR)/sqlite.exports" \
	    $(CC) -nostdlib -o $@ $(SQLITE_OBJS) $(CFLAGS) \
	    -fPIC -mlong-calls -fno-common \
	    -Wl,-elf2flt -Wl,-T,$(CURDIR)/shlib-elf2flt-sqlite.ld \
	    -Wl,--start-group -lc -lgloss -lgcc -lm -Wl,--end-group -Wl,-e,0

sqlite_runtime.o: sqlite_runtime.c
	$(CC) -c -o $@ $< $(CFLAGS)

libsqlite_runtime.a: sqlite_runtime.o
	$(AR) rcs $@ $^

# Fetch sqlite-src zip from sqlite.org, unpack it and generate the
# amalgamation on the host. `./configure && make sqlite3.c shell.c`
# needs tclsh on the build machine; we probe for it up-front.
sqlite/.tag:
	@command -v tclsh >/dev/null 2>&1 || { \
	    echo "error: tclsh is required to generate the SQLite amalgamation"; \
	    echo "       install it (e.g. 'apt install tcl') and re-run make"; \
	    exit 1; }
	@command -v unzip >/dev/null 2>&1 || { \
	    echo "error: unzip is required to unpack $(SQLITE_ZIP)"; exit 1; }
	@echo "  FETCH   sqlite (src $(SQLITE_VERSION))"
	@rm -rf sqlite $(SQLITE_SRCDIR) $(SQLITE_ZIP)
	@curl -sfL -o $(SQLITE_ZIP) $(SQLITE_URL)
	@unzip -q $(SQLITE_ZIP)
	@mv $(SQLITE_SRCDIR) sqlite
	@rm -f $(SQLITE_ZIP)
	@echo "  GEN     sqlite3.c sqlite3.h shell.c (amalgamation)"
	@cd sqlite && ./configure --quiet >/dev/null
	@cd sqlite && $(MAKE) --no-print-directory sqlite3.c sqlite3.h shell.c
	@echo "$(SQLITE_VERSION)" > sqlite/.tag
	@echo "  OK      sqlite ($(SQLITE_VERSION))"
endif

fetch: $(addsuffix /.tag,$(LIBS-y))

# Build shared libraries and any runtime resolver archives.
shlibs: fetch $(SHLIB_TARGETS) $(RUNTIME_ARCHIVES)

# Re-fetch: remove tag files to trigger fresh downloads
refetch:
	rm -f wolfssl/.tag wolfssh/.tag sqlite/.tag
	$(MAKE) fetch

clean:
	rm -f *.a *.o *.map *.ld
	rm -f wolfssl/wolfcrypt/src/*.o
	rm -f wolfssl/wolfcrypt/src/port/st/*.o
	rm -f sqlite/sqlite3.o sqlite/shell.o
	rm -f ../out/libwolfssl.so
	rm -f ../out/libdltest.so
	rm -f ../out/libsqlite.so

distclean: clean
	rm -rf wolfssl wolfssh sqlite sqlite-src-*.zip
