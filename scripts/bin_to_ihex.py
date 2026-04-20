#!/usr/bin/env python3
import sys


def checksum(record_bytes):
    total = sum(record_bytes) & 0xFF
    return ((~total + 1) & 0xFF)


def emit_record(out, rectype, address, payload):
    record = bytearray()
    record.append(len(payload))
    record.append((address >> 8) & 0xFF)
    record.append(address & 0xFF)
    record.append(rectype)
    record.extend(payload)
    out.write(":" + record.hex().upper() + f"{checksum(record):02X}\n")


def main():
    if len(sys.argv) != 4:
        print("usage: bin_to_ihex.py <input.bin> <output.hex> <base-addr>", file=sys.stderr)
        return 2

    in_path, out_path, base_arg = sys.argv[1:]
    base_addr = int(base_arg, 0)

    with open(in_path, "rb") as f:
        data = f.read()

    with open(out_path, "w", encoding="ascii") as out:
        current_upper = None
        for offset in range(0, len(data), 16):
            chunk = data[offset:offset + 16]
            absolute = base_addr + offset
            upper = (absolute >> 16) & 0xFFFF
            lower = absolute & 0xFFFF

            if upper != current_upper:
                emit_record(out, 0x04, 0x0000, bytes([(upper >> 8) & 0xFF, upper & 0xFF]))
                current_upper = upper

            emit_record(out, 0x00, lower, chunk)

        emit_record(out, 0x01, 0x0000, b"")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
