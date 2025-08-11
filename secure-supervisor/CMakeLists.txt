cmake_minimum_required(VERSION 3.13)
set(CMAKE_CXX_COMPILER arm-none-eabi-gcc)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_LINKER arm-none-eabi-gcc)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfloat-abi=soft")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfloat-abi=soft")


set(PICOTOOL_FETCH_FROM_GIT_PATH build/picotool)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)


project(secure)
set(PICO_PLATFORM=rp2350)
set(PICO_BOARD=pico2_w)
set(PICO_CYW43_SUPPORTED=1)
set(CYW43_CONFIG_FILE="cyw43_configport.h")
set(PICO_NO_SHARED_INTERRUPT_TABLE)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()


add_executable(secure 
    ./ivt.c
    ./startup_secure.c
    ./secure_main.c
    ./mempool.c
    ./task.c
    ./mpu.c
    ./rp2350.c
)

add_compile_definitions(PICO_STDIO_USB_SUPPORT_CHARS_AVAILABLE_CALLBACK=0
    PICO_CYW43_SUPPORTED=1
    CYW43_LWIP=0 
    CYW43_PIN_WL_REG_ON=23u
    CYW43_PIN_WL_DATA_OUT=24u
    CYW43_PIN_WL_DATA_IN=24u
    CYW43_PIN_WL_HOST_WAKE=24u
    CYW43_PIN_WL_CLK=29u
    CYW43_PIN_WL_CS=25u
    CYW43_PIN_WL_DYNAMIC=0
    PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1
)

# Add cflags
target_compile_options(secure PRIVATE
    -Wstack-usage=8192
    -Wno-stringop-overflow
    -Wno-unused-function
    -fno-exceptions
    -fstack-check
    -mcpu=cortex-m33
    -mthumb
    -mcmse
)

target_link_options(secure PRIVATE
    -mcpu=cortex-m33
    -mthumb
    -mcmse
    -Wl,--cmse-implib -Wl,--out-implib=../../frosted/nsc_kernel.o
)


target_include_directories(secure PRIVATE 
    ./include
)

target_link_libraries(secure pico_stdlib hardware_flash)
pico_set_linker_script(secure ../secure-supervisor.ld)
pico_enable_stdio_usb(secure 1)
pico_enable_stdio_uart(secure 0)


pico_add_extra_outputs(secure)
