# FrostZone

Originally developed in 2015, Frosted OS began as a minimalist, open-source real-time operating system aimed at embedded platforms, with a clean UNIX-like design and a focus on simplicity over bloat. It gained a following among hobbyists and systems developers for its modular kernel, small footprint, and approachable codebase. While active development slowed as embedded systems evolved, Frosted remained a solid example of efficient, well-structured OS design. Now, ten years on, the project returns as **FrostZone**, revamped for modern ARMv8-M microcontrollers with a dual-kernel configuration. And strengthened by TrustZone-based supervision.

## Goal

This project implements a small secure microkernel and a modular real-time operating system designed for ARMv8-M TrustZone-enabled microcontrollers. It aims to provide a POSIX compliant interfaces towards userspace, while keeping sandboxing and isolation between processes using MPU and TrustZone.

## Supported Hardware

FrostZone is currently under development, and only supports RP2350 (i.e. Raspberry pi Pico-2).

Support for other ARMv8-M targets will be added in the future.


## Architecture Overview

```
+-------------+      +--------------+                                                                
|             |      |              |                  - POSIX system calls                         
|   POSIX     |      |  POSIX       |                  - vfork, exec                                
|   thread    |      |  thread      |                  - XIP executables                            
+-----|-------+      +-----|--------+                  - micropython, TCP/IP pthreads       
      |                    |                                                                        
      |system call         | system call                                                            
      v                    v                                                                        
 +-----------------------------------------------+                                                  
 |   Frosted                                     |     - POSIX preemptive kernel with RT priorities
 |   preemptive scheduler                        |     - MPU based stack protection
 |    |                                          |     - TCP/IP sockets
 +----|------------------------------------------+                                                  
      |                                                                                             
      | nsc gateway                                                                                
      |                                                                                             
      v                                                                                             
 +-----------------------------------------------+                                                  
 |                                               |     - per-task limits + capabilities check 
 |Secure Microkernel                             |     - memory pool        
 |                                               |     - regulate access to peripherals             
 |                                               |     - crypto back-end, secure vault, ...
 +-----------------------------------------------+
```

## Memory layout (rp2350)

* **Flash layout**:
  * `0x10000000 - 0x10007FFF`: Secure supervisor (32 KB)
  * `0x10008000 - 0x1000FFFF`: Non-secure callables (32 KB)
  * `0x10010000 - 0x1002FFFF`: Frosted OS
  * `0x10030000 - ...       `: XIPFS (executable-in-place file system)  (1.8MB)

* **RAM layout**:

  * `0x20000000 - 0x2000FFFF`: Secure supervisor RAM
  * `0x20010000 - 0x2001FFFF`: Kernel RAM
  * `0x20040000 - ...       `: Memory pool managed in secure mode (384KB)

## Building and installing the kernels

To build both kernels, pico-sdk is needed. The scripts assume that you have
a clone of the repository in `~/src/pico-sdk`. If pico-sdk is in a different
directory, adjust build.sh accordingly.

Use `build.sh` scripts. Build the supervisor first:

```
cd secure-supervisor
./build.sh
``` 

This also generates the `frosted/nsc_kernel.o`, which is the interface used by Frosted
to interact with the secure supervisor, and must be included in the Frosted build.

Build Frosted afterwards:

```
cd frosted
./build.sh
```

To install both kernels, since they are separate binaries, you will need a debugger connected
to the SWD pins on your raspberry pi pico device. The scripts `install.sh` in both directories will automatically
flash the two kernels in the assigned partitions.


## Building and installing userspace

 * Userland build requires a specific toolchain with the `arm-frosted-eabi` triplet.
   This process will be documented soon.


## Current Status

* Secure supervisor running with TrustZone configured SAU
* CMSE gateways for allocation and task limit enforcement
* Process scheduler with vfork+exec
* XIPFS integration
* Minimal userspace (init, "fresh" shell, a few filesystem utilities

## TODO

* Expand userspace support for base tools
* TCP/IP support in-kernel, socket interfaces
* HW support: USB-ETH, Pico2-W wlan
* Port more features and system calls from original Frosted OS
* Secure boot / dual-kernel and xipfs updates

## License

Supervisor code: GPLv3

Legacy Frosted Kernel: GPLv2
Including the following in-kernel libraries:
 * TinyUSB: MIT License (MIT)  Copyright (c) 2019 Ha Thach (tinyusb.org)

## Copyright 

FrostZone RTOS kernel is a port of Frosted OS (c) 2015 insane-adding-machines
Cortex-M33 port (c) 2025 @danielinux


