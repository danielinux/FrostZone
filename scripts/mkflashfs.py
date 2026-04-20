#!/usr/bin/env python3
"""
mkflashfs.py — Create a flashfs image for Frosted OS.

Builds a binary image that can be loaded as a JEDEC SPI flash
(or internal flash partition).  The format matches the kernel's
flashfs implementation in frosted/flashfs.c.

Page layout
-----------
  Page 0..N-1 : data pages (files + directory markers)
  Page N..N+B-1 : bitmap pages (inverted: bit=1 → free, bit=0 → used)

Each file occupies one or more consecutive 256-byte pages:

  File first page:   [fname_len:u16le][fsize:u16le][filename][NUL][data...]
  Continuation page: [0xFFFE:u16le][0x0000:u16le][data...]

Directories are stored as single-page entries with the directory flag set
in the fname_len field (top bit, `F_DIR_FLAG = 0x8000`). Real directory
name lengths are stored in the low 15 bits, so a max of 0x7FFF (far above
CONFIG_MAX_FNAME=128):

  Dir page:          [fname_len|0x8000:u16le][0:u16le][dirpath][NUL]

The dir entry carries no data; later pages are unrelated. Each directory
on the flash tree has exactly one dir entry. Subdirectories are named by
their full relative path ("a/b"), matching how file entries are named.

Bitmap
------
  - Stored in the last B pages of the partition where
    B = ceil(total_pages / 2048)
  - NOR-flash erased state is 0xFF → all bits set → all pages free.
  - Bit 0 in the bitmap means the corresponding page is allocated.

Layout invariant
----------------
Entries are written in bytewise-sorted order by path. The kernel iterator
relies on this: within the same parent directory, sibling entries appear
contiguously, and the lookup early-exits once past the target window.
"""

import argparse
import math
import os
import struct
import sys

PAGE_SIZE = 256
HDR_SIZE = 4          # sizeof(flashfs_file_hdr): fname_len(2) + fsize(2)
F_PREV_PAGE = 0xFFFE
F_DIR_FLAG = 0x8000   # top bit of fname_len marks a directory entry
BITS_PER_BMP_PAGE = PAGE_SIZE * 8  # 2048


def pages_for_file(relpath, data_len):
    """Return the number of flash pages needed for *relpath* with *data_len* bytes."""
    fname_len = len(relpath)
    first_cap = PAGE_SIZE - (HDR_SIZE + fname_len + 1)
    if data_len <= first_cap:
        return 1
    cont_cap = PAGE_SIZE - HDR_SIZE  # 252
    return 1 + math.ceil((data_len - first_cap) / cont_cap)


def build_file_pages(relpath, data):
    """Return a list of 256-byte page buffers for a regular file *relpath*."""
    fname_bytes = relpath.encode("utf-8")
    fname_len = len(fname_bytes)
    fsize = len(data)

    pages = []

    # --- first page ---
    first_cap = PAGE_SIZE - (HDR_SIZE + fname_len + 1)
    first_data = data[:first_cap]
    page = bytearray(b"\xff" * PAGE_SIZE)
    struct.pack_into("<HH", page, 0, fname_len, fsize)
    page[HDR_SIZE : HDR_SIZE + fname_len] = fname_bytes
    page[HDR_SIZE + fname_len] = 0  # NUL terminator
    off = HDR_SIZE + fname_len + 1
    page[off : off + len(first_data)] = first_data
    pages.append(bytes(page))

    # --- continuation pages ---
    cont_cap = PAGE_SIZE - HDR_SIZE  # 252
    pos = first_cap
    while pos < fsize:
        chunk = data[pos : pos + cont_cap]
        page = bytearray(b"\xff" * PAGE_SIZE)
        struct.pack_into("<HH", page, 0, F_PREV_PAGE, 0)
        page[HDR_SIZE : HDR_SIZE + len(chunk)] = chunk
        pages.append(bytes(page))
        pos += cont_cap

    return pages


def build_dir_page(relpath):
    """Return a single 256-byte page buffer for a directory entry *relpath*."""
    fname_bytes = relpath.encode("utf-8")
    fname_len = len(fname_bytes)
    page = bytearray(b"\xff" * PAGE_SIZE)
    struct.pack_into("<HH", page, 0, fname_len | F_DIR_FLAG, 0)
    page[HDR_SIZE : HDR_SIZE + fname_len] = fname_bytes
    page[HDR_SIZE + fname_len] = 0
    return bytes(page)


def main():
    parser = argparse.ArgumentParser(
        description="Build a flashfs image for Frosted OS"
    )
    parser.add_argument(
        "inputs",
        nargs="+",
        help="Files to include.  Use FLASH_PATH=HOST_PATH to set the "
        "on-flash path, or just HOST_PATH to derive it automatically.",
    )
    parser.add_argument(
        "-o", "--output",
        required=True,
        help="Output image file.",
    )
    parser.add_argument(
        "-s", "--size",
        required=True,
        help="Image size (e.g. 8M, 1048576, 64K).",
    )
    parser.add_argument(
        "-r", "--root",
        default=None,
        help="Strip this prefix from host paths when deriving flash paths.",
    )

    args = parser.parse_args()

    # Parse image size
    size_str = args.size.upper().strip()
    if size_str.endswith("M"):
        image_size = int(size_str[:-1]) * 1024 * 1024
    elif size_str.endswith("K"):
        image_size = int(size_str[:-1]) * 1024
    else:
        image_size = int(size_str)

    total_pages = image_size // PAGE_SIZE
    bmp_count = math.ceil(total_pages / BITS_PER_BMP_PAGE)
    usable_pages = total_pages - bmp_count

    # Collect files
    entries = []  # list of (flash_relpath, host_path)
    for spec in args.inputs:
        if "=" in spec:
            flash_path, host_path = spec.split("=", 1)
        else:
            host_path = spec
            if args.root:
                flash_path = os.path.relpath(host_path, args.root)
            else:
                flash_path = os.path.basename(host_path)
        # Normalize: strip leading /
        flash_path = flash_path.lstrip("/")
        entries.append((flash_path, host_path))

    # Derive the set of unique parent directory paths implied by the file
    # list, so each one gets its own explicit on-flash entry. The kernel
    # then doesn't need to synthesise directory fnodes via prefix matching
    # — it just looks them up like regular entries.
    dirs = set()
    for flash_path, _ in entries:
        parent = flash_path.rsplit("/", 1)[0] if "/" in flash_path else ""
        while parent:
            dirs.add(parent)
            parent = parent.rsplit("/", 1)[0] if "/" in parent else ""

    # Build a merged, sorted list of (path, is_dir, host_path_or_None) with
    # strict bytewise ordering. The kernel iterator relies on this: sibling
    # entries under the same parent are contiguous, and lookup short-circuits
    # once the scan passes the target window. Locale-aware sort (the shell's
    # default) would break this — e.g. it can place `hashlib/_sha.py` after
    # `hashlib/__init__.py` even though '_' (0x5F) < 'i' (0x69) bytewise.
    merged = [(p, False, h) for p, h in entries] + [(d, True, None) for d in dirs]
    merged.sort(key=lambda e: e[0].encode("utf-8"))

    # Build pages for each entry
    image = bytearray(b"\xff" * image_size)
    cur_page = 0
    file_count = 0
    dir_count = 0

    for flash_path, is_dir, host_path in merged:
        if is_dir:
            pages_to_write = [build_dir_page(flash_path)]
        else:
            with open(host_path, "rb") as f:
                data = f.read()
            if len(data) > 65535:
                print(
                    f"WARNING: Skipping '{flash_path}' ({len(data)} bytes > 64K limit)",
                    file=sys.stderr,
                )
                continue
            pages_to_write = build_file_pages(flash_path, data)

        needed = len(pages_to_write)
        if cur_page + needed > usable_pages:
            print(
                f"ERROR: Not enough space for '{flash_path}' "
                f"({needed} pages, {usable_pages - cur_page} remaining)",
                file=sys.stderr,
            )
            sys.exit(1)

        for i, pg in enumerate(pages_to_write):
            offset = (cur_page + i) * PAGE_SIZE
            image[offset : offset + PAGE_SIZE] = pg

        cur_page += needed
        if is_dir:
            dir_count += 1
        else:
            file_count += 1

    # Write bitmap — clear bits for used pages
    for page_idx in range(cur_page):
        bmp_page_num = total_pages - bmp_count + page_idx // BITS_PER_BMP_PAGE
        byte_off = (page_idx % BITS_PER_BMP_PAGE) // 8
        bit = page_idx & 7
        abs_byte = bmp_page_num * PAGE_SIZE + byte_off
        image[abs_byte] &= ~(1 << bit)

    with open(args.output, "wb") as f:
        f.write(image)

    used_kb = (cur_page * PAGE_SIZE) / 1024
    total_kb = (usable_pages * PAGE_SIZE) / 1024
    print(
        f"Created {args.output}: {file_count} files, {dir_count} dirs, "
        f"{used_kb:.1f}K / {total_kb:.1f}K used, "
        f"{bmp_count} bitmap page(s)"
    )


if __name__ == "__main__":
    main()
