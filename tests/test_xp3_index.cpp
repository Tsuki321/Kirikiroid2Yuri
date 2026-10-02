#include <gtest/gtest.h>
#include "XP3IndexValidator.h"
#include <random>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;
using TVPXP3::IndexStatus;

void Put(Bytes &data, uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        data.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
void Set(Bytes &data, size_t at, uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        data.at(at + i) = static_cast<uint8_t>(value >> (8 * i));
}
void Add(Bytes &data, const Bytes &part) { data.insert(data.end(), part.begin(), part.end()); }
Bytes Chunk(const char *tag, const Bytes &payload) {
    Bytes data(tag, tag + 4);
    Put(data, payload.size(), 8);
    Add(data, payload);
    return data;
}
struct File {
    Bytes info, segments, checksum;
    File() {
        Put(info, 0, 4);
        Put(info, 4, 8);
        Put(info, 4, 8);
        Put(info, 1, 2);
        Put(info, 'x', 2);
        Put(segments, 0, 4);
        Put(segments, 64, 8);
        Put(segments, 4, 8);
        Put(segments, 4, 8);
        Put(checksum, 1, 4);
    }
    Bytes Index() const {
        Bytes parts;
        Add(parts, Chunk("info", info));
        Add(parts, Chunk("segm", segments));
        Add(parts, Chunk("adlr", checksum));
        return Chunk("File", parts);
    }
};
IndexStatus Validate(const Bytes &data, uint64_t size = 1024, uint64_t offset = 0) {
    return TVPXP3::ValidateIndex(data.data(), data.size(), size, offset);
}
} // namespace

TEST(XP3Index, AcceptsRawCompressedAndEmptyFiles) {
    File file;
    EXPECT_EQ(IndexStatus::Valid, Validate(file.Index()));
    Set(file.info, 12, 2, 8);
    Set(file.segments, 0, 1, 4);
    Set(file.segments, 20, 2, 8);
    EXPECT_EQ(IndexStatus::Valid, Validate(file.Index()));
    file = File();
    Set(file.info, 4, 0, 8);
    Set(file.info, 12, 0, 8);
    Set(file.segments, 12, 0, 8);
    Set(file.segments, 20, 0, 8);
    EXPECT_EQ(IndexStatus::Valid, Validate(file.Index()));
}

TEST(XP3Index, AllowsEmptyContinuationIndicesAndUnknownChunks) {
    EXPECT_EQ(IndexStatus::Valid, TVPXP3::ValidateIndex(nullptr, 0, 1024));
    Bytes data = Chunk("note", {1, 2, 3});
    Add(data, File().Index());
    EXPECT_EQ(IndexStatus::Valid, Validate(data));
    EXPECT_EQ(IndexStatus::Malformed, TVPXP3::ValidateIndex(nullptr, 1, 1024));
}

TEST(XP3Index, RejectsEveryTruncatedFilePrefix) {
    auto data = File().Index();
    for (size_t size = 1; size < data.size(); ++size)
        EXPECT_EQ(IndexStatus::Malformed, TVPXP3::ValidateIndex(data.data(), size, 1024)) << size;
}

TEST(XP3Index, RejectsChunksThatOverflowTheirParent) {
    auto data = File().Index();
    Set(data, 4, UINT64_MAX, 8);
    EXPECT_EQ(IndexStatus::Malformed, Validate(data));
    data = File().Index();
    Set(data, 16, data.size(), 8);
    EXPECT_EQ(IndexStatus::Malformed, Validate(data));
    data = File().Index();
    data.push_back(0);
    EXPECT_EQ(IndexStatus::Malformed, Validate(data));
}

TEST(XP3Index, RejectsMissingShortAndDuplicateMetadata) {
    File file;
    file.info.resize(21);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    file = File();
    file.segments.resize(27);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    file = File();
    file.checksum.resize(3);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    file = File();
    file.segments.clear();
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    EXPECT_EQ(IndexStatus::Malformed, Validate(Chunk("File", Chunk("info", File().info))));
    file = File();
    Bytes parts = Chunk("info", file.info);
    Add(parts, Chunk("info", file.info));
    Add(parts, Chunk("segm", file.segments));
    Add(parts, Chunk("adlr", file.checksum));
    EXPECT_EQ(IndexStatus::Malformed, Validate(Chunk("File", parts)));
}

TEST(XP3Index, ValidatesUnsignedUtf16NameLengthsWithoutUnalignedReads) {
    File file;
    Set(file.info, 20, 2, 2);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    Set(file.info, 20, 0, 2);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    file = File();
    Set(file.info, 22, 0, 2);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    file = File();
    Set(file.info, 20, 32768, 2);
    for (unsigned i = 1; i < 32768; ++i)
        Put(file.info, 'a', 2);
    Bytes index = Chunk("note", {0});
    Add(index, file.Index());
    EXPECT_EQ(IndexStatus::Valid, Validate(index));
}

TEST(XP3Index, ChecksSegmentRangesIncludingEmbeddedArchives) {
    File file;
    EXPECT_EQ(IndexStatus::Valid, Validate(file.Index(), 68));
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index(), 67));
    EXPECT_EQ(IndexStatus::Valid, Validate(file.Index(), 168, 100));
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index(), 167, 100));
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index(), 100, 101));
    Set(file.segments, 4, UINT64_MAX - 1, 8);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    EXPECT_FALSE(TVPXP3::Contains(UINT64_MAX, UINT64_MAX - 1, 4));
}

TEST(XP3Index, ChecksSegmentLengthsMethodsAndTotals) {
    File file;
    Set(file.segments, 0, 2, 4);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    file = File();
    Set(file.info, 12, 3, 8);
    Set(file.segments, 20, 3, 8);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    file = File();
    Set(file.info, 4, 5, 8);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    file = File();
    Set(file.info, 12, 5, 8);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    file = File();
    Set(file.segments, 12, UINT64_MAX, 8);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
}

TEST(XP3Index, AcceptsSharedSegmentsAndRejectsOverflowingTotals) {
    File file;
    Bytes segment = file.segments;
    Add(file.segments, segment);
    Set(file.info, 4, 8, 8);
    Set(file.info, 12, 8, 8);
    EXPECT_EQ(IndexStatus::Valid, Validate(file.Index()));
    Set(file.info, 4, 4, 8);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
}

TEST(XP3Index, RejectsCompressedSegmentsThatTruncateBufferSizes) {
    File file;
    Set(file.segments, 0, 1, 4);
    Set(file.info, 4, uint64_t(UINT32_MAX) + 1, 8);
    Set(file.segments, 12, uint64_t(UINT32_MAX) + 1, 8);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
    file = File();
    Set(file.segments, 0, 1, 4);
    Set(file.info, 12, 0, 8);
    Set(file.segments, 20, 0, 8);
    EXPECT_EQ(IndexStatus::Malformed, Validate(file.Index()));
}

TEST(XP3Index, DistinguishesUnsupportedNameTablesFromOrdinaryProtectionFlags) {
    File file;
    Set(file.info, 0, 0x80000000, 4);
    EXPECT_EQ(IndexStatus::Valid, Validate(file.Index()));
    Bytes index = Chunk("Hxv4", {0, 0, 0, 0});
    Add(index, file.Index());
    EXPECT_EQ(IndexStatus::UnsupportedNameTable, Validate(index));
    Set(file.info, 4, 123, 8); // variant metadata need not follow standard segment totals
    index = Chunk("Hxv4", {0, 0, 0, 0});
    Add(index, file.Index());
    EXPECT_EQ(IndexStatus::UnsupportedNameTable, Validate(index));
}

TEST(XP3Index, BoundsArbitraryAndMutatedInputsUnderSanitizers) {
    std::mt19937 random(0x585033);
    const auto valid = File().Index();
    for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        Bytes bytes = iteration % 2 ? valid : Bytes(random() % 512);
        for (unsigned edit = 0; edit < 8 && !bytes.empty(); ++edit)
            bytes[random() % bytes.size()] = static_cast<uint8_t>(random());
        (void)Validate(bytes);
    }
    EXPECT_EQ(IndexStatus::Malformed,
              TVPXP3::ValidateIndex(valid.data(), TVPXP3::MaxIndexBytes + 1, 1024));
}
