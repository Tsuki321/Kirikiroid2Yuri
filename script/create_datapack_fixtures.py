#!/usr/bin/env python3
"""Create original TJS DataPack conformance inputs; contains no game content."""
from pathlib import Path
import hashlib
import struct
import sys

MASK = (1 << 32) - 1
VOID = object()


def rol(value, count):
    return ((value << count) | (value >> (32 - count))) & MASK


def xxh32(data, seed):
    p1, p2, p3, p4, p5 = 2654435761, 2246822519, 3266489917, 668265263, 374761393
    pos = 0
    if len(data) >= 16:
        state = [(seed + p1 + p2) & MASK, (seed + p2) & MASK, seed, (seed - p1) & MASK]
        while pos + 16 <= len(data):
            words = struct.unpack_from('<4I', data, pos)
            state = [(rol((v + word * p2) & MASK, 13) * p1) & MASK for v, word in zip(state, words)]
            pos += 16
        result = sum(rol(v, n) for v, n in zip(state, (1, 7, 12, 18))) & MASK
    else:
        result = (seed + p5) & MASK
    result = (result + len(data)) & MASK
    while pos + 4 <= len(data):
        result = (rol((result + struct.unpack_from('<I', data, pos)[0] * p3) & MASK, 17) * p4) & MASK
        pos += 4
    for byte in data[pos:]:
        result = (rol((result + byte * p5) & MASK, 11) * p1) & MASK
    result = ((result ^ (result >> 15)) * p2) & MASK
    result = ((result ^ (result >> 13)) * p3) & MASK
    return result ^ (result >> 16)


def encrypt(payload, seed, iv, mode):
    rounds, batch = ((8, 16), (12, 8), (20, 4), (8, 1), (12, 1), (20, 1))[mode - 1]
    key = hashlib.blake2s(iv, key=struct.pack('<I', seed)).digest()
    state = list(struct.unpack('<4I', b'expand 32-byte k') + struct.unpack('<8I', key))
    state += [0, 0, xxh32(iv, seed), seed]
    fallback = (seed ^ state[14]) or seed or MASK
    result = bytearray(payload)
    at = 0
    while at < len(result):
        words = state.copy()
        def quarter(a, b, c, d):
            words[a] = (words[a] + words[b]) & MASK
            words[d] = rol(words[d] ^ words[a], 16)
            words[c] = (words[c] + words[d]) & MASK
            words[b] = rol(words[b] ^ words[c], 12)
            words[a] = (words[a] + words[b]) & MASK
            words[d] = rol(words[d] ^ words[a], 8)
            words[c] = (words[c] + words[d]) & MASK
            words[b] = rol(words[b] ^ words[c], 7)
        for _ in range(rounds // 2):
            for indices in ((0,4,8,12), (1,5,9,13), (2,6,10,14), (3,7,11,15),
                            (0,5,10,15), (1,6,11,12), (2,7,8,13), (3,4,9,14)):
                quarter(*indices)
        words = [(a + b) & MASK for a, b in zip(words, state)]
        for block in range(batch):
            if block:
                for i, word in enumerate(words):
                    word ^= (word << 13) & MASK
                    word ^= word >> 17
                    word ^= (word << 5) & MASK
                    words[i] = word or fallback
            stream = struct.pack('<16I', *words)
            size = min(64, len(result) - at)
            for i in range(size):
                result[at + i] ^= stream[i]
            at += size
            if at == len(result):
                break
        state[12] = (state[12] + 1) & MASK
        if not state[12]:
            state[13] = (state[13] + 1) & MASK
    return bytes(result)


def body(value, seed, endian='<'):
    check = bytearray(((seed ^ (seed >> 24)) & 255, (seed >> 8) & 255, (seed >> 16) & 255))
    def advance():
        a = check[0] ^ ((check[0] * 2) & 255)
        b = ((a >> 2) ^ check[2]) >> 3
        check[:] = check[1], check[2], (b ^ check[2] ^ a) & 255
    def string(text):
        encoded = text.encode('utf-16le')
        return struct.pack(endian + 'I', len(encoded) // 2) + encoded
    def value_bytes(item):
        if item is VOID: kind = 0
        elif item is None: kind = 1
        elif isinstance(item, str): kind = 2
        elif isinstance(item, bytes): kind = 3
        elif isinstance(item, int): kind = 4
        elif isinstance(item, float): kind = 5
        elif isinstance(item, list): kind = 0x81
        elif isinstance(item, dict): kind = 0xc1
        else: raise ValueError(type(item))
        if kind: advance()
        result = struct.pack(endian + 'H', kind | (check[2] << 8))
        if kind == 2: result += string(item)
        elif kind == 3: result += struct.pack(endian + 'I', len(item)) + item
        elif kind == 4: result += struct.pack(endian + 'q', item)
        elif kind == 5: result += struct.pack(endian + 'd', item)
        elif kind == 0x81: result += struct.pack(endian + 'I', len(item)) + b''.join(value_bytes(v) for v in item)
        elif kind == 0xc1: result += struct.pack(endian + 'I', len(item)) + b''.join(string(k) + value_bytes(v) for k, v in item.items())
        return result
    result = value_bytes(value)
    for _ in range(3): advance()
    return result + struct.pack(endian + 'I', check[2] | check[1] << 8 | check[0] << 16)


def literal_frames(data):
    framed = bytearray()
    for at in range(0, len(data), 4096):
        part = data[at:at + 4096]
        encoded = bytearray([min(len(part), 15) << 4])
        if len(part) >= 15:
            remaining = len(part) - 15
            while remaining >= 255:
                encoded.append(255)
                remaining -= 255
            encoded.append(remaining)
        encoded.extend(part)
        framed.extend(struct.pack('<H', len(encoded)))
        framed.extend(encoded)
    return bytes(framed)


def pack(value, seed=0xa1b2c3d4, mode=0, compressed=False, iv=b'', outer=None, big=False):
    endian = '>' if big else '<'
    payload = body(value, seed, endian)
    if compressed: payload = literal_frames(payload)
    if mode: payload = encrypt(payload, seed, iv if outer is None else outer, mode)
    return (b'TJS\\' if big else b'TJS/') + (b'4s0\0' if compressed else b'ns0\0') + struct.pack(endian + 'IHH', seed, mode, len(iv)) + iv + payload


def main(directory):
    directory.mkdir(parents=True, exist_ok=True)
    value = {'title': 'Synthetic metadata', 'unicode': '\u65e5\u672c\u8a9e\U0001f600',
             'items': [VOID, None, -7, 1099511627776, 3.25, '', b'', bytes(range(256))],
             'longtext': 'abc' * 2200, 'rows': [[i, -i, None] for i in range(80)]}
    outputs = {'plain.pbd': pack(value), 'big.pbd': pack(value, big=True),
               'compressed.pbd': pack(value, compressed=True),
               'empty-iv.pbd': pack(value, mode=1, compressed=True),
               'outer.pbd': pack(value, mode=1, compressed=True, iv=b'hint', outer='\u5916\u90e8-IV'.encode()),
               'scalar.pbd': pack(-9223372036854775808)}
    for mode in range(1, 7):
        outputs[f'cipher-{mode}.pbd'] = pack(value, mode=mode, compressed=True, iv=bytes(range(17)))
    for name, data in outputs.items():
        (directory / name).write_bytes(data)
    (directory / 'truncated.pbd').write_bytes(outputs['plain.pbd'][:-1])
    print(f'Created {len(outputs) + 1} synthetic DataPack fixtures in {directory}')


if __name__ == '__main__':
    main(Path(sys.argv[1]) if len(sys.argv) > 1 else Path('tests/fixtures/datapack'))
