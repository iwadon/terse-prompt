/**
 * @file tprompt_prompt.c
 * @brief Continuation prompt helpers
 * @version 0.1
 * @date 2025-11-20
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_internal.h"
#include <stdio.h>
#include <string.h>

#define DEFAULT_CONTINUATION_PROMPT "| "

/* ========================================================================
 * Continuation Prompt Helpers
 * ======================================================================== */

/**
 * @brief Calculate display width of a prompt string (UTF-8 aware)
 */
size_t tprompt_get_prompt_width(tprompt_handle_t handle, const char *prompt)
{
	if (!handle || !prompt) {
		return 0;
	}

	size_t width = 0;
	size_t len = strlen(prompt);
	size_t i = 0;

	while (i < len) {
		unsigned char byte = (unsigned char)prompt[i];
		size_t char_len = tprompt_utf8_char_length(byte);

		if (char_len == 0 || i + char_len > len) {
			// Invalid UTF-8 or truncated sequence, treat as single-byte
			width++;
			i++;
			continue;
		}

		// Decode UTF-8 character
		unsigned int scalar = tprompt_utf8_decode((const unsigned char *)&prompt[i], char_len);
		int char_width = tprompt_get_char_width(scalar);

		// Add character width (0 for control/combining, 1 for narrow, 2 for wide)
		width += (char_width > 0) ? char_width : 1;
		i += char_len;
	}

	return width;
}

/**
 * @brief Get the formatted continuation line prompt
 *
 * Returns the continuation prompt, padded or truncated to match the initial prompt width.
 * Thread-unsafe: uses static buffer.
 */
const char *tprompt_get_continuation_prompt(tprompt_handle_t handle)
{
	static char formatted_prompt[512];

	if (!handle || !handle->prompt) {
		return "";
	}

	// Use default continuation prompt if not set
	const char *cont_prompt = handle->continuation_prompt ? handle->continuation_prompt : DEFAULT_CONTINUATION_PROMPT;

	// Calculate widths
	size_t initial_width = tprompt_get_prompt_width(handle, handle->prompt);
	size_t cont_width = tprompt_get_prompt_width(handle, cont_prompt);

	if (cont_width == initial_width) {
		// Perfect match, return as-is
		return cont_prompt;
	} else if (cont_width < initial_width) {
		// Right-pad with spaces
		size_t padding = initial_width - cont_width;
		size_t cont_len = strlen(cont_prompt);
		size_t max_len = sizeof(formatted_prompt) - 1;

		if (cont_len > max_len) {
			cont_len = max_len;
		}

		if (padding > max_len - cont_len) {
			// Truncate padding to fit buffer
			padding = max_len - cont_len;
		}

		// Build padded prompt: spaces + continuation_prompt
		memset(formatted_prompt, ' ', padding);
		memcpy(formatted_prompt + padding, cont_prompt, cont_len);
		formatted_prompt[padding + cont_len] = '\0';

		return formatted_prompt;
	} else {
		// Truncate (cont_width > initial_width)
		// Emit warning once (use static flag)
		static bool warning_emitted = false;
		if (!warning_emitted) {
			fprintf(stderr, "tprompt warning: continuation_prompt is longer than initial prompt, truncating\n");
			warning_emitted = true;
		}

		// Truncate to initial_width by copying bytes until we reach the width limit
		size_t byte_count = 0;
		size_t current_width = 0;
		size_t cont_len = strlen(cont_prompt);

		while (byte_count < cont_len && current_width < initial_width) {
			unsigned char byte = (unsigned char)cont_prompt[byte_count];
			size_t char_len = tprompt_utf8_char_length(byte);

			if (char_len == 0 || byte_count + char_len > cont_len) {
				// Invalid UTF-8, stop here
				break;
			}

			unsigned int scalar = tprompt_utf8_decode((const unsigned char *)&cont_prompt[byte_count], char_len);
			int char_width = tprompt_get_char_width(scalar);
			int actual_width = (char_width > 0) ? char_width : 1;

			// Check if adding this character would exceed width
			if (current_width + actual_width > initial_width) {
				break;
			}

			current_width += actual_width;
			byte_count += char_len;
		}

		// Copy truncated prompt
		if (byte_count >= sizeof(formatted_prompt)) {
			byte_count = sizeof(formatted_prompt) - 1;
		}
		memcpy(formatted_prompt, cont_prompt, byte_count);
		formatted_prompt[byte_count] = '\0';

		return formatted_prompt;
	}
}
