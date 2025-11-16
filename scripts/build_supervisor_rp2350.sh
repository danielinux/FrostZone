#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPONENT_DIR="${ROOT}/secure-supervisor"
BUILD_DIR="${COMPONENT_DIR}/build"

PICO_SDK_PATH="${PICO_SDK_PATH:-${HOME}/src/pico-sdk}"

cmake -S "${COMPONENT_DIR}" \
      -B "${BUILD_DIR}" \
      -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
      -DPICO_PLATFORM=rp2350 \
      -DPICO_BOARD=pico2_w \
      -DFAMILY=rp2040

cmake --build "${BUILD_DIR}"
