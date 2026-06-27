#include <gtest/gtest.h>

#include "bitap_fuzzy.hpp"

#include <cstdint>
#include <string>

// scriptsEx.cpp defaults chbits=7; mirror that so tests exercise the real
// character-mask width (7 bits → ASCII-range bucket table).
static const size_t CHBITS = 7;

// ---------------------------------------------------------------------------
// uint32 variant (scriptsEx uses this for patterns shorter than 32 chars)
// ---------------------------------------------------------------------------

TEST(BitapFuzzy_u32, ExactMatch) {
	// "world" begins at index 6 in "hello world"
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              "hello world", 11, "world", 5, 0, CHBITS), 6);
}

TEST(BitapFuzzy_u32, ExactMatchAtStart) {
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              "hello world", 11, "hello", 5, 0, CHBITS), 0);
}

TEST(BitapFuzzy_u32, ExactMatchAtEnd) {
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              "hello world", 11, "world", 5, 0, CHBITS), 6);
}

TEST(BitapFuzzy_u32, NoMatchExact) {
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              "hello world", 11, "xyz", 3, 0, CHBITS), -1);
}

TEST(BitapFuzzy_u32, OneSubstitutionAllowed) {
	// "wurld": o→u (1 sub), k=1 → found at 6
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              "hello world", 11, "wurld", 5, 1, CHBITS), 6);
}

TEST(BitapFuzzy_u32, OneSubstitutionNotEnoughForTwo) {
	// "azrld": w→a, o→z (2 subs), k=1 → not found
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              "hello world", 11, "azrld", 5, 1, CHBITS), -1);
}

TEST(BitapFuzzy_u32, TwoSubstitutionsAllowed) {
	// "azrld": 2 subs, k=2 → found at 6
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              "hello world", 11, "azrld", 5, 2, CHBITS), 6);
}

TEST(BitapFuzzy_u32, SubstitutionFindsEarlierExact) {
	// k=0 finds the exact substring; "lo" appears at index 3
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              "hello world", 11, "lo", 2, 0, CHBITS), 3);
}

// Edge cases mirroring the guard clauses in bitap_fuzzy.hpp
TEST(BitapFuzzy_u32, EmptyText) {
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              "", 0, "abc", 3, 0, CHBITS), -3);
}

TEST(BitapFuzzy_u32, NullText) {
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              nullptr, 0, "abc", 3, 0, CHBITS), -3);
}

TEST(BitapFuzzy_u32, EmptyPattern) {
	// Per implementation: empty pattern is "found" at index 0.
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              "abc", 3, "", 0, 0, CHBITS), 0);
}

TEST(BitapFuzzy_u32, PatternTooLong) {
	// pattern_len >= sizeof(MASK)*digits → -2.
	// For uint32: sizeof=4, digits=32 → threshold 128. Use 128 chars.
	std::string long_pat(128, 'x');
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint32_t>(
	              "abc", 3, long_pat.c_str(), long_pat.size(), 0, CHBITS), -2);
}

// ---------------------------------------------------------------------------
// uint64 variant (scriptsEx uses this for patterns of length 32–63)
// ---------------------------------------------------------------------------

TEST(BitapFuzzy_u64, ExactMatch) {
	std::string text(40, 'x');
	text += "target";
	text += std::string(10, 'y');
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint64_t>(
	              text.c_str(), text.size(), "target", 6, 0, CHBITS), 40);
}

TEST(BitapFuzzy_u64, LongPatternExact) {
	// 36-char pattern (26 letters + 10 digits), well within the 64-bit window.
	std::string pat = "abcdefghijklmnopqrstuvwxyz0123456789";
	ASSERT_EQ(pat.size(), 36u);
	std::string text = "prefix_" + pat + "_suffix";
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint64_t>(
	              text.c_str(), text.size(), pat.c_str(), pat.size(), 0, CHBITS), 7);
}

TEST(BitapFuzzy_u64, OneSubstitutionAllowed) {
	std::string pat = "abcdefghijklmnopqrstuvwxyz0123456789";
	ASSERT_EQ(pat.size(), 36u);
	// Mutate one char: position 5 'f' → 'F' (1 substitution)
	std::string fuzzy = pat;
	fuzzy[5] = 'F';
	std::string text = "prefix_" + pat + "_suffix";
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint64_t>(
	              text.c_str(), text.size(), fuzzy.c_str(), fuzzy.size(), 1, CHBITS), 7);
}

TEST(BitapFuzzy_u64, EmptyText) {
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint64_t>(
	              "", 0, "abc", 3, 0, CHBITS), -3);
}

TEST(BitapFuzzy_u64, EmptyPattern) {
	EXPECT_EQ(bitap_fuzzy_bitwise_search<char, uint64_t>(
	              "abc", 3, "", 0, 0, CHBITS), 0);
}
