#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FLASH_SCRIPT="${ROOT}/frosted/flash_kernel.jlink"

"${ROOT}/scripts/build_frosted_rp2350.sh"

if ! command -v JLinkExe >/dev/null 2>&1; then
    echo "Error: JLinkExe not found in PATH." >&2
    exit 1
fi

JLinkExe -Device RP2350_M33_0 -If swd -Speed 4000 -CommanderScript "${FLASH_SCRIPT}"
