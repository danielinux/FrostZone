#!/usr/bin/env bash
# Run the hardfault diagnostics GDB flow against the live target.
# Usage: scripts/hardfault_diag.sh

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

GDB_BIN="${GDB:-arm-none-eabi-gdb}"
SUPERVISOR_ELF="${REPO_ROOT}/secure-supervisor/secure.elf"
GDB_CMDS="${REPO_ROOT}/scripts/hardfault_diag.gdb"

exec "${GDB_BIN}" -q -n "${SUPERVISOR_ELF}" -x "${GDB_CMDS}"
