cmake -B build -DPICO_SDK_PATH=/home/dan/src/pico-sdk -DPICO_PLATFORM=rp2350 -DPICO_BOARD=pico2_w
make -C build
JLinkExe -Device RP2350_M33_0 -If swd -Speed 4000 -CommanderScript flash_hypervisor.jlink
