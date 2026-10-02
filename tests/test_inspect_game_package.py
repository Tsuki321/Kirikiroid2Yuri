import io
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "script"))
from create_xp3_fixtures import fixtures, MAGIC, TEXT, chunk, container, file_index
from inspect_game_package import inspect_xp3_stream


class MetadataOnlyReader(io.BytesIO):
    def read(self, size=-1):
        start = self.tell()
        end = len(self.getbuffer()) if size < 0 else start + size
        if start < 19 + len(TEXT) and end > 19:
            raise AssertionError("inspector attempted to read a member payload")
        return super().read(size)


class PackageInspectionTest(unittest.TestCase):
    def test_valid_synthetic_archives(self):
        for name, data in fixtures().items():
            if name.startswith("valid-"):
                with self.subTest(name=name):
                    result = inspect_xp3_stream(io.BytesIO(data))
                    self.assertEqual("metadata_readable", result["status"])
                    self.assertEqual(1, result["entries"])
                    self.assertEqual(0, result["protected_entries"])

    def test_member_payloads_are_not_read(self):
        result = inspect_xp3_stream(MetadataOnlyReader(fixtures()["valid-raw.xp3"]))
        self.assertEqual(1, result["entries"])

    def test_malformed_synthetic_archives(self):
        for name, data in fixtures().items():
            if name.startswith("bad-"):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    inspect_xp3_stream(io.BytesIO(data))

    def test_protected_name_table_is_reported_without_decoding(self):
        result = inspect_xp3_stream(io.BytesIO(fixtures()["unsupported-names.xp3"]))
        self.assertEqual(["Hxv4"], result["unsupported_name_tables"])
        self.assertEqual("unsupported_name_table", result["status"])
        index = bytearray(file_index())
        struct.pack_into("<Q", index, 12 + 12 + 4, 123)
        result = inspect_xp3_stream(io.BytesIO(container(chunk(b"Hxv4", b"synthetic") + index)))
        self.assertEqual("unsupported_name_table", result["status"])
        self.assertEqual(1, result["nonstandard_file_metadata"])

    def test_truncated_headers_and_unbounded_indices(self):
        data = fixtures()["valid-raw.xp3"]
        for length in range(19):
            with self.subTest(length=length), self.assertRaises(ValueError):
                inspect_xp3_stream(io.BytesIO(data[:length]))
        with self.assertRaises(ValueError):
            inspect_xp3_stream(io.BytesIO(MAGIC + struct.pack("<Q", 19) + b"\x02" + b"\0" * 8))


if __name__ == "__main__":
    unittest.main()
