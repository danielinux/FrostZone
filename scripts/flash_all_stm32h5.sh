#!/usr/bin/env bash
#
# Flash all FrostZone STM32H5 binaries (secure supervisor, frosted kernel, userland)
# onto the target board using STM32_Programmer_CLI.
#
# Requirements:
#   - build artifacts already exist:
#       secure-supervisor/secure.bin  (Secure world)
#       frosted/kernel.bin            (Non-secure kernel)
#       userland/build/userland.bin   (Userspace image placed in XIP area)
#   - STM32_Programmer_CLI available in PATH (or override via PROGRAMMER_CLI env)
#
# Optional environment overrides:
#   PROGRAMMER_CLI   (default: STM32_Programmer_CLI)
#   SUPERVISOR_IMAGE (default: secure-supervisor/secure.bin)
#   KERNEL_IMAGE     (default: frosted/kernel.bin)
#   USERLAND_IMAGE   (default: userland/build/userland.bin)
#   SUPERVISOR_ADDR  (default: 0x08000000)   # Secure flash base
#   KERNEL_ADDR      (default: 0x0C010000)   # Non-secure kernel ROM
#   USERLAND_ADDR    (default: 0x0C030000)   # XIP / userspace payload
#
# Usage:
#   ./scripts/flash_all_stm32h5.sh
#   ./scripts/flash_all_stm32h5.sh --skip-userland
#   PROGRAMMER_CLI=/opt/STM32/STLink/STM32_Programmer_CLI ./scripts/flash_all_stm32h5.sh
#

set -euo pipefail

PROGRAMMER_CLI="${PROGRAMMER_CLI:-STM32_Programmer_CLI}"
SUPERVISOR_IMAGE="${SUPERVISOR_IMAGE:-secure-supervisor/secure.bin}"
KERNEL_IMAGE="${KERNEL_IMAGE:-frosted/kernel.bin}"
USERLAND_IMAGE="${USERLAND_IMAGE:-userspace.bin}"

SUPERVISOR_ADDR="${SUPERVISOR_ADDR:-0x0C000000}"
KERNEL_ADDR="${KERNEL_ADDR:-0x08010000}"
USERLAND_ADDR="${USERLAND_ADDR:-0x08030000}"

FLASH_USERLAND=1

usage() {
    cat <<EOF
Flash secure supervisor, frosted kernel, and userland images to STM32H5.

Options:
  --skip-userland   Do not program the userland image.
  -h, --help        Show this help text.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-userland)
            FLASH_USERLAND=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if ! command -v "${PROGRAMMER_CLI}" >/dev/null 2>&1; then
    echo "Error: '${PROGRAMMER_CLI}' not found in PATH." >&2
    exit 1
fi

check_file() {
    local path="$1"
    if [[ ! -f "${path}" ]]; then
        echo "Missing artifact: ${path}" >&2
        exit 1
    fi
}

echo "Checking build artifacts..."
check_file "${SUPERVISOR_IMAGE}"
check_file "${KERNEL_IMAGE}"
if (( FLASH_USERLAND )); then
    if [[ ! -f "${USERLAND_IMAGE}" && -f "userland/build/userland.bin" ]]; then
        USERLAND_IMAGE="userland/build/userland.bin"
        echo "Falling back to ${USERLAND_IMAGE} for userland image."
    fi
    check_file "${USERLAND_IMAGE}"
fi

echo "Connecting to target via ST-LINK..."
"${PROGRAMMER_CLI}" -c port=swd

echo "Erasing flash..."
"${PROGRAMMER_CLI}" -c port=swd -e all

echo "Programming secure supervisor (${SUPERVISOR_IMAGE}) @ ${SUPERVISOR_ADDR}"
"${PROGRAMMER_CLI}" -c port=swd -d "${SUPERVISOR_IMAGE}" "${SUPERVISOR_ADDR}"

echo "Programming frosted kernel (${KERNEL_IMAGE}) @ ${KERNEL_ADDR}"
"${PROGRAMMER_CLI}" -c port=swd -d "${KERNEL_IMAGE}" "${KERNEL_ADDR}"

if (( FLASH_USERLAND )); then
    echo "Programming userland (${USERLAND_IMAGE}) @ ${USERLAND_ADDR}"
    "${PROGRAMMER_CLI}" -c port=swd -d "${USERLAND_IMAGE}" "${USERLAND_ADDR}"
else
    echo "Skipping userland programming."
fi

echo "Resetting target..."
"${PROGRAMMER_CLI}" -c port=swd -rst

echo "Verifying programmed images..."
"${PROGRAMMER_CLI}" -c port=swd -d "${SUPERVISOR_IMAGE}" "${SUPERVISOR_ADDR}" -v
"${PROGRAMMER_CLI}" -c port=swd -d "${KERNEL_IMAGE}" "${KERNEL_ADDR}" -v
if (( FLASH_USERLAND )); then
    "${PROGRAMMER_CLI}" -c port=swd -d "${USERLAND_IMAGE}" "${USERLAND_ADDR}" -v
fi

echo "Flashing complete."
