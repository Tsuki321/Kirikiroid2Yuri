#include <gtest/gtest.h>
#include "DataPackReader.h"
#include <fstream>
#include <iterator>

namespace {
std::vector<uint8_t> fixture(const std::string &name) {
    std::ifstream input(std::string(DATAPACK_FIXTURE_DIR) + "/" + name, std::ios::binary);
    if(!input) throw std::runtime_error("fixture missing");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}
void checkRoot(const DataPack::Value &root) {
    using V = DataPack::Value;
    ASSERT_EQ(root.kind, V::Object);
    ASSERT_EQ(root.keys.size(), 5u);
    EXPECT_EQ(root.keys[0], u"title");
    EXPECT_EQ(root.values[0].text, u"Synthetic metadata");
    EXPECT_EQ(root.values[1].text, u"\u65e5\u672c\u8a9e\U0001f600");
    const auto &items = root.values[2].values;
    ASSERT_EQ(items.size(), 8u);
    EXPECT_EQ(items[0].kind, V::Void); EXPECT_EQ(items[1].kind, V::Null);
    EXPECT_EQ(items[2].integer, -7); EXPECT_EQ(items[3].integer, INT64_C(1099511627776));
    EXPECT_DOUBLE_EQ(items[4].real, 3.25);
    EXPECT_EQ(items[5].kind, V::String); EXPECT_TRUE(items[5].text.empty());
    EXPECT_EQ(items[6].kind, V::Octet); EXPECT_TRUE(items[6].bytes.empty());
    ASSERT_EQ(items[7].bytes.size(), 256u);
    for(unsigned i = 0; i < 256; ++i) EXPECT_EQ(items[7].bytes[i], i);
    EXPECT_EQ(root.values[3].text.size(), 6600u);
    ASSERT_EQ(root.values[4].values.size(), 80u);
    EXPECT_EQ(root.values[4].values[79].values[1].integer, -79);
}
}

TEST(DataPack, StructuredValuesAndByteOrders) {
    for(const char *name : {"plain.pbd", "big.pbd", "compressed.pbd", "empty-iv.pbd"}) {
        SCOPED_TRACE(name); checkRoot(DataPack::decode(fixture(name)));
    }
    EXPECT_EQ(DataPack::decode(fixture("scalar.pbd")).integer, INT64_MIN);
}
TEST(DataPack, CipherModesCrossMultipleBatchesAndFrames) {
    for(int mode = 1; mode <= 6; ++mode) {
        SCOPED_TRACE(mode); checkRoot(DataPack::decode(fixture("cipher-" + std::to_string(mode) + ".pbd")));
    }
}
TEST(DataPack, ExternalIvIsRequiredWhenSpecifiedByProducer) {
    auto data = fixture("outer.pbd");
    EXPECT_THROW(DataPack::decode(data), std::runtime_error);
    std::string text = "\xe5\xa4\x96\xe9\x83\xa8-IV";
    std::vector<uint8_t> iv(text.begin(), text.end());
    checkRoot(DataPack::decode(data, &iv));
}
TEST(DataPack, RefusesTruncationMalformedHeadersAndTrailingData) {
    auto original = fixture("plain.pbd");
    for(size_t length : {size_t(0), size_t(8), size_t(15), size_t(16), size_t(17), original.size() / 2, original.size() - 1}) {
        auto data = original; data.resize(length);
        EXPECT_THROW(DataPack::decode(data), std::runtime_error);
    }
    for(size_t offset : {size_t(0), size_t(4), size_t(7), size_t(8), size_t(12), size_t(14), size_t(17), original.size() - 1}) {
        auto data = original; data[offset] ^= 0x80;
        EXPECT_THROW(DataPack::decode(data), std::runtime_error) << offset;
    }
    original.push_back(0);
    EXPECT_THROW(DataPack::decode(original), std::runtime_error);
}
TEST(DataPack, RejectsForgedCountsBeforeAllocating) {
    auto data = fixture("plain.pbd");
    std::fill(data.begin() + 18, data.begin() + 22, 0xff);
    EXPECT_THROW(DataPack::decode(data), std::runtime_error);
}
TEST(DataPack, Lz4OverlappingAndCrossFrameMatches) {
    std::vector<uint8_t> input;
    for(int frame = 0; frame < 2; ++frame) {
        std::vector<uint8_t> block;
        if(frame == 0) block = {0x1f, 'x', 1, 0};
        else block = {0x0f, 1, 0};
        size_t remaining = 4091 - (frame == 0 ? 1 : 0) - 19;
        while(remaining >= 255) { block.push_back(255); remaining -= 255; }
        block.push_back(static_cast<uint8_t>(remaining));
        block.push_back(0x50); block.insert(block.end(), 5, 'x');
        input.push_back(static_cast<uint8_t>(block.size())); input.push_back(0);
        input.insert(input.end(), block.begin(), block.end());
    }
    EXPECT_EQ(DataPack::inflate(input), std::vector<uint8_t>(8192, 'x'));
    for(const auto &bad : std::vector<std::vector<uint8_t>>{{1}, {0,0}, {4,0,0,0,0,0}, {3,0,0,1,0}, {2,0,0xf0,255}})
        EXPECT_THROW(DataPack::inflate(bad), std::runtime_error);
}
TEST(DataPack, HashVectors) {
    EXPECT_EQ(DataPack::hash32(nullptr, 0, 0), 0x02cc5d05U);
    const uint8_t abc[] = {'a', 'b', 'c'};
    EXPECT_EQ(DataPack::hash32(abc, 3, 0), 0x32d153ffU);
}
