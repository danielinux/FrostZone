define addex
  if $argc < 1
    printf "usage: addex <file>\n"
  else
    # Helpful echo
    printf "Adding %s @ text=%p data=%p bss=%p\n", $arg0, reloc_text, reloc_data, reloc_bss
    # Do the thing
    add-symbol-file ../../../frosted-userland/gdb/$arg0.gdb (uintptr_t)reloc_text \
      -s .data (uintptr_t)reloc_data \
      -s .bss  (uintptr_t)reloc_bss
  end
end





file build/secure.elf
target remote :3333
mon reset init
break secure_main
add-symbol-file ../../../nonsecure_kernel/port/rp2350/build/task0.elf
focus cmd
continue
