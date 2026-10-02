#!/usr/bin/env python3
"""Generate small, original XP3 regression inputs; no game files are used."""

import argparse
from pathlib import Path
import struct
import zlib

MAGIC = b"XP3\r\n \n\x1a\x8bg\x01"
TEXT = b"\xff\xfe" + "archive fixture OK 日本語".encode("utf-16le")


def chunk(tag, data):
    return tag + struct.pack("<Q", len(data)) + data


def file_index(data=TEXT, segments=None, name="hello.txt"):
    if segments is None:
        segments = [(0, 19, len(data), len(data))]
    encoded_name = name.encode("utf-16le")
    info = struct.pack("<IQQH", 0, len(data), sum(s[3] for s in segments), len(encoded_name) // 2)
    parts = chunk(b"info", info + encoded_name)
    parts += chunk(b"segm", b"".join(struct.pack("<IQQQ", *s) for s in segments))
    parts += chunk(b"adlr", struct.pack("<I", zlib.adler32(data)))
    return chunk(b"File", parts)


def container(index, payload=TEXT, compressed=False, chained=False):
    first = 36 if chained else 19
    index_offset = first + len(payload)
    encoded = zlib.compress(index) if compressed else index
    index_header = bytes([int(compressed)]) + struct.pack("<Q", len(encoded))
    if compressed:
        index_header += struct.pack("<Q", len(index))
    header = MAGIC + struct.pack("<Q", 19 if chained else index_offset)
    if chained:
        header += b"\x80" + struct.pack("<QQ", 0, index_offset)
    return header + payload + index_header + encoded


def fixtures():
    result = {"valid-raw.xp3": container(file_index())}
    packed = zlib.compress(TEXT)
    result["valid-compressed.xp3"] = container(
        file_index(segments=[(1, 19, len(TEXT), len(packed))]), packed, compressed=True)
    result["valid-chained.xp3"] = container(
        file_index(segments=[(0, 36, len(TEXT), len(TEXT))]), chained=True, compressed=True)
    result["valid-unknown.xp3"] = container(chunk(b"note", b"future metadata") + file_index())
    result["valid-empty-file.xp3"] = container(file_index(b""), b"")
    first, second = TEXT[:10], TEXT[10:]
    encoded = zlib.compress(second)
    result["valid-multisegment.xp3"] = container(file_index(segments=[
        (0, 19, len(first), len(first)), (1, 19 + len(first), len(second), len(encoded))
    ]), first + encoded)
    result["bad-chunk-header.xp3"] = container(b"File\0")
    result["bad-chunk-length.xp3"] = container(b"File" + struct.pack("<Q", 2**64 - 1))
    result["bad-info.xp3"] = container(chunk(b"File", chunk(b"info", b"\0")))
    bad_name = bytearray(file_index())
    struct.pack_into("<H", bad_name, 12 + 12 + 20, 65535)
    result["bad-name.xp3"] = container(bad_name)
    result["bad-segment-range.xp3"] = container(
        file_index(segments=[(0, 2**64 - 4, len(TEXT), len(TEXT))]))
    result["bad-segment-size.xp3"] = container(
        file_index(segments=[(0, 19, len(TEXT) + 1, len(TEXT))]))
    result["bad-segment-method.xp3"] = container(
        file_index(segments=[(7, 19, len(TEXT), len(TEXT))]))
    result["bad-index-pointer.xp3"] = MAGIC + struct.pack("<Q", 2**64 - 1)
    header = MAGIC + struct.pack("<Q", 19)
    result["bad-index-size.xp3"] = header + b"\0" + struct.pack("<Q", 2**64 - 1)
    result["bad-compressed-size.xp3"] = header + b"\1" + struct.pack("<QQ", 1, 2**64 - 1) + b"x"
    result["bad-compressed-index.xp3"] = header + b"\1" + struct.pack("<QQ", 4, 4) + b"oops"
    result["bad-index-cycle.xp3"] = header + b"\x80" + struct.pack("<QQ", 0, 19)
    result["bad-index-tail.xp3"] = header + b"\x80" + struct.pack("<Q", 0)
    chain = b"".join(b"\x80" + struct.pack("<QQ", 0, 19 + (i + 1) * 17) for i in range(1024))
    result["bad-index-count.xp3"] = header + chain + b"\0" + struct.pack("<Q", 0)
    result["unsupported-names.xp3"] = container(chunk(b"Hxv4", b"synthetic") + file_index())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    archives = fixtures()
    for name, data in archives.items():
        (args.output / name).write_bytes(data)
    print(f"Generated {len(archives)} synthetic XP3 archives in {args.output}")


if __name__ == "__main__":
    main()
