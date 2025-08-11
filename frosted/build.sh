#!/bin/bash
PICO_SDK_PATH=~/src/pico-sdk
cmake -DPICO_SDK_PATH=$PICO_SDK_PATH -DPICO_PLATFORM=rp2350 -B build

make -C build clean
make -C build || exit 1
arm-none-eabi-objcopy -O binary build/task0.elf build/task0.bin

