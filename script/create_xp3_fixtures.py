#!/usr/bin/env python3
"""Generate small, original XP3 regression inputs; no game files are used."""

import argparse
from pathlib import Path
import struct
import zlib

from hxv4 import Filter, companion, companion_file, lookup_hash

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
    startup = (Path(__file__).resolve().parents[1] / "tests/fixtures/engine/archive-launch-startup.tjs").read_text(encoding="utf-8")
    startup = b"\xff\xfe" + startup.encode("utf-16le")
    result["launch-path.xp3"] = container(file_index(startup, name="startup.tjs"), startup)
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
    result.update(hxv4_fixtures())
    return result


def hxv4_fixtures():
    """The encrypted table is synthetic opaque bytes; the reader needs only
    its fingerprint. Actual authenticated table decoding is tested separately.
    Each filter uses deliberate header, split and correction-byte overlaps.
    """
    import hashlib
    result = {}
    filters = [Filter(True, 23, (4 << 48) | (12 << 32) | 0x341256,
                      (27 << 48) | (41 << 32) | 0x789abc, bytes(range(16))),
               Filter(True, 1, 0, (16 << 48) | (16 << 32) | 0x432100, bytes(reversed(range(16))))]
    for kind in ('raw', 'compressed', 'multisegment', 'chained', 'startup', 'patch'):
        payload = bytearray()
        index_files, companion_files = [], []
        first = 36 if kind == 'chained' else 19
        specs = [('hello.txt', TEXT), ('nested/dynamic.txt', b'\xff\xfe' + 'hashed lookup OK'.encode('utf-16le'))]
        if kind == 'patch':
            specs[1] = ('nested/dynamic.txt', b'\xff\xfe' + 'hashed patch OK'.encode('utf-16le'))
        if kind == 'startup':
            specs = [('startup.tjs', b'global.hxFixtureExecuted = "hx startup OK";')]
        for i, (name, plain) in enumerate(specs):
            filter = filters[i]
            encrypted = filter.apply(plain)
            pieces = [encrypted[:9], encrypted[9:29], encrypted[29:]] if kind == 'multisegment' else [encrypted]
            segments = []
            for j, piece in enumerate(pieces):
                method = int(kind == 'compressed' or (kind == 'multisegment' and j == 1))
                stored = zlib.compress(piece) if method else piece
                segments.append((method, first + len(payload), len(piece), len(stored)))
                payload.extend(stored)
            index_files.append(file_index(plain, segments, chr(0x5000 + i + 1)))
            entry = {'flags': 0x80000000, 'size': len(plain), 'packed': sum(s[3] for s in segments),
                     'segments': segments, 'adler': zlib.adler32(plain)}
            # The second file deliberately lacks its real name in enumeration.
            exported = name if i == 0 else '__hxv4_2'
            companion_files.append(companion_file(entry, exported, lookup_hash(name), filter))
        table = b'synthetic Hxv4 table and tag; no game data'
        descriptor = struct.pack('<QIH', first + len(payload), len(table), 0)
        original_index = chunk(b'Hxv4', descriptor) + b''.join(index_files)
        archive = container(original_index, bytes(payload) + table, compressed=True, chained=kind == 'chained')
        metadata = {'size': len(archive), 'binding': hashlib.blake2s(original_index + table).digest()}
        sidecar = companion(metadata, companion_files)
        prefix = 'hxv4-' + kind + '.xp3'
        result[prefix], result[prefix + '.hxidx'] = archive, sidecar
    priority = b'\xff\xfe' + 'ordinary priority OK'.encode('utf-16le')
    result['priority.xp3'] = container(file_index(priority, name='dynamic.txt'), priority)
    base, good = result['hxv4-raw.xp3'], result['hxv4-raw.xp3.hxidx']
    result['hxv4-missing.xp3'] = base
    for kind, value in [('truncated', good[:60]), ('mismatched', good[:16] + bytes(32) + good[48:]),
                        ('corrupt', good[:-1] + bytes([good[-1] ^ 1]))]:
        result['hxv4-' + kind + '.xp3'] = base
        result['hxv4-' + kind + '.xp3.hxidx'] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    archives = fixtures()
    for name, data in archives.items():
        (args.output / name).write_bytes(data)
    print(f"Generated {len(archives)} synthetic archive and companion files in {args.output}")


if __name__ == "__main__":
    main()
