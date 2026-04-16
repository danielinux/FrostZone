# build a minimal interpreter

FROZEN_MANIFEST =

MICROPY_PY_BTREE = 0
MICROPY_PY_FFI = 0
MICROPY_PY_SOCKET = 1
MICROPY_PY_THREAD = 0
MICROPY_PY_TERMIOS = 0
MICROPY_PY_SSL = 0
MICROPY_USE_READLINE = 1

MICROPY_VFS_FAT = 0
MICROPY_VFS_LFS1 = 0
MICROPY_VFS_LFS2 = 0
MICROPY_VFS = 1
MICROPY_VFS_WRITABLE = 0
MICROPY_VFS_POSIX = 1
MICROPY_VFS_POSIX_WRITABLE = 0

CFLAGS_EXTRA += \
    -D__Frosted__ \
    -DMICROPY_VFS_POSIX_HAVE_STATVFS=0 \
    -DMICROPY_VFS_POSIX_HAVE_D_TYPE=0 \
    -DMICROPY_PORT_HEAP_SIZE=16384 \
    -DMICROPY_UNIX_STACK_SIZE=6144 \
    -DUNIX_STACK_MULTIPLIER=1 \
    -DMICROPY_READLINE_MAX_LINE_LEN=2048 \
    -mthumb -mlittle-endian -mthumb-interwork \
    -ffunction-sections -fdata-sections \
    -fPIC -mlong-calls -fno-common -msingle-pic-base -mno-pic-data-is-text-relative

LDFLAGS_EXTRA += -nostartfiles -nostdlib -fPIC -mlong-calls -fno-common -Wl,-elf2flt -Wl,--allow-multiple-definition -Wl,--start-group,-lc,-lgloss,-lgcc,--end-group

STRIP = true
STRIPFLAGS_EXTRA =
SIZE = true
