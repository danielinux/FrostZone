#!/usr/bin/env bash
#
# Configure STM32H5 option bytes for the FrostZone platform.
# This script relies on STM32_Programmer_CLI being available in PATH.
#
#   - Enables TrustZone (TZEN) using the 0xB4 key sequence.
#   - Locks the secure supervisor window to the first 32 KiB (pages 0..3).
#   - Forces single-bank mode (DUALBANK = 0).
#   - Sets the non-secure boot address (NSBOOTADD) to the FrostZone kernel location.
#   - Leaves RDP/WDP untouched.
#
# Environment overrides:
#   TZEN_VALUE        (default: 0xB4)
#   SECWM1_START      (default: 0x00)
#   SECWM1_END        (default: 0x03)
#   DBANK_VALUE       (default: 0)
#   NSBOOTADD_VALUE   (default: 0x80100)
#   PROGRAMMER_CLI    (default: STM32_Programmer_CLI)
#
# Usage:
#   ./scripts/configure_stm32h5_option_bytes.sh
#   ./scripts/configure_stm32h5_option_bytes.sh --dry-run

set -euo pipefail

PROGRAMMER_CLI="${PROGRAMMER_CLI:-STM32_Programmer_CLI}"
TZEN_VALUE="${TZEN_VALUE:-0xB4}"
SECWM1_START="${SECWM1_START:-0x00}"
SECWM1_END="${SECWM1_END:-0x07}"
DBANK_VALUE="${DBANK_VALUE:-0}"
NSBOOTADD_VALUE="${NSBOOTADD_VALUE:-0x80100}"
DRY_RUN=0

usage() {
    cat <<EOF
Configure STM32H5 option bytes for FrostZone.

Options:
  --dry-run     Show the STM32_Programmer_CLI commands that would be executed.
  -h, --help    Display this help message.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run)
            DRY_RUN=1
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

echo "=== Current option bytes ==="
if (( DRY_RUN )); then
    echo "DRY-RUN: ${PROGRAMMER_CLI} -c port=swd -ob displ"
else
    "${PROGRAMMER_CLI}" -c port=swd -ob displ
fi

OB_COMMAND=(
    "${PROGRAMMER_CLI}"
    -c port=swd
    -ob
    "tzen=${TZEN_VALUE}"
    "secwm1_start=${SECWM1_START}"
    "secwm1_end=${SECWM1_END}"
    "dbank=${DBANK_VALUE}"
    "nsbootadd=${NSBOOTADD_VALUE}"
)

echo "=== Programming option bytes ==="
if (( DRY_RUN )); then
    printf 'DRY-RUN: %q ' "${OB_COMMAND[@]}"
    printf '\n'
else
    "${OB_COMMAND[@]}"
fi

echo "=== Reloading option bytes ==="
if (( DRY_RUN )); then
    echo "DRY-RUN: ${PROGRAMMER_CLI} -c port=swd -rst"
else
    "${PROGRAMMER_CLI}" -c port=swd -rst
fi

echo "=== Option bytes after programming ==="
if (( DRY_RUN )); then
    echo "DRY-RUN: ${PROGRAMMER_CLI} -c port=swd -ob displ"
else
    "${PROGRAMMER_CLI}" -c port=swd -ob displ
fi

echo "Done."
