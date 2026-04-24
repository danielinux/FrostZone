#!/bin/fresh
mount /dev/spiflash0 /mnt flashfs
echo "--- ip link show ---"
ip link show
echo "--- ip addr show ---"
ip addr show
echo "--- ip route show ---"
ip route show
echo "--- ip tests done ---"
/bin/fresh -t /dev/ttyS0
