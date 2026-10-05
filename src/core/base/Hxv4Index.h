#pragma once

#include "XP3IndexValidator.h"
#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <vector>

// The companion contains ordinary XP3 File records, lookup hashes and expanded
// XOR filters. It is generated offline after authenticating the Hxv4 table.
// No executable code or game-wide encryption key is loaded by this reader.
namespace TVPHxv4 {
using Digest = std::array<uint8_t, 32>;
using Lookup = std::array<uint8_t, 40>;
constexpr size_t HeaderSize = 96;
constexpr size_t FilterSize = 44;

inline uint32_t Read32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint32_t Ror32(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
inline uint64_t Rol64(uint64_t x, unsigned n) { return (x << n) | (x >> (64 - n)); }

// BLAKE2s-256, as specified by RFC 7693. All input loads are byte-wise so that
// archive offsets and UTF-16 strings need not be aligned on ARM.
// keyBytes is nonzero only when data already starts with the padded key block.
inline Digest Blake2s(const uint8_t *data, size_t size, uint8_t keyBytes = 0) {
    static const uint32_t iv[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    static const uint8_t sigma[10][16] = {
        {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
        {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},
        {11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4},
        {7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},
        {9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13},
        {2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},
        {12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11},
        {13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},
        {6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5},
        {10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0}
    };
    uint32_t h[8];
    std::copy(iv, iv + 8, h);
    h[0] ^= 0x01010020 | (uint32_t(keyBytes) << 8);
    uint64_t total = 0;
    do {
        uint8_t block[64] = {};
        size_t take = std::min<size_t>(size, sizeof(block));
        if (take) { std::memcpy(block, data, take); data += take; }
        size -= take;
        total += take;
        uint32_t m[16], v[16];
        for (unsigned i = 0; i < 16; ++i) m[i] = Read32(block + i * 4);
        std::copy(h, h + 8, v);
        std::copy(iv, iv + 8, v + 8);
        v[12] ^= static_cast<uint32_t>(total);
        v[13] ^= static_cast<uint32_t>(total >> 32);
        if (!size) v[14] = ~v[14];
        auto mix = [&](unsigned a, unsigned b, unsigned c, unsigned d, uint32_t x, uint32_t y) {
            v[a] += v[b] + x; v[d] = Ror32(v[d] ^ v[a], 16);
            v[c] += v[d]; v[b] = Ror32(v[b] ^ v[c], 12);
            v[a] += v[b] + y; v[d] = Ror32(v[d] ^ v[a], 8);
            v[c] += v[d]; v[b] = Ror32(v[b] ^ v[c], 7);
        };
        for (unsigned round = 0; round < 10; ++round) {
            const uint8_t *s = sigma[round];
            mix(0,4,8,12,m[s[0]],m[s[1]]); mix(1,5,9,13,m[s[2]],m[s[3]]);
            mix(2,6,10,14,m[s[4]],m[s[5]]); mix(3,7,11,15,m[s[6]],m[s[7]]);
            mix(0,5,10,15,m[s[8]],m[s[9]]); mix(1,6,11,12,m[s[10]],m[s[11]]);
            mix(2,7,8,13,m[s[12]],m[s[13]]); mix(3,4,9,14,m[s[14]],m[s[15]]);
        }
        for (unsigned i = 0; i < 8; ++i) h[i] ^= v[i] ^ v[i + 8];
    } while (size);
    Digest digest;
    for (unsigned i = 0; i < 32; ++i) digest[i] = static_cast<uint8_t>(h[i / 4] >> (8 * (i % 4)));
    return digest;
}

inline uint64_t SipHash24(const uint8_t *data, size_t size, uint64_t k0 = 0, uint64_t k1 = 0) {
    uint64_t a = 0x736f6d6570736575ULL ^ k0, b = 0x646f72616e646f6dULL ^ k1;
    uint64_t c = 0x6c7967656e657261ULL ^ k0, d = 0x7465646279746573ULL ^ k1;
    auto round = [&]() {
        a += b; b = Rol64(b, 13) ^ a; a = Rol64(a, 32);
        c += d; d = Rol64(d, 16) ^ c;
        a += d; d = Rol64(d, 21) ^ a;
        c += b; b = Rol64(b, 17) ^ c; c = Rol64(c, 32);
    };
    uint64_t last = uint64_t(size & 255) << 56;
    while (size >= 8) {
        uint64_t m = TVPXP3::Read64(data);
        d ^= m; round(); round(); a ^= m;
        data += 8; size -= 8;
    }
    for (size_t i = 0; i < size; ++i) last |= uint64_t(data[i]) << (8 * i);
    d ^= last; round(); round(); a ^= last;
    c ^= 255;
    round(); round(); round(); round();
    return a ^ b ^ c ^ d;
}

inline std::vector<uint8_t> Utf16Bytes(const std::u16string &text, const std::u16string &suffix) {
    std::vector<uint8_t> bytes;
    bytes.reserve((text.size() + suffix.size()) * 2);
    for (const auto *part : {&text, &suffix})
        for (auto ch : *part) { bytes.push_back(ch & 255); bytes.push_back(ch >> 8); }
    return bytes;
}

// The engine normalizes ASCII case and slashes before asking for a lookup.
inline Lookup HashName(const std::u16string &name, const std::u16string &media) {
    auto slash = name.rfind(u'/');
    std::u16string path = slash == std::u16string::npos ? u"" : name.substr(0, slash + 1);
    std::u16string file = slash == std::u16string::npos ? name : name.substr(slash + 1);
    auto directory = Utf16Bytes(path, media), filename = Utf16Bytes(file, media);
    uint64_t pathHash = SipHash24(directory.data(), directory.size());
    auto fileHash = Blake2s(filename.data(), filename.size());
    Lookup result;
    for (unsigned i = 0; i < 8; ++i) result[i] = static_cast<uint8_t>(pathHash >> (8 * i));
    std::copy(fileHash.begin(), fileHash.end(), result.begin() + 8);
    return result;
}

struct Filter {
    bool active = false;
    uint64_t split = 0, left = 0, right = 0;
    std::array<uint8_t, 16> header = {};

    bool Read(const uint8_t *data, size_t size) {
        if (size != FilterSize || Read32(data) > 1) return false;
        active = Read32(data) != 0;
        split = TVPXP3::Read64(data + 4);
        left = TVPXP3::Read64(data + 12); right = TVPXP3::Read64(data + 20);
        std::copy(data + 28, data + 44, header.begin());
        if (!active)
            for (size_t i = 4; i < size; ++i) if (data[i]) return false;
        return split <= UINT32_MAX;
    }

    static void Span(uint64_t key, uint64_t offset, uint8_t *buffer, size_t size) {
        uint8_t bulk = static_cast<uint8_t>(key);
        if (!bulk) bulk = 0xa5;
        for (size_t i = 0; i < size; ++i) buffer[i] ^= bulk;
        uint64_t p0 = (key >> 48) & 65535, p1 = (key >> 32) & 65535;
        if (p0 == p1) ++p1;
        if (p0 >= offset && p0 - offset < size) buffer[p0 - offset] ^= (key >> 8) & 255;
        if (p1 >= offset && p1 - offset < size) buffer[p1 - offset] ^= (key >> 16) & 255;
    }

    void Apply(uint64_t offset, void *data, size_t size) const {
        if (!active || !size) return;
        auto *buffer = static_cast<uint8_t *>(data);
        if (offset < header.size()) {
            size_t count = std::min<size_t>(size, header.size() - static_cast<size_t>(offset));
            for (size_t i = 0; i < count; ++i) buffer[i] ^= header[static_cast<size_t>(offset) + i];
        }
        size_t first = offset < split ? static_cast<size_t>(std::min<uint64_t>(size, split - offset)) : 0;
        if (first) Span(left, offset, buffer, first);
        if (first < size) Span(right, offset + first, buffer + first, size - first);
    }
};

inline bool ValidName(const std::u16string &name) {
    if (name.empty() || name.front() == u'/' || name.back() == u'/') return false;
    size_t start = 0;
    for (size_t i = 0; i <= name.size(); ++i) {
        if (i < name.size()) {
            auto ch = name[i];
            if (ch < 32 || ch == u'\\' || ch == u':' || ch == u'>' || (ch >= u'A' && ch <= u'Z')) return false;
            if (ch != u'/') continue;
        }
        auto part = name.substr(start, i - start);
        if (part.empty() || part == u"." || part == u"..") return false;
        start = i + 1;
    }
    return true;
}

struct Companion {
    std::u16string media;
    const uint8_t *index = nullptr;
    size_t indexSize = 0;
    uint32_t records = 0;
};

inline bool ReadCompanion(const uint8_t *data, size_t size, uint64_t archiveSize,
                          const Digest &sourceDigest, Companion &out) {
    out = Companion();
    if (!data || size < HeaderSize || size - HeaderSize > TVPXP3::MaxIndexBytes ||
        std::memcmp(data, "K2HXIDX1", 8) || TVPXP3::Read64(data + 8) != archiveSize ||
        std::memcmp(data + 16, sourceDigest.data(), 32) ||
        TVPXP3::Read64(data + 80) != size - HeaderSize) return false;
    auto digest = Blake2s(data + HeaderSize, size - HeaderSize);
    if (std::memcmp(data + 48, digest.data(), 32)) return false;
    uint32_t units = Read32(data + 88), records = Read32(data + 92);
    if (!units || units > 256 || uint64_t(units) * 2 > size - HeaderSize ||
        !records || records > (size - HeaderSize) / 150) return false;
    Companion result;
    for (uint32_t i = 0; i < units; ++i) {
        auto ch = TVPXP3::Read16(data + HeaderSize + i * 2);
        if (ch < 32 || ch == u'/' || ch == u'\\' || ch == u':' || ch == u'>') return false;
        result.media.push_back(ch);
    }
    result.index = data + HeaderSize + units * 2;
    result.indexSize = size - HeaderSize - units * 2;
    result.records = records;
    std::set<std::u16string> names;
    std::set<Lookup> hashes;
    size_t at = 0;
    uint32_t count = 0;
    while (at < result.indexSize) {
        TVPXP3::Chunk file;
        if (!TVPXP3::NextChunk(result.index, result.indexSize, at, file) ||
            !file.Is("File") || !TVPXP3::ValidateFile(file, archiveSize, 0)) return false;
        bool hasFilter = false, hasHash = false;
        size_t position = 0;
        while (position < file.size) {
            TVPXP3::Chunk part;
            if (!TVPXP3::NextChunk(file.data, file.size, position, part)) return false;
            if (part.Is("info")) {
                std::u16string name;
                uint16_t length = TVPXP3::Read16(part.data + 20);
                for (uint16_t i = 0; i < length; ++i) name.push_back(TVPXP3::Read16(part.data + 22 + i * 2));
                if (!ValidName(name) || !names.insert(name).second) return false;
            } else if (part.Is("hxky")) {
                Filter filter;
                if (hasFilter || !filter.Read(part.data, part.size)) return false;
                hasFilter = true;
            } else if (part.Is("hnam")) {
                if (hasHash || part.size != 40) return false;
                Lookup key;
                std::copy(part.data, part.data + 40, key.begin());
                if (!hashes.insert(key).second) return false;
                hasHash = true;
            }
        }
        if (!hasFilter || !hasHash || ++count > records) return false;
    }
    if (count != records) return false;
    out = result;
    return true;
}
} // namespace TVPHxv4
