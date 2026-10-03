"""Offline Hxv4 primitives. No game code, DLL loading, or network access.

The filter generator follows GARbro's Cx/Hx algorithms (MIT, morkt).
See hxv4-LICENSE.txt. Control words here are the actual table values; GARbro
stores their complement internally. Arithmetic expressions are interpreted as
data, never emitted or passed to eval/exec.
"""

import hashlib
import struct
import zlib
from dataclasses import dataclass

from inspect_game_package import MAGIC, MAX_INDEX_BYTES, chunks, read_exact

U32 = (1 << 32) - 1
U64 = (1 << 64) - 1


def rol(value, count):
    return ((value << count) | (value >> (64 - count))) & U64


def siphash24(data, k0=0, k1=0):
    v = [0x736f6d6570736575 ^ k0, 0x646f72616e646f6d ^ k1,
         0x6c7967656e657261 ^ k0, 0x7465646279746573 ^ k1]

    def rounds(count):
        for _ in range(count):
            v[0] = (v[0] + v[1]) & U64
            v[1] = rol(v[1], 13) ^ v[0]
            v[0] = rol(v[0], 32)
            v[2] = (v[2] + v[3]) & U64
            v[3] = rol(v[3], 16) ^ v[2]
            v[0] = (v[0] + v[3]) & U64
            v[3] = rol(v[3], 21) ^ v[0]
            v[2] = (v[2] + v[1]) & U64
            v[1] = rol(v[1], 17) ^ v[2]
            v[2] = rol(v[2], 32)

    full = len(data) & ~7
    words = [int.from_bytes(data[i:i + 8], 'little') for i in range(0, full, 8)]
    words.append((len(data) & 255) << 56 | int.from_bytes(data[full:], 'little'))
    for word in words:
        v[3] ^= word
        rounds(2)
        v[0] ^= word
    v[2] ^= 255
    rounds(4)
    return v[0] ^ v[1] ^ v[2] ^ v[3]


def normalize(name):
    return '/'.join(part for part in name.replace('\\', '/').split('/') if part).translate(
        str.maketrans('ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'abcdefghijklmnopqrstuvwxyz'))


def valid_name(name):
    return (bool(name) and len(name.encode('utf-16le')) // 2 <= 65535
            and name == normalize(name)
            and not any(ord(c) < 32 or c in ':>' for c in name)
            and all(part not in ('', '.', '..') for part in name.split('/')))


def path_hash(path, media='xp3hnp'):
    path = normalize(path)
    if path: path += '/'
    return siphash24((path + media).encode('utf-16le')).to_bytes(8, 'little')


def file_hash(name, media='xp3hnp'):
    return hashlib.blake2s((normalize(name) + media).encode('utf-16le')).digest()


def lookup_hash(name, media='xp3hnp'):
    name = normalize(name)
    path, _, file = name.rpartition('/')
    return path_hash(path, media) + file_hash(file, media)


class Random:
    def __init__(self, lane, kind):
        self.kind = kind
        value = lane | ((~lane & U32) << 32)
        seeds = []
        for _ in range(2):
            value = (value + 0x9e3779b97f4a7c15) & U64
            word = ((value ^ (value >> 30)) * 0xbf58476d1ce4e5b9) & U64
            word = ((word ^ (word >> 27)) * 0x94d049bb133111eb) & U64
            seeds.append(word ^ (word >> 31))
        self.a, self.b = seeds

    def next(self):
        a, b = self.a, self.b
        b ^= a
        if self.kind == 0:
            result = (rol((a + self.b) & U64, 17) + a) & U64
            self.a = rol(a, 49) ^ b ^ ((b << 21) & U64)
            self.b = rol(b, 28)
        else:
            result = (rol((a * 5) & U64, 7) * 9) & U64
            self.a = rol(a, 24) ^ b ^ ((b << 16) & U64)
            self.b = rol(b, 37)
        return result & U32


class ProgramTooLarge(Exception):
    pass


class FilterGenerator:
    def __init__(self, params, control, holder):
        if len(params) != 22 or len(control) != 4096 or len(holder) != 8:
            raise ValueError('invalid Hxv4 filter parameter sizes')
        for part in (params[:8], params[8:14], params[14:17]):
            if sorted(part) != list(range(len(part))):
                raise ValueError('invalid Hxv4 filter permutation')
        if params[17] & 0x7e:
            raise ValueError('unsupported Hxv4 filter mode')
        self.params = params
        self.table = struct.unpack('<1024I', control)
        self.holder = int.from_bytes(holder, 'little')
        self.mask, self.offset = struct.unpack_from('<HH', params, 18)
        self.programs = {}

    def program(self, lane):
        if lane in self.programs:
            return self.programs[lane]
        p = self.params
        random = Random(lane, p[17] >> 7)
        length = 0

        def spend(size):
            nonlocal length
            if length + size > 128:
                raise ProgramTooLarge()
            length += size

        def leaf():
            op = p[14:17].index(random.next() % 3)
            if op == 0:
                spend(1)
                value = random.next()
                spend(4)
                return ('constant', value)
            if op == 1:
                spend(2)
                return ('seed',)
            spend(7)
            value = random.next() & 1023
            spend(4)
            return ('constant', self.table[value])

        def child(depth):
            return node(depth, bool(random.next() & 1))

        def node(depth, binary):
            if depth == 1:
                return leaf()
            if binary:
                spend(1)
                first = child(depth - 1)
                spend(2)
                second = child(depth - 1)
                op = p[8:14].index(random.next() % 6)
                spend([2, 2, 4, 3, 9, 9][op])
                spend(1)
                return ('binary', op, first, second)
            value = child(depth - 1)
            op = p[:8].index(random.next() & 7)
            if op in (5, 6):
                add = bool(random.next() & 1) if op == 6 else False
                spend(1)
                immediate = random.next()
                spend(4)
                return ('immediate', 'xor' if op == 5 else 'add' if add else 'sub', value, immediate)
            spend([2, 2, 1, 1, 21, 0, 0, 13][op])
            return ('unary', op, value)

        for depth in range(5, 0, -1):
            length = 0
            try:
                spend(9)
                result = node(depth, True)
                spend(6)
                self.programs[lane] = result
                return result
            except ProgramTooLarge:
                # Retrying at a shallower depth preserves the PRNG state.
                pass
        raise ValueError('Hxv4 filter exceeded its program budget')

    def evaluate(self, node, seed):
        kind = node[0]
        if kind == 'seed':
            return seed
        if kind == 'constant':
            return node[1]
        if kind == 'binary':
            a = self.evaluate(node[2], seed)
            b = self.evaluate(node[3], seed)
            op = node[1]
            return (b + a if op == 0 else b - a if op == 1 else a - b if op == 2 else
                    a * b if op == 3 else b << (a & 15) if op == 4 else b >> (a & 15)) & U32
        value = self.evaluate(node[2], seed)
        if kind == 'immediate':
            return (value ^ node[3] if node[1] == 'xor' else value + node[3]
                    if node[1] == 'add' else value - node[3]) & U32
        op = node[1]
        return (~value if op == 0 else -value if op == 1 else value + 1 if op == 2 else
                value - 1 if op == 3 else ((value & 0xaaaaaaaa) >> 1) | ((value & 0x55555555) << 1)
                if op == 4 else self.table[value & 1023]) & U32

    def drip(self, value):
        program = self.program(value & 127)
        seed = (value & U32) >> 7
        return self.evaluate(program, seed) | self.evaluate(program, ~seed & U32) << 32

    def filter(self, key, flag):
        if flag not in (0, 1):
            raise ValueError('unsupported Hxv4 record filter flag')
        if not flag:
            key ^= self.holder
        first, second = self.drip(key & U32), self.drip(key >> 32)
        header0 = ~self.drip(~key & U32) & U64
        header1 = ~self.drip(header0 & U32) & U64
        return Filter(True, self.offset + ((key >> 16) & self.mask), first, second,
                      header0.to_bytes(8, 'big') + header1.to_bytes(8, 'big'))


@dataclass(frozen=True)
class Filter:
    active: bool = False
    split: int = 0
    left: int = 0
    right: int = 0
    header: bytes = bytes(16)

    def pack(self):
        return struct.pack('<IQQQ16s', self.active, self.split, self.left, self.right, self.header)

    def apply(self, data, offset=0):
        if not self.active:
            return bytes(data)
        output = bytearray(data)
        for i in range(min(len(data), max(0, 16 - offset))):
            output[i] ^= self.header[offset + i]
        split = max(0, min(len(data), self.split - offset))
        for start, end, key in ((0, split, self.left), (split, len(data), self.right)):
            if start == end:
                continue
            bulk = (key & 255) or 0xa5
            output[start:end] = output[start:end].translate(bytes(x ^ bulk for x in range(256)))
            p0, p1 = (key >> 48) & 65535, (key >> 32) & 65535
            if p0 == p1:
                p1 += 1
            for position, correction in ((p0, (key >> 8) & 255), (p1, (key >> 16) & 255)):
                if offset + start <= position < offset + end:
                    output[position - offset] ^= correction
        return bytes(output)


def inflate(data, expected):
    if not 0 <= expected <= MAX_INDEX_BYTES:
        raise ValueError('Hxv4 decompression size exceeds limit')
    inflater = zlib.decompressobj()
    decoded = inflater.decompress(data, expected + 1)
    if len(decoded) != expected or not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
        raise ValueError('Hxv4 decompression size or stream mismatch')
    return decoded


class TableReader:
    """Bounded reader for the serialized objects in an authenticated Hxv4 table."""
    def __init__(self, data):
        self.data = memoryview(data)
        self.at = 0
        self.objects = 0

    def take(self, size):
        if size < 0 or size > len(self.data) - self.at:
            raise ValueError('truncated Hxv4 table')
        value = self.data[self.at:self.at + size]
        self.at += size
        return bytes(value)

    def count(self):
        count = int.from_bytes(self.take(4), 'big', signed=True)
        if count < 0 or count > len(self.data) - self.at:
            raise ValueError('invalid Hxv4 object count')
        return count

    def string(self):
        return self.take(self.count() * 2).decode('utf-16be')

    def value(self, depth=0):
        self.objects += 1
        if depth > 32 or self.objects > 2000000:
            raise ValueError('Hxv4 object limit exceeded')
        tag = self.take(1)[0]
        if tag in (0, 1): return None
        if tag == 2: return self.string()
        if tag == 3: return self.take(self.count())
        if tag == 4: return int.from_bytes(self.take(8), 'big', signed=True)
        if tag == 5: return struct.unpack('>d', self.take(8))[0]
        if tag == 0x81: return [self.value(depth + 1) for _ in range(self.count())]
        if tag == 0xc1:
            result = {}
            for _ in range(self.count()):
                name = self.string()
                if name in result: raise ValueError('duplicate Hxv4 dictionary key')
                result[name] = self.value(depth + 1)
            return result
        raise ValueError('unknown Hxv4 object tag')


def authenticate_table(payload, flags, keys):
    from Crypto.Cipher import ChaCha20_Poly1305
    if flags not in (0, 1) or len(payload) < 20:
        raise ValueError('unsupported Hxv4 table flags or truncated payload')
    cipher = ChaCha20_Poly1305.new(key=keys['key'], nonce=keys['nonce1' if flags else 'nonce0'])
    plain = cipher.decrypt_and_verify(payload[16:], payload[:16])
    decoded = inflate(plain[4:], int.from_bytes(plain[:4], 'little'))
    reader = TableReader(decoded)
    table = reader.value()
    if reader.at != len(decoded) or not isinstance(table, list) or len(table) % 2:
        raise ValueError('invalid Hxv4 root object or trailing data')
    records = []
    for at in range(0, len(table), 2):
        domain, group = table[at:at + 2]
        if not isinstance(domain, bytes) or len(domain) != 8:
            raise ValueError('invalid Hxv4 directory group')
        if not isinstance(group, list) or len(group) % 2:
            raise ValueError('invalid Hxv4 record list')
        for pos in range(0, len(group), 2):
            name_hash, item = group[pos:pos + 2]
            if (not isinstance(item, list) or len(item) != 2 or type(item[0]) is not int
                    or type(item[1]) is not int or not isinstance(name_hash, bytes) or len(name_hash) != 32):
                raise ValueError('invalid Hxv4 record')
            locator, key = item
            locator &= U64
            if locator >> 48 or (locator >> 32) & 65535 not in (0, 1):
                raise ValueError('cross-archive Hxv4 records are not supported')
            records.append({'id': locator & U32, 'flag': (locator >> 32) & 65535,
                            'key': key & U64, 'lookup': domain + name_hash})
    if not records or len(records) != len({r['id'] for r in records}) or len(records) != len({r['lookup'] for r in records}):
        raise ValueError('empty or duplicate Hxv4 records')
    return records


def fake_name(number):
    name = ''
    while True:
        name += chr((number & 0x3fff) + 0x5000)
        number >>= 14
        if not number: return name


def archive_metadata(path):
    with path.open('rb') as stream:
        size = stream.seek(0, 2)
        stream.seek(0)
        if read_exact(stream, 11) != MAGIC: raise ValueError('not a standalone XP3 archive')
        offset, = struct.unpack('<Q', read_exact(stream, 8))
        seen = set()
        while True:
            if offset > size - 9 or offset in seen or len(seen) >= 1024:
                raise ValueError('invalid or cyclic XP3 index offset')
            seen.add(offset)
            stream.seek(offset)
            flag = read_exact(stream, 1)[0]
            if flag not in (0, 1, 128, 129): raise ValueError('invalid XP3 index flags')
            stored, = struct.unpack('<Q', read_exact(stream, 8))
            original = struct.unpack('<Q', read_exact(stream, 8))[0] if flag & 1 else stored
            if stored > MAX_INDEX_BYTES or original > MAX_INDEX_BYTES or stored > size - stream.tell():
                raise ValueError('invalid XP3 index size')
            index = read_exact(stream, stored)
            if flag & 1: index = inflate(index, original)
            if not flag & 128: break
            # KiriKiri Z starts with an empty continuation index. A companion
            # describes one populated final index, with optional empty links.
            if index: raise ValueError('multiple populated Hxv4 indices are unsupported')
            offset, = struct.unpack('<Q', read_exact(stream, 8))
        entries, descriptor = {}, None
        for tag, value in chunks(index):
            if tag == b'Hxv4':
                if descriptor is not None or len(value) != 14: raise ValueError('invalid Hxv4 descriptor')
                descriptor = struct.unpack('<QIH', value)
            elif tag == b'File':
                fields = {}
                for field, data in chunks(value):
                    if field in fields: raise ValueError('duplicate XP3 File field')
                    fields[field] = data
                if not all(k in fields for k in (b'info', b'segm', b'adlr')): raise ValueError('missing XP3 File field')
                info, segs, adlr = (fields[k] for k in (b'info', b'segm', b'adlr'))
                if len(info) < 22 or len(segs) % 28 or len(adlr) != 4: raise ValueError('invalid XP3 File fields')
                flags, length, packed, units = struct.unpack_from('<IQQH', info)
                if len(info) < 22 + units * 2: raise ValueError('invalid XP3 filename size')
                name = bytes(info[22:22 + units * 2]).decode('utf-16le')
                if '\0' in name: raise ValueError('NUL in XP3 filename')
                if not name or name in entries: raise ValueError('empty or duplicate XP3 name')
                segments = list(struct.iter_unpack('<IQQQ', segs))
                for method, at, unpacked, compressed in segments:
                    if method not in (0, 1) or at > size or compressed > size - at or (not method and unpacked != compressed):
                        raise ValueError('invalid XP3 segment')
                entries[name] = {'flags': flags, 'size': length, 'packed': packed, 'segments': segments,
                                 'adler': int.from_bytes(adlr, 'little')}
        if descriptor is None: raise ValueError('archive has no Hxv4 table')
        at, count, table_flag = descriptor
        if count < 20 or count > MAX_INDEX_BYTES - len(index) or at > size or count > size - at:
            raise ValueError('invalid Hxv4 table range')
        stream.seek(at)
        payload = read_exact(stream, count)
    return {'size': size, 'index': index, 'payload': payload, 'flags': table_flag,
            'binding': hashlib.blake2s(index + payload).digest(), 'entries': entries}


def entry_chunks(stream, entry):
    """Bounded, sequential decompression, yielding at most 1 MiB at a time."""
    limit = 1024 * 1024
    for method, offset, expected, stored in entry['segments']:
        stream.seek(offset)
        inflater = zlib.decompressobj() if method else None
        produced = 0
        while stored:
            data = read_exact(stream, min(stored, limit))
            stored -= len(data)
            if not inflater:
                produced += len(data)
                yield data
                continue
            while data:
                output = inflater.decompress(data, min(limit, expected - produced + 1))
                data = inflater.unconsumed_tail
                produced += len(output)
                if produced > expected or inflater.unused_data:
                    raise ValueError('XP3 segment decompression overflow or trailing data')
                if output: yield output
        if produced != expected or (inflater and not inflater.eof):
            raise ValueError('XP3 segment size mismatch')


def chunk(tag, data):
    return tag + struct.pack('<Q', len(data)) + data


def companion_file(entry, name, lookup, filter):
    if not valid_name(name) or len(lookup) != 40: raise ValueError('invalid companion name or lookup')
    encoded = name.encode('utf-16le')
    info = struct.pack('<IQQH', entry['flags'], entry['size'], entry['packed'], len(encoded) // 2) + encoded
    fields = chunk(b'info', info) + chunk(b'segm', b''.join(struct.pack('<IQQQ', *s) for s in entry['segments']))
    fields += chunk(b'adlr', struct.pack('<I', entry['adler']))
    fields += chunk(b'hnam', lookup) + chunk(b'hxky', filter.pack())
    return chunk(b'File', fields)


def companion(metadata, files, media='xp3hnp'):
    encoded = media.encode('utf-16le')
    if not encoded or len(encoded) > 512 or any(ord(c) < 32 or c in '/\\:>' for c in media):
        raise ValueError('invalid Hxv4 storage media identifier')
    body = encoded + b''.join(files)
    if len(body) > MAX_INDEX_BYTES: raise ValueError('companion exceeds index size limit')
    return (b'K2HXIDX1' + struct.pack('<Q', metadata['size']) + metadata['binding']
            + hashlib.blake2s(body).digest() + struct.pack('<QII', len(body), len(encoded) // 2, len(files)) + body)
