#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPONENT_DIR="${ROOT}/frosted"
BUILD_DIR="${COMPONENT_DIR}/build"
PICO_SDK_PATH="${PICO_SDK_PATH:-${HOME}/src/pico-sdk}"

cmake -S "${COMPONENT_DIR}" \
      -B "${BUILD_DIR}" \
      -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
      -DPICO_PLATFORM=rp2350

cmake --build "${BUILD_DIR}" --clean-first

arm-none-eabi-objcopy -O binary "${BUILD_DIR}/task0.elf" "${BUILD_DIR}/task0.bin"
