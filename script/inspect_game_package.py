#!/usr/bin/env python3
"""Inspect local XP3 indices and PE headers without extracting or running files.

Only container metadata is read. Protected payloads, scripts, pictures, account
settings and saves are not decoded. This tool has no network or process-launch
functionality. Its JSON output describes evidence, not gameplay compatibility.
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import zlib

MAGIC = b"XP3\r\n \n\x1a\x8bg\x01"
MAX_INDEX_BYTES = 64 * 1024 * 1024
MAX_INDICES = 1024


def read_exact(stream, count):
    data = stream.read(count)
    if len(data) != count:
        raise ValueError("truncated container metadata")
    return data


def chunks(data):
    position = 0
    while position < len(data):
        if len(data) - position < 12:
            raise ValueError("truncated chunk header")
        tag, size = struct.unpack_from("<4sQ", data, position)
        position += 12
        if size > len(data) - position:
            raise ValueError("chunk extends beyond its parent")
        yield tag, memoryview(data)[position:position + size]
        position += size


def inspect_xp3_stream(stream):
    """Read only the signature, index chain and index contents of a seekable file."""
    stream.seek(0, 2)
    archive_size = stream.tell()
    stream.seek(0)
    if read_exact(stream, 11) != MAGIC:
        raise ValueError("not a standalone XP3 archive")
    index_offset, = struct.unpack("<Q", read_exact(stream, 8))
    seen = set()
    total_index_bytes = entries = protected = extensionless = 0
    top_chunks, extensions, methods = Counter(), Counter(), Counter()
    startup = []
    nonstandard_file_metadata = 0
    while True:
        if index_offset in seen or len(seen) >= MAX_INDICES:
            raise ValueError("cyclic or excessive index chain")
        if index_offset > archive_size or archive_size - index_offset < 9:
            raise ValueError("index offset is outside the archive")
        seen.add(index_offset)
        stream.seek(index_offset)
        flag = read_exact(stream, 1)[0]
        if flag & ~0x81:
            raise ValueError("unsupported index encoding flags")
        packed_size, = struct.unpack("<Q", read_exact(stream, 8))
        original_size = packed_size
        if flag & 1:
            original_size, = struct.unpack("<Q", read_exact(stream, 8))
        if packed_size > MAX_INDEX_BYTES or original_size > MAX_INDEX_BYTES - total_index_bytes:
            raise ValueError("index exceeds inspection memory limit")
        if packed_size > archive_size - stream.tell():
            raise ValueError("index extends beyond the archive")
        total_index_bytes += original_size
        index = read_exact(stream, packed_size)
        if flag & 1:
            decoder = zlib.decompressobj()
            try:
                index = decoder.decompress(index, original_size + 1)
            except zlib.error as error:
                raise ValueError("invalid compressed index") from error
            if len(index) != original_size or not decoder.eof or decoder.unused_data:
                raise ValueError("compressed index size or stream mismatch")
        index_chunks = list(chunks(index))
        has_unsupported_names = "Hxv4" in top_chunks or any(tag == b"Hxv4" for tag, _ in index_chunks)
        for tag, data in index_chunks:
            top_chunks[tag.decode("ascii", errors="backslashreplace")] += 1
            if tag != b"File":
                continue
            parts = {}
            for name, content in chunks(data):
                if name in parts and name in (b"info", b"segm", b"adlr"):
                    raise ValueError("duplicate file metadata")
                parts[name] = content
            info, segments, checksum = (parts.get(k, b"") for k in (b"info", b"segm", b"adlr"))
            if len(info) < 22 or not segments or len(segments) % 28 or len(checksum) < 4:
                raise ValueError("incomplete file metadata")
            flags, original, packed, name_length = struct.unpack_from("<IQQH", info)
            if not name_length or name_length * 2 > len(info) - 22:
                raise ValueError("filename extends beyond its metadata")
            name = bytes(info[22:22 + name_length * 2]).decode("utf-16le")
            if "\0" in name:
                raise ValueError("embedded NUL in filename")
            original_total = packed_total = 0
            standard_metadata = True
            for position in range(0, len(segments), 28):
                method, start, size, stored = struct.unpack_from("<IQQQ", segments, position)
                if method & 7 > 1 or start > archive_size or stored > archive_size - start:
                    standard_metadata = False
                if method & 7 == 0 and size != stored:
                    standard_metadata = False
                original_total += size
                packed_total += stored
                methods["zlib" if method & 1 else "raw"] += 1
            if (original_total, packed_total) != (original, packed):
                standard_metadata = False
            if not standard_metadata:
                nonstandard_file_metadata += 1
                if not has_unsupported_names:
                    raise ValueError("invalid standard file/segment metadata")
            entries += 1
            protected += bool(flags & 0x80000000)
            suffix = Path(name).suffix.lower()
            extensionless += not suffix
            extensions[suffix or "(none)"] += 1
            if name.lower() == "startup.tjs":
                startup.append({"protected": bool(flags & 0x80000000), "bytes": original})
        if not flag & 0x80:
            break
        index_offset, = struct.unpack("<Q", read_exact(stream, 8))
    unsupported = [name for name in top_chunks if name == "Hxv4"]
    return {
        "bytes": archive_size, "index_blocks": len(seen), "index_bytes": total_index_bytes,
        "entries": entries, "protected_entries": protected,
        "extensionless_entries": extensionless, "extensions": dict(extensions),
        "index_chunks": dict(top_chunks), "segment_methods": dict(methods),
        "nonstandard_file_metadata": nonstandard_file_metadata,
        "startup_entries": startup, "unsupported_name_tables": unsupported,
        "status": "unsupported_name_table" if unsupported else "metadata_readable",
    }


def inspect_pe(path):
    with path.open("rb") as stream:
        header = read_exact(stream, 64)
        if header[:2] != b"MZ":
            raise ValueError("missing MZ signature")
        offset, = struct.unpack_from("<I", header, 60)
        if offset > path.stat().st_size - 24:
            raise ValueError("PE header outside file")
        stream.seek(offset)
        header = read_exact(stream, 24)
        if header[:4] != b"PE\0\0":
            raise ValueError("missing PE signature")
        machine, = struct.unpack_from("<H", header, 4)
    return {"machine": hex(machine), "architecture": {0x14c: "Windows x86", 0x8664: "Windows x64",
            0xaa64: "Windows ARM64"}.get(machine, "unknown PE architecture"), "bytes": path.stat().st_size}


def inspect_package(root):
    archives, modules = [], []
    for path in sorted(root.iterdir()):
        if path.is_file() and path.suffix.lower() == ".xp3":
            result = {"file": path.name}
            try:
                with path.open("rb") as stream:
                    result.update(inspect_xp3_stream(stream))
            except (OSError, ValueError, zlib.error) as error:
                result.update(status="inspection_error", error=str(error))
            archives.append(result)
    candidates = list(root.iterdir())
    if (root / "plugin").is_dir():
        candidates.extend((root / "plugin").iterdir())
    for path in sorted(candidates):
        if path.is_file() and path.suffix.lower() in (".exe", ".dll"):
            result = {"file": str(path.relative_to(root))}
            try:
                result.update(inspect_pe(path))
            except (OSError, ValueError) as error:
                result["error"] = str(error)
            modules.append(result)
    return {"inspection": "local metadata only; no extraction, execution or network access",
            "archives": archives, "windows_modules": modules}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    if not args.directory.is_dir():
        parser.error("directory must be an existing local folder")
    print(json.dumps(inspect_package(args.directory), indent=2, ensure_ascii=True))


if __name__ == "__main__":
    main()
