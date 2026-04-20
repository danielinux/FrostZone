# Debug a Frosted userspace process (python) via m33mu's gdbstub.
#
# Usage:
#   1. Start m33mu with the gdb stub:
#        sudo stdbuf -oL -eL m33mu --cpu stm32h563 --gdb --port 1234 --uart-stdout \
#             --tap:tap1 \
#             --spiflash:SPI3:file=jedec_pylibs.bin:size=0x800000:mmap=0x60000000:cs=PC8 \
#             secure-supervisor/secure.elf frosted/kernel.elf /tmp/frostzone-userspace.hex
#
#   2. In another terminal:
#        arm-frosted-eabi-gdb -x scripts/debug_python.gdb
#
#   3. Let it `continue` — the emulator runs until it stops on a fault
#      (SIGSEGV) or you hit ^C. At that point use:
#        (gdb) info reg
#        (gdb) bt
#        (gdb) x/8i $pc-16
#        (gdb) disassemble $pc-32, $pc+32
#
#   4. To put a hardware breakpoint after python is loaded, do:
#        (gdb) hbreak process_import_at_level
#
# /bin/python's runtime text base in xipfs is 0x080bdd4c (derived from
# scripts layout: xipfs entry @ 0x80bdccc, payload @ 0x80bdd0c,
# bFLT text_start @ payload+64).

set pagination off
set confirm off

# ----- kernel symbols -----
file frosted/kernel.elf

# ----- python user binary (bFLT) — load its ELF with runtime text base -----
add-symbol-file userland/micropython/ports/unix/build-frosted/micropython.gdb 0x080bdd4c

# ----- attach to m33mu -----
target remote :1234

# Use hardware breakpoints only; software breakpoints write BKPT to flash
# which triggers a kernel MEMFAULT in the TrustZone-protected image.
set breakpoint auto-hw on

echo \n*** Connected. "continue" to run — interrupt (^C) when the fault happens, or:\n
echo     (gdb) hbreak mp_obj_get_array\n
echo     (gdb) continue\n
echo \n
