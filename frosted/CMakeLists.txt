cmake_minimum_required(VERSION 3.13)
set(CMAKE_CXX_COMPILER arm-none-eabi-gcc)
set(LIB_PICO_RUNTIME_INIT=0)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
set(PICOTOOL_FETCH_FROM_GIT_PATH build/picotool)

set(BOOT_STAGE2_FILE ${CMAKE_CURRENT_LIST_DIR}/boot2_empty.S)
#set(PICO_NO_RUNTIME 1)
#set(PICO_NO_FLASH 1)
#set(PICO_RUNTIME_SKIP_INIT_CLOCKS 1)
#set(LIB_PICO_RUNTIME 0)

project(task0)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()


add_executable(task0

    # Secure microkernel API
    ./nsc_kernel.o

    # Frosted kernel sources
    ./ns_main.c
    ./startup_ns.c
    ./frosted.c
    ./cirbuf.c
    ./scheduler.c
    ./module.c
    ./sys.c
    ./device.c
    ./privileged_alloc.c
    ./tasklet.c
    ./syscall_table.c
    ./systick.c
    ./vfs.c
    ./xipfs.c
    ./bflt.c
    ./term.c
    ./fpb.c
    ./string.c
    ./null.c
    ./pipe.c
    ./pty.c
    ./locks.c
    ./getaddrinfo.c
    ./locks.c
    ./usb.c
    ./usb_descriptors.c
    ./tty_console.c
    ./sysfs.c
    ./memfs.c
    
    ./frosted_rp2350.c

    ./socket_in.c
    ./wolfip.c
	
    # TinyUSB
    ./lib/tinyusb/src/tusb.c
	./lib/tinyusb/src/common/tusb_fifo.c
	./lib/tinyusb/src/device/usbd.c
	./lib/tinyusb/src/device/usbd_control.c
	./lib/tinyusb/src/typec/usbc.c
	./lib/tinyusb/src/class/audio/audio_device.c
	./lib/tinyusb/src/class/cdc/cdc_device.c
	./lib/tinyusb/src/class/dfu/dfu_device.c
	./lib/tinyusb/src/class/dfu/dfu_rt_device.c
	./lib/tinyusb/src/class/hid/hid_device.c
	./lib/tinyusb/src/class/midi/midi_device.c
	./lib/tinyusb/src/class/msc/msc_device.c
	./lib/tinyusb/src/class/net/ecm_rndis_device.c
	./lib/tinyusb/src/class/net/ncm_device.c
	./lib/tinyusb/src/class/usbtmc/usbtmc_device.c
	./lib/tinyusb/src/class/video/video_device.c
	./lib/tinyusb/src/class/vendor/vendor_device.c
    ./lib/tinyusb/src/host/usbh.c
    ./lib/tinyusb/src/host/hub.c
    ./lib/tinyusb/src/class/cdc/cdc_host.c
    ./lib/tinyusb/src/class/hid/hid_host.c
    ./lib/tinyusb/src/class/msc/msc_host.c
    ./lib/tinyusb/src/class/vendor/vendor_host.c
    ./lib/tinyusb/src/typec/usbc.c
    ./lib/tinyusb/src/portable/raspberrypi/rp2040/dcd_rp2040.c
    ./lib/tinyusb/src/portable/raspberrypi/rp2040/hcd_rp2040.c
    ./lib/tinyusb/src/portable/raspberrypi/rp2040/rp2040_usb.c

)

target_compile_definitions(task0 PRIVATE
    PICO_NO_FLASH=1
    PICO_NO_RUNTIME=1
    PICO_RUNTIME_SKIP_INIT_CLOCKS=1
    PICO_RUNTIME_NO_INIT_CLOCKS=1
    PICO_RUNTIME_SKIP_INIT_BOOTROM_RESET=1
    PICO_RUNTIME_NO_INIT_BOOTROM_RESET=1
    PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1
    PICO_NO_FLASH=1
    LIB_PICO_RUNTIME=0
    LIB_PICO_CRT0=0
    LIB_PICO_STANDARD_LINK=0
    PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1

    CONFIG_SYS_CLOCK=120000000
    CONFIG_APPS_ORIGIN=0x10030000
    CONFIG_FLASH_ORIGIN=0x10010000

    CFG_TUSB_MCU=OPT_MCU_RP2040
    CONFIG_DEVUSB=1
    CFG_TUD_ENABLED=1
    #TUD_OPT_RP2040_USB_DEVICE_UFRAME_FIX=1


    #CONFIG_USB_POLLING=1
    CONFIG_USB_NET=1
    CONFIG_SIGNALS=1
    CONFIG_PTY_UNIX=1

    LINK_MTU=1514
)

target_include_directories(task0 PRIVATE 
    ./
    ./rp2350
    ./include
    ./libc/include
    ../frosted-headers/include
    ./lib/tinyusb/src
    ./lib/tinyusb/hw
)

target_compile_options(task0 PRIVATE
    -nostartfiles
    -nostdlib
    -nostdinc
    -ffreestanding
    -fno-toplevel-reorder
    -fno-common
    -fno-lto
    -Wl,--emit-relocs
    -mlong-calls
    -O0
    -ggdb
)

target_link_options(task0 PRIVATE
    -nostartfiles
    -nostdlib
    -ffreestanding
    -mlong-calls
    -fno-lto
    -Wl,--build-id=none
    -Wl,--print-memory-usage
    -Wl,--print-output-format
    -Wl,--no-dynamic-linker
    -Wl,--emit-relocs
    -Wl,--Map=task0.map
    -Wl,-T../kernel.ld
)

pico_set_linker_script(task0 ../kernel.ld)
# target_link_libraries(task0 pico_stdlib)

# create map/bin/hex/uf2 file etc.
#pico_add_extra_outputs(task0)
pico_set_binary_type(task0 hex map bin dis)

