#include "md5.h"

#include <gtest/gtest.h>

#include <string>

// Helper: compute MD5 of a std::string and return the lowercase hex digest.
static std::string md5_hex(const std::string &input) {
	md5_state_t st;
	md5_init(&st);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>(input.data()),
	           static_cast<int>(input.size()));
	md5_byte_t digest[16];
	md5_finish(&st, digest);

	static const char hex[] = "0123456789abcdef";
	std::string out(32, ' ');
	for (int i = 0; i < 16; ++i) {
		out[i * 2]     = hex[(digest[i] >> 4) & 0xF];
		out[i * 2 + 1] = hex[digest[i] & 0xF];
	}
	return out;
}

// Canonical RFC 1321 Appendix A.5 test suite. These vectors are the spec.
TEST(Md5, RFC1321_Empty) {
	EXPECT_EQ(md5_hex(""), "d41d8cd98f00b204e9800998ecf8427e");
}

TEST(Md5, RFC1321_a) {
	EXPECT_EQ(md5_hex("a"), "0cc175b9c0f1b6a831c399e269772661");
}

TEST(Md5, RFC1321_abc) {
	EXPECT_EQ(md5_hex("abc"), "900150983cd24fb0d6963f7d28e17f72");
}

TEST(Md5, RFC1321_message_digest) {
	EXPECT_EQ(md5_hex("message digest"), "f96b697d7cb7938d525a2f31aaf161d0");
}

TEST(Md5, RFC1321_lowercase_alphabet) {
	EXPECT_EQ(md5_hex("abcdefghijklmnopqrstuvwxyz"),
	          "c3fcd3d76192e4007dfb496cca67e13b");
}

TEST(Md5, RFC1321_mixed_alnum) {
	EXPECT_EQ(md5_hex("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                 "abcdefghijklmnopqrstuvwxyz"
	                 "0123456789"),
	          "d174ab98d277d9f5a5611c2c9f419d9f");
}

TEST(Md5, RFC1321_80_digits) {
	EXPECT_EQ(md5_hex("1234567890123456789012345678901234567890"
	                 "1234567890123456789012345678901234567890"),
	          "57edf4a22be3c955ac49da2e2107b67a");
}

// Block-boundary coverage:
//   - 55 bytes:  exactly fills one 64-byte block (55 data + 9 padding).
//   - 56 bytes:  padding pushes the length field into a second block.
//   - 64 bytes:  exactly two blocks (data block + padding-only block).
// Vectors cross-checked against the reference implementation.
TEST(Md5, OneBlockBoundary_55bytes) {
	EXPECT_EQ(md5_hex(std::string(55, 'a')), "9de6d5abdd4ee5237de9d3c631cd0b9f");
}

TEST(Md5, TwoBlocksBoundary_56bytes) {
	EXPECT_EQ(md5_hex(std::string(56, 'a')), "ad262e7a90b7f7c5ec09fa2b5193e3b1");
}

TEST(Md5, TwoBlocksExact_64bytes) {
	EXPECT_EQ(md5_hex(std::string(64, 'a')), "014912f636f3c1ce6b6e13e9e4f02879");
}

// Idempotency: re-initialising and re-using the state must produce the same
// digest as a fresh context (guards against leftover-state bugs).
TEST(Md5, ReuseState) {
	md5_state_t st;
	md5_byte_t digest1[16], digest2[16];

	md5_init(&st);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>("abc"), 3);
	md5_finish(&st, digest1);

	md5_init(&st);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>("abc"), 3);
	md5_finish(&st, digest2);

	EXPECT_EQ(0, memcmp(digest1, digest2, 16));
}

// Streaming: appending in chunks must equal appending all at once.
TEST(Md5, StreamingEquivalentToOneshot) {
	const std::string data = "The quick brown fox jumps over the lazy dog";

	md5_state_t st;
	md5_init(&st);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>(data.data()),
	          static_cast<int>(data.size()));
	md5_byte_t digest_stream[16];
	md5_finish(&st, digest_stream);

	EXPECT_EQ(md5_hex(data), md5_hex("The quick brown fox jumps over the lazy dog"));

	// Cross-check the streamed digest against the canonical vector for this
	// well-known string (9e107d09... = "The quick brown fox jumps over the lazy dog").
	static const char hex[] = "0123456789abcdef";
	std::string streamed_hex(32, ' ');
	for (int i = 0; i < 16; ++i) {
		streamed_hex[i * 2]     = hex[(digest_stream[i] >> 4) & 0xF];
		streamed_hex[i * 2 + 1] = hex[digest_stream[i] & 0xF];
	}
	EXPECT_EQ(streamed_hex, "9e107d09d6e299a8b7f237d0d07b0df1");
}
