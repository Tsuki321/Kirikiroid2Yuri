import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'script'))
from hxv4 import (Filter, FilterGenerator, TableReader, archive_metadata, authenticate_table,
                  chunk, companion, entry_chunks, fake_name, inflate, lookup_hash, siphash24)
from create_xp3_fixtures import container, file_index, hxv4_fixtures
from prepare_hxv4 import prepare, script_strings, verify_entry
from Crypto.Cipher import ChaCha20_Poly1305


PARAMS = bytes([2, 0, 6, 5, 1, 4, 3, 7, 1, 5, 3, 2, 0, 4, 1, 0, 2, 1, 0xe2, 2, 0x83, 2])
CONTROL = struct.pack('<1024I', *((((x << 11) | (x >> 21)) & 0xffffffff) ^ 0xa5a55a5a for x in range(1024)))
KEYS = {'key': bytes(range(32)), 'nonce0': bytes(range(24)), 'nonce1': bytes(range(1, 25)),
        'params': PARAMS, 'control': CONTROL}


def obj(value):
    if isinstance(value, list): return b'\x81' + struct.pack('>i', len(value)) + b''.join(obj(x) for x in value)
    if isinstance(value, bytes): return b'\x03' + struct.pack('>i', len(value)) + value
    if isinstance(value, int): return b'\x04' + (value & ((1 << 64) - 1)).to_bytes(8, 'big')
    raise ValueError('unsupported synthetic object')


def encrypted_table(table, flag=0):
    data = obj(table)
    plaintext = struct.pack('<I', len(data)) + zlib.compress(data)
    cipher = ChaCha20_Poly1305.new(key=KEYS['key'], nonce=KEYS['nonce1' if flag else 'nonce0'])
    ciphertext, tag = cipher.encrypt_and_digest(plaintext)
    return tag + ciphertext


def synthetic_archive(flag=0):
    name = 'startup.tjs'
    lookup = lookup_hash(name)
    key = 0x1122334455667788
    table = [lookup[:8], [lookup[8:], [(flag << 32) | 7, key]]]
    payload = encrypted_table(table, flag)
    plain = b'global.originalSyntheticFixture = true;'
    filter = FilterGenerator(PARAMS, CONTROL, KEYS['nonce0'][:8]).filter(key, flag)
    data = filter.apply(plain)
    descriptor = struct.pack('<QIH', 19 + len(data), len(payload), flag)
    index = chunk(b'Hxv4', descriptor) + file_index(plain, name=fake_name(7))
    return container(index, data + payload), plain, table


class Hxv4Tests(unittest.TestCase):
    def test_filter_generator_matches_independent_garbro_vectors(self):
        generator = FilterGenerator(PARAMS, CONTROL, bytes(8))
        expected = {0: 0x2d47d8849ed0f6eb, 1: 0xbdd60675bdd60676, 127: 0x003a6259003a6259,
                    128: 0x78b8cd3753600238, 0x12345678: 0xede2c8f8268ad5ae,
                    0x11223344: 0x0014bb390014b2a8, 0xffffffff: 0x003a6259003a6259}
        for key, answer in expected.items():
            self.assertEqual(answer, generator.drip(key))
        for lane in range(128): self.assertIsNotNone(generator.program(lane))
        self.assertNotEqual(generator.filter(100, 1), FilterGenerator(PARAMS, CONTROL, bytes(range(8))).filter(100, 0))
        with self.assertRaises(ValueError): FilterGenerator(bytes(22), CONTROL, bytes(8))

    def test_siphash_reference_vectors(self):
        key0, key1 = 0x0706050403020100, 0x0f0e0d0c0b0a0908
        for size, expected in [(0, 0x726fdb47dd0e0e31), (1, 0x74f839c593dc67fd),
                               (8, 0x93f5f5799a932462), (15, 0xa129ca6149be45e5)]:
            self.assertEqual(expected, siphash24(bytes(range(size)), key0, key1))
        self.assertNotEqual(lookup_hash('nested/file.txt'), lookup_hash('file.txt'))
        self.assertEqual(lookup_hash('NESTED\\FILE.TXT'), lookup_hash('nested/file.txt'))

    def test_filter_partial_reads_match_whole_file(self):
        # Equal correction positions, zero bulk fallback and header/split overlap.
        filter = Filter(True, 9, (3 << 48) | (3 << 32) | 0x653200,
                        (12 << 48) | (18 << 32) | 0x9a1284, bytes(range(16)))
        data = bytes(range(100))
        expected = filter.apply(data)
        self.assertEqual(data, filter.apply(expected))
        for start in range(101):
            for count in (0, 1, 3, 16, 100):
                end = min(len(data), start + count)
                self.assertEqual(expected[start:end], filter.apply(data[start:end], start))

    def test_authenticated_tables_reject_bad_tag_wrong_keys_and_bad_objects(self):
        for flag in (0, 1):
            _, _, table = synthetic_archive(flag)
            payload = encrypted_table(table, flag)
            self.assertEqual(7, authenticate_table(payload, flag, KEYS)[0]['id'])
            corrupt = bytes([payload[0] ^ 1]) + payload[1:]
            with self.assertRaises(ValueError): authenticate_table(corrupt, flag, KEYS)
            with self.assertRaises(ValueError): authenticate_table(payload, flag ^ 1, KEYS)
            with self.assertRaises(ValueError): authenticate_table(payload, flag, dict(KEYS, key=bytes(32)))
        for value in [[], [bytes(8)], [bytes(7), []], [bytes(8), [bytes(31), [1, 2]]],
                      [bytes(8), [bytes(32), [1 << 48, 2]]],
                      [bytes(8), [bytes(32), [1, 2], bytes(32), [2, 3]]]]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                authenticate_table(encrypted_table(value), 0, KEYS)

    def test_bounded_table_and_decompression(self):
        for data in [b'', b'\x81\x7f\xff\xff\xff', b'\x03\xff\xff\xff\xff', b'\x04\0', b'\x7f']:
            with self.assertRaises(ValueError): TableReader(data).value()
        nested = b'\x81\0\0\0\x01' * 34 + b'\0'
        with self.assertRaises(ValueError): TableReader(nested).value()
        packed = zlib.compress(b'a' * 1000)
        for data, size in [(packed, 999), (packed, 1001), (packed[:-1], 1000), (packed + b'extra', 1000)]:
            with self.assertRaises(ValueError): inflate(data, size)

    def test_chunked_zlib_is_bounded_and_checks_declared_size(self):
        data = b'a' * (3 * 1024 * 1024 + 31)
        packed = zlib.compress(data)
        entry = {'segments': [(1, 0, len(data), len(packed))]}
        parts = list(entry_chunks(io.BytesIO(packed), entry))
        self.assertEqual(data, b''.join(parts))
        self.assertLessEqual(max(map(len, parts)), 1024 * 1024)
        for size in (len(data) - 1, len(data) + 1):
            with self.assertRaises(ValueError):
                list(entry_chunks(io.BytesIO(packed), {'segments': [(1, 0, size, len(packed))]}))

    def test_prepare_authenticates_verifies_and_preserves_original_archive(self):
        for flag in (0, 1):
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                archive, plain, _ = synthetic_archive(flag)
                (root / 'data.xp3').write_bytes(archive)
                (root / 'keys.json').write_text(json.dumps({k: v.hex() for k, v in KEYS.items()}))
                with contextlib.redirect_stdout(io.StringIO()):
                    report = prepare(root, root / 'keys.json', root / 'output')
                self.assertEqual(archive, (root / 'data.xp3').read_bytes())
                self.assertTrue(report['archives'][0]['startup_resolved'])
                sidecar = (root / 'output/data.xp3.hxidx').read_bytes()
                self.assertEqual(b'K2HXIDX1', sidecar[:8])
                self.assertEqual(hashlib.blake2s(sidecar[96:]).digest(), sidecar[48:80])
                self.assertNotIn(plain, sidecar)
                self.assertNotIn(KEYS['key'], sidecar)
                metadata = archive_metadata(root / 'data.xp3')
                self.assertEqual(metadata['binding'], sidecar[16:48])
                # A changed content byte still has valid table authentication,
                # but the content checksum prevents publication of companions.
                corrupted = bytearray(archive)
                corrupted[19] ^= 1
                (root / 'data.xp3').write_bytes(corrupted)
                with self.assertRaisesRegex(ValueError, 'checksum'):
                    prepare(root, root / 'keys.json', root / 'rejected')
                self.assertFalse((root / 'rejected').exists())

    def test_reader_accepts_empty_continuation_but_rejects_cycles(self):
        files = hxv4_fixtures()
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'data.xp3'
            path.write_bytes(files['hxv4-chained.xp3'])
            self.assertEqual(2, len(archive_metadata(path)['entries']))
            bad = bytearray(files['hxv4-chained.xp3'])
            struct.pack_into('<Q', bad, 28, 19)
            path.write_bytes(bad)
            with self.assertRaises(ValueError): archive_metadata(path)

    def test_scripts_are_only_read_as_data(self):
        source = 'Plugins.link("example.dll"); Scripts.execStorage("system/initialize.tjs");'
        self.assertIn(('system/initialize.tjs', ''), script_strings(source.encode()))
        units = [ord(c) for c in source]
        encrypted = b'\xfe\xfe\x01\xff\xfe' + b''.join(struct.pack('<H', ((c & 0xaaaa) >> 1) | ((c & 0x5555) << 1)) for c in units)
        self.assertEqual(script_strings(source.encode()), script_strings(encrypted))
        with self.assertRaises(ValueError): script_strings(b'TJS2100\0' + bytes(4))


if __name__ == '__main__':
    unittest.main()
