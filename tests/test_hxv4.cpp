#include <gtest/gtest.h>
#include "Hxv4Index.h"
#include <fstream>
#include <iterator>
#include <zlib.h>

namespace {
using Bytes = std::vector<uint8_t>;

Bytes Load(const std::string &name) {
    std::ifstream stream(std::string(HXV4_FIXTURE_DIR) + "/" + name, std::ios::binary);
    if (!stream) throw std::runtime_error("missing synthetic Hxv4 fixture");
    return Bytes(std::istreambuf_iterator<char>(stream), {});
}

std::string Hex(const TVPHxv4::Digest &digest) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}

Bytes Inflate(const uint8_t *data, size_t size, size_t expected) {
    Bytes out(expected);
    uLongf length = static_cast<uLongf>(expected);
    if (uncompress(out.data(), &length, data, static_cast<uLong>(size)) != Z_OK || length != expected)
        throw std::runtime_error("fixture decompression failed");
    return out;
}

struct Fixture {
    Bytes archive, sidecar, index;
    TVPHxv4::Digest digest;
    TVPHxv4::Companion companion;

    explicit Fixture(const std::string &name) : archive(Load(name)), sidecar(Load(name + ".hxidx")) {
        size_t offset = static_cast<size_t>(TVPXP3::Read64(archive.data() + 11));
        if (archive[offset] == 128 && TVPXP3::Read64(archive.data() + offset + 1) == 0)
            offset = static_cast<size_t>(TVPXP3::Read64(archive.data() + offset + 9));
        auto size = TVPXP3::Read64(archive.data() + offset + 1);
        if (archive[offset] == 1)
            index = Inflate(archive.data() + offset + 17, size, TVPXP3::Read64(archive.data() + offset + 9));
        else index.assign(archive.begin() + offset + 9, archive.begin() + offset + 9 + size);
        Bytes binding(index);
        size_t at = 0;
        TVPXP3::Chunk chunk;
        while (TVPXP3::NextChunk(index.data(), index.size(), at, chunk)) {
            if (!chunk.Is("Hxv4")) continue;
            auto start = TVPXP3::Read64(chunk.data);
            auto length = TVPHxv4::Read32(chunk.data + 8);
            binding.insert(binding.end(), archive.begin() + start, archive.begin() + start + length);
        }
        digest = TVPHxv4::Blake2s(binding.data(), binding.size());
    }

    bool Read() {
        return TVPHxv4::ReadCompanion(sidecar.data(), sidecar.size(), archive.size(), digest, companion);
    }

    void Rehash() {
        auto hash = TVPHxv4::Blake2s(sidecar.data() + TVPHxv4::HeaderSize, sidecar.size() - TVPHxv4::HeaderSize);
        std::copy(hash.begin(), hash.end(), sidecar.begin() + 48);
    }
};
}

TEST(Hxv4, Blake2sMatchesPublishedAndMultiblockVectors) {
    EXPECT_EQ("69217a3079908094e11121d042354a7c1f55b6482ca1a51e1b250dfd1ed0eef9",
              Hex(TVPHxv4::Blake2s(nullptr, 0)));
    const uint8_t abc[] = {'a', 'b', 'c'};
    EXPECT_EQ("508c5e8c327c14e2e1a72ba34eeb452f37458b209ed63a294d999b4c86675982",
              Hex(TVPHxv4::Blake2s(abc, 3)));
    // hashlib.blake2s independently generates these boundary-length answers.
    Bytes sequence(257);
    for (size_t i = 0; i < sequence.size(); ++i) sequence[i] = static_cast<uint8_t>(i);
    EXPECT_EQ("56f34e8b96557e90c1f24b52d0c89d51086acf1b00f634cf1dde9233b8eaaa3e", Hex(TVPHxv4::Blake2s(sequence.data(), 64)));
    EXPECT_EQ("1b53ee94aaf34e4b159d48de352c7f0661d0a40edff95a0b1639b4090e974472", Hex(TVPHxv4::Blake2s(sequence.data(), 65)));
    EXPECT_EQ("5fdeb59f681d975f52c8e69c5502e02a12a3afcc5836ba58f42784c439228781", Hex(TVPHxv4::Blake2s(sequence.data(), 256)));
    EXPECT_EQ("fc7d6075f3d37faf3392507b99fa53715942ed3c64f170da6a76e9f44b633ccb", Hex(TVPHxv4::Blake2s(sequence.data() + 1, 256)));
}

TEST(Hxv4, SipHashMatchesReferenceVectors) {
    const uint64_t key0 = 0x0706050403020100ULL, key1 = 0x0f0e0d0c0b0a0908ULL;
    uint8_t data[16];
    for (unsigned i = 0; i < 16; ++i) data[i] = i;
    EXPECT_EQ(0x726fdb47dd0e0e31ULL, TVPHxv4::SipHash24(data, 0, key0, key1));
    EXPECT_EQ(0x74f839c593dc67fdULL, TVPHxv4::SipHash24(data, 1, key0, key1));
    EXPECT_EQ(0x93f5f5799a932462ULL, TVPHxv4::SipHash24(data, 8, key0, key1));
    EXPECT_EQ(0xa129ca6149be45e5ULL, TVPHxv4::SipHash24(data, 15, key0, key1));
}

TEST(Hxv4, CompanionDecodesOriginalContentAcrossSegmentsAndReadBoundaries) {
    for (const std::string kind : {"raw", "compressed", "multisegment", "chained", "startup"}) {
        SCOPED_TRACE(kind);
        Fixture fixture("hxv4-" + kind + ".xp3");
        ASSERT_TRUE(fixture.Read());
        EXPECT_EQ(u"xp3hnp", fixture.companion.media);
        size_t at = 0;
        unsigned count = 0;
        while (at < fixture.companion.indexSize) {
            TVPXP3::Chunk file, part;
            ASSERT_TRUE(TVPXP3::NextChunk(fixture.companion.index, fixture.companion.indexSize, at, file));
            TVPHxv4::Filter filter;
            TVPHxv4::Lookup lookup;
            Bytes raw;
            uint32_t adler = 0;
            size_t position = 0;
            while (position < file.size) {
                ASSERT_TRUE(TVPXP3::NextChunk(file.data, file.size, position, part));
                if (part.Is("hxky")) ASSERT_TRUE(filter.Read(part.data, part.size));
                if (part.Is("hnam")) std::copy(part.data, part.data + 40, lookup.begin());
                if (part.Is("adlr")) adler = TVPHxv4::Read32(part.data);
                if (part.Is("segm")) {
                    for (size_t s = 0; s < part.size; s += 28) {
                        const uint8_t *p = part.data + s;
                        auto start = TVPXP3::Read64(p + 4), original = TVPXP3::Read64(p + 12), stored = TVPXP3::Read64(p + 20);
                        Bytes data;
                        if (TVPHxv4::Read32(p) == 1) data = Inflate(fixture.archive.data() + start, stored, original);
                        else data.assign(fixture.archive.begin() + start, fixture.archive.begin() + start + stored);
                        raw.insert(raw.end(), data.begin(), data.end());
                    }
                }
            }
            const auto wanted = kind == "startup" ? u"startup.tjs" : count ? u"nested/dynamic.txt" : u"hello.txt";
            EXPECT_EQ(TVPHxv4::HashName(wanted, u"xp3hnp"), lookup);
            Bytes full = raw;
            filter.Apply(0, full.data(), full.size());
            EXPECT_EQ(adler, adler32(1, full.data(), static_cast<uInt>(full.size())));
            EXPECT_NE(full, raw);
            // Arbitrary partial reads, including split and correction bytes,
            // must agree with decoding the entire stream.
            for (size_t begin = 0; begin <= raw.size(); ++begin) {
                Bytes piece(raw.begin() + begin, raw.end());
                filter.Apply(begin, piece.data(), piece.size());
                EXPECT_TRUE(std::equal(piece.begin(), piece.end(), full.begin() + begin));
            }
            Bytes bytewise = raw;
            for (size_t i = 0; i < bytewise.size(); ++i) filter.Apply(i, bytewise.data() + i, 1);
            EXPECT_EQ(full, bytewise);
            ++count;
        }
        EXPECT_EQ(fixture.companion.records, count);
    }
}

TEST(Hxv4, RejectsTruncatedCorruptAndMismatchedCompanions) {
    Fixture good("hxv4-raw.xp3");
    ASSERT_TRUE(good.Read());
    TVPHxv4::Companion out;
    EXPECT_FALSE(TVPHxv4::ReadCompanion(nullptr, 100, good.archive.size(), good.digest, out));
    for (size_t length = 0; length < good.sidecar.size(); ++length)
        EXPECT_FALSE(TVPHxv4::ReadCompanion(good.sidecar.data(), length, good.archive.size(), good.digest, out));
    EXPECT_FALSE(TVPHxv4::ReadCompanion(good.sidecar.data(), good.sidecar.size(), good.archive.size() + 1, good.digest, out));
    for (const auto *kind : {"truncated", "mismatched", "corrupt"}) {
        Fixture bad(std::string("hxv4-") + kind + ".xp3");
        EXPECT_FALSE(bad.Read());
    }
}

TEST(Hxv4, RejectsMalformedRecordsEvenWithRecomputedCompanionDigest) {
    // Mutation checks exercise validation independently of the body checksum.
    Fixture original("hxv4-raw.xp3");
    ASSERT_TRUE(original.Read());
    size_t at = 0, position = 0;
    TVPXP3::Chunk file, part;
    ASSERT_TRUE(TVPXP3::NextChunk(original.companion.index, original.companion.indexSize, at, file));
    std::vector<std::pair<size_t, uint8_t>> mutations;
    while (position < file.size) {
        ASSERT_TRUE(TVPXP3::NextChunk(file.data, file.size, position, part));
        size_t base = static_cast<size_t>(part.data - original.sidecar.data());
        if (part.Is("info")) {
            mutations.push_back({base + 22, '/'});
            mutations.push_back({base + 22, 0});
            mutations.push_back({base + 4, 255});
            mutations.push_back({base + 20, 255});
        }
        if (part.Is("segm")) { mutations.push_back({base, 7}); mutations.push_back({base + 11, 255}); }
        if (part.Is("hxky")) mutations.push_back({base, 2});
    }
    for (const auto &mutation : mutations) {
        Fixture changed("hxv4-raw.xp3");
        changed.sidecar[mutation.first] = mutation.second;
        changed.Rehash();
        EXPECT_FALSE(changed.Read()) << mutation.first;
    }
}

TEST(Hxv4, FilterHandlesEmptyAndVeryLargeOffsetsWithoutWrapping) {
    TVPHxv4::Filter filter;
    filter.active = true;
    filter.split = UINT32_MAX;
    filter.left = 0x11;
    filter.right = 0x22;
    uint8_t value = 3;
    filter.Apply(UINT64_MAX, &value, 1);
    EXPECT_EQ(3 ^ 0x22, value);
    filter.Apply(0, nullptr, 0);
    EXPECT_FALSE(TVPHxv4::ValidName(u"../startup.tjs"));
    EXPECT_FALSE(TVPHxv4::ValidName(u"a//b"));
    EXPECT_TRUE(TVPHxv4::ValidName(u"system/initialize.tjs"));
}
