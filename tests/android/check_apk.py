"""Check actual native load segments and APK packing, without a compiler."""
import pathlib
import struct
import sys
import zipfile


def check(path):
    libraries = 0
    with zipfile.ZipFile(path) as apk:
        for entry in apk.infolist():
            if not entry.filename.startswith("lib/") or not entry.filename.endswith(".so"):
                continue
            libraries += 1
            data = apk.read(entry)
            assert data[:6] == b"\x7fELF\x02\x01", f"Unexpected ELF format: {entry.filename}"
            offset = struct.unpack_from("<Q", data, 32)[0]
            stride, count = struct.unpack_from("<HH", data, 54)
            loads = 0
            for i in range(count):
                kind, flags, file_offset, virtual, physical, filesz, memsz, align = struct.unpack_from(
                    "<IIQQQQQQ", data, offset + i * stride
                )
                if kind == 1:
                    loads += 1
                    assert align >= 16384, f"4 KB load alignment: {entry.filename}"
                    assert (virtual - file_offset) % 16384 == 0, f"Unaligned load: {entry.filename}"
                if kind == 0x6474E552:
                    assert (virtual + memsz) % 16384 == 0, f"Unaligned RELRO end: {entry.filename}"
            assert loads, f"No load segments: {entry.filename}"
            if entry.compress_type == zipfile.ZIP_STORED:
                with open(path, "rb") as raw:
                    raw.seek(entry.header_offset + 26)
                    name_length, extra_length = struct.unpack("<HH", raw.read(4))
                data_offset = entry.header_offset + 30 + name_length + extra_length
                assert data_offset % 16384 == 0, f"Unaligned uncompressed library: {entry.filename}"
            print(f"PASS 16 KB ELF/APK alignment: {entry.filename}")
    assert libraries, "APK contains no native libraries"


if __name__ == "__main__":
    for apk_path in sys.argv[1:]:
        check(pathlib.Path(apk_path))
