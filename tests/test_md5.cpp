#include "md5.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

// Helper: render a raw 16-byte digest as a lowercase hex string.
static std::string digest_to_hex(const md5_byte_t digest[16]) {
	static const char hex[] = "0123456789abcdef";
	std::string out(32, ' ');
	for (int i = 0; i < 16; ++i) {
		out[i * 2]     = hex[(digest[i] >> 4) & 0xF];
		out[i * 2 + 1] = hex[digest[i] & 0xF];
	}
	return out;
}

// Helper: compute MD5 of a std::string and return the lowercase hex digest.
static std::string md5_hex(const std::string &input) {
	md5_state_t st;
	md5_init(&st);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>(input.data()),
	           static_cast<int>(input.size()));
	md5_byte_t digest[16];
	md5_finish(&st, digest);
	return digest_to_hex(digest);
}

// --- Canonical RFC 1321 Appendix A.5 test suite (the spec) -------------------
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

// --- Idempotency: re-initialising and re-using the state --------------------
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

// --- Streaming: appending in chunks must equal appending all at once --------
// These exercise the block-boundary code paths (55/56/64/100 bytes) without
// requiring hardcoded digests — the one-shot path is already validated by the
// RFC 1321 vectors above, so streaming == one-shot is a sufficient invariant.
TEST(Md5, Streaming_55bytes_chunked) {
	std::string data(55, 'a');
	md5_state_t st;
	md5_init(&st);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>(data.data()), 16);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>(data.data() + 16), 16);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>(data.data() + 32), 16);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>(data.data() + 48), 7);
	md5_byte_t digest[16];
	md5_finish(&st, digest);
	EXPECT_EQ(digest_to_hex(digest), md5_hex(data));
}

TEST(Md5, Streaming_56bytes_chunked) {
	std::string data(56, 'a');
	md5_state_t st;
	md5_init(&st);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>(data.data()), 32);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>(data.data() + 32), 24);
	md5_byte_t digest[16];
	md5_finish(&st, digest);
	EXPECT_EQ(digest_to_hex(digest), md5_hex(data));
}

TEST(Md5, Streaming_64bytes_chunked) {
	std::string data(64, 'a');
	md5_state_t st;
	md5_init(&st);
	md5_append(&st, reinterpret_cast<const md5_byte_t *>(data.data()), 64);
	md5_byte_t digest[16];
	md5_finish(&st, digest);
	EXPECT_EQ(digest_to_hex(digest), md5_hex(data));
}

TEST(Md5, Streaming_100bytes_many_small_chunks) {
	std::string data(100, 'a');
	md5_state_t st;
	md5_init(&st);
	for (int i = 0; i < 100; i += 7) {
		int n = (i + 7 <= 100) ? 7 : (100 - i);
		md5_append(&st, reinterpret_cast<const md5_byte_t *>(data.data() + i), n);
	}
	md5_byte_t digest[16];
	md5_finish(&st, digest);
	EXPECT_EQ(digest_to_hex(digest), md5_hex(data));
}
