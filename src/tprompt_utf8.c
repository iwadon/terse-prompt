/**
 * @file tprompt_utf8.c
 * @brief UTF-8 text handling utilities for terse-prompt
 *
 * Provides functions for UTF-8 validation, character navigation, and
 * display width calculation.
 */

#include "tprompt_internal.h"
#include <stdbool.h>
#include <string.h>

/* ========================================================================
 * UTF-8 Utilities - Internal Helpers
 * ======================================================================== */

size_t tprompt_utf8_char_length(unsigned char byte)
{
	if ((byte & 0x80) == 0x00) {
		return 1;
	} else if ((byte & 0xE0) == 0xC0) {
		return 2;
	} else if ((byte & 0xF0) == 0xE0) {
		return 3;
	} else if ((byte & 0xF8) == 0xF0) {
		return 4;
	}
	return 0; // Invalid
}

/**
 * @brief Decode UTF-8 bytes to Unicode scalar value
 */
unsigned int tprompt_utf8_decode(const unsigned char *bytes, size_t len)
{
	if (len == 0)
		return 0;

	if (len == 1) {
		return bytes[0];
	} else if (len == 2) {
		return ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
	} else if (len == 3) {
		return ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
	} else if (len == 4) {
		return ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) | ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
	}
	return 0;
}

/**
 * @brief Get display width of a UTF-8 character
 *
 * Returns the number of columns the character occupies on screen.
 * Based on Unicode East Asian Width property, similar to terse's compute_cell_width().
 * Note: This is a simplified implementation. Ideally, terse would expose its
 * compute_cell_width() function as a public API for perfect consistency.
 *
 * @param scalar Unicode scalar value
 * @return Display width (0 for control/combining, 1 for narrow, 2 for wide)
 */
int tprompt_get_char_width(unsigned int scalar)
{
	// NULL and control characters
	if (scalar == 0 || scalar < 0x20 || (scalar >= 0x7F && scalar < 0xA0)) {
		return 0;
	}

	// Combining marks (zero width) - most common ranges
	if ((scalar >= 0x0300 && scalar <= 0x036F) || // Combining Diacritical Marks
		(scalar >= 0x0483 && scalar <= 0x0489) || // Cyrillic combining
		(scalar >= 0x0591 && scalar <= 0x05BD) || // Hebrew combining
		(scalar >= 0x0610 && scalar <= 0x061A) || // Arabic combining
		(scalar >= 0x064B && scalar <= 0x065F) || // Arabic combining
		(scalar >= 0x0670 && scalar <= 0x0670) || // Arabic letter superscript alef
		(scalar >= 0x06D6 && scalar <= 0x06ED) || // Arabic combining
		(scalar >= 0x0711 && scalar <= 0x0711) || // Syriac combining
		(scalar >= 0x0730 && scalar <= 0x074A) || // Syriac combining
		(scalar >= 0x07A6 && scalar <= 0x07B0) || // Thaana combining
		(scalar >= 0x07EB && scalar <= 0x07F3) || // NKo combining
		(scalar >= 0x0816 && scalar <= 0x082D) || // Samaritan combining
		(scalar >= 0x0859 && scalar <= 0x085B) || // Mandaic combining
		(scalar >= 0x08D4 && scalar <= 0x08E1) || // Arabic extended combining
		(scalar >= 0x08E3 && scalar <= 0x0902) || // Arabic/Devanagari combining
		(scalar >= 0x093A && scalar <= 0x093C) || // Devanagari combining
		(scalar >= 0x0941 && scalar <= 0x0948) || // Devanagari combining
		(scalar >= 0x094D && scalar <= 0x094D) || // Devanagari virama
		(scalar >= 0x0951 && scalar <= 0x0957) || // Devanagari combining
		(scalar >= 0x0962 && scalar <= 0x0963) || // Devanagari combining
		(scalar >= 0x1AB0 && scalar <= 0x1ACE) || // Combining Diacritical Marks Extended
		(scalar >= 0x1DC0 && scalar <= 0x1DFF) || // Combining Diacritical Marks Supplement
		(scalar >= 0x20D0 && scalar <= 0x20F0) || // Combining Diacritical Marks for Symbols
		(scalar >= 0xFE20 && scalar <= 0xFE2F)) { // Combining Half Marks
		return 0;
	}

	// Wide characters (East Asian Wide and Fullwidth) = 2 columns
	// Based on Unicode East Asian Width property
	if ((scalar >= 0x1100 && scalar <= 0x115F) ||	// Hangul Jamo
		(scalar >= 0x2329 && scalar <= 0x232A) ||	// Left/Right-Pointing Angle Bracket
		(scalar >= 0x2E80 && scalar <= 0x2FFB) ||	// CJK Radicals Supplement + Kangxi Radicals + Ideographic Description
		(scalar >= 0x3000 && scalar <= 0x303E) ||	// CJK Symbols and Punctuation
		(scalar >= 0x3041 && scalar <= 0x33FF) ||	// Hiragana + Katakana + Bopomofo + Hangul Compat + Kanbun + etc
		(scalar >= 0x3400 && scalar <= 0x4DBF) ||	// CJK Unified Ideographs Extension A
		(scalar >= 0x4E00 && scalar <= 0xA4C6) ||	// CJK Unified Ideographs + Yi + etc
		(scalar >= 0xA960 && scalar <= 0xA97C) ||	// Hangul Jamo Extended-A
		(scalar >= 0xAC00 && scalar <= 0xD7A3) ||	// Hangul Syllables
		(scalar >= 0xF900 && scalar <= 0xFAFF) ||	// CJK Compatibility Ideographs
		(scalar >= 0xFE10 && scalar <= 0xFE19) ||	// Vertical Forms
		(scalar >= 0xFE30 && scalar <= 0xFE6B) ||	// CJK Compatibility Forms
		(scalar >= 0xFF01 && scalar <= 0xFF60) ||	// Fullwidth ASCII variants
		(scalar >= 0xFFE0 && scalar <= 0xFFE6) ||	// Fullwidth symbol variants
		(scalar >= 0x16FE0 && scalar <= 0x16FE4) || // Tangut symbols
		(scalar >= 0x17000 && scalar <= 0x187F7) || // Tangut + Tangut Components
		(scalar >= 0x18800 && scalar <= 0x18AFF) || // Tangut Supplement
		(scalar >= 0x1B000 && scalar <= 0x1B122) || // Kana Supplement + Kana Extended-A
		(scalar >= 0x1B150 && scalar <= 0x1B152) || // Small Kana Extension
		(scalar >= 0x1B164 && scalar <= 0x1B167) || // Small Kana Extension
		(scalar >= 0x1B170 && scalar <= 0x1B2FB) || // Nushu
		(scalar >= 0x1F004 && scalar <= 0x1F004) || // Mahjong Tile Red Dragon
		(scalar >= 0x1F0CF && scalar <= 0x1F0CF) || // Playing Card Black Joker
		(scalar >= 0x1F100 && scalar <= 0x1F10A) || // Enclosed Alphanumeric Supplement
		(scalar >= 0x1F110 && scalar <= 0x1F12D) || // Enclosed Alphanumeric Supplement
		(scalar >= 0x1F130 && scalar <= 0x1F169) || // Enclosed Alphanumeric Supplement
		(scalar >= 0x1F170 && scalar <= 0x1F18D) || // Enclosed Alphanumeric Supplement
		(scalar >= 0x1F18F && scalar <= 0x1F190) || // Squared symbols
		(scalar >= 0x1F19B && scalar <= 0x1F1AC) || // Squared symbols
		(scalar >= 0x1F200 && scalar <= 0x1F266) || // Enclosed Ideographic Supplement
		(scalar >= 0x1F300 && scalar <= 0x1F6D7) || // Misc Symbols and Pictographs + Emoticons + Transport
		(scalar >= 0x1F6DC && scalar <= 0x1F6EC) || // Transport and Map Symbols
		(scalar >= 0x1F6F0 && scalar <= 0x1F6FC) || // Transport and Map Symbols
		(scalar >= 0x1F700 && scalar <= 0x1F776) || // Alchemical Symbols
		(scalar >= 0x1F77B && scalar <= 0x1F7D9) || // Geometric Shapes Extended
		(scalar >= 0x1F7E0 && scalar <= 0x1F7EB) || // Geometric Shapes Extended
		(scalar >= 0x1F7F0 && scalar <= 0x1F7F0) || // Heavy Equals Sign
		(scalar >= 0x1F800 && scalar <= 0x1F80B) || // Supplemental Arrows-C
		(scalar >= 0x1F810 && scalar <= 0x1F847) || // Supplemental Arrows-C
		(scalar >= 0x1F850 && scalar <= 0x1F859) || // Supplemental Arrows-C
		(scalar >= 0x1F860 && scalar <= 0x1F882) || // Supplemental Symbols and Pictographs
		(scalar >= 0x1F890 && scalar <= 0x1F8AD) || // Supplemental Symbols and Pictographs
		(scalar >= 0x1F8B0 && scalar <= 0x1F8B1) || // Supplemental Symbols and Pictographs
		(scalar >= 0x1F900 && scalar <= 0x1FA53) || // Supplemental Symbols and Pictographs + Symbols and Pictographs Extended-A
		(scalar >= 0x1FA60 && scalar <= 0x1FA6D) || // Symbols and Pictographs Extended-A
		(scalar >= 0x1FA70 && scalar <= 0x1FA7C) || // Symbols and Pictographs Extended-A
		(scalar >= 0x1FA80 && scalar <= 0x1FA88) || // Symbols and Pictographs Extended-A
		(scalar >= 0x1FA90 && scalar <= 0x1FAE7) || // Symbols and Pictographs Extended-A
		(scalar >= 0x1FAF0 && scalar <= 0x1FAF6) || // Symbols and Pictographs Extended-A
		(scalar >= 0x1FB00 && scalar <= 0x1FBCA) || // Symbols for Legacy Computing
		(scalar >= 0x20000 && scalar <= 0x2FFFD) || // CJK Unified Ideographs Extensions B-F + Supplement
		(scalar >= 0x30000 && scalar <= 0x3FFFD)) { // CJK Unified Ideographs Extension G
		return 2;
	}

	// Default to width 1 for all other characters (including ambiguous width)
	return 1;
}

size_t tprompt_utf8_prev_char(const char *text, size_t offset)
{
	if (!text || offset == 0) {
		return 0;
	}

	// Move backward from current position
	size_t pos = offset - 1;

	// Skip backward over continuation bytes (10xxxxxx)
	while (pos > 0 && (text[pos] & 0xC0) == 0x80) {
		pos--;
	}

	return pos;
}

size_t tprompt_utf8_next_char(const char *text, size_t offset, size_t max_length)
{
	if (!text || offset >= max_length) {
		return max_length;
	}

	// Get character length at current position
	size_t char_len = tprompt_utf8_char_length((unsigned char)text[offset]);
	if (char_len == 0) {
		// Invalid UTF-8, skip one byte
		return offset + 1;
	}

	// Move forward by character length
	size_t new_offset = offset + char_len;
	if (new_offset > max_length) {
		new_offset = max_length;
	}

	return new_offset;
}

bool tprompt_utf8_validate(const char *text, size_t length)
{
	if (!text) {
		return false;
	}

	size_t i = 0;
	while (i < length) {
		unsigned char byte = (unsigned char)text[i];
		size_t char_len = tprompt_utf8_char_length(byte);

		// Invalid lead byte
		if (char_len == 0) {
			return false;
		}

		// Check if we have enough bytes remaining
		if (i + char_len > length) {
			return false;
		}

		// Validate continuation bytes
		for (size_t j = 1; j < char_len; j++) {
			unsigned char cont = (unsigned char)text[i + j];
			if ((cont & 0xC0) != 0x80) {
				return false;
			}
		}

		// Additional validation for overlong encodings and invalid ranges
		if (char_len == 2) {
			// 2-byte: 0xC2-0xDF (must be >= 0x80)
			if (byte < 0xC2) {
				return false;
			}
		} else if (char_len == 3) {
			// 3-byte: Check for overlong and surrogates
			if (byte == 0xE0 && (unsigned char)text[i + 1] < 0xA0) {
				return false; // Overlong
			}
			if (byte == 0xED && (unsigned char)text[i + 1] >= 0xA0) {
				return false; // UTF-16 surrogate
			}
		} else if (char_len == 4) {
			// 4-byte: Check for overlong and out of range
			if (byte == 0xF0 && (unsigned char)text[i + 1] < 0x90) {
				return false; // Overlong
			}
			if (byte > 0xF4) {
				return false; // Beyond U+10FFFF
			}
			if (byte == 0xF4 && (unsigned char)text[i + 1] > 0x8F) {
				return false; // Beyond U+10FFFF
			}
		}

		i += char_len;
	}

	return true;
}

size_t tprompt_utf8_char_count(const char *text, size_t length)
{
	if (!text) {
		return 0;
	}

	size_t count = 0;
	size_t i = 0;

	while (i < length) {
		size_t char_len = tprompt_utf8_char_length((unsigned char)text[i]);
		if (char_len == 0) {
			// Invalid byte, skip it
			i++;
		} else {
			i += char_len;
			count++;
		}
	}

	return count;
}

/**
 * @brief Encode Unicode scalar value to UTF-8 bytes
 *
 * Converts a Unicode scalar value to its UTF-8 byte sequence representation.
 * Validates the scalar value and ensures proper encoding.
 *
 * @param scalar Unicode scalar value (0x0 to 0x10FFFF, excluding surrogates)
 * @param out_buf Output buffer for UTF-8 bytes (must be at least 4 bytes)
 * @param max_buf_size Size of output buffer
 * @return Number of bytes written (1-4), or -1 on error
 */
int tprompt_utf8_encode(unsigned int scalar, char *out_buf, size_t max_buf_size)
{
	if (!out_buf || max_buf_size < 4) {
		return -1;
	}

	// Validate scalar range
	if (scalar > 0x10FFFF) {
		return -1; // Beyond valid Unicode range
	}

	// Reject UTF-16 surrogates
	if (scalar >= 0xD800 && scalar <= 0xDFFF) {
		return -1;
	}

	int len = 0;

	// 1-byte ASCII (0x0 to 0x7F)
	if (scalar < 0x80) {
		out_buf[len++] = (char)scalar;
	}
	// 2-byte UTF-8 (0x80 to 0x7FF)
	else if (scalar < 0x800) {
		out_buf[len++] = (char)(0xC0 | (scalar >> 6));
		out_buf[len++] = (char)(0x80 | (scalar & 0x3F));
	}
	// 3-byte UTF-8 (0x800 to 0xFFFF)
	else if (scalar < 0x10000) {
		out_buf[len++] = (char)(0xE0 | (scalar >> 12));
		out_buf[len++] = (char)(0x80 | ((scalar >> 6) & 0x3F));
		out_buf[len++] = (char)(0x80 | (scalar & 0x3F));
	}
	// 4-byte UTF-8 (0x10000 to 0x10FFFF)
	else if (scalar < 0x110000) {
		out_buf[len++] = (char)(0xF0 | (scalar >> 18));
		out_buf[len++] = (char)(0x80 | ((scalar >> 12) & 0x3F));
		out_buf[len++] = (char)(0x80 | ((scalar >> 6) & 0x3F));
		out_buf[len++] = (char)(0x80 | (scalar & 0x3F));
	}

	return len;
}
