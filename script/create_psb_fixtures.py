#!/usr/bin/env python3
"""Generate small, original PSB v2/v3 data fixtures, without compiling scripts."""
import argparse
from collections import deque
from dataclasses import dataclass
from pathlib import Path
import struct
import zlib


@dataclass(frozen=True)
class Resource:
    index: int


class PackedArray(list):
    pass


@dataclass(frozen=True)
class Float32:
    value: float


def width(value):
    return max(1, (value.bit_length() + 7) // 8)


def table(values):
    count_width = width(len(values))
    value_width = width(max(values, default=0))
    return (bytes([12 + count_width]) + len(values).to_bytes(count_width, 'little') +
            bytes([12 + value_width]) + b''.join(v.to_bytes(value_width, 'little') for v in values))


def key_trie(names):
    tree = {}
    for name in names:
        node = tree
        for ch in name.encode('utf-8') + b'\0':
            node = node.setdefault(ch, {})
        node['name'] = name
    bases, parents, terminals = {0: 0}, {0: 0}, {}
    occupied = {0}
    pending = deque([(0, tree)])
    candidate = 1
    while pending:
        index, node = pending.popleft()
        if 'name' in node:
            terminals[node['name']] = index
            continue
        children = sorted(node)
        while any(candidate + ch in occupied for ch in children):
            candidate += 1
        bases[index] = candidate
        for ch in children:
            child = candidate + ch
            occupied.add(child)
            parents[child] = index
            pending.append((child, node[ch]))
        candidate += 1
    count = max(occupied) + 1
    return (table([bases.get(i, 0) for i in range(count)]) +
            table([parents.get(i, 0) for i in range(count)]) + table([terminals[n] for n in names]))


def build(root, resources=(), version=3):
    names, strings = set(), set()
    def collect(value):
        if isinstance(value, dict):
            names.update(value)
            for child in value.values(): collect(child)
        elif isinstance(value, list):
            for child in value: collect(child)
        elif isinstance(value, str): strings.add(value)
    collect(root)
    names, strings = sorted(names), sorted(strings)
    name_ids = {n:i for i,n in enumerate(names)}
    string_ids = {n:i for i,n in enumerate(strings)}
    def encode(value):
        if value is None: return b'\x01'
        if isinstance(value, bool): return b'\x03' if value else b'\x02'
        if isinstance(value, int):
            if value == 0: return b'\x04'
            length = next(n for n in range(1, 9) if -(1 << (8*n-1)) <= value < (1 << (8*n-1)))
            return bytes([4+length]) + value.to_bytes(length, 'little', signed=True)
        if isinstance(value, float): return b'\x1f' + struct.pack('<d', value)
        if isinstance(value, str):
            index = string_ids[value]
            return bytes([0x14+width(index)]) + index.to_bytes(width(index), 'little')
        if isinstance(value, Resource):
            return bytes([0x18+width(value.index)]) + value.index.to_bytes(width(value.index), 'little')
        if isinstance(value, PackedArray): return table(value)
        if isinstance(value, Float32): return b'\x1e' + struct.pack('<f', value.value)
        keys = sorted(value, key=name_ids.get) if isinstance(value, dict) else None
        items = [value[k] for k in keys] if keys is not None else value
        encoded = [encode(v) for v in items]
        offsets, position = [], 0
        for child in encoded:
            offsets.append(position)
            position += len(child)
        return ((b'\x21' + table([name_ids[k] for k in keys]) if keys is not None else b'\x20') +
                table(offsets) + b''.join(encoded))
    string_offsets, string_data = [], bytearray()
    for s in strings:
        string_offsets.append(len(string_data))
        string_data += s.encode('utf-8') + b'\0'
    resource_offsets, resource_data = [], bytearray()
    for data in resources:
        resource_offsets.append(len(resource_data))
        resource_data += data
    header_length = 44 if version == 3 else 40
    parts = [key_trie(names), table(string_offsets), bytes(string_data) or b'\0',
             table(resource_offsets), table([len(r) for r in resources]), encode(root)]
    offsets, position = [], header_length
    for part in parts:
        offsets.append(position)
        position += len(part)
    fields = struct.pack('<8I', header_length, *offsets[:5], position, offsets[5])
    header = b'PSB\0' + struct.pack('<HH', version, 0) + fields
    if version == 3: header += struct.pack('<I', zlib.adler32(fields))
    return header + b''.join(parts) + resource_data


def png():
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))
    pixels = b'\0\xff\0\0\xff\0\xff\0\xff\0\0\0\xff\xff\0\0\0\0'
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


def tlg():
    # One opaque RGB(0x20, 0x40, 0x80) pixel, four uncompressed TLG5 planes.
    planes = [0xe0, 0x40, 0x40, 0xff]  # R-G, G, B-G, A (modulo 256)
    data = b''.join(b'\x01' + struct.pack('<I', 1) + bytes([value]) for value in planes)
    return b'TLG5.0\0raw\x1a' + b'\x04' + struct.pack('<4I', 1, 1, 1, len(data)) + data


def create(directory):
    directory.mkdir(parents=True, exist_ok=True)
    scene = {'scenes': [{'label':'intro', 'text':'Synthetic dialogue', 'choices':['left', 'right']}],
             'enabled': True, 'disabled': False, 'empty': None, 'zero': 0,
             'negative': -1234567, 'wide': -(1 << 55), 'fraction': 1.25, 'unicode': '日本語 😀',
             '日本語😀': 'key', 'numbers': PackedArray([0, 255, 0xffffffff]),
             'single': Float32(1.5), 'zero_real': 0.0, 'empty_array': [], 'empty_object': {},
             'min_integer': -(1 << 63), 'max_integer': (1 << 63) - 1}
    images = {'width':2, 'height':2, 'layers':[{'name':'test', 'layer_id':0, 'width':2, 'height':2}],
              'tile.png':Resource(0), 'empty.bin':Resource(1), '0.tlg':Resource(2),
              'folder/tile.png':Resource(0), '日本語.png':Resource(0), 'emoji😀.png':Resource(0)}
    (directory/'scene.psb').write_bytes(build(scene))
    (directory/'images.pimg').write_bytes(build(images, [png(), b'', tlg()], version=2))
    invalid = bytearray(build(scene))
    struct.pack_into('<I', invalid, 36, 0xffffffff)
    (directory/'invalid.psb').write_bytes(invalid)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    create(parser.parse_args().output)
