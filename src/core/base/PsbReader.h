#pragma once

// Bounded reader for the unencrypted PSB v2/v3 data format. Resources remain
// ranges in the source file so inspecting scene/layer metadata needs no image
// allocation. This parser has no interpreter or executable-code support.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Psb {
constexpr size_t MaxMetadata = 16 * 1024 * 1024;
constexpr size_t MaxDecoded = 64 * 1024 * 1024;
constexpr size_t MaxItems = 1000000;
constexpr uint64_t MaxResource = 256 * 1024 * 1024;

struct Value {
    enum Kind { Null, Boolean, Integer, Real, String, Resource, Array, Object } kind = Null;
    int64_t integer = 0;
    double real = 0;
    std::string text;
    std::vector<std::string> keys;
    std::vector<Value> values;
    const Value *find(const std::string &key) const {
        if(kind != Object) return nullptr;
        for(size_t i = 0; i < keys.size(); ++i) if(keys[i] == key) return &values[i];
        return nullptr;
    }
};

struct Range { uint64_t offset, size; };
struct Document {
    Value root;
    std::vector<Range> resources;
    uint16_t version = 0;
};

class Reader {
    const uint8_t *data;
    size_t size;
    uint64_t fileSize;
    size_t nodes = 1, textBytes = 0, decodedBytes = sizeof(Value);
    uint32_t offsets[8] = {};
    std::vector<std::string> names;
    std::vector<uint32_t> strings;
    Document document;

    [[noreturn]] static void fail(const char *message) { throw std::runtime_error(message); }
    void allocate(size_t count, size_t width) {
        if(count > (MaxDecoded - decodedBytes) / width) fail("PSB: decoded data exceeds the limit");
        decodedBytes += count * width;
    }
    void children(size_t count) {
        if(count > MaxItems - nodes) fail("PSB: object tree exceeds the limit");
        nodes += count;
        allocate(count, sizeof(Value));
    }
    void check(size_t position, size_t length) const {
        if(position > size || length > size - position) fail("PSB: metadata range is outside the file");
    }
    uint64_t number(size_t &position, unsigned length) const {
        if(length > 8) fail("PSB: invalid integer width");
        check(position, length);
        uint64_t value = 0;
        for(unsigned i = 0; i < length; ++i) value |= uint64_t(data[position++]) << (8 * i);
        return value;
    }
    uint8_t byte(size_t &position) const { return static_cast<uint8_t>(number(position, 1)); }
    size_t relative(size_t base, uint64_t offset) const {
        if(base > size || offset > size - base) fail("PSB: invalid relative offset");
        return base + static_cast<size_t>(offset);
    }
    std::vector<uint32_t> table(size_t &position) {
        unsigned width = byte(position);
        if(width < 13 || width > 16) fail("PSB: invalid table count encoding");
        uint64_t count = number(position, width - 12);
        width = byte(position);
        if(width < 13 || width > 16 || count > MaxItems) fail("PSB: invalid table size");
        width -= 12;
        check(position, static_cast<size_t>(count) * width);
        allocate(static_cast<size_t>(count), sizeof(uint32_t));
        std::vector<uint32_t> result;
        result.reserve(static_cast<size_t>(count));
        for(uint64_t i = 0; i < count; ++i) result.push_back(static_cast<uint32_t>(number(position, width)));
        return result;
    }
    static void utf8(const std::string &text) {
        for(size_t i = 0; i < text.size();) {
            uint8_t ch = static_cast<uint8_t>(text[i++]);
            if(ch < 0x80) continue;
            unsigned remaining;
            uint32_t value, minimum;
            if(ch >= 0xc2 && ch <= 0xdf) { remaining = 1; value = ch & 31; minimum = 0x80; }
            else if(ch >= 0xe0 && ch <= 0xef) { remaining = 2; value = ch & 15; minimum = 0x800; }
            else if(ch >= 0xf0 && ch <= 0xf4) { remaining = 3; value = ch & 7; minimum = 0x10000; }
            else fail("PSB: invalid UTF-8");
            if(remaining > text.size() - i) fail("PSB: truncated UTF-8");
            while(remaining--) {
                ch = static_cast<uint8_t>(text[i++]);
                if((ch & 0xc0) != 0x80) fail("PSB: invalid UTF-8 continuation");
                value = (value << 6) | (ch & 63);
            }
            if(value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
                fail("PSB: invalid Unicode scalar");
        }
    }
    void accountText(const std::string &text) {
        if(text.size() > MaxMetadata || textBytes > MaxMetadata - text.size())
            fail("PSB: decoded text exceeds the limit");
        textBytes += text.size();
        allocate(text.size(), sizeof(char));
        utf8(text);
    }
    std::string stringAt(size_t position) {
        check(position, 1);
        size_t end = position;
        while(end < size && data[end]) ++end;
        if(end == size) fail("PSB: unterminated string");
        std::string text(reinterpret_cast<const char *>(data + position), end - position);
        accountText(text);
        return text;
    }
    void readNames() {
        size_t position = offsets[1];
        auto bases = table(position), parents = table(position), indexes = table(position);
        allocate(indexes.size(), sizeof(std::string));
        names.reserve(indexes.size());
        for(uint32_t index : indexes) {
            if(index >= parents.size()) fail("PSB: invalid key index");
            uint32_t node = parents[index];
            std::string text;
            while(node) {
                if(node >= parents.size() || text.size() >= 4096) fail("PSB: invalid or cyclic key trie");
                uint32_t parent = parents[node];
                if(parent >= bases.size() || node <= bases[parent] || node - bases[parent] > 255)
                    fail("PSB: invalid key character");
                text.push_back(static_cast<char>(node - bases[parent]));
                node = parent;
            }
            std::reverse(text.begin(), text.end());
            accountText(text);
            names.push_back(std::move(text));
        }
    }
    Value value(size_t position, unsigned depth) {
        if(depth > 128) fail("PSB: object tree exceeds the limit");
        uint8_t type = byte(position);
        Value result;
        if(type == 1) return result;
        if(type == 2 || type == 3) { result.kind = Value::Boolean; result.integer = type == 3; }
        else if(type >= 4 && type <= 12) {
            result.kind = Value::Integer;
            unsigned width = type - 4;
            uint64_t bits = number(position, width);
            if(width && width < 8 && (bits & (uint64_t(1) << (width * 8 - 1)))) bits |= ~uint64_t(0) << (width * 8);
            std::memcpy(&result.integer, &bits, sizeof(bits));
        } else if(type >= 13 && type <= 16) {
            --position;
            auto values = table(position);
            result.kind = Value::Array;
            children(values.size());
            result.values.reserve(values.size());
            for(uint32_t item : values) {
                Value child; child.kind = Value::Integer; child.integer = item;
                result.values.push_back(std::move(child));
            }
        } else if(type >= 0x15 && type <= 0x18) {
            uint64_t index = number(position, type - 0x14);
            if(index >= strings.size()) fail("PSB: invalid string index");
            result.kind = Value::String;
            result.text = stringAt(relative(offsets[3], strings[static_cast<size_t>(index)]));
        } else if(type >= 0x19 && type <= 0x1c) {
            uint64_t index = number(position, type - 0x18);
            if(index >= document.resources.size()) fail("PSB: invalid resource index");
            result.kind = Value::Resource; result.integer = static_cast<int64_t>(index);
        } else if(type >= 0x1d && type <= 0x1f) {
            result.kind = Value::Real;
            if(type == 0x1e) {
                uint32_t bits = static_cast<uint32_t>(number(position, 4));
                float real; std::memcpy(&real, &bits, sizeof(bits)); result.real = real;
            } else if(type == 0x1f) {
                uint64_t bits = number(position, 8); std::memcpy(&result.real, &bits, sizeof(bits));
            }
        } else if(type == 0x20 || type == 0x21) {
            result.kind = type == 0x20 ? Value::Array : Value::Object;
            std::vector<uint32_t> keys;
            if(type == 0x21) keys = table(position);
            auto entries = table(position);
            if(type == 0x21 && keys.size() != entries.size()) fail("PSB: dictionary table lengths differ");
            children(entries.size());
            result.values.reserve(entries.size());
            if(type == 0x21) {
                allocate(keys.size(), sizeof(std::string));
                result.keys.reserve(keys.size());
            }
            for(size_t i = 0; i < entries.size(); ++i) {
                if(type == 0x21) {
                    if(keys[i] >= names.size()) fail("PSB: invalid dictionary key");
                    if(i && keys[i] <= keys[i - 1]) fail("PSB: dictionary keys are not unique and sorted");
                    accountText(names[keys[i]]);
                    result.keys.push_back(names[keys[i]]);
                }
                result.values.push_back(value(relative(position, entries[i]), depth + 1));
            }
        } else fail("PSB: unsupported value type");
        return result;
    }
public:
    static size_t metadataSize(const uint8_t *header, size_t available, uint64_t fileSize) {
        if(!header || available < 40 || std::memcmp(header, "PSB\0", 4)) fail("PSB: invalid header");
        unsigned version = header[4] | unsigned(header[5]) << 8;
        if(version != 2 && version != 3) fail("PSB: supported versions are 2 and 3");
        if(header[6] || header[7]) fail("PSB: encrypted PSB headers are unsupported");
        size_t minimum = version == 3 ? 44 : 40;
        if(available < minimum) fail("PSB: truncated header");
        uint32_t dataStart = 0;
        for(unsigned i = 0; i < 4; ++i) dataStart |= uint32_t(header[32 + i]) << (8 * i);
        uint64_t length = dataStart ? dataStart : fileSize;
        if(length < minimum || length > fileSize || length > MaxMetadata) fail("PSB: invalid metadata length");
        return static_cast<size_t>(length);
    }
    Reader(const uint8_t *data, size_t size, uint64_t fileSize) : data(data), size(size), fileSize(fileSize) {}
    Document read() {
        size_t metadata = metadataSize(data, size, fileSize);
        if(metadata > size) fail("PSB: truncated metadata");
        size = metadata;
        document.version = data[4];
        size_t position = 8;
        for(auto &offset : offsets) offset = static_cast<uint32_t>(number(position, 4));
        size_t minimum = document.version == 3 ? 44 : 40;
        if(offsets[0] < minimum || offsets[0] > size) fail("PSB: invalid header length");
        for(unsigned i : {1u, 2u, 3u, 4u, 5u, 7u}) {
            if(offsets[i] < offsets[0] || offsets[i] >= size) fail("PSB: invalid section offset");
        }
        readNames();
        position = offsets[2]; strings = table(position);
        position = offsets[4]; auto starts = table(position);
        position = offsets[5]; auto lengths = table(position);
        if(starts.size() != lengths.size()) fail("PSB: resource table lengths differ");
        allocate(starts.size(), sizeof(Range));
        document.resources.reserve(starts.size());
        for(size_t i = 0; i < starts.size(); ++i) {
            uint64_t start = uint64_t(offsets[6]) + starts[i];
            if(start < size || start > fileSize || lengths[i] > fileSize - start || lengths[i] > MaxResource)
                fail("PSB: resource range is outside the file");
            document.resources.push_back({start, lengths[i]});
        }
        document.root = value(offsets[7], 0);
        if(document.root.kind != Value::Object) fail("PSB: root must be a dictionary");
        return std::move(document);
    }
};
} // namespace Psb
