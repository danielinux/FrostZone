#!/usr/bin/env bash

set -euo pipefail

OUTPUT="${1:-jedec_flash.bin}"
SIZE_BYTES="${2:-8388608}"

case "$SIZE_BYTES" in
    ''|*[!0-9]*)
        echo "size must be a positive decimal byte count" >&2
        exit 2
        ;;
esac

if [[ "$SIZE_BYTES" -le 0 ]]; then
    echo "size must be greater than zero" >&2
    exit 2
fi

dd if=/dev/zero bs=1 count="$SIZE_BYTES" status=none | tr '\000' '\377' > "$OUTPUT"
echo "[format] wrote empty FlashFS image: $OUTPUT (${SIZE_BYTES} bytes of 0xFF)"
