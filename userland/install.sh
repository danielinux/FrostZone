#!/bin/bash
make clean all
JLinkExe -Device RP2350_M33_0 -If swd -Speed 4000 -CommanderScript flash_userspace.jlink
