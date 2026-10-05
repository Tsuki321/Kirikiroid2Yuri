#pragma once

// A bounded, data-only reader for the TJS/ns0 and TJS/4s0 containers. It does
// not evaluate scripts. See docs/DATAPACK.md for the wire-format references.
#include "Hxv4Index.h"
#include <cstring>
#include <stdexcept>
#include <utility>

namespace DataPack {
constexpr size_t MaxBytes = 64 * 1024 * 1024;
constexpr size_t MaxValues = 500000;
constexpr unsigned MaxDepth = 128;

struct Value {
    enum Kind { Void, Null, String, Octet, Integer, Real, Array, Object } kind = Void;
    int64_t integer = 0;
    double real = 0;
    std::u16string text;
    std::vector<uint8_t> bytes;
    std::vector<std::u16string> keys;
    std::vector<Value> values;
};

[[noreturn]] inline void fail(const char *message) { throw std::runtime_error(message); }
inline uint32_t rotate(uint32_t value, unsigned count) {
    return (value << count) | (value >> (32 - count));
}
inline uint32_t read32(const uint8_t *data) { return TVPHxv4::Read32(data); }

// XXH32 is the container's nonce derivation, not an authenticity check.
inline uint32_t hash32(const uint8_t *data, size_t size, uint32_t seed) {
    constexpr uint32_t p1 = 2654435761U, p2 = 2246822519U, p3 = 3266489917U;
    constexpr uint32_t p4 = 668265263U, p5 = 374761393U;
    auto round = [&](uint32_t value, uint32_t word) { return rotate(value + word * p2, 13) * p1; };
    size_t pos = 0;
    uint32_t hash;
    if(size >= 16) {
        uint32_t v[4] = {seed + p1 + p2, seed + p2, seed, seed - p1};
        do {
            for(auto &word : v) { word = round(word, read32(data + pos)); pos += 4; }
        } while(size - pos >= 16);
        hash = rotate(v[0], 1) + rotate(v[1], 7) + rotate(v[2], 12) + rotate(v[3], 18);
    } else hash = seed + p5;
    hash += static_cast<uint32_t>(size);
    while(size - pos >= 4) { hash = rotate(hash + read32(data + pos) * p3, 17) * p4; pos += 4; }
    while(pos < size) hash = rotate(hash + data[pos++] * p5, 11) * p1;
    hash ^= hash >> 15; hash *= p2; hash ^= hash >> 13; hash *= p3;
    return hash ^ (hash >> 16);
}

inline void crypt(std::vector<uint8_t> &data, uint32_t seed,
                  const std::vector<uint8_t> &iv, unsigned mode) {
    if(mode < 1 || mode > 6) fail("DataPack: unsupported cipher mode");
    if(iv.size() > 65535) fail("DataPack: IV is too long");
    const unsigned rounds[] = {8, 12, 20, 8, 12, 20};
    const unsigned batches[] = {16, 8, 4, 1, 1, 1};
    std::vector<uint8_t> keyed(64, 0);
    for(unsigned i = 0; i < 4; ++i) keyed[i] = uint8_t(seed >> (i * 8));
    keyed.insert(keyed.end(), iv.begin(), iv.end());
    auto key = TVPHxv4::Blake2s(keyed.data(), keyed.size(), 4);
    uint32_t initial[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for(unsigned i = 0; i < 8; ++i) initial[4 + i] = read32(key.data() + i * 4);
    initial[14] = hash32(iv.data(), iv.size(), seed);
    initial[15] = seed;
    uint32_t fallback = seed ^ initial[14];
    if(!fallback) fallback = seed ? seed : UINT32_MAX;
    size_t pos = 0;
    while(pos < data.size()) {
        uint32_t words[16]; std::copy(initial, initial + 16, words);
        auto quarter = [&](unsigned a, unsigned b, unsigned c, unsigned d) {
            words[a] += words[b]; words[d] = rotate(words[d] ^ words[a], 16);
            words[c] += words[d]; words[b] = rotate(words[b] ^ words[c], 12);
            words[a] += words[b]; words[d] = rotate(words[d] ^ words[a], 8);
            words[c] += words[d]; words[b] = rotate(words[b] ^ words[c], 7);
        };
        for(unsigned r = 0; r < rounds[mode - 1]; r += 2) {
            quarter(0,4,8,12); quarter(1,5,9,13); quarter(2,6,10,14); quarter(3,7,11,15);
            quarter(0,5,10,15); quarter(1,6,11,12); quarter(2,7,8,13); quarter(3,4,9,14);
        }
        for(unsigned i = 0; i < 16; ++i) words[i] += initial[i];
        for(unsigned b = 0; b < batches[mode - 1] && pos < data.size(); ++b) {
            if(b) for(auto &word : words) {
                word ^= word << 13; word ^= word >> 17; word ^= word << 5;
                if(!word) word = fallback;
            }
            for(unsigned i = 0; i < 64 && pos < data.size(); ++i, ++pos)
                data[pos] ^= uint8_t(words[i / 4] >> ((i % 4) * 8));
        }
        if(++initial[12] == 0) ++initial[13];
    }
}

inline std::vector<uint8_t> inflate(const std::vector<uint8_t> &input) {
    std::vector<uint8_t> output;
    size_t pos = 0;
    while(pos < input.size()) {
        if(input.size() - pos < 2) fail("DataPack: truncated LZ4 frame");
        size_t length = input[pos] | size_t(input[pos + 1]) << 8; pos += 2;
        if(!length || length > 4128 || length > input.size() - pos) fail("DataPack: invalid LZ4 frame size");
        size_t end = pos + length, start = output.size();
        auto extend = [&](size_t count) {
            if(count > 4096 - (output.size() - start) || count > MaxBytes - output.size())
                fail("DataPack: LZ4 output limit exceeded");
        };
        auto extra = [&](size_t count, size_t sentinel) {
            if(count == sentinel) {
                uint8_t byte;
                do {
                    if(pos == end) fail("DataPack: truncated LZ4 length");
                    byte = input[pos++]; count += byte;
                    if(count > 4096) fail("DataPack: LZ4 run is too long");
                } while(byte == 255);
            }
            return count;
        };
        while(pos < end) {
            uint8_t token = input[pos++];
            size_t count = extra(token >> 4, 15);
            if(count > end - pos) fail("DataPack: truncated LZ4 literal");
            extend(count);
            output.insert(output.end(), input.begin() + pos, input.begin() + pos + count); pos += count;
            if(pos == end) break;
            if(end - pos < 2) fail("DataPack: truncated LZ4 offset");
            size_t distance = input[pos] | size_t(input[pos + 1]) << 8; pos += 2;
            if(!distance || distance > output.size()) fail("DataPack: invalid LZ4 offset");
            count = extra((token & 15) + 4, 19); extend(count);
            while(count--) output.push_back(output[output.size() - distance]);
        }
        if(output.size() == start) fail("DataPack: empty LZ4 frame");
    }
    return output;
}

class Reader {
    const std::vector<uint8_t> &data;
    size_t pos = 0, values = 0, allocated = sizeof(Value);
    bool big;
    uint8_t check[3];
    void budget(size_t count, size_t width) {
        if(count > (MaxBytes - allocated) / width) fail("DataPack: decoded allocation limit exceeded");
        allocated += count * width;
    }
    void advance() {
        uint8_t a = uint8_t(check[0] ^ uint8_t(check[0] * 2));
        uint8_t b = uint8_t((a >> 2) ^ check[2]);
        b = uint8_t((b >> 3) ^ check[2] ^ a);
        check[0] = check[1]; check[1] = check[2]; check[2] = b;
    }
    uint64_t number(size_t width) {
        if(width > data.size() - pos) fail("DataPack: truncated value");
        uint64_t value = 0;
        for(size_t i = 0; i < width; ++i) value |= uint64_t(data[pos++]) << ((big ? width - 1 - i : i) * 8);
        return value;
    }
    std::u16string string() {
        size_t count = static_cast<size_t>(number(4));
        if(count > (data.size() - pos) / 2) fail("DataPack: truncated UTF-16 string");
        budget(count, 2);
        std::u16string text; text.reserve(count);
        // Strings keep UTF-16LE code units even in a big-endian container.
        while(count--) { text.push_back(char16_t(data[pos] | uint16_t(data[pos + 1]) << 8)); pos += 2; }
        return text;
    }
    Value value(unsigned depth) {
        if(depth > MaxDepth || ++values > MaxValues) fail("DataPack: value nesting/count limit exceeded");
        unsigned tag = static_cast<unsigned>(number(2)), type = tag & 255;
        if(type) advance();
        if((tag >> 8) != check[2]) fail("DataPack: value checksum mismatch");
        Value result;
        switch(type) {
        case 0: break;
        case 1: result.kind = Value::Null; break;
        case 2: result.kind = Value::String; result.text = string(); break;
        case 3: {
            size_t count = static_cast<size_t>(number(4));
            if(count > data.size() - pos) fail("DataPack: truncated octet");
            budget(count, 1); result.kind = Value::Octet;
            result.bytes.assign(data.begin() + pos, data.begin() + pos + count); pos += count;
            break;
        }
        case 4: {
            uint64_t bits = number(8); result.kind = Value::Integer;
            std::memcpy(&result.integer, &bits, sizeof(bits)); break;
        }
        case 5: {
            uint64_t bits = number(8); result.kind = Value::Real;
            std::memcpy(&result.real, &bits, sizeof(bits)); break;
        }
        case 0x81: case 0xc1: {
            size_t count = static_cast<size_t>(number(4));
            if(count > MaxValues - values || count > (data.size() - pos) / (type == 0x81 ? 2 : 6))
                fail("DataPack: invalid container count");
            budget(count, sizeof(Value) + (type == 0xc1 ? sizeof(std::u16string) : 0));
            result.kind = type == 0x81 ? Value::Array : Value::Object;
            result.values.reserve(count);
            if(type == 0xc1) result.keys.reserve(count);
            for(size_t i = 0; i < count; ++i) {
                if(type == 0xc1) result.keys.push_back(string());
                result.values.push_back(value(depth + 1));
            }
            break;
        }
        default: fail("DataPack: unknown value type");
        }
        return result;
    }
public:
    Reader(const std::vector<uint8_t> &bytes, uint32_t seed, bool bigEndian)
        : data(bytes), big(bigEndian), check{uint8_t(seed ^ (seed >> 24)), uint8_t(seed >> 8), uint8_t(seed >> 16)} {}
    Value read() {
        auto result = value(0);
        advance(); advance(); advance();
        uint32_t expected = check[2] | uint32_t(check[1]) << 8 | uint32_t(check[0]) << 16;
        if(number(4) != expected) fail("DataPack: final checksum mismatch");
        if(pos != data.size()) fail("DataPack: trailing bytes after value");
        return result;
    }
};

inline Value decode(const std::vector<uint8_t> &bytes, const std::vector<uint8_t> *outerIv = nullptr) {
    if(bytes.size() < 16 || bytes.size() > MaxBytes) fail("DataPack: invalid file size");
    bool big = bytes[3] == '\\';
    if(std::memcmp(bytes.data(), "TJS", 3) || (!big && bytes[3] != '/') ||
       bytes[5] != 's' || bytes[6] != '0' || bytes[7] != 0)
        fail("DataPack: unsupported file header");
    if(bytes[4] != 'n' && bytes[4] != '4') fail("DataPack: unsupported compression");
    auto field = [&](size_t at, size_t width) {
        uint32_t result = 0;
        for(size_t i = 0; i < width; ++i) result |= uint32_t(bytes[at + i]) << ((big ? width - 1 - i : i) * 8);
        return result;
    };
    uint32_t seed = field(8, 4), mode = field(12, 2), length = field(14, 2);
    if(length > bytes.size() - 16) fail("DataPack: truncated IV");
    std::vector<uint8_t> body(bytes.begin() + 16 + length, bytes.end());
    if(mode) {
        std::vector<uint8_t> iv(bytes.begin() + 16, bytes.begin() + 16 + length);
        crypt(body, seed, outerIv ? *outerIv : iv, mode);
    }
    if(bytes[4] == '4') body = inflate(body);
    return Reader(body, seed, big).read();
}
} // namespace DataPack
