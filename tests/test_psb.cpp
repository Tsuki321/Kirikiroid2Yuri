#include <gtest/gtest.h>
#include "PsbReader.h"
#include <fstream>
#include <iterator>

namespace {
std::vector<uint8_t> fixture(const char *name) {
    std::ifstream file(std::string(PSB_FIXTURE_DIR) + "/" + name, std::ios::binary);
    if(!file) throw std::runtime_error("missing PSB fixture");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
Psb::Document read(const std::vector<uint8_t> &bytes) {
    return Psb::Reader(bytes.data(), bytes.size(), bytes.size()).read();
}
void put32(std::vector<uint8_t> &bytes, size_t at, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<uint8_t>(value >> (i * 8));
}
uint32_t get32(const std::vector<uint8_t> &bytes, size_t at) {
    uint32_t result = 0;
    for(unsigned i = 0; i < 4; ++i) result |= uint32_t(bytes.at(at + i)) << (i * 8);
    return result;
}
std::vector<uint8_t> withValue(const std::vector<uint8_t> &value) {
    auto bytes = fixture("scene.psb");
    bytes.resize(get32(bytes, 36));
    // A dictionary with name 0 and one value. Its tables use one-byte fields.
    bytes.insert(bytes.end(), {0x21, 13, 1, 13, 0, 13, 1, 13, 0});
    bytes.insert(bytes.end(), value.begin(), value.end());
    put32(bytes, 32, static_cast<uint32_t>(bytes.size()));
    return bytes;
}
}

TEST(PSB, SceneMetadataUsesNativeTypesAndUnicode) {
    auto doc = read(fixture("scene.psb"));
    ASSERT_EQ(3, doc.version);
    const auto &root = doc.root;
    ASSERT_NE(nullptr, root.find("scenes"));
    ASSERT_EQ(1u, root.find("scenes")->values.size());
    EXPECT_EQ("intro", root.find("scenes")->values[0].find("label")->text);
    EXPECT_EQ(-1234567, root.find("negative")->integer);
    EXPECT_EQ(-(int64_t(1) << 55), root.find("wide")->integer);
    EXPECT_EQ(1.25, root.find("fraction")->real);
    EXPECT_EQ(1, root.find("enabled")->integer);
    EXPECT_EQ(0, root.find("disabled")->integer);
    EXPECT_EQ(Psb::Value::Null, root.find("empty")->kind);
    EXPECT_EQ(u8"日本語 😀", root.find("unicode")->text);
    EXPECT_EQ("key", root.find(u8"日本語😀")->text);
    EXPECT_EQ(std::numeric_limits<int64_t>::min(), root.find("min_integer")->integer);
    EXPECT_EQ(std::numeric_limits<int64_t>::max(), root.find("max_integer")->integer);
    EXPECT_EQ(1.5, root.find("single")->real);
    EXPECT_EQ(0.0, root.find("zero_real")->real);
    EXPECT_EQ(0xffffffffLL, root.find("numbers")->values[2].integer);
    EXPECT_TRUE(root.find("empty_array")->values.empty());
    EXPECT_TRUE(root.find("empty_object")->keys.empty());
    EXPECT_TRUE(doc.resources.empty());
}

TEST(PSB, ResourcesAreBoundedFileRangesAndMetadataCanBeReadAlone) {
    auto bytes = fixture("images.pimg");
    size_t metadata = Psb::Reader::metadataSize(bytes.data(), bytes.size(), bytes.size());
    auto doc = Psb::Reader(bytes.data(), metadata, bytes.size()).read();
    ASSERT_EQ(2, doc.version);
    ASSERT_EQ(3u, doc.resources.size());
    EXPECT_EQ(Psb::Value::Resource, doc.root.find("tile.png")->kind);
    EXPECT_EQ(0, doc.root.find("tile.png")->integer);
    EXPECT_EQ(0u, doc.resources[1].size);
    EXPECT_EQ(metadata, doc.resources[0].offset);
    EXPECT_EQ(0, std::memcmp(bytes.data() + doc.resources[0].offset, "\x89PNG", 4));
    EXPECT_EQ(0, std::memcmp(bytes.data() + doc.resources[2].offset, "TLG5.0", 6));
    EXPECT_THROW(Psb::Reader(bytes.data(), metadata, bytes.size()-1).read(), std::runtime_error);
}

TEST(PSB, TruncatedMetadataAndUnsupportedHeadersFail) {
    EXPECT_THROW(Psb::Reader(nullptr, 40, 40).read(), std::runtime_error);
    auto bytes = fixture("scene.psb");
    for(size_t size = 0; size < bytes.size(); ++size)
        EXPECT_THROW(Psb::Reader(bytes.data(), size, bytes.size()).read(), std::runtime_error) << size;
    bytes[4] = 4;
    EXPECT_THROW(read(bytes), std::runtime_error);
    bytes[4] = 3; bytes[6] = 1;
    EXPECT_THROW(read(bytes), std::runtime_error);
    EXPECT_THROW(read(fixture("invalid.psb")), std::runtime_error);
}

TEST(PSB, BadTextAndResourceReferencesFail) {
    const std::string unicode = u8"日本語 😀";
    for(const auto &invalid : std::vector<std::vector<uint8_t>>{
            {0xc0, 0x80}, {0xed, 0xa0, 0x80}, {0xf4, 0x90, 0x80, 0x80}, {0xe3, 0x28, 0x82}}) {
        auto bytes = fixture("scene.psb");
        auto start = std::search(bytes.begin(), bytes.end(), unicode.begin(), unicode.end(),
            [](uint8_t a, char b) { return a == static_cast<uint8_t>(b); });
        ASSERT_NE(bytes.end(), start);
        std::copy(invalid.begin(), invalid.end(), start);
        EXPECT_THROW(read(bytes), std::runtime_error);
    }
    EXPECT_THROW(read(withValue({0x19, 0})), std::runtime_error); // no resources in scene.psb
    EXPECT_THROW(read(withValue({0x15, 0xff})), std::runtime_error); // missing string index
}

TEST(PSB, NestedAndSharedContainersHaveAllocationBudgets) {
    std::vector<uint8_t> nested;
    for(unsigned depth = 0; depth < 150; ++depth)
        nested.insert(nested.end(), {0x20, 13, 1, 13, 0});
    nested.push_back(1);
    EXPECT_THROW(read(withValue(nested)), std::runtime_error);

    // Each child reuses the next table. A small file describes an exponential
    // object tree, so file bounds alone cannot constrain decoded allocation.
    std::vector<uint8_t> shared;
    for(unsigned depth = 0; depth < 24; ++depth)
        shared.insert(shared.end(), {0x20, 13, 2, 13, 0, 0});
    shared.push_back(1);
    EXPECT_THROW(read(withValue(shared)), std::runtime_error);

    // Reserve must be charged before descending into any child.
    std::vector<uint8_t> large = {0x20, 15, 0x40, 0x42, 0x0f, 13}; // 1,000,000 offsets
    large.resize(large.size() + 1000000, 0);
    large.push_back(1);
    EXPECT_THROW(read(withValue(large)), std::runtime_error);
}

TEST(PSB, OffsetAndTableMutationsAreBounded) {
    const auto original = fixture("scene.psb");
    for(size_t field = 8; field <= 36; field += 4) {
        auto bytes = original; put32(bytes, field, 0xffffffff);
        EXPECT_THROW(read(bytes), std::runtime_error);
    }
    uint32_t random = 0x7287abcd;
    for(unsigned iteration = 0; iteration < 4000; ++iteration) {
        auto bytes = original;
        random = random * 1664525 + 1013904223;
        size_t where = random % bytes.size();
        random = random * 1664525 + 1013904223;
        bytes[where] ^= static_cast<uint8_t>(random >> 24);
        try { auto result = read(bytes); EXPECT_EQ(Psb::Value::Object, result.root.kind); }
        catch(const std::runtime_error &) {}
    }
}
