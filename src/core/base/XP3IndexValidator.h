#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

// Validate decompressed XP3 indices before the engine interprets any fields.
// This stays independent of Android so malformed input can also run under
// address/undefined-behavior sanitizers without loading game data.
namespace TVPXP3 {
constexpr uint64_t MaxIndexBytes = 64u * 1024u * 1024u;
constexpr size_t MaxIndexCount = 1024;

enum class IndexStatus { Valid, Malformed, UnsupportedNameTable };

inline bool Contains(uint64_t total, uint64_t start, uint64_t size) {
    return start <= total && size <= total - start;
}

inline uint16_t Read16(const uint8_t *data) {
    return uint16_t(data[0]) | (uint16_t(data[1]) << 8);
}

inline uint64_t Read64(const uint8_t *data) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= uint64_t(data[i]) << (8 * i);
    return value;
}

struct Chunk {
    const uint8_t *tag = nullptr;
    const uint8_t *data = nullptr;
    size_t size = 0;
    bool Is(const char *name) const { return std::memcmp(tag, name, 4) == 0; }
};

inline bool NextChunk(const uint8_t *data, size_t size, size_t &position, Chunk &chunk) {
    if (!data || !Contains(size, position, 12))
        return false;
    uint64_t payload = Read64(data + position + 4);
    if (!Contains(size, position + 12, payload))
        return false;
    chunk.tag = data + position;
    chunk.data = data + position + 12;
    chunk.size = static_cast<size_t>(payload);
    position += 12 + chunk.size;
    return true;
}

inline bool ValidateFile(const Chunk &file, uint64_t archiveSize, uint64_t archiveOffset) {
    Chunk info, segments, checksum;
    size_t position = 0;
    while (position < file.size) {
        Chunk part;
        if (!NextChunk(file.data, file.size, position, part))
            return false;
        Chunk *known = part.Is("info") ? &info : part.Is("segm") ? &segments
                                                : part.Is("adlr") ? &checksum : nullptr;
        if (known) {
            if (known->data)
                return false;
            *known = part;
        }
    }
    if (info.size < 22 || !segments.data || !segments.size || segments.size % 28 ||
        checksum.size < 4 || archiveOffset > archiveSize)
        return false;
    uint16_t nameLength = Read16(info.data + 20);
    if (!nameLength || !Contains(info.size, 22, uint64_t(nameLength) * 2))
        return false;
    for (size_t i = 0; i < nameLength; ++i)
        if (!Read16(info.data + 22 + i * 2))
            return false; // embedded NUL would truncate the storage name

    uint64_t original = Read64(info.data + 4), packed = Read64(info.data + 12);
    if (original > uint64_t(std::numeric_limits<int64_t>::max()))
        return false; // stream seeks take signed offsets
    uint64_t originalTotal = 0, packedTotal = 0;
    for (size_t at = 0; at < segments.size; at += 28) {
        const uint8_t *segment = segments.data + at;
        unsigned method = segment[0] & 7;
        uint64_t start = Read64(segment + 4);
        uint64_t originalSize = Read64(segment + 12), packedSize = Read64(segment + 20);
        if (method > 1 || !Contains(archiveSize - archiveOffset, start, packedSize) ||
            !Contains(original, originalTotal, originalSize) ||
            !Contains(packed, packedTotal, packedSize))
            return false;
        if (method == 0 && originalSize != packedSize)
            return false;
        // Compressed segments are materialized using 32-bit buffer sizes.
        if (method == 1 && (!packedSize || originalSize > UINT32_MAX || packedSize > UINT32_MAX))
            return false;
        originalTotal += originalSize;
        packedTotal += packedSize;
    }
    return originalTotal == original && packedTotal == packed;
}

inline IndexStatus ValidateIndex(const uint8_t *data, size_t size, uint64_t archiveSize,
                                 uint64_t archiveOffset = 0) {
    if ((!data && size) || size > MaxIndexBytes || archiveOffset > archiveSize)
        return IndexStatus::Malformed;
    size_t position = 0;
    bool unsupportedNames = false;
    bool malformedFile = false;
    while (position < size) {
        Chunk chunk;
        if (!NextChunk(data, size, position, chunk))
            return IndexStatus::Malformed;
        if (chunk.Is("File") && !ValidateFile(chunk, archiveSize, archiveOffset))
            malformedFile = true;
        if (chunk.Is("Hxv4"))
            unsupportedNames = true;
    }
    // A known unsupported name-table variant may also transform ordinary File
    // fields. Do not misdiagnose its metadata as a corrupt standard archive.
    if (unsupportedNames)
        return IndexStatus::UnsupportedNameTable;
    return malformedFile ? IndexStatus::Malformed : IndexStatus::Valid;
}
} // namespace TVPXP3
