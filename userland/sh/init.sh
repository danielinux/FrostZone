#!/bin/fresh
mount /dev/spiflash0 /mnt flashfs
#/bin/strace echo strace-smoke
/bin/fresh -t /dev/ttyS0
