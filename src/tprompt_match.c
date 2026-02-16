/**
 * @file tprompt_match.c
 * @brief Completion matching helper functions
 *
 * Provides prefix, substring, and subsequence matching for completion
 * candidates. All comparisons are case-insensitive for ASCII letters;
 * non-ASCII bytes are compared exactly.
 */

#include "tprompt_internal.h"
#include <stddef.h>

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

static unsigned char ascii_tolower(unsigned char c)
{
	if (c >= 'A' && c <= 'Z') {
		return c + ('a' - 'A');
	}
	return c;
}

/**
 * @brief Compare two UTF-8 codepoints case-insensitively
 *
 * For single-byte ASCII, applies case folding. For multi-byte sequences,
 * compares bytes exactly.
 *
 * @return true if the codepoints are equal (case-insensitive for ASCII)
 */
static bool codepoints_equal_ci(const char *a, size_t a_len,
                                const char *b, size_t b_len)
{
	if (a_len != b_len) {
		return false;
	}

	if (a_len == 1) {
		return ascii_tolower((unsigned char)a[0]) ==
		       ascii_tolower((unsigned char)b[0]);
	}

	for (size_t i = 0; i < a_len; i++) {
		if (a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool tprompt_match_prefix(const char *input, const char *candidate)
{
	if (!input || !input[0]) {
		return true;
	}
	if (!candidate) {
		return false;
	}

	size_t i_pos = 0;
	size_t c_pos = 0;

	while (input[i_pos]) {
		if (!candidate[c_pos]) {
			return false;
		}

		size_t i_len = tprompt_utf8_char_length((unsigned char)input[i_pos]);
		size_t c_len = tprompt_utf8_char_length((unsigned char)candidate[c_pos]);

		if (!codepoints_equal_ci(input + i_pos, i_len,
		                         candidate + c_pos, c_len)) {
			return false;
		}

		i_pos += i_len;
		c_pos += c_len;
	}

	return true;
}

bool tprompt_match_substring(const char *input, const char *candidate)
{
	if (!input || !input[0]) {
		return true;
	}
	if (!candidate) {
		return false;
	}

	size_t c_pos = 0;

	while (candidate[c_pos]) {
		/* Try prefix match starting from this codepoint boundary */
		size_t i_pos = 0;
		size_t t_pos = c_pos;
		bool match = true;

		while (input[i_pos]) {
			if (!candidate[t_pos]) {
				match = false;
				break;
			}

			size_t i_len = tprompt_utf8_char_length((unsigned char)input[i_pos]);
			size_t t_len = tprompt_utf8_char_length((unsigned char)candidate[t_pos]);

			if (!codepoints_equal_ci(input + i_pos, i_len,
			                         candidate + t_pos, t_len)) {
				match = false;
				break;
			}

			i_pos += i_len;
			t_pos += t_len;
		}

		if (match) {
			return true;
		}

		/* Advance to next codepoint boundary in candidate */
		c_pos += tprompt_utf8_char_length((unsigned char)candidate[c_pos]);
	}

	return false;
}

bool tprompt_match_subsequence(const char *input, const char *candidate)
{
	if (!input || !input[0]) {
		return true;
	}
	if (!candidate) {
		return false;
	}

	size_t i_pos = 0;
	size_t c_pos = 0;

	while (input[i_pos] && candidate[c_pos]) {
		size_t i_len = tprompt_utf8_char_length((unsigned char)input[i_pos]);
		size_t c_len = tprompt_utf8_char_length((unsigned char)candidate[c_pos]);

		if (codepoints_equal_ci(input + i_pos, i_len,
		                        candidate + c_pos, c_len)) {
			i_pos += i_len;
		}

		c_pos += c_len;
	}

	return !input[i_pos];
}
