#!/usr/bin/env python3
"""Create local .hxidx companions from XP3 files and statically recovered keys.

Reads archive content as data and verifies Adler-32 checksums. It never loads
game DLLs, launches executables, interprets game scripts, or contacts a server.
Only companion metadata is written; original archives are opened read-only.
Requires pycryptodome for authenticated XChaCha20-Poly1305 table decryption.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

from hxv4 import (Filter, FilterGenerator, MAX_INDEX_BYTES, archive_metadata, authenticate_table,
                  companion, companion_file, entry_chunks, fake_name, file_hash, inflate,
                  lookup_hash, normalize, path_hash, valid_name)

SCRIPT_LIMIT = 8 * 1024 * 1024
MAX_CANDIDATES = 500000
EXTENSIONS = ('.tjs', '.ks', '.ksd', '.txt', '.ini', '.csv', '.png', '.jpg', '.tlg',
              '.bmp', '.ogg', '.opus', '.wav', '.sli', '.ttf', '.otf', '.psb', '.pimg', '.json')
SEEDS = ('startup.tjs', 'system/initialize.tjs', 'system', 'engine', 'setup', 'scenario', 'image',
         'bgimage', 'fgimage', 'bgm', 'sound', 'voice', 'video', 'rule', 'others', 'font',
         'sysscn', 'scn', 'face', 'init', 'sysse', 'main', 'evimage', 'thum', 'uipsd',
         'motion', 'motiondx', 'emote', 'emotedx', 'bishamon', 'glsl', 'first.ks')


def read_keys(path):
    if path.stat().st_size > 20000: raise ValueError('key file exceeds expected size')
    obj = json.loads(path.read_text(encoding='utf-8'))
    expected = {'key': 32, 'nonce0': 24, 'nonce1': 24, 'params': 22, 'control': 4096}
    keys = {k: bytes.fromhex(obj[k]) for k in expected}
    if any(len(keys[k]) != size for k, size in expected.items()): raise ValueError('invalid recovered key sizes')
    return keys


def bytecode_strings(data):
    """Read only TJS2 constant strings; do not decode or execute instructions."""
    if not data.startswith(b'TJS2100\0') or len(data) < 12:
        return []
    if struct.unpack_from('<I', data, 8)[0] != len(data): raise ValueError('invalid TJS2 bytecode size')
    at = 12
    while at < len(data):
        if len(data) - at < 8: raise ValueError('truncated TJS2 chunk')
        size = struct.unpack_from('<I', data, at + 4)[0]
        if size < 8 or size > len(data) - at: raise ValueError('invalid TJS2 chunk size')
        if data[at:at + 4] == b'DATA':
            part = memoryview(data)[at + 8:at + size]
            pos = 0

            def number():
                nonlocal pos
                if len(part) - pos < 4: raise ValueError('truncated TJS2 constant pool')
                value = struct.unpack_from('<I', part, pos)[0]
                pos += 4
                return value

            def take(size):
                nonlocal pos
                aligned = (size + 3) & ~3
                if aligned > len(part) - pos: raise ValueError('invalid TJS2 constant size')
                value = bytes(part[pos:pos + size])
                pos += aligned
                return value

            for unit in (1, 2, 4, 8, 8): take(number() * unit)
            count = number()
            if count > (len(part) - pos) // 4: raise ValueError('invalid TJS2 string count')
            return [take(number() * 2).decode('utf-16le') for _ in range(count)]
        at += size
    return []


def script_strings(data):
    if data.startswith(b'TJS2100\0'):
        return bytecode_strings(data)
    if data.startswith(b'\xfe\xfe') and len(data) >= 5:
        mode = data[2]
        if data[3:5] != b'\xff\xfe': return []
        if mode == 1:
            if (len(data) - 5) % 2: return []
            units = struct.unpack('<' + 'H' * ((len(data) - 5) // 2), data[5:])
            data = b'\xff\xfe' + b''.join(struct.pack('<H', ((v & 0xaaaa) >> 1) | ((v & 0x5555) << 1)) for v in units)
        elif mode == 2 and len(data) >= 21:
            stored, original = struct.unpack_from('<QQ', data, 5)
            if stored != len(data) - 21 or original > SCRIPT_LIMIT: return []
            data = b'\xff\xfe' + inflate(data[21:], original)
        else:
            return []
    if data.startswith(b'\xff\xfe'):
        text = data[2:].decode('utf-16le', errors='replace')
    elif data.startswith(b'\xfe\xff'):
        text = data[2:].decode('utf-16be', errors='replace')
    else:
        try: text = data.decode('utf-8-sig')
        except UnicodeError: return []
        if '\0' in text[:1000]: return []
    return re.findall(r'''["']([^"'\r\n]{1,256})["']|(?:\b(?:storage|graphic|font|face|file)\s*=\s*)([^\s\]"']{1,256})''', text)


class Names:
    def __init__(self, media):
        self.media = media
        self.paths, self.files = {}, {}
        self.seen = set()
        self.add('')
        for name in SEEDS: self.add(name)

    def add(self, value):
        if isinstance(value, tuple):
            for item in value:
                if item: self.add(item)
            return
        value = normalize(value.strip())
        if value in self.seen or len(self.seen) >= MAX_CANDIDATES or len(value) > 512:
            return
        if value and not valid_name(value): return
        self.seen.add(value)
        parts = value.split('/')
        for count in range(len(parts) + 1):
            path = '/'.join(parts[:count])
            self.paths.setdefault(path_hash(path, self.media), path)
        if value:
            filename = parts[-1]
            self.files.setdefault(file_hash(filename, self.media), filename)
            if '.' not in filename:
                for extension in EXTENSIONS:
                    self.files.setdefault(file_hash(filename + extension, self.media), filename + extension)

    def resolve(self, lookup):
        path, name = self.paths.get(lookup[:8]), self.files.get(lookup[8:])
        return (path + '/' if path else '') + name if path is not None and name is not None else None


def verify_entry(stream, entry, filter):
    raw_adler = decoded_adler = 1
    position = 0
    decoded = bytearray() if entry['size'] <= SCRIPT_LIMIT else None
    for data in entry_chunks(stream, entry):
        raw_adler = zlib.adler32(data, raw_adler)
        plain = filter.apply(data, position)
        decoded_adler = zlib.adler32(plain, decoded_adler)
        position += len(plain)
        if decoded is not None: decoded += plain
    if position != entry['size']: raise ValueError('XP3 file size mismatch')
    if decoded_adler == entry['adler']:
        return filter, bytes(decoded) if decoded is not None else None
    if raw_adler == entry['adler']:
        if decoded is not None: decoded = bytearray(filter.apply(decoded))
        return Filter(), bytes(decoded) if decoded is not None else None
    raise ValueError('decoded content checksum mismatch')


def prepare(game, key_file, output, media='xp3hnp', dictionary=None):
    keys = read_keys(key_file)
    generator = FilterGenerator(keys['params'], keys['control'], keys['nonce0'][:8])
    names = Names(media)
    if dictionary:
        if dictionary.stat().st_size > SCRIPT_LIMIT: raise ValueError('name dictionary too large')
        for line in dictionary.read_text(encoding='utf-8').splitlines(): names.add(line)
    archives = []
    for archive in sorted(game.glob('*.xp3')):
        metadata = archive_metadata(archive)
        records = authenticate_table(metadata['payload'], metadata['flags'], keys)
        verified, warnings = [], 0
        with archive.open('rb') as stream:
            for record in sorted(records, key=lambda r: metadata['entries'].get(fake_name(r['id']), {}).get('segments', [(0, 0, 0, 0)])[0][1]):
                entry = metadata['entries'].get(fake_name(record['id']))
                if entry is None: raise ValueError('Hxv4 record has no XP3 entry')
                # Some packers add a deliberately inconsistent, unprotected
                # warning entry at id zero. Never expose it to the engine.
                totals = (sum(s[2] for s in entry['segments']), sum(s[3] for s in entry['segments']))
                if totals != (entry['size'], entry['packed']):
                    if record['id'] == 0 and entry['flags'] == 0:
                        warnings += 1
                        continue
                    raise ValueError('inconsistent protected XP3 entry sizes')
                if not entry['segments'] or entry['size'] >= 1 << 63:
                    raise ValueError('unsupported XP3 entry size')
                if any(m and (not packed or max(size, packed) > 0xffffffff) for m, _, size, packed in entry['segments']):
                    raise ValueError('compressed XP3 segment exceeds Android limits')
                filter, plain = verify_entry(stream, entry, generator.filter(record['key'], record['flag']))
                if plain is not None:
                    for value in script_strings(plain): names.add(value)
                verified.append((record, entry, filter))
        print(f'{archive.name}: authenticated {len(records)} records; verified {len(verified)} file checksums; skipped {warnings} warning entry', flush=True)
        archives.append((archive, metadata, verified, warnings))
    if not archives: raise ValueError('no XP3 archives found')
    # All content checks finish before any companion is published.
    output.mkdir(parents=True, exist_ok=True)
    report = {'format': 'K2HXIDX1', 'game_code_executed': False, 'archives': []}
    for archive, metadata, records, warnings in archives:
        files, resolved, startup = [], 0, False
        for record, entry, filter in records:
            name = names.resolve(record['lookup'])
            if name:
                resolved += 1
            else:
                # Hash lookup remains available when static mining cannot
                # recover a filename generated dynamically by a script.
                path = names.paths.get(record['lookup'][:8])
                prefix = path + '/' if path else '' if path == '' else '__hxv4__/'
                name = prefix + '__hxv4_' + str(record['id'])
            startup |= name == 'startup.tjs'
            files.append(companion_file(entry, name, record['lookup'], filter))
        blob = companion(metadata, files, media)
        destination = output / (archive.name + '.hxidx')
        temporary = destination.with_suffix(destination.suffix + '.tmp')
        temporary.write_bytes(blob)
        temporary.replace(destination)
        report['archives'].append({'archive': archive.name, 'verified_entries': len(records),
                                   'resolved_names': resolved, 'hashed_only_names': len(records) - resolved,
                                   'skipped_warning_entries': warnings, 'startup_resolved': startup,
                                   'companion_bytes': len(blob), 'companion_blake2s': hashlib.blake2s(blob).hexdigest()})
    (output / 'hxv4-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'Wrote {len(archives)} companions to {output}; original archives unchanged.', flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--keys', type=Path, required=True, help='local JSON from static recovery; never publish it')
    parser.add_argument('--output', type=Path, required=True, help='companions go beside the XP3 files when copied to Android')
    parser.add_argument('--media', default='xp3hnp')
    parser.add_argument('--names', type=Path, help='optional UTF-8 name dictionary, one path per line')
    args = parser.parse_args()
    try:
        prepare(args.game, args.keys, args.output, args.media, args.names)
    except (ValueError, KeyError, OSError, UnicodeError, zlib.error, struct.error) as error:
        parser.exit(1, f'Hxv4 preparation failed: {error}\n')


if __name__ == '__main__':
    main()
