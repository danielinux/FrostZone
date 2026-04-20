#!/usr/bin/env bash
#
# build_jedec_pylibs.sh — Collect MicroPython stdlib + unix-ffi modules
# and generate an 8 MB JEDEC SPI flash image for Frosted.
#
# Usage:
#   ./scripts/build_jedec_pylibs.sh [-o output.bin] [-s 8M]
#
# The resulting image can be used with m33mu:
#   JEDEC_SPI_FLASH=jedec_pylibs.bin ./run-m33mu-connected.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MPLIB="$REPO_ROOT/userland/micropython/lib/micropython-lib"
MKFLASHFS="$SCRIPT_DIR/mkflashfs.py"

OUTPUT="${1:-$REPO_ROOT/jedec_pylibs.bin}"
IMAGE_SIZE="${2:-8M}"

if [ ! -d "$MPLIB" ]; then
    echo "ERROR: micropython-lib not found at $MPLIB" >&2
    echo "       Run:  git submodule update --init --recursive" >&2
    exit 1
fi

STAGING=$(mktemp -d)
trap 'rm -rf "$STAGING"' EXIT

echo "==> Collecting python-stdlib modules..."
STDLIB_COUNT=0
for pkg_dir in "$MPLIB"/python-stdlib/*/; do
    pkg_name=$(basename "$pkg_dir")
    # Skip packages that only have manifest.py / setup.py / test*
    find "$pkg_dir" -maxdepth 3 -name "*.py" \
        -not -name "manifest.py" -not -name "setup.py" \
        -not -path "*/test*" -not -path "*/example*" |
    while IFS= read -r pyfile; do
        # Compute the flash path relative to the package directory
        rel="${pyfile#"$pkg_dir"}"
        # Create parent directories in staging
        mkdir -p "$STAGING/$(dirname "$rel")"
        cp "$pyfile" "$STAGING/$rel"
    done
    STDLIB_COUNT=$((STDLIB_COUNT + 1))
done

echo "==> Collecting unix-ffi modules..."
FFI_COUNT=0
for pkg_dir in "$MPLIB"/unix-ffi/*/; do
    pkg_name=$(basename "$pkg_dir")
    [ "$pkg_name" = "README.md" ] && continue
    find "$pkg_dir" -maxdepth 3 -name "*.py" \
        -not -name "manifest.py" -not -name "setup.py" \
        -not -path "*/test*" -not -path "*/example*" |
    while IFS= read -r pyfile; do
        rel="${pyfile#"$pkg_dir"}"
        mkdir -p "$STAGING/$(dirname "$rel")"
        cp "$pyfile" "$STAGING/$rel"
    done
    FFI_COUNT=$((FFI_COUNT + 1))
done

echo "==> Writing custom _libc.py (dlopen(None) for Frosted)..."
cat > "$STAGING/_libc.py" << 'PYEOF'
"""Frosted _libc — opens the process itself via dlopen(None)."""
import ffi
import sys

_h = None

def get():
    global _h
    if _h:
        return _h
    _h = ffi.open(None)
    return _h

bitness = 1
v = sys.maxsize
while v:
    bitness += 1
    v >>= 1
PYEOF

echo "==> Writing custom ffilib.py..."
cat > "$STAGING/ffilib.py" << 'PYEOF'
"""Frosted ffilib — library loader via dlopen(None)."""
import ffi

def open(name=None, maxver=10, flags=0):
    if name is None or name in ("libc", "libc.so", "libc.so.6"):
        return ffi.open(None)
    return ffi.open(name)
PYEOF

echo "==> Building file list..."
FILE_LIST=()
while IFS= read -r -d '' pyfile; do
    rel="${pyfile#"$STAGING/"}"
    FILE_LIST+=("${rel}=${pyfile}")
done < <(find "$STAGING" -name "*.py" -print0 | sort -z)

echo "    ${#FILE_LIST[@]} files total"

echo "==> Running mkflashfs.py..."
python3 "$MKFLASHFS" -o "$OUTPUT" -s "$IMAGE_SIZE" "${FILE_LIST[@]}"

echo ""
echo "Done! JEDEC image: $OUTPUT"
echo "Usage: JEDEC_SPI_FLASH=$OUTPUT ./run-m33mu-connected.sh"
