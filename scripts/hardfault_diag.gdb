# Hardfault diagnostic script for arm-none-eabi-gdb.
# Invoked by scripts/hardfault_diag.sh.

set pagination off
set confirm off
set breakpoint always-inserted on
set print pretty on

target remote :3333
monitor reset
add-symbol-file frosted/kernel.elf
hbreak scheduler.c:2217
continue
info registers
list scheduler.c:2217
printf "Ready to return from pend_sv_handler\nStepping once:"
stepi
info registers
printf "Stepping again:"
stepi
info registers
printf "Waiting for hardfault\n"
continue

set $hf_reported = 0

define hardfault_report
    printf "\n=== HardFault captured ===\n"
    printf "[Core] PC = 0x%08x  LR = 0x%08x  xPSR = 0x%08x\n", $pc, $lr, $xpsr

    printf "\n-- General-purpose registers --\n"
    info registers
    info registers msp msp_ns psp psp_ns control control_ns xpsr

    printf "\n-- Exception stack (argument r0) --\n"
    set $stackptr = (unsigned int *)$r0
    if $stackptr == 0
        if ($lr & 0x4) == 0
            set $stackptr = (unsigned int *)$msp
        else
            set $stackptr = (unsigned int *)$psp
        end
    end
    printf "stackptr = 0x%08x\n", $stackptr
    if $stackptr != 0
        x/8wx $stackptr
    else
        printf "(stack pointer unavailable)\n"
    end

    printf "\n-- Non-secure process stack snapshot (PSP_NS) --\n"
    printf "psp_ns = 0x%08x\n", $psp_ns
    if $psp_ns != 0
        x/64wx $psp_ns
    else
        printf "(non-secure PSP not available)\n"
    end

    printf "\n-- Fault status registers --\n"
    set $cfsr = *(unsigned int *)0xE000ED28
    set $mmfsr = $cfsr & 0xff
    set $bfsr = ($cfsr >> 8) & 0xff
    set $ufsr = ($cfsr >> 16) & 0xffff
    set $hfsr = *(unsigned int *)0xE000ED2c
    set $dfsr = *(unsigned int *)0xE000ED30
    set $shcsr = *(unsigned int *)0xE000ED24
    set $mmfar = *(unsigned int *)0xE000ED34
    set $bfar = *(unsigned int *)0xE000ED38
    set $afsr = *(unsigned int *)0xE000ED3c
    set $sfsr = *(unsigned int *)0xE000EDE4
    set $sfar = *(unsigned int *)0xE000EDE8

    printf "CFSR  = 0x%08x (MMFSR=0x%02x BFSR=0x%02x UFSR=0x%04x)\n", $cfsr, $mmfsr, $bfsr, $ufsr
    printf "HFSR  = 0x%08x\n", $hfsr
    printf "DFSR  = 0x%08x\n", $dfsr
    printf "SHCSR = 0x%08x\n", $shcsr
    printf "MMFAR = 0x%08x\n", $mmfar
    printf "BFAR  = 0x%08x\n", $bfar
    printf "AFSR  = 0x%08x\n", $afsr
    printf "SFSR  = 0x%08x\n", $sfsr
    printf "SFAR  = 0x%08x\n", $sfar


    printf "SAU Info:\n"
    x/1wx 0xE000EDD0    
    x/1wx 0xE000EDD4     

    set $i=0
    while $i < 8
      set *(unsigned int*)0xE000EDD8 = $i          
      p/x *(unsigned int*)0xE000EDDC               
      p/x *(unsigned int*)0xE000EDE0               
      set $i = $i + 1
    end

    printf "MPU info:\n"

    x/1wx 0xE000ED90     
    x/1wx 0xE000ED94     

    x/1wx 0xE000EDC0     
    x/1wx 0xE000EDC4      

    set $i=0
    while $i < 8
      set *(unsigned int*)0xE000ED98 = $i     
      p/x *(unsigned int*)0xE000ED9C          
      p/x *(unsigned int*)0xE000EDA0          
      set $i = $i + 1
    end


    printf "\n-- Memory map --\n"
    info files

    printf "\n-- Call stack --\n"
    bt

    printf "\n=== End of HardFault report ===\n"
    set $hf_reported = 1
    quit
end

hbreak HardFault_Handler
hbreak hardfault_handler_dbg
hbreak hard_fault_handler

define hook-stop
    if $hf_reported == 0
        if $pc == HardFault_Handler || $pc == hard_fault_handler
            hardfault_report
        end
    end
end

continue
