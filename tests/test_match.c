/**
 * @file test_match.c
 * @brief Tests for completion matching helper functions
 *
 * Tests tprompt_match_prefix, tprompt_match_substring, and
 * tprompt_match_subsequence. These are pure functions that don't
 * require a tprompt handle or mock setup.
 */

#include <attest/attest.h>
#include <tprompt.h>

/* ========================================================================
 * MatchPrefix Tests
 * ======================================================================== */

TEST(MatchPrefix, BasicASCII)
{
	EXPECT_TRUE(tprompt_match_prefix("he", "hello"));
	EXPECT_TRUE(tprompt_match_prefix("hello", "hello"));
	EXPECT_FALSE(tprompt_match_prefix("hello!", "hello"));
	EXPECT_FALSE(tprompt_match_prefix("world", "hello"));
}

TEST(MatchPrefix, CaseInsensitive)
{
	EXPECT_TRUE(tprompt_match_prefix("HE", "hello"));
	EXPECT_TRUE(tprompt_match_prefix("he", "HELLO"));
	EXPECT_TRUE(tprompt_match_prefix("He", "hElLo"));
}

TEST(MatchPrefix, EmptyInput)
{
	EXPECT_TRUE(tprompt_match_prefix("", "hello"));
	EXPECT_TRUE(tprompt_match_prefix("", ""));
	EXPECT_TRUE(tprompt_match_prefix(NULL, "hello"));
}

TEST(MatchPrefix, NullCandidate)
{
	EXPECT_FALSE(tprompt_match_prefix("hello", NULL));
}

TEST(MatchPrefix, UTF8Japanese)
{
	/* "日本" is a prefix of "日本語" */
	EXPECT_TRUE(tprompt_match_prefix("\xe6\x97\xa5\xe6\x9c\xac",
	                                 "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));
	/* "日本語" exact match */
	EXPECT_TRUE(tprompt_match_prefix("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e",
	                                 "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));
	/* "語" is not a prefix of "日本語" */
	EXPECT_FALSE(tprompt_match_prefix("\xe8\xaa\x9e",
	                                  "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));
}

/* ========================================================================
 * MatchSubstring Tests
 * ======================================================================== */

TEST(MatchSubstring, BasicASCII)
{
	EXPECT_TRUE(tprompt_match_substring("ell", "hello"));
	EXPECT_TRUE(tprompt_match_substring("hello", "hello"));
	EXPECT_FALSE(tprompt_match_substring("xyz", "hello"));
}

TEST(MatchSubstring, CaseInsensitive)
{
	EXPECT_TRUE(tprompt_match_substring("ELL", "hello"));
	EXPECT_TRUE(tprompt_match_substring("ell", "HELLO"));
}

TEST(MatchSubstring, MiddleMatch)
{
	EXPECT_TRUE(tprompt_match_substring("or", "world"));
	EXPECT_TRUE(tprompt_match_substring("orl", "world"));
}

TEST(MatchSubstring, TailMatch)
{
	EXPECT_TRUE(tprompt_match_substring("llo", "hello"));
	EXPECT_TRUE(tprompt_match_substring("rld", "world"));
}

TEST(MatchSubstring, EmptyInput)
{
	EXPECT_TRUE(tprompt_match_substring("", "hello"));
	EXPECT_TRUE(tprompt_match_substring(NULL, "anything"));
}

TEST(MatchSubstring, NullCandidate)
{
	EXPECT_FALSE(tprompt_match_substring("test", NULL));
}

TEST(MatchSubstring, UTF8)
{
	/* "本語" is a substring of "日本語" */
	EXPECT_TRUE(tprompt_match_substring("\xe6\x9c\xac\xe8\xaa\x9e",
	                                    "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));
	/* "語" is a substring of "日本語" (tail) */
	EXPECT_TRUE(tprompt_match_substring("\xe8\xaa\x9e",
	                                    "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));
}

/* ========================================================================
 * MatchSubsequence Tests
 * ======================================================================== */

TEST(MatchSubsequence, Basic)
{
	/* "xi" appears in order in "exit" */
	EXPECT_TRUE(tprompt_match_subsequence("xi", "exit"));
	EXPECT_TRUE(tprompt_match_subsequence("et", "exit"));
	EXPECT_TRUE(tprompt_match_subsequence("exit", "exit"));
}

TEST(MatchSubsequence, ReverseOrderNoMatch)
{
	/* "xe" does not appear in order in "exit" (x comes after e) */
	EXPECT_FALSE(tprompt_match_subsequence("xe", "exit"));
	EXPECT_FALSE(tprompt_match_subsequence("ti", "exit"));
}

TEST(MatchSubsequence, CaseInsensitive)
{
	EXPECT_TRUE(tprompt_match_subsequence("XI", "exit"));
	EXPECT_TRUE(tprompt_match_subsequence("xi", "EXIT"));
	EXPECT_TRUE(tprompt_match_subsequence("Et", "EXIT"));
}

TEST(MatchSubsequence, EmptyInput)
{
	EXPECT_TRUE(tprompt_match_subsequence("", "exit"));
	EXPECT_TRUE(tprompt_match_subsequence(NULL, "anything"));
}

TEST(MatchSubsequence, NullCandidate)
{
	EXPECT_FALSE(tprompt_match_subsequence("test", NULL));
}

TEST(MatchSubsequence, UTF8Japanese)
{
	/* "日語" is a subsequence of "日本語" (skip "本") */
	EXPECT_TRUE(tprompt_match_subsequence(
		"\xe6\x97\xa5\xe8\xaa\x9e",
		"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));
	/* "語日" is NOT a subsequence of "日本語" (wrong order) */
	EXPECT_FALSE(tprompt_match_subsequence(
		"\xe8\xaa\x9e\xe6\x97\xa5",
		"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));
}

/* ========================================================================
 * Edge Case Tests
 * ======================================================================== */

TEST(Edge, EmptyCandidate)
{
	EXPECT_FALSE(tprompt_match_prefix("a", ""));
	EXPECT_FALSE(tprompt_match_substring("a", ""));
	EXPECT_FALSE(tprompt_match_subsequence("a", ""));
	/* Empty input with empty candidate -> true */
	EXPECT_TRUE(tprompt_match_prefix("", ""));
	EXPECT_TRUE(tprompt_match_substring("", ""));
	EXPECT_TRUE(tprompt_match_subsequence("", ""));
}

TEST(Edge, SingleCharacter)
{
	EXPECT_TRUE(tprompt_match_prefix("a", "abc"));
	EXPECT_TRUE(tprompt_match_substring("b", "abc"));
	EXPECT_TRUE(tprompt_match_subsequence("c", "abc"));
	EXPECT_FALSE(tprompt_match_prefix("b", "abc"));
	EXPECT_FALSE(tprompt_match_subsequence("d", "abc"));
}

TEST(Edge, InputLongerThanCandidate)
{
	EXPECT_FALSE(tprompt_match_prefix("abcdef", "abc"));
	EXPECT_FALSE(tprompt_match_substring("abcdef", "abc"));
	EXPECT_FALSE(tprompt_match_subsequence("abcdef", "abc"));
}

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
