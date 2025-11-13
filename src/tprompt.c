/**
 * @file tprompt.c
 * @brief Implementation of terse-prompt library
 * @version 0.1
 * @date 2025-11-02
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_internal.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h>
#endif

/* ========================================================================
 * Global Error Information
 * ======================================================================== */

tprompt_error_info_t tprompt_global_error = {
	.category = TPROMPT_ERROR_NONE,
	.code = 0,
	.message = { 0 }
};

/* ========================================================================
 * Constants
 * ======================================================================== */

#define DEFAULT_BUFFER_SIZE 256
#define DEFAULT_PROMPT "> "



/* ========================================================================
 * History Management - Internal Helpers
 * ======================================================================== */

void tprompt_history_init(tprompt_history_t *history, size_t max_size)
{
	if (!history) {
		return;
	}

	history->head = NULL;
	history->tail = NULL;
	history->count = 0;
	history->max_size = max_size;
	history->current = NULL;
	history->saved_input = NULL;
}

void tprompt_history_free(tprompt_history_t *history)
{
	if (!history) {
		return;
	}

	tprompt_history_entry_t *entry = history->head;
	while (entry) {
		tprompt_history_entry_t *next = entry->next;
		free(entry->text);
		free(entry);
		entry = next;
	}

	history->head = NULL;
	history->tail = NULL;
	history->count = 0;
	history->current = NULL;

	// Free saved input if any
	if (history->saved_input) {
		free(history->saved_input);
		history->saved_input = NULL;
	}
}

int tprompt_history_add_internal(tprompt_history_t *history, const char *text)
{
	if (!history || !text) {
		return -1;
	}

	// Skip empty entries
	if (text[0] == '\0') {
		return 0;
	}

	// Duplicate check: don't add if it's the same as the most recent entry
	if (history->head && history->head->text) {
		if (strcmp(history->head->text, text) == 0) {
			return 0; // Skip duplicate
		}
	}

	// Allocate new entry
	tprompt_history_entry_t *entry = malloc(sizeof(tprompt_history_entry_t));
	if (!entry) {
		return -1;
	}

	// Duplicate text
	entry->text = strdup(text);
	if (!entry->text) {
		free(entry);
		return -1;
	}

	entry->next = NULL;
	entry->prev = NULL;

	// Insert at head (most recent position)
	if (!history->head) {
		// Empty history
		history->head = entry;
		history->tail = entry;
	} else {
		// Add to head
		entry->next = history->head;
		history->head->prev = entry;
		history->head = entry;
	}

	history->count++;

	// LRU eviction: remove from tail if we exceed max_size
	while (history->max_size > 0 && history->count > history->max_size) {
		if (!history->tail) {
			break; // Safety check
		}

		tprompt_history_entry_t *old_tail = history->tail;
		history->tail = old_tail->prev;

		if (history->tail) {
			history->tail->next = NULL;
		} else {
			// List became empty
			history->head = NULL;
		}

		free(old_tail->text);
		free(old_tail);
		history->count--;
	}

	// Reset navigation position to head (most recent)
	history->current = NULL;

	return 0;
}

const char *tprompt_history_prev(tprompt_history_t *history)
{
	if (!history) {
		return NULL;
	}

	// If current is NULL, start from head (most recent)
	if (!history->current) {
		history->current = history->head;
		return history->current ? history->current->text : NULL;
	}

	// Try to move to next (older) entry
	// Note: 'next' points toward older entries, 'prev' points toward newer entries
	if (history->current->next) {
		history->current = history->current->next;
		return history->current->text;
	}

	// Already at the oldest entry, return NULL but keep current position
	return NULL;
}

const char *tprompt_history_next(tprompt_history_t *history)
{
	if (!history || !history->current) {
		return NULL;
	}

	// Move to prev (newer) entry
	// Note: 'next' points toward older entries, 'prev' points toward newer entries
	history->current = history->current->prev;

	// Return current entry's text (or NULL if at end - returning to current input)
	return history->current ? history->current->text : NULL;
}

void tprompt_history_reset_position(tprompt_history_t *history)
{
	if (history) {
		history->current = NULL;
		// Free saved input when exiting history navigation
		if (history->saved_input) {
			free(history->saved_input);
			history->saved_input = NULL;
		}
	}
}

/**
 * @brief Escape special characters in history entry for file storage
 *
 * Escapes: newline (\n), carriage return (\r), backslash (\\)
 * Caller must free the returned string.
 */
static char *tprompt_history_escape(const char *text)
{
	if (!text) {
		return NULL;
	}

	// Count characters that need escaping
	size_t escape_count = 0;
	for (const char *p = text; *p; p++) {
		if (*p == '\n' || *p == '\r' || *p == '\\') {
			escape_count++;
		}
	}

	// Allocate buffer (original length + escape characters + null terminator)
	size_t len = strlen(text);
	char *escaped = malloc(len + escape_count + 1);
	if (!escaped) {
		return NULL;
	}

	// Escape characters
	char *dst = escaped;
	for (const char *src = text; *src; src++) {
		if (*src == '\n') {
			*dst++ = '\\';
			*dst++ = 'n';
		} else if (*src == '\r') {
			*dst++ = '\\';
			*dst++ = 'r';
		} else if (*src == '\\') {
			*dst++ = '\\';
			*dst++ = '\\';
		} else {
			*dst++ = *src;
		}
	}
	*dst = '\0';

	return escaped;
}

/**
 * @brief Unescape special characters from history file
 *
 * Unescapes: \n, \r, \\
 * Modifies the string in-place.
 */
static void tprompt_history_unescape(char *text)
{
	if (!text) {
		return;
	}

	char *src = text;
	char *dst = text;

	while (*src) {
		if (*src == '\\' && *(src + 1)) {
			src++; // Skip backslash
			if (*src == 'n') {
				*dst++ = '\n';
			} else if (*src == 'r') {
				*dst++ = '\r';
			} else if (*src == '\\') {
				*dst++ = '\\';
			} else {
				// Unknown escape sequence, keep both characters
				*dst++ = '\\';
				*dst++ = *src;
			}
			src++;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';
}

int tprompt_history_load_internal(tprompt_history_t *history, const char *file_path)
{
	if (!history || !file_path) {
		return -1;
	}

	// Open file for reading
	FILE *fp = fopen(file_path, "r");
	if (!fp) {
		// File not found is not an error (first time use)
		if (errno == ENOENT) {
			return 0;
		}
		return -1;
	}

	// Read line by line using fixed-size buffer
	// Note: 4096 bytes should be sufficient for typical history entries
	char buffer[4096];
	char *line = NULL;
	size_t line_len = 0;
	size_t line_cap = 0;

	while (fgets(buffer, sizeof(buffer), fp)) {
		size_t buffer_len = strlen(buffer);

		// Check if this is a continuation of a previous line
		// (no newline at end means buffer was full)
		bool has_newline = (buffer_len > 0 && buffer[buffer_len - 1] == '\n');

		// Append buffer to line
		if (line_len + buffer_len >= line_cap) {
			// Expand line buffer
			size_t new_cap = line_cap == 0 ? 4096 : line_cap * 2;
			while (new_cap < line_len + buffer_len + 1) {
				new_cap *= 2;
			}
			char *new_line = realloc(line, new_cap);
			if (!new_line) {
				free(line);
				fclose(fp);
				return -1;
			}
			line = new_line;
			line_cap = new_cap;
		}

		memcpy(line + line_len, buffer, buffer_len + 1); // +1 for NUL
		line_len += buffer_len;

		// If we found a newline, process this line
		if (has_newline) {
			// Remove trailing newline
			if (line_len > 0 && line[line_len - 1] == '\n') {
				line[line_len - 1] = '\0';
				line_len--;
			}

			// Remove trailing carriage return if present (CRLF handling)
			if (line_len > 0 && line[line_len - 1] == '\r') {
				line[line_len - 1] = '\0';
				line_len--;
			}

			// Unescape special characters
			tprompt_history_unescape(line);

			// Add to history (skip empty lines handled by add_internal)
			tprompt_history_add_internal(history, line);

			// Reset for next line
			line_len = 0;
			if (line) {
				line[0] = '\0';
			}
		}
	}

	// Handle case where file doesn't end with newline
	if (line_len > 0) {
		// Remove trailing carriage return if present
		if (line_len > 0 && line[line_len - 1] == '\r') {
			line[line_len - 1] = '\0';
			line_len--;
		}

		tprompt_history_unescape(line);
		tprompt_history_add_internal(history, line);
	}

	free(line);
	fclose(fp);

	return 0;
}

int tprompt_history_save_internal(tprompt_history_t *history, const char *file_path)
{
	if (!history || !file_path) {
		return -1;
	}

	// Open file for writing (truncate)
	FILE *fp = fopen(file_path, "w");
	if (!fp) {
		return -1;
	}

	// Write entries from tail to head (oldest to most recent)
	// This ensures that when we load and insert at head, the order is preserved
	tprompt_history_entry_t *entry = history->tail;
	while (entry) {
		// Escape special characters before writing
		char *escaped = tprompt_history_escape(entry->text);
		if (!escaped) {
			fclose(fp);
			return -1;
		}

		// Write escaped entry with newline
		int result = fprintf(fp, "%s\n", escaped);
		free(escaped);

		if (result < 0) {
			fclose(fp);
			return -1;
		}

		entry = entry->prev;
	}

	if (fclose(fp) != 0) {
		return -1;
	}

	return 0;
}


/* ========================================================================
 * Completion - Internal Helpers
 * ======================================================================== */

void tprompt_completion_init(tprompt_completion_state_t *state)
{
	if (!state) {
		return;
	}

	state->active = false;
	state->candidates = NULL;
	state->candidate_count = 0;
	state->selected_index = 0;
	state->trigger_offset = 0;
	state->trigger_char = '\0';
}

void tprompt_completion_free(tprompt_completion_state_t *state)
{
	if (!state) {
		return;
	}

	if (state->candidates) {
		for (size_t i = 0; i < state->candidate_count; i++) {
			free(state->candidates[i]);
		}
		free(state->candidates);
	}

	tprompt_completion_init(state);
}

int tprompt_completion_activate(tprompt_handle_t handle, char trigger_char, size_t trigger_offset)
{
	if (!handle) {
		return -1;
	}

	// Set completion state
	handle->completion_state.active = true;
	handle->completion_state.trigger_char = trigger_char;
	handle->completion_state.trigger_offset = trigger_offset;

	// Fetch initial candidates
	return tprompt_completion_update(handle);
}

void tprompt_completion_deactivate(tprompt_handle_t handle)
{
	if (!handle) {
		return;
	}

	// Free candidates
	if (handle->completion_state.candidates) {
		for (size_t i = 0; i < handle->completion_state.candidate_count; i++) {
			free(handle->completion_state.candidates[i]);
		}
		free(handle->completion_state.candidates);
	}

	// Reset state
	tprompt_completion_init(&handle->completion_state);
}

int tprompt_completion_update(tprompt_handle_t handle)
{
	if (!handle || !handle->completion_state.active) {
		return -1;
	}

	// Free previous candidates
	if (handle->completion_state.candidates) {
		for (size_t i = 0; i < handle->completion_state.candidate_count; i++) {
			free(handle->completion_state.candidates[i]);
		}
		free(handle->completion_state.candidates);
		handle->completion_state.candidates = NULL;
		handle->completion_state.candidate_count = 0;
		handle->completion_state.selected_index = 0;
	}

	// Call the completion callback
	if (!handle->completion_callback) {
		return -1;
	}

	// Prepare parameters for callback
	const char *text = handle->buffer.data;
	size_t cursor_pos = handle->buffer.cursor;
	char prefix_str[2] = { handle->completion_state.trigger_char, '\0' };

	// Invoke callback
	tprompt_completion_result_t result = handle->completion_callback(
		text,
		cursor_pos,
		prefix_str,
		handle->completion_user_data);

	// Store the results
	handle->completion_state.candidates = result.candidates;
	handle->completion_state.candidate_count = result.count;
	handle->completion_state.selected_index = 0;

	// If no candidates, deactivate completion
	if (result.count == 0) {
		tprompt_completion_deactivate(handle);
		return 0;
	}

	return 0;
}

void tprompt_completion_select_next(tprompt_completion_state_t *state)
{
	if (!state || !state->active || state->candidate_count == 0) {
		return;
	}

	// Move to next candidate (wrap around at end)
	state->selected_index = (state->selected_index + 1) % state->candidate_count;
}

void tprompt_completion_select_prev(tprompt_completion_state_t *state)
{
	if (!state || !state->active || state->candidate_count == 0) {
		return;
	}

	// Move to previous candidate (wrap around at beginning)
	if (state->selected_index == 0) {
		state->selected_index = state->candidate_count - 1;
	} else {
		state->selected_index--;
	}
}

int tprompt_completion_confirm(tprompt_handle_t handle)
{
	if (!handle || !handle->completion_state.active) {
		return -1;
	}

	// Check if we have a valid selected candidate
	if (handle->completion_state.candidate_count == 0 || handle->completion_state.selected_index >= handle->completion_state.candidate_count) {
		return -1;
	}

	// Get the selected candidate text
	const char *selected = handle->completion_state.candidates[handle->completion_state.selected_index];
	if (!selected) {
		return -1;
	}

	// Delete text from trigger position to current cursor position
	size_t trigger_pos = handle->completion_state.trigger_offset;
	size_t current_pos = handle->buffer.cursor;

	// Safety check: ensure trigger_pos is valid
	if (trigger_pos > current_pos || trigger_pos > handle->buffer.length) {
		return -1;
	}

	// Move cursor to trigger position
	handle->buffer.cursor = trigger_pos;

	// Delete characters from trigger_pos to current_pos (byte-wise)
	size_t delete_count = current_pos - trigger_pos;
	if (delete_count > 0) {
		// Shift remaining text left
		memmove(handle->buffer.data + trigger_pos,
			handle->buffer.data + current_pos,
			handle->buffer.length - current_pos);

		handle->buffer.length -= delete_count;
		handle->buffer.data[handle->buffer.length] = '\0';
	}

	// Insert the selected candidate at trigger position
	size_t selected_len = strlen(selected);
	if (tprompt_buffer_insert(&handle->buffer, selected, selected_len) != 0) {
		return -1;
	}

	return 0;
}

/* ========================================================================
 * Display and Rendering - Internal Helpers
 * ======================================================================== */

/**
 * @brief Calculate absolute column position from byte offset
 * @param handle Prompt handle
 * @param byte_offset Byte offset in buffer
 * @param include_prompt Whether to include prompt width
 * @return Absolute column position (0-based)
 */
static size_t tprompt_calculate_column_at_offset(tprompt_handle_t handle, size_t byte_offset, bool include_prompt)
{
	if (!handle) {
		return 0;
	}

	// Start with prompt length if requested
	size_t col = 0;
	if (include_prompt && handle->prompt) {
		col = strlen(handle->prompt);
	}

	// Add character widths from buffer start to given offset
	size_t char_count = tprompt_utf8_char_count(handle->buffer.data, byte_offset);
	col += char_count;

	return col;
}

/**
 * @brief Calculate cursor column position including prompt width
 * @param handle Prompt handle
 * @return Physical column position (0-based)
 */
size_t tprompt_calculate_cursor_col(tprompt_handle_t handle)
{
	return tprompt_calculate_column_at_offset(handle, handle->buffer.cursor, true);
}

/**
 * @brief Calculate physical line and column from byte offset
 *
 * This function determines the physical screen position (row and column) for a given
 * byte offset in the buffer, accounting for both terminal wrapping and explicit newlines.
 *
 * Algorithm:
 * 1. Start with prompt width on first line (if include_prompt is true)
 * 2. Iterate through buffer character-by-character up to byte_offset
 * 3. For each explicit newline (\n): move to next physical line, reset column to 0
 * 4. For auto-wrap (column >= terminal_width): move to next physical line, reset column
 * 5. For regular character: increment column by 1 (assumes single-width characters)
 *
 * @param handle Prompt handle
 * @param byte_offset Byte offset in buffer (will be clamped to buffer length)
 * @param include_prompt Whether to include prompt width in first line calculation
 * @param out_physical_line Output: physical line number (0-based from start of input)
 * @param out_physical_col Output: physical column within current line (0-based)
 */
void tprompt_calculate_physical_position(tprompt_handle_t handle, size_t byte_offset,
	bool include_prompt,
	size_t *out_physical_line, size_t *out_physical_col)
{
	if (!handle) {
		*out_physical_line = 0;
		*out_physical_col = 0;
		return;
	}

	size_t terminal_width = handle->display.terminal_width;
	if (terminal_width == 0) {
		terminal_width = 80; // Fallback default
	}

	// Clamp byte_offset to buffer length
	if (byte_offset > handle->buffer.length) {
		byte_offset = handle->buffer.length;
	}

	// Start position accounting for prompt
	size_t current_col = 0;
	if (include_prompt && handle->prompt) {
		current_col = strlen(handle->prompt);
	}
	size_t current_physical_line = 0;

	// Iterate through buffer up to byte_offset
	const char *data = handle->buffer.data;
	size_t i = 0;

	while (i < byte_offset) {
		// Check for newline character
		if (data[i] == '\n') {
			// Newline forces move to next physical line
			current_physical_line++;
			current_col = 0;

			// Add continuation marker width
			size_t marker_width = tprompt_get_continuation_marker_width(handle);
			current_col += marker_width;

			i++;
			continue;
		}

		// Check if we need to wrap to next physical line (auto-wrap)
		if (current_col >= terminal_width) {
			current_physical_line++;
			current_col = 0;
		}

		// Get character length and display width
		size_t char_len = tprompt_utf8_char_length((unsigned char)data[i]);
		if (char_len == 0) {
			char_len = 1; // Invalid UTF-8, skip one byte
			i++;
			current_col++;
		} else {
			// Decode UTF-8 character and get its display width
			unsigned int scalar = tprompt_utf8_decode((const unsigned char *)&data[i], char_len);
			int char_width = tprompt_get_char_width(scalar);
			i += char_len;
			current_col += char_width;
		}
	}

	*out_physical_line = current_physical_line;
	*out_physical_col = current_col;
}

/**
 * @brief Calculate total physical lines needed for buffer content
 *
 * Determines how many screen lines are required to display the entire buffer,
 * accounting for prompt width, terminal wrapping, and explicit newlines.
 * Used to know which screen lines need to be cleared before re-rendering.
 *
 * Algorithm is identical to tprompt_calculate_physical_position but iterates
 * through the entire buffer instead of stopping at a specific offset.
 *
 * @param handle Prompt handle
 * @return Total number of physical lines (minimum 1)
 */
static size_t tprompt_calculate_total_physical_lines(tprompt_handle_t handle)
{
	if (!handle) {
		return 1;
	}

	size_t terminal_width = handle->display.terminal_width;
	if (terminal_width == 0) {
		terminal_width = 80; // Fallback default
	}

	// Start with prompt on first physical line
	size_t current_col = 0;
	if (handle->prompt) {
		current_col = strlen(handle->prompt);
	}
	size_t total_physical_lines = 1;

	// Iterate through entire buffer
	const char *data = handle->buffer.data;
	size_t length = handle->buffer.length;
	size_t i = 0;

	while (i < length) {
		// Check for newline character
		if (data[i] == '\n') {
			// Newline forces move to next physical line
			total_physical_lines++;
			current_col = 0;

			// Add continuation marker width
			size_t marker_width = tprompt_get_continuation_marker_width(handle);
			current_col += marker_width;

			i++;
			continue;
		}

		// Check if we need to wrap to next physical line (auto-wrap)
		if (current_col >= terminal_width) {
			total_physical_lines++;
			current_col = 0;
		}

		// Get character length and display width
		size_t char_len = tprompt_utf8_char_length((unsigned char)data[i]);
		if (char_len == 0) {
			char_len = 1; // Invalid UTF-8, skip one byte
			i++;
			current_col++;
		} else {
			// Decode UTF-8 character and get its display width
			unsigned int scalar = tprompt_utf8_decode((const unsigned char *)&data[i], char_len);
			int char_width = tprompt_get_char_width(scalar);
			i += char_len;
			current_col += char_width;
		}
	}

	return total_physical_lines;
}

/**
 * @brief Get the width of the continuation line marker
 *
 * Returns the width (in columns) of the continuation marker that should be
 * displayed at the start of continuation lines (lines after the first logical line).
 * The marker format is: spaces for (prompt_width - 2) + '|' + space
 * For example, if prompt is "tprompt> " (9 chars), marker is "       | " (7 spaces + | + space)
 * This aligns the '|' with the '>' in the prompt.
 *
 * @param handle Prompt handle
 * @return Width in columns (same as prompt width), or 0 if no prompt
 */
size_t tprompt_get_continuation_marker_width(tprompt_handle_t handle)
{
	if (!handle || !handle->prompt) {
		return 0;
	}
	return strlen(handle->prompt);
}

void tprompt_display_calculate_layout(tprompt_handle_t handle)
{
	if (!handle) {
		return;
	}

	// In test mode, preserve manually-set dimensions
	// Check for TPROMPT_TEST_MODE environment variable
	static int test_mode = -1; // -1 = not checked, 0 = normal, 1 = test
	if (test_mode == -1) {
		test_mode = getenv("TPROMPT_TEST_MODE") ? 1 : 0;
	}

	// Save current dimensions in case we're in test mode
	size_t prev_width = handle->display.terminal_width;
	size_t prev_height = handle->display.terminal_height;

	// Get terminal dimensions from terse (skip in test mode if dimensions already set)
	if (!test_mode || prev_width == 0 || prev_height == 0) {
		terse_size_t size = terse_get_size(handle->terse);
		if (size.known && size.cols > 0 && size.rows > 0) {
			handle->display.terminal_width = (size_t)size.cols;
			handle->display.terminal_height = (size_t)size.rows;
		}
	}

	// Use fallback if terminal size still unknown
	if (handle->display.terminal_width == 0) {
		handle->display.terminal_width = prev_width > 0 ? prev_width : 80;
	}
	if (handle->display.terminal_height == 0) {
		handle->display.terminal_height = prev_height > 0 ? prev_height : 24;
	}

	// Calculate physical line and column at cursor position
	tprompt_calculate_physical_position(handle, handle->buffer.cursor, true,
		&handle->display.physical_line,
		&handle->display.physical_column);

	// Calculate total number of physical lines needed
	handle->display.total_physical_lines = tprompt_calculate_total_physical_lines(handle);
}

/**
 * @brief Render the complete input display
 *
 * Main rendering function that redraws the entire input area including:
 * - Prompt text
 * - Buffer contents with proper line wrapping
 * - Cursor positioning
 * - Completion list (if active)
 *
 * Algorithm:
 * 1. Calculate layout (physical line count, cursor position)
 * 2. Clear all physical lines used by previous render
 * 3. Write prompt on first line
 * 4. Write buffer contents character-by-character:
 *    - Handle explicit newlines (\n)
 *    - Handle auto-wrapping at terminal width
 *    - Skip invalid UTF-8 bytes
 * 5. Position cursor at correct location
 * 6. Render completion list if active
 *
 * @param handle Prompt handle
 * @return 0 on success, -1 on error (error details in handle->last_error)
 */
int tprompt_display_render(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Calculate layout first to know how many physical lines we need
	tprompt_display_calculate_layout(handle);

	// Check if we can use differential rendering (single-line, simple edits)
	bool use_differential = false;

	// Enable differential rendering for simple edits on single line
	if (handle->display.is_dirty &&
	    !handle->display.force_full_redraw &&
	    handle->display.total_physical_lines == handle->display.prev_total_physical_lines &&
	    handle->display.total_physical_lines <= 1) {  // Single line only

		// Allow differential rendering for small changes
		// This includes character insertions and deletions
		size_t dirty_size = handle->display.dirty_end_byte - handle->display.dirty_start_byte;
		if (dirty_size <= handle->buffer.length) {  // Any size, as long as single line
			use_differential = tprompt_display_can_use_differential(handle);
		}
	}

	if (use_differential) {
		// === DIFFERENTIAL RENDERING PATH ===
		// Only redraw the changed portion of the line

		// Ensure we know our starting row
		int base_row = handle->display.start_row;
		if (!handle->display.start_row_known) {
			terse_cursor_position_t start_pos = terse_get_cursor_position(handle->terse);
			if (start_pos.known) {
				handle->display.start_row = start_pos.row;
				handle->display.start_row_known = true;
				base_row = start_pos.row;
			} else {
				base_row = 0;
				handle->display.start_row = base_row;
				handle->display.start_row_known = true;
			}
		}

		// Calculate physical position of dirty region start
		size_t dirty_line, dirty_col_start;
		tprompt_calculate_physical_position(handle, handle->display.dirty_start_byte,
			true, &dirty_line, &dirty_col_start);

		// Move to the dirty region on the screen
		int target_row = base_row + (int)dirty_line;
		terse_error_t terr = terse_move_to(handle->terse, target_row, (int)dirty_col_start);
		if (terr != TERSE_OK) {
			// Fall back to full redraw on error
			tprompt_display_mark_all_dirty(handle);
			goto full_redraw;
		}

		// Clear from this position to end of line
		terr = terse_clear_line(handle->terse, TERSE_CLEAR_AFTER);
		if (terr != TERSE_OK) {
			// Fallback: manually write spaces to clear
			size_t terminal_width = handle->display.terminal_width;
			size_t chars_to_clear = terminal_width - dirty_col_start;
			for (size_t i = 0; i < chars_to_clear; i++) {
				terse_write_text(handle->terse, " ");
			}
			// Move back to start of dirty region
			terse_move_to(handle->terse, target_row, (int)dirty_col_start);
		}

		// Render from dirty_start_byte to end of current physical line
		size_t current_col = dirty_col_start;
		size_t terminal_width = handle->display.terminal_width;
		size_t i = handle->display.dirty_start_byte;

		// Find where the physical line ends
		while (i < handle->buffer.length && current_col < terminal_width) {
			// Check for newline (should not happen in validated differential case)
			if (handle->buffer.data[i] == '\n') {
				break;
			}

			// Get character length
			size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[i]);
			if (char_len == 0 || i + char_len > handle->buffer.length) {
				i++;
				current_col++;
				continue;
			}

			// Extract and write the character
			char temp[5] = { 0 };
			memcpy(temp, &handle->buffer.data[i], char_len);
			temp[char_len] = '\0';

			terr = terse_write_text(handle->terse, temp);
			if (terr != TERSE_OK) {
				// On error, fall back to full redraw
				tprompt_display_mark_all_dirty(handle);
				goto full_redraw;
			}

			// Get character display width
			unsigned int scalar = tprompt_utf8_decode((const unsigned char *)temp, char_len);
			int char_width = tprompt_get_char_width(scalar);
			current_col += char_width;
			i += char_len;
		}

		// Position cursor at the correct location
		size_t cursor_line, cursor_col;
		tprompt_calculate_physical_position(handle, handle->buffer.cursor,
			true, &cursor_line, &cursor_col);
		terr = terse_move_to(handle->terse, base_row + (int)cursor_line, (int)cursor_col);
		if (terr != TERSE_OK) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
				"Failed to position cursor after differential render");
			return -1;
		}

		// Flush output
		terr = terse_flush(handle->terse);
		if (terr != TERSE_OK) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
				"Failed to flush output after differential render");
			return -1;
		}

		// Update previous line count for next redraw
		handle->display.prev_total_physical_lines = handle->display.total_physical_lines;

		// Clear dirty flags
		tprompt_display_clear_dirty(handle);

		return 0;
	}

full_redraw:
	// === FULL REDRAW PATH (existing implementation) ===
	;  // C11 requires a statement after a label

	// Capture the starting row (0-based) if we have not done so yet
	int base_row = handle->display.start_row;
	if (!handle->display.start_row_known) {
		terse_cursor_position_t start_pos = terse_get_cursor_position(handle->terse);
		if (start_pos.known) {
			handle->display.start_row = start_pos.row;
			handle->display.start_row_known = true;
			base_row = start_pos.row;
		} else {
			// Cursor position not available - start tracking manually from current position
			// We assume we're at row 0 for the first time, then track from there
			base_row = 0;
			handle->display.start_row = base_row;
			handle->display.start_row_known = true; // Enable manual tracking
		}
	}

	// Check if rendering would exceed terminal boundaries
	// If so, we need to scroll the terminal to make room
	size_t terminal_height = handle->display.terminal_height;
	// Account for debug line: total_physical_lines + 1
	size_t lines_needed = handle->display.total_physical_lines + 1;
	int last_row = base_row + (int)lines_needed - 1;

	if (last_row >= (int)terminal_height) {
		// Calculate how many lines we need to scroll
		int scroll_count = last_row - (int)terminal_height + 1;

		// Move to bottom of terminal and write newlines to force scrolling
		terse_error_t terr = terse_move_to(handle->terse, (int)terminal_height - 1, 0);
		if (terr != TERSE_OK) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
				"Failed to move cursor to bottom for scrolling");
			return -1;
		}

		for (int i = 0; i < scroll_count; i++) {
			terr = terse_write_text(handle->terse, "\r\n");
			if (terr != TERSE_OK) {
				tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
					"Failed to write newline for scrolling");
				return -1;
			}
		}

		// Adjust start_row to account for the scroll
		handle->display.start_row -= scroll_count;
		base_row = handle->display.start_row;
	}

	// Clear all physical lines used by the input (including debug line)
	// Use the maximum of current and previous line counts to ensure orphaned lines are cleared
	size_t lines_to_clear = handle->display.total_physical_lines > handle->display.prev_total_physical_lines
		? handle->display.total_physical_lines
		: handle->display.prev_total_physical_lines;
	for (size_t i = 0; i <= lines_to_clear; i++) {
		int target_row = base_row + (int)i;
		terse_error_t terr = terse_move_to(handle->terse, target_row, 0);
		if (terr != TERSE_OK) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
				"Failed to move cursor to clear line %zu", i);
			return -1;
		}
		terr = terse_clear_line(handle->terse, TERSE_CLEAR_ALL);
		if (terr != TERSE_OK) {
			// Fallback: clear line by writing spaces
			size_t width = handle->display.terminal_width;
			for (size_t j = 0; j < width; j++) {
				terr = terse_write_text(handle->terse, " ");
				if (terr != TERSE_OK) {
					tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
						"Failed to clear physical line %zu", i);
					return -1;
				}
			}
			// Move back to start of line
			terr = terse_move_to(handle->terse, target_row, 0);
			if (terr != TERSE_OK) {
				tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
					"Failed to move cursor after fallback clear");
				return -1;
			}
		}
	}

	// Move back to start position
	{
		terse_error_t terr = terse_move_to(handle->terse, base_row, 0);
		if (terr != TERSE_OK) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
				"Failed to move cursor to start position (row %d)", base_row);
			return -1;
		}
	}

	// Render text with wrapping and explicit newlines
	size_t current_col = 0;
	size_t terminal_width = handle->display.terminal_width;
	bool first_logical_line = true;

	// Write prompt on first line
	if (handle->prompt && first_logical_line) {
		size_t prompt_len = strlen(handle->prompt);
		terse_error_t terr = terse_write_text(handle->terse, handle->prompt);
		if (terr != TERSE_OK) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
				"Failed to write prompt text to terminal");
			return -1;
		}
		current_col += prompt_len;
	}

	// Write buffer contents character by character, handling both \n and wrapping
	size_t i = 0;
	while (i < handle->buffer.length) {
		// Check for explicit newline character
		if (handle->buffer.data[i] == '\n') {
			// Explicit newline: move to next physical line and return to column 0
			terse_error_t terr = terse_write_text(handle->terse, "\r\n");
			if (terr != TERSE_OK) {
				tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
					"Failed to write newline character at byte offset %zu", i);
				return -1;
			}
			current_col = 0;
			first_logical_line = false;

			// Write continuation marker on the new line
			size_t marker_width = tprompt_get_continuation_marker_width(handle);
			if (marker_width > 0) {
				// Build continuation marker: spaces + '|' + space
				// The '|' should align with the second-to-last character of the prompt
				// Example: "tprompt> " (9 chars) -> "       | " (7 spaces + | + space)
				char marker[256];
				size_t spaces_before = marker_width >= 2 ? marker_width - 2 : 0;
				if (spaces_before + 2 >= sizeof(marker)) {
					spaces_before = sizeof(marker) - 3;
				}
				memset(marker, ' ', spaces_before);
				marker[spaces_before] = '|';
				marker[spaces_before + 1] = ' ';
				marker[spaces_before + 2] = '\0';

				terse_error_t terr = terse_write_text(handle->terse, marker);
				if (terr != TERSE_OK) {
					tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
						"Failed to write continuation marker");
					return -1;
				}
				current_col += marker_width;
			}

			i++;
			continue;
		}

		// Check if we need to wrap to next line (auto-wrap)
		if (current_col >= terminal_width) {
			// Move to next physical line and return to column 0
			terse_error_t terr = terse_write_text(handle->terse, "\r\n");
			if (terr != TERSE_OK) {
				tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
					"Failed to write auto-wrap newline at column %zu", current_col);
				return -1;
			}
			current_col = 0;
		}

		// Get current character length
		size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[i]);
		if (char_len == 0) {
			// Invalid byte, skip it
			i++;
			continue;
		}

		// Write the character
		char temp[5] = { 0 };
		if (char_len <= 4) {
			memcpy(temp, &handle->buffer.data[i], char_len);
			temp[char_len] = '\0';

			terse_error_t terr = terse_write_text(handle->terse, temp);
			if (terr != TERSE_OK) {
				tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
					"Failed to write character at byte offset %zu", i);
				return -1;
			}

			// Get character display width
			unsigned int scalar = tprompt_utf8_decode((const unsigned char *)temp, char_len);
			int char_width = tprompt_get_char_width(scalar);
			current_col += char_width;
		}

		i += char_len;
	}

	// Update cursor position to where the cursor should be
	if (tprompt_display_update_cursor(handle) != 0) {
		return -1;
	}

	// Render completion list if active
	if (handle->completion_state.active) {
		if (tprompt_display_render_completion(handle) != 0) {
			return -1;
		}
	}

	// DEBUG: Display cursor position
	// Move to debug line (one line below the last input line)
	int debug_row = base_row + (int)handle->display.total_physical_lines;
	terse_error_t terr = terse_move_to(handle->terse, debug_row, 0);
	if (terr != TERSE_OK) {
		// If we can't move to debug line, skip debug output
	} else {
		// Clear the debug line
		terse_clear_line(handle->terse, TERSE_CLEAR_ALL);

		// Write debug info
		char debug_info[128];
		int target_col = (int)handle->display.physical_column;
		if (handle->input_state.has_goal_column) {
			snprintf(debug_info, sizeof(debug_info), "x=%d y=%d goal=%zu",
				target_col, (int)handle->display.physical_line, handle->input_state.goal_column);
		} else {
			snprintf(debug_info, sizeof(debug_info), "x=%d y=%d goal=-",
				target_col, (int)handle->display.physical_line);
		}
		terse_write_text(handle->terse, debug_info);

		// Return cursor to target position
		int target_row = base_row + (int)handle->display.physical_line;
		terse_move_to(handle->terse, target_row, target_col);
		terse_flush(handle->terse);
	}

	// Update previous line count for next redraw
	handle->display.prev_total_physical_lines = handle->display.total_physical_lines;

	// Clear dirty flags after successful full redraw
	tprompt_display_clear_dirty(handle);

	return 0;
}

int tprompt_display_update_cursor(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Calculate cursor position
	tprompt_display_calculate_layout(handle);

	int base_row = handle->display.start_row;
	if (!handle->display.start_row_known) {
		terse_cursor_position_t pos = terse_get_cursor_position(handle->terse);
		if (pos.known) {
			handle->display.start_row = pos.row;
			handle->display.start_row_known = true;
			base_row = pos.row;
		} else {
			base_row = 0;
		}
	}

	// Calculate target row (start_row + physical_line offset)
	int target_row = base_row + (int)handle->display.physical_line;

	// Move cursor to calculated position using 0-based coordinates
	int target_col = (int)handle->display.physical_column;
	terse_error_t terr = terse_move_to(handle->terse, target_row, target_col);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to move cursor to position (row=%d, col=%zu)",
			target_row, handle->display.physical_column);
		return -1;
	}

	// Flush to ensure cursor is visible
	terr = terse_flush(handle->terse);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to flush output after cursor update");
		return -1;
	}

	return 0;
}

int tprompt_display_clear(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Clear the current line
	terse_error_t terr = terse_clear_line(handle->terse, TERSE_CLEAR_ALL);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to clear current display line");
		return -1;
	}

	// Move cursor to start of line
	terse_cursor_position_t pos = terse_get_cursor_position(handle->terse);
	int row = pos.known ? pos.row : 0;
	terr = terse_move_to(handle->terse, row, 0);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to move cursor to line start");
		return -1;
	}

	// Flush
	terr = terse_flush(handle->terse);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to flush output after display clear");
		return -1;
	}

	return 0;
}

int tprompt_display_render_completion(tprompt_handle_t handle)
{
	if (!handle || !handle->completion_state.active) {
		return 0; // Not an error, just nothing to render
	}

	size_t candidate_count = handle->completion_state.candidate_count;
	if (candidate_count == 0) {
		return 0; // No candidates to display
	}

	char **candidates = handle->completion_state.candidates;
	size_t selected_index = handle->completion_state.selected_index;

	// Get current cursor position (save it for later restoration)
	terse_cursor_position_t saved_pos = terse_get_cursor_position(handle->terse);

	// Move cursor to next line after input
	terse_error_t terr = terse_move_by(handle->terse, 1, 0);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to move cursor down to render completion list");
		return -1;
	}

	// Render each candidate
	for (size_t i = 0; i < candidate_count; i++) {
		// Clear the line first
		terr = terse_clear_line(handle->terse, TERSE_CLEAR_ALL);
		if (terr != TERSE_OK) {
			return -1;
		}

		// Move to start of line
		terse_cursor_position_t line_pos = terse_get_cursor_position(handle->terse);
		int row = 0;
		if (line_pos.known) {
			row = line_pos.row;
		} else if (saved_pos.known) {
			row = saved_pos.row + 1 + (int)i;
		}
		terr = terse_move_to(handle->terse, row, 0);
		if (terr != TERSE_OK) {
			return -1;
		}

		// Render selection indicator and candidate text
		char prefix[8] = "  ";
		if (i == selected_index) {
			snprintf(prefix, sizeof(prefix), "> ");
		}

		// Write prefix
		terr = terse_write_text(handle->terse, prefix);
		if (terr != TERSE_OK) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
				"Failed to write completion prefix for candidate %zu", i);
			return -1;
		}

		// Write candidate text
		terr = terse_write_text(handle->terse, candidates[i]);
		if (terr != TERSE_OK) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
				"Failed to write completion candidate %zu: '%s'", i, candidates[i]);
			return -1;
		}

		// Move to next line if not the last candidate
		if (i < candidate_count - 1) {
			terr = terse_move_by(handle->terse, 1, 0);
			if (terr != TERSE_OK) {
				return -1;
			}
		}
	}

	// Restore cursor to input position
	if (saved_pos.known) {
		terr = terse_move_to(handle->terse, saved_pos.row, saved_pos.col);
		if (terr != TERSE_OK) {
			return -1;
		}
	}

	// Flush output
	terr = terse_flush(handle->terse);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to flush output after rendering %zu completion candidates", candidate_count);
		return -1;
	}

	return 0;
}

/* ========================================================================
 * Dirty Region Tracking for Differential Rendering
 * ======================================================================== */

void tprompt_display_mark_dirty_range(tprompt_handle_t handle,
	size_t start_byte,
	size_t end_byte)
{
	if (!handle) {
		return;
	}

	// Clamp to valid range
	if (end_byte > handle->buffer.length) {
		end_byte = handle->buffer.length;
	}
	if (start_byte > end_byte) {
		start_byte = end_byte;
	}

	if (!handle->display.is_dirty) {
		// First dirty region
		handle->display.is_dirty = true;
		handle->display.dirty_start_byte = start_byte;
		handle->display.dirty_end_byte = end_byte;
	} else {
		// Expand existing dirty region to include new range
		if (start_byte < handle->display.dirty_start_byte) {
			handle->display.dirty_start_byte = start_byte;
		}
		if (end_byte > handle->display.dirty_end_byte) {
			handle->display.dirty_end_byte = end_byte;
		}
	}
}

void tprompt_display_mark_all_dirty(tprompt_handle_t handle)
{
	if (!handle) {
		return;
	}

	handle->display.is_dirty = true;
	handle->display.force_full_redraw = true;
}

void tprompt_display_clear_dirty(tprompt_handle_t handle)
{
	if (!handle) {
		return;
	}

	handle->display.is_dirty = false;
	handle->display.dirty_start_byte = 0;
	handle->display.dirty_end_byte = 0;
	handle->display.force_full_redraw = false;
}

bool tprompt_display_can_use_differential(tprompt_handle_t handle)
{
	if (!handle || !handle->display.is_dirty) {
		return false;
	}

	// Forced full redraw requested
	if (handle->display.force_full_redraw) {
		return false;
	}

	// Physical line count changed - need full redraw
	if (handle->display.total_physical_lines != handle->display.prev_total_physical_lines) {
		return false;
	}

	// Calculate which physical lines are affected by the dirty region
	size_t dirty_start_line, dirty_start_col;
	size_t dirty_end_line, dirty_end_col;

	// Clamp dirty region to current buffer bounds
	size_t dirty_start = handle->display.dirty_start_byte;
	size_t dirty_end = handle->display.dirty_end_byte;
	if (dirty_start > handle->buffer.length) {
		dirty_start = handle->buffer.length;
	}
	if (dirty_end > handle->buffer.length) {
		dirty_end = handle->buffer.length;
	}

	tprompt_calculate_physical_position(handle, dirty_start,
		true, &dirty_start_line, &dirty_start_col);
	tprompt_calculate_physical_position(handle, dirty_end,
		true, &dirty_end_line, &dirty_end_col);

	// Dirty region spans multiple physical lines - use full redraw
	// (Differential rendering across lines is complex due to wrapping)
	if (dirty_start_line != dirty_end_line) {
		return false;
	}

	// Check if the dirty region affects a logical line with newlines
	// (Multi-line logic is complex, safer to do full redraw)
	// (dirty_start and dirty_end already clamped above)

	// Check for newlines in dirty region
	for (size_t i = dirty_start; i < dirty_end && i < handle->buffer.length; i++) {
		if (handle->buffer.data[i] == '\n') {
			return false; // Contains newline, use full redraw
		}
	}

	return true; // Safe for differential rendering
}

/* ========================================================================
 * Key Event Handlers - Internal Helpers
 * ======================================================================== */

int tprompt_handle_char_input(tprompt_handle_t handle, const char *ch, int width)
{
	if (!handle || !ch) {
		return -1;
	}

	// Check if this is a completion trigger character
	if (ch[0] != '\0' && tprompt_is_completion_trigger(handle, ch[0])) {
		// Activate completion if not already active
		if (!handle->completion_state.active) {
			// Insert the character first
			size_t ch_len = strlen(ch);
			size_t insert_pos = handle->buffer.cursor;
			if (tprompt_buffer_insert(&handle->buffer, ch, ch_len) != 0) {
				return -1;
			}

			// Mark the inserted region as dirty
			tprompt_display_mark_dirty_range(handle, insert_pos, handle->buffer.cursor);

			// Activate completion at the current cursor position
			if (tprompt_completion_activate(handle, ch[0], handle->buffer.cursor - ch_len) != 0) {
				return -1;
			}

			return 0;
		}
	}

	// Normal character insertion
	size_t ch_len = strlen(ch);
	size_t insert_pos = handle->buffer.cursor;
	if (tprompt_buffer_insert(&handle->buffer, ch, ch_len) != 0) {
		return -1;
	}

	// Mark the inserted region as dirty
	tprompt_display_mark_dirty_range(handle, insert_pos, handle->buffer.cursor);

	// If completion is active, update candidates with new input
	if (handle->completion_state.active) {
		if (tprompt_completion_update(handle) != 0) {
			return -1;
		}
	}

	return 0;
}

/**
 * @brief Handle keyboard events during editing
 *
 * Main event handler that processes all keyboard input. This is called
 * by tprompt_readline() for each key event.
 *
 * Key behavior priorities:
 * 1. Completion navigation (if active): UP/DOWN navigate candidates, TAB confirms, ESC cancels
 * 2. Multi-line navigation vs. history: UP/DOWN behavior depends on whether buffer has newlines
 * 3. Home/End staged movement: First press moves to logical line boundary, second press to buffer boundary
 * 4. Enter key mode: Single-line submits, multi-line inserts newline (Ctrl/Alt+Enter always submits)
 *
 * @param handle Prompt handle
 * @param event Terse event to process
 * @return 1 to submit input, 0 to continue editing, -1 on error
 */
int tprompt_handle_key_event(tprompt_handle_t handle, const terse_event_t *event)
{
	if (!handle || !event) {
		return -1;
	}

	// If completion is active, handle UP/DOWN for candidate navigation
	if (handle->completion_state.active && handle->completion_state.candidate_count > 0) {
		switch (event->type) {
		case TERSE_EVENT_ARROW_UP:
			// Navigate to previous candidate
			tprompt_completion_select_prev(&handle->completion_state);
			return 0; // Continue editing (don't return input)

		case TERSE_EVENT_ARROW_DOWN:
			// Navigate to next candidate
			tprompt_completion_select_next(&handle->completion_state);
			return 0; // Continue editing

		case TERSE_EVENT_TAB:
			// Confirm selected completion
			if (tprompt_completion_confirm(handle) == 0) {
				tprompt_completion_deactivate(handle);
			}
			return 0; // Continue editing

		default:
			break;
		}
	}

	// Handle ESC to cancel completion (check regardless of candidate count)
	if (handle->completion_state.active && event->type == TERSE_EVENT_CHAR) {
		if (event->data.ch.scalar == 27) { // ESC
			tprompt_completion_deactivate(handle);
			return 0;
		}
	}

	// If completion is not active, handle UP/DOWN for either line navigation or history
	if (!handle->completion_state.active) {
		// Check if buffer has multiple lines
		bool has_multiple_lines = tprompt_buffer_has_newlines(handle);

		switch (event->type) {
		case TERSE_EVENT_ARROW_UP:
			if (has_multiple_lines) {
				// In multi-line mode: check if at first line
				size_t current_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
				if (current_line == 0) {
					// At first line, navigate to previous history entry
					// Save current input if not already in history navigation
					if (!handle->history.current) {
						if (handle->history.saved_input) {
							free(handle->history.saved_input);
						}
						handle->history.saved_input = strdup(handle->buffer.data);
					}
					const char *entry = tprompt_history_prev(&handle->history);
					if (entry) {
						tprompt_buffer_set(&handle->buffer, entry);
						tprompt_display_mark_all_dirty(handle);
					}
				} else {
					// Not at first line, navigate up within buffer
					tprompt_cursor_move_up(handle);
				}
			} else {
				// Single line mode: always navigate history
				// Save current input if not already in history navigation
				if (!handle->history.current) {
					if (handle->history.saved_input) {
						free(handle->history.saved_input);
					}
					handle->history.saved_input = strdup(handle->buffer.data);
				}
				const char *entry = tprompt_history_prev(&handle->history);
				if (entry) {
					tprompt_buffer_set(&handle->buffer, entry);
				}
			}
			return 0; // Continue editing

		case TERSE_EVENT_ARROW_DOWN:
			if (has_multiple_lines) {
				// In multi-line mode: check if at last line
				size_t current_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
				size_t total_lines = tprompt_count_logical_lines(handle);
				if (current_line >= total_lines - 1) {
					// At last line, navigate to next history entry
					const char *entry = tprompt_history_next(&handle->history);
					if (entry) {
						tprompt_buffer_set(&handle->buffer, entry);
					} else {
						// At the end of history, restore saved input
						if (handle->history.saved_input) {
							tprompt_buffer_set(&handle->buffer, handle->history.saved_input);
						} else {
							tprompt_buffer_clear(&handle->buffer);
						}
					}
				} else {
					// Not at last line, navigate down within buffer
					tprompt_cursor_move_down(handle);
				}
			} else {
				// Single line mode: always navigate history
				const char *entry = tprompt_history_next(&handle->history);
				if (entry) {
					tprompt_buffer_set(&handle->buffer, entry);
				} else {
					// At the end of history, restore saved input
					if (handle->history.saved_input) {
						tprompt_buffer_set(&handle->buffer, handle->history.saved_input);
					} else {
						tprompt_buffer_clear(&handle->buffer);
					}
				}
			}
			return 0; // Continue editing

		case TERSE_EVENT_ARROW_LEFT:
			// Move cursor left by one character
			tprompt_cursor_move_left(&handle->buffer, 1);
			handle->input_state.has_goal_column = false;
			return 0; // Continue editing

		case TERSE_EVENT_ARROW_RIGHT:
			// Move cursor right by one character
			tprompt_cursor_move_right(&handle->buffer, 1);
			handle->input_state.has_goal_column = false;
			return 0; // Continue editing

		default:
			break;
		}
	}

	// Handle Home key - staged movement (Claude Code style)
	// First press: move to physical line start, second press: move to logical line start
	if (event->type == TERSE_EVENT_HOME) {
		// Check if this is a consecutive Home press
		bool is_consecutive = (handle->input_state.last_key_type == TERSE_EVENT_HOME && handle->input_state.last_cursor_pos == handle->buffer.cursor);

		if (is_consecutive) {
			// Second press: move to logical line start
			tprompt_cursor_move_to_logical_line_start(handle);
		} else {
			// First press: move to physical line start
			tprompt_cursor_move_to_physical_line_start(handle);
		}

		// Update input state for next key press
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0; // Continue editing
	}

	// Handle End key - staged movement (Claude Code style)
	// First press: move to physical line end, second press: move to logical line end
	if (event->type == TERSE_EVENT_END) {
		// Check if this is a consecutive End press
		bool is_consecutive = (handle->input_state.last_key_type == TERSE_EVENT_END && handle->input_state.last_cursor_pos == handle->buffer.cursor);

		if (is_consecutive) {
			// Second press: move to logical line end
			tprompt_cursor_move_to_logical_line_end(handle);
		} else {
			// First press: move to physical line end
			tprompt_cursor_move_to_physical_line_end(handle);
		}

		// Update input state for next key press
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0; // Continue editing
	}

	// Check custom keybindings first (takes precedence over default behavior)
	int custom_action = tprompt_find_keybinding_action(handle, event);
	if (custom_action != TPROMPT_ACTION_NONE) {
		switch (custom_action) {
		case TPROMPT_ACTION_CONFIRM_INPUT:
			// Mark confirmation as pending (will be validated in main loop)
			handle->pending_confirmation = true;
			return 0;

		case TPROMPT_ACTION_INSERT_NEWLINE:
			// Insert newline at cursor position
			if (tprompt_buffer_insert(&handle->buffer, "\n", 1) != 0) {
				tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
					"Failed to insert newline: buffer at %zu/%zu bytes",
					handle->buffer.length, handle->buffer.size);
				return -1;
			}
			return 0; // Continue editing

		// Future actions can be added here
		default:
			// Unknown action, fall through to default behavior
			break;
		}
	}

	// Handle Enter key behavior based on mode and modifiers (default behavior)
	if (event->type == TERSE_EVENT_ENTER) {
		int mods = event->data.key.mods;
		bool is_multiline = (handle->options.flags & TPROMPT_FLAG_MULTILINE) != 0;

		// Ctrl+Enter or Alt+Enter: confirm input (always accept, but call validation for logging/statistics)
		if ((mods & TERSE_MOD_CTRL) || (mods & TERSE_MOD_ALT)) {
			// Call validation callback if configured (for logging/statistics), but always accept
			if (handle->options.validation_callback) {
				handle->options.validation_callback(
					handle->buffer.data,
					handle->buffer.length,
					handle->options.validation_user_data);
				// Ignore result - always confirm
			}
			return 1; // Immediately confirm input, regardless of validation result
		}

		// Plain Enter: behavior depends on mode and validation callback
		// Mark confirmation as pending (will be validated in main loop)
		handle->pending_confirmation = true;
		return 0;
	}

	// Handle Ctrl+D as EOF
	if (event->type == TERSE_EVENT_CHAR && event->data.ch.scalar == 4 && // Ctrl+D
		(event->data.ch.mods & TERSE_MOD_CTRL)) {
		// Mark confirmation as pending (will be validated in main loop)
		handle->pending_confirmation = true;
		return 0;
	}

	// Handle Ctrl+P (previous history) - Emacs-style binding
	if (event->type == TERSE_EVENT_CHAR && event->data.ch.scalar == 'p' && // Ctrl+P
		(event->data.ch.mods & TERSE_MOD_CTRL)) {
		// Check if buffer has multiple lines
		bool has_multiple_lines = tprompt_buffer_has_newlines(handle);

		if (has_multiple_lines) {
			// In multi-line mode: check if at first line
			size_t current_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
			if (current_line == 0) {
				// At first line, navigate to previous history entry
				// Save current input if not already in history navigation
				if (!handle->history.current) {
					if (handle->history.saved_input) {
						free(handle->history.saved_input);
					}
					handle->history.saved_input = strdup(handle->buffer.data);
				}
				const char *entry = tprompt_history_prev(&handle->history);
				if (entry) {
					tprompt_buffer_set(&handle->buffer, entry);
					tprompt_display_mark_all_dirty(handle);
				}
			} else {
				// Not at first line, navigate up within buffer
				tprompt_cursor_move_up(handle);
			}
		} else {
			// Single line mode: always navigate history
			// Save current input if not already in history navigation
			if (!handle->history.current) {
				if (handle->history.saved_input) {
					free(handle->history.saved_input);
				}
				handle->history.saved_input = strdup(handle->buffer.data);
			}
			const char *entry = tprompt_history_prev(&handle->history);
			if (entry) {
				tprompt_buffer_set(&handle->buffer, entry);
				tprompt_display_mark_all_dirty(handle);
			}
		}
		return 0;
	}

	// Handle Ctrl+N (next history) - Emacs-style binding
	if (event->type == TERSE_EVENT_CHAR && event->data.ch.scalar == 'n' && // Ctrl+N
		(event->data.ch.mods & TERSE_MOD_CTRL)) {
		// Check if buffer has multiple lines
		bool has_multiple_lines = tprompt_buffer_has_newlines(handle);

		if (has_multiple_lines) {
			// In multi-line mode: check if at last line
			size_t current_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
			size_t total_lines = tprompt_count_logical_lines(handle);
			if (current_line >= total_lines - 1) {
				// At last line, navigate to next history entry
				const char *entry = tprompt_history_next(&handle->history);
				if (entry) {
					tprompt_buffer_set(&handle->buffer, entry);
					tprompt_display_mark_all_dirty(handle);
				} else {
					// At the end of history, restore saved input
					if (handle->history.saved_input) {
						tprompt_buffer_set(&handle->buffer, handle->history.saved_input);
					} else {
						tprompt_buffer_clear(&handle->buffer);
					}
					tprompt_display_mark_all_dirty(handle);
				}
			} else {
				// Not at last line, navigate down within buffer
				tprompt_cursor_move_down(handle);
			}
		} else {
			// Single line mode: always navigate history
			const char *entry = tprompt_history_next(&handle->history);
			if (entry) {
				tprompt_buffer_set(&handle->buffer, entry);
				tprompt_display_mark_all_dirty(handle);
			} else {
				// At the end of history, restore saved input
				if (handle->history.saved_input) {
					tprompt_buffer_set(&handle->buffer, handle->history.saved_input);
				} else {
					tprompt_buffer_clear(&handle->buffer);
				}
				tprompt_display_mark_all_dirty(handle);
			}
		}
		return 0;
	}

	// Handle left/right arrow keys for cursor movement
	if (event->type == TERSE_EVENT_ARROW_LEFT) {
		tprompt_cursor_move_left(&handle->buffer, 1);
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	if (event->type == TERSE_EVENT_ARROW_RIGHT) {
		tprompt_cursor_move_right(&handle->buffer, 1);
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle backspace key
	if (event->type == TERSE_EVENT_BACKSPACE) {
		size_t old_length = handle->buffer.length;
		size_t deleted_bytes = tprompt_buffer_delete_before(&handle->buffer, 1);
		if (deleted_bytes > 0) {
			// Mark from new cursor position to old end as dirty (characters shift left)
			tprompt_display_mark_dirty_range(handle, handle->buffer.cursor, old_length);
		}
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle delete key
	if (event->type == TERSE_EVENT_DELETE) {
		size_t old_length = handle->buffer.length;
		size_t deleted_bytes = tprompt_buffer_delete_at(&handle->buffer, 1);
		if (deleted_bytes > 0) {
			// Mark from cursor to old end as dirty (characters shift left)
			tprompt_display_mark_dirty_range(handle, handle->buffer.cursor, old_length);
		}
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle Tab key (when completion is not active)
	if (event->type == TERSE_EVENT_TAB) {
		// Insert tab character
		if (tprompt_buffer_insert(&handle->buffer, "\t", 1) != 0) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to insert tab character: buffer at %zu/%zu bytes",
				handle->buffer.length, handle->buffer.size);
			return -1;
		}
		// Tab affects display width, force full redraw
		tprompt_display_mark_all_dirty(handle);
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// For any other key, clear the last key tracking and goal column
	handle->input_state.last_key_type = event->type;
	handle->input_state.last_cursor_pos = handle->buffer.cursor;

	// Clear goal column on non-vertical movements
	handle->input_state.has_goal_column = false;

	return 0; // Continue editing
}

bool tprompt_is_completion_trigger(tprompt_handle_t handle, char ch)
{
	if (!handle || !handle->completion_prefixes || !handle->completion_callback) {
		return false;
	}

	// Check if the character is in the completion_prefixes string
	return strchr(handle->completion_prefixes, ch) != NULL;
}

bool tprompt_buffer_has_newlines(tprompt_handle_t handle)
{
	if (!handle || !handle->buffer.data) {
		return false;
	}

	// Check if buffer contains any newline characters
	return memchr(handle->buffer.data, '\n', handle->buffer.length) != NULL;
}

/* ========================================================================
 * Custom Keybindings - Internal Helpers
 * ======================================================================== */

int tprompt_find_keybinding_action(tprompt_handle_t handle, const terse_event_t *event)
{
	if (!handle || !event || !handle->keybindings) {
		return TPROMPT_ACTION_NONE;
	}

	for (size_t i = 0; i < handle->keybinding_count; i++) {
		const tprompt_keybinding_t *kb = &handle->keybindings[i];

		// Check key type match
		if (kb->key != event->type) {
			continue;
		}

		// Extract modifiers from event based on type
		int event_mods = 0;
		if (event->type == TERSE_EVENT_CHAR) {
			event_mods = event->data.ch.mods;
		} else if (event->type == TERSE_EVENT_FUNCTION) {
			event_mods = event->data.function.mods;
		} else {
			// For other key types (ENTER, BACKSPACE, arrows, etc.)
			event_mods = event->data.key.mods;
		}

		// Check modifier match
		if (kb->modifiers != event_mods) {
			continue;
		}

		// Event-type-specific matching
		if (event->type == TERSE_EVENT_CHAR) {
			// For character events, also match the scalar value
			if (kb->data.scalar != event->data.ch.scalar) {
				continue;
			}
		} else if (event->type == TERSE_EVENT_FUNCTION) {
			// For function keys, match the function number
			if (kb->data.function_num != event->data.function.number) {
				continue;
			}
		}

		// Match found!
		return kb->action;
	}

	return TPROMPT_ACTION_NONE;
}

int tprompt_validate_keybindings(const tprompt_keybinding_t *bindings,
	size_t count,
	tprompt_error_info_t *error)
{
	// NULL bindings with count > 0 is a critical error
	if (!bindings && count > 0) {
		tprompt_set_error(error, TPROMPT_ERROR_INVALID_ARGS, 0,
			"NULL keybindings array with count > 0");
		return -1;
	}

	// NULL or count == 0 is valid (means no custom bindings)
	if (!bindings || count == 0) {
		return 0;
	}

	int warning_count = 0;
	char warning_msg[256] = { 0 };

	for (size_t i = 0; i < count; i++) {
		// Check for unknown/invalid action values
		if (bindings[i].action < 0 || bindings[i].action > TPROMPT_ACTION_INSERT_NEWLINE) {
			warning_count++;
			snprintf(warning_msg, sizeof(warning_msg),
				"Warning: keybinding %zu has unknown action %d (will be ignored)",
				i, bindings[i].action);
			// Just record warning, don't fail
		}

		// Check for duplicates
		for (size_t j = i + 1; j < count; j++) {
			if (bindings[i].key != bindings[j].key) {
				continue;
			}
			if (bindings[i].modifiers != bindings[j].modifiers) {
				continue;
			}

			// For CHAR and FUNCTION events, also check data field
			bool is_duplicate = false;
			if (bindings[i].key == TERSE_EVENT_CHAR) {
				is_duplicate = (bindings[i].data.scalar == bindings[j].data.scalar);
			} else if (bindings[i].key == TERSE_EVENT_FUNCTION) {
				is_duplicate = (bindings[i].data.function_num == bindings[j].data.function_num);
			} else {
				// For other key types, key+modifiers is enough to determine duplicate
				is_duplicate = true;
			}

			if (is_duplicate) {
				warning_count++;
				snprintf(warning_msg, sizeof(warning_msg),
					"Warning: duplicate keybinding at indices %zu and %zu", i, j);
			}
		}
	}

	// Record warnings in error info if any
	if (warning_count > 0 && error) {
		tprompt_set_error(error, TPROMPT_ERROR_NONE, 0,
			"%s (total %d warnings)", warning_msg, warning_count);
	}

	return 0; // Success (warnings don't cause failure)
}

/* ========================================================================
 * Logical Line Navigation - Internal Helpers (Phase 5)
 * ======================================================================== */

/**
 * @brief Count total number of logical lines in buffer
 *
 * A logical line is delimited by explicit newline characters (\n).
 * Empty buffer has 1 logical line.
 *
 * @param handle Prompt handle
 * @return Number of logical lines (minimum 1)
 */
size_t tprompt_count_logical_lines(tprompt_handle_t handle)
{
	if (!handle || !handle->buffer.data) {
		return 1;
	}

	// Count newline characters in buffer
	size_t line_count = 1; // Always at least 1 logical line
	const char *data = handle->buffer.data;
	size_t length = handle->buffer.length;

	for (size_t i = 0; i < length; i++) {
		if (data[i] == '\n') {
			line_count++;
		}
	}

	return line_count;
}

/**
 * @brief Get logical line number containing given byte offset
 *
 * Lines are 0-indexed. Counts number of newlines before the offset.
 *
 * @param handle Prompt handle
 * @param byte_offset Byte offset in buffer (clamped to buffer length)
 * @return Logical line number (0-based)
 */
size_t tprompt_get_logical_line_at_offset(tprompt_handle_t handle, size_t byte_offset)
{
	if (!handle || !handle->buffer.data) {
		return 0;
	}

	// Clamp offset to buffer length
	if (byte_offset > handle->buffer.length) {
		byte_offset = handle->buffer.length;
	}

	// Count newlines before the offset
	size_t line_number = 0;
	const char *data = handle->buffer.data;

	for (size_t i = 0; i < byte_offset; i++) {
		if (data[i] == '\n') {
			line_number++;
		}
	}

	return line_number;
}

/**
 * @brief Get byte range (start and end offsets) for a logical line
 *
 * Finds the start and end byte offsets of the specified logical line.
 * The end offset is exclusive (points to the newline or end of buffer).
 *
 * Algorithm:
 * 1. Iterate through buffer counting newlines until we reach the target line
 * 2. Record the start offset (byte after previous newline, or 0 for first line)
 * 3. Continue iterating until next newline or end of buffer
 * 4. Record the end offset (byte at newline or end of buffer)
 *
 * @param handle Prompt handle
 * @param logical_line Logical line number (0-based)
 * @param out_start Output: byte offset of line start (inclusive)
 * @param out_end Output: byte offset of line end (exclusive, at \n or buffer end)
 * @return 0 on success, -1 if line number is out of range
 */
int tprompt_get_logical_line_bounds(tprompt_handle_t handle, size_t logical_line,
	size_t *out_start, size_t *out_end)
{
	if (!handle || !handle->buffer.data || !out_start || !out_end) {
		return -1;
	}

	const char *data = handle->buffer.data;
	size_t length = handle->buffer.length;
	size_t current_line = 0;
	size_t line_start = 0;

	// Find the start of the requested logical line
	for (size_t i = 0; i < length; i++) {
		if (current_line == logical_line) {
			line_start = i;
			break;
		}
		if (data[i] == '\n') {
			current_line++;
			line_start = i + 1; // Start of next line is after newline
		}
	}

	// Check if we found the requested line
	if (current_line != logical_line) {
		// Line number is beyond buffer content
		return -1;
	}

	// Find the end of the logical line (next newline or end of buffer)
	size_t line_end = line_start;
	for (size_t i = line_start; i < length; i++) {
		if (data[i] == '\n') {
			line_end = i; // End is at newline (exclusive)
			break;
		}
		line_end = i + 1; // If no newline found, end is at buffer end
	}

	*out_start = line_start;
	*out_end = line_end;
	return 0;
}

size_t tprompt_get_logical_line_length(tprompt_handle_t handle, size_t logical_line)
{
	size_t start, end;
	if (tprompt_get_logical_line_bounds(handle, logical_line, &start, &end) != 0) {
		return 0;
	}

	return end - start;
}

/* ========================================================================
 * Public API - Simple Wrapper
 * ======================================================================== */

char *tprompt(const char *prompt_text)
{
	// Create options with defaults
	tprompt_options_t opts = {
		.prompt = prompt_text ? prompt_text : DEFAULT_PROMPT,
		.history_file = NULL,
		.max_input_size = 1024 * 1024,
		.max_history_size = 100,
		.completion_callback = NULL,
		.completion_user_data = NULL,
		.completion_prefixes = NULL,
		.terse_handle = NULL,
		.flags = 0
	};

	// Open temporary handle
	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		return NULL;
	}

	// Read one line
	char *result = tprompt_readline(handle, NULL);

	// If readline failed, copy the error to global error before closing handle
	if (result == NULL && handle->last_error.category != TPROMPT_ERROR_NONE) {
		tprompt_global_error = handle->last_error;
	}

	// Close handle
	tprompt_close(handle);

	return result;
}

/* ========================================================================
 * Public API - Framework: Initialization and Cleanup
 * ======================================================================== */

tprompt_handle_t tprompt_open(const tprompt_options_t *options)
{
	tprompt_clear_error(&tprompt_global_error);

	// Allocate handle
	tprompt_handle_t handle = calloc(1, sizeof(struct tprompt_handle));
	if (!handle) {
		tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
			"Failed to allocate handle");
		return NULL;
	}

	// Use default options if none provided
	tprompt_options_t default_opts = {
		.prompt = DEFAULT_PROMPT,
		.history_file = NULL,
		.max_input_size = 1024 * 1024,
		.max_history_size = 100,
		.completion_callback = NULL,
		.completion_user_data = NULL,
		.completion_prefixes = NULL,
		.terse_handle = NULL,
		.flags = TPROMPT_FLAG_MULTILINE,
		.custom_keybindings = NULL,
		.keybinding_count = 0,
		.validation_callback = NULL,
		.validation_user_data = NULL
	};

	if (!options) {
		options = &default_opts;
	}

	// Copy options
	handle->options = *options;
	if (handle->options.prompt == NULL) {
		handle->options.prompt = DEFAULT_PROMPT;
	}

	// Initialize terse handle
	if (options->terse_handle) {
		handle->terse = options->terse_handle;
		handle->owns_terse = false;
	} else {
		// Try TERSE_PROFILE_AUTO first, fall back to TERSE_P0 for non-TTY environments
		handle->terse = terse_open(TERSE_PROFILE_AUTO, NULL);
		if (!handle->terse) {
			// Fall back to P0 (basic profile) which works in non-TTY environments
			handle->terse = terse_open(TERSE_P0, NULL);
		}
		if (!handle->terse) {
			tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_TERSE, errno,
				"Failed to create terse handle");
			free(handle);
			return NULL;
		}
		handle->owns_terse = true;
	}

	// Initialize raw mode flag
#if defined(__unix__) || defined(__APPLE__)
	handle->raw_mode_active = false;
#endif

	// Enable enhanced keyboard features if supported
	unsigned int keyboard_supported = terse_keyboard_get_supported(handle->terse);
	if (keyboard_supported & TERSE_KEYBOARD_FEATURE_MODIFY_OTHER_KEYS) {
		terse_keyboard_enable(handle->terse, TERSE_KEYBOARD_FEATURE_MODIFY_OTHER_KEYS);
	}
	if (keyboard_supported & TERSE_KEYBOARD_FEATURE_KITTY_PROTOCOL) {
		terse_keyboard_enable(handle->terse, TERSE_KEYBOARD_FEATURE_KITTY_PROTOCOL);
	}

	// Initialize buffer
	if (tprompt_buffer_init(&handle->buffer, DEFAULT_BUFFER_SIZE) != 0) {
		tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
			"Failed to initialize buffer");
		if (handle->owns_terse) {
			terse_close(handle->terse);
		}
		free(handle);
		return NULL;
	}

	// Initialize history
	tprompt_history_init(&handle->history, options->max_history_size);

	// Load history from file if specified
	if (options->history_file && !(options->flags & TPROMPT_FLAG_DISABLE_HISTORY)) {
		handle->history_file_path = strdup(options->history_file);
		if (!handle->history_file_path) {
			tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to allocate history file path");
			tprompt_history_free(&handle->history);
			tprompt_buffer_free(&handle->buffer);
			if (handle->owns_terse) {
				terse_close(handle->terse);
			}
			free(handle);
			return NULL;
		}
		tprompt_history_load_internal(&handle->history, handle->history_file_path);
		// Ignore errors on load
	}

	// Initialize completion
	tprompt_completion_init(&handle->completion_state);
	handle->completion_callback = options->completion_callback;
	handle->completion_user_data = options->completion_user_data;
	if (options->completion_prefixes) {
		handle->completion_prefixes = strdup(options->completion_prefixes);
		if (!handle->completion_prefixes) {
			tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to allocate completion prefixes");
			free(handle->history_file_path);
			tprompt_history_free(&handle->history);
			tprompt_buffer_free(&handle->buffer);
			if (handle->owns_terse) {
				terse_close(handle->terse);
			}
			free(handle);
			return NULL;
		}
	}

	// Initialize display state
	handle->display.physical_line = 0;
	handle->display.physical_column = 0;
	handle->display.total_physical_lines = 0;
	handle->display.prev_total_physical_lines = 0;
	handle->display.terminal_width = 80; // Will be updated from terse
	handle->display.terminal_height = 24;
	handle->display.start_row = 0;
	handle->display.start_row_known = false;

	// Initialize input state
	handle->input_state.last_key_type = 0;
	handle->input_state.last_cursor_pos = 0;
	handle->input_state.goal_column = 0;
	handle->input_state.has_goal_column = false;

	// Initialize validation state
	handle->pending_confirmation = false;
	handle->validation_error_msg = NULL;

	// Store prompt
	handle->prompt = strdup(options->prompt);
	if (!handle->prompt) {
		tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
			"Failed to allocate prompt string");
		free(handle->completion_prefixes);
		tprompt_completion_free(&handle->completion_state);
		free(handle->history_file_path);
		tprompt_history_free(&handle->history);
		tprompt_buffer_free(&handle->buffer);
		if (handle->owns_terse) {
			terse_close(handle->terse);
		}
		free(handle);
		return NULL;
	}

	// Copy and validate custom keybindings
	handle->keybindings = NULL;
	handle->keybinding_count = 0;
	if (options->custom_keybindings && options->keybinding_count > 0) {
		// Validate keybindings first
		tprompt_error_info_t validation_error = { 0 };
		if (tprompt_validate_keybindings(options->custom_keybindings,
				options->keybinding_count,
				&validation_error)
			!= 0) {
			// Critical validation error
			tprompt_set_error(&tprompt_global_error, validation_error.category,
				validation_error.code, "%s", validation_error.message);
			free(handle->prompt);
			free(handle->completion_prefixes);
			tprompt_completion_free(&handle->completion_state);
			free(handle->history_file_path);
			tprompt_history_free(&handle->history);
			tprompt_buffer_free(&handle->buffer);
			if (handle->owns_terse) {
				terse_close(handle->terse);
			}
			free(handle);
			return NULL;
		}

		// Copy keybindings array
		size_t bindings_size = sizeof(tprompt_keybinding_t) * options->keybinding_count;
		handle->keybindings = malloc(bindings_size);
		if (!handle->keybindings) {
			tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to allocate keybindings array");
			free(handle->prompt);
			free(handle->completion_prefixes);
			tprompt_completion_free(&handle->completion_state);
			free(handle->history_file_path);
			tprompt_history_free(&handle->history);
			tprompt_buffer_free(&handle->buffer);
			if (handle->owns_terse) {
				terse_close(handle->terse);
			}
			free(handle);
			return NULL;
		}
		memcpy(handle->keybindings, options->custom_keybindings, bindings_size);
		handle->keybinding_count = options->keybinding_count;

		// Record any validation warnings in handle error info (non-fatal)
		if (validation_error.category != TPROMPT_ERROR_NONE) {
			handle->last_error = validation_error;
		}
	}

	// Clear error if no warnings
	if (handle->last_error.category == TPROMPT_ERROR_NONE) {
		tprompt_clear_error(&handle->last_error);
	}

	// Initialize buffer-based rendering (Phase 1-3)
	if (tprompt_buffer_based_rendering_init(handle) != 0) {
		// Non-fatal: buffer-based rendering is optional optimization
		// Continue without it
	}

	return handle;
}

void tprompt_close(tprompt_handle_t handle)
{
	if (!handle) {
		return;
	}

	// Save history if needed
	if (handle->history_file_path && !(handle->options.flags & TPROMPT_FLAG_NO_AUTO_SAVE)) {
		tprompt_history_save_internal(&handle->history, handle->history_file_path);
		// Ignore errors on save
	}

	// Free buffer-based rendering resources (Phase 1-3)
	tprompt_buffer_based_rendering_free(handle);

	// Free resources
	free(handle->history_file_path);
	free(handle->prompt);
	free(handle->completion_prefixes);
	free(handle->keybindings);
	free(handle->validation_error_msg);

	tprompt_completion_free(&handle->completion_state);
	tprompt_history_free(&handle->history);
	tprompt_buffer_free(&handle->buffer);

	// Close terse handle if we own it
	if (handle->owns_terse && handle->terse) {
		terse_close(handle->terse);
	}

	free(handle);
}

/* ========================================================================
 * Public API - Framework: Editing Session
 * ======================================================================== */

char *tprompt_readline(tprompt_handle_t handle, const char *prompt_override)
{
	if (!handle) {
		errno = EINVAL;
		return NULL;
	}

	tprompt_clear_error(&handle->last_error);

	// Clear buffer for new input
	tprompt_buffer_clear(&handle->buffer);

	// Reset history navigation
	tprompt_history_reset_position(&handle->history);

	// Reset display state for new readline session
	handle->display.start_row = 0; // Will be set on first render
	handle->display.start_row_known = false;

	// Mark display as needing full render for initial draw
	tprompt_display_mark_all_dirty(handle);

	// Update prompt if override is provided
	if (prompt_override) {
		free(handle->prompt);
		handle->prompt = strdup(prompt_override);
		if (!handle->prompt) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to allocate prompt override");
			return NULL;
		}
	}

	// Enable raw mode
#if defined(__unix__) || defined(__APPLE__)
	if (tcgetattr(STDIN_FILENO, &handle->original_termios) == 0) {
		struct termios raw = handle->original_termios;
		raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
		raw.c_oflag &= ~(OPOST);
		raw.c_cflag |= CS8;
		raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
		raw.c_cc[VMIN] = 1;
		raw.c_cc[VTIME] = 0;
		if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
			handle->raw_mode_active = true;
		}
	}
#endif

	// Initial render
	if (tprompt_display_render_buffered(handle) != 0) {
		// Restore terminal on error
#if defined(__unix__) || defined(__APPLE__)
		if (handle->raw_mode_active) {
			tcsetattr(STDIN_FILENO, TCSANOW, &handle->original_termios);
			handle->raw_mode_active = false;
		}
#endif
		return NULL;
	}

	// Main event loop
	while (1) {
		// Wait for next event
		terse_event_t event;
		terse_error_t result = terse_read_event(handle->terse, -1, &event); // -1 = wait indefinitely
		if (result != TERSE_OK) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)result,
				"Failed to read event");
			return NULL;
		}

		// Handle different event types
		if (event.type == TERSE_EVENT_CHAR) {
			unsigned int scalar = event.data.ch.scalar;
			int mods = event.data.ch.mods;

			// Check custom keybindings first (before built-in Ctrl shortcuts)
			int custom_action = tprompt_find_keybinding_action(handle, &event);
			if (custom_action != TPROMPT_ACTION_NONE) {
				int key_result = tprompt_handle_key_event(handle, &event);
				if (key_result == 1) {
					break; // Confirm input
				} else if (key_result == -1) {
					return NULL; // Error
				}
				// key_result == 0: continue editing
			}
			// Handle Ctrl+W - delete word backward
			else if (scalar == 'w' && (mods & TERSE_MOD_CTRL)) { // Ctrl+W
				tprompt_key_handle_ctrl_w(handle);
				handle->input_state.has_goal_column = false;
			}
			// Handle Ctrl+K - delete to end of line
			else if (scalar == 'k' && (mods & TERSE_MOD_CTRL)) { // Ctrl+K
				tprompt_key_handle_ctrl_k(handle);
				handle->input_state.has_goal_column = false;
			}
			// Handle Ctrl+U - delete to start of line
			else if (scalar == 'u' && (mods & TERSE_MOD_CTRL)) { // Ctrl+U
				tprompt_key_handle_ctrl_u(handle);
				handle->input_state.has_goal_column = false;
			}
			// Handle Ctrl+A - move to start of line
			else if (scalar == 'a' && (mods & TERSE_MOD_CTRL)) { // Ctrl+A
				tprompt_key_handle_ctrl_a(handle);
				handle->input_state.has_goal_column = false;
			}
			// Handle Ctrl+E - move to end of line
			else if (scalar == 'e' && (mods & TERSE_MOD_CTRL)) { // Ctrl+E
				tprompt_key_handle_ctrl_e(handle);
				handle->input_state.has_goal_column = false;
			}
			// Handle Ctrl+P - previous history (Emacs-style)
			else if (scalar == 'p' && (mods & TERSE_MOD_CTRL)) { // Ctrl+P
				int key_result = tprompt_handle_key_event(handle, &event);
				if (key_result == 1) {
					break; // Confirm input
				} else if (key_result == -1) {
					return NULL; // Error
				}
				// key_result == 0: continue editing
			}
			// Handle Ctrl+N - next history (Emacs-style)
			else if (scalar == 'n' && (mods & TERSE_MOD_CTRL)) { // Ctrl+N
				int key_result = tprompt_handle_key_event(handle, &event);
				if (key_result == 1) {
					break; // Confirm input
				} else if (key_result == -1) {
					return NULL; // Error
				}
				// key_result == 0: continue editing
			}
			// Handle Ctrl+D (EOF-like behavior)
			else if (scalar == 'd' && (mods & TERSE_MOD_CTRL)) { // Ctrl+D
				if (handle->buffer.length == 0) {
					tprompt_clear_error(&handle->last_error); // EOF is not an error
															  // Restore terminal before returning
#if defined(__unix__) || defined(__APPLE__)
					if (handle->raw_mode_active) {
						tcsetattr(STDIN_FILENO, TCSANOW, &handle->original_termios);
						handle->raw_mode_active = false;
					}
#endif
					return NULL;
				}
				// If buffer is not empty, ignore Ctrl+D
			}
			// Regular character input - convert scalar to UTF-8
			else if (!(mods & TERSE_MOD_CTRL)) { // Ignore unhandled Ctrl combinations
				char utf8_buf[5];
				int len = 0;

				// Simple UTF-8 encoding
				if (scalar < 0x80) {
					utf8_buf[len++] = (char)scalar;
				} else if (scalar < 0x800) {
					utf8_buf[len++] = (char)(0xC0 | (scalar >> 6));
					utf8_buf[len++] = (char)(0x80 | (scalar & 0x3F));
				} else if (scalar < 0x10000) {
					utf8_buf[len++] = (char)(0xE0 | (scalar >> 12));
					utf8_buf[len++] = (char)(0x80 | ((scalar >> 6) & 0x3F));
					utf8_buf[len++] = (char)(0x80 | (scalar & 0x3F));
				} else if (scalar < 0x110000) {
					utf8_buf[len++] = (char)(0xF0 | (scalar >> 18));
					utf8_buf[len++] = (char)(0x80 | ((scalar >> 12) & 0x3F));
					utf8_buf[len++] = (char)(0x80 | ((scalar >> 6) & 0x3F));
					utf8_buf[len++] = (char)(0x80 | (scalar & 0x3F));
				}
				utf8_buf[len] = '\0';

				// Use char input handler for completion trigger detection
				if (tprompt_handle_char_input(handle, utf8_buf, event.data.ch.width) != 0) {
					tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
						"Failed to insert character (scalar=0x%X) into buffer", scalar);
					return NULL;
				}
				handle->input_state.has_goal_column = false;
			}
		} else if (event.type == TERSE_EVENT_ENTER) {
			// Handle Enter key through tprompt_handle_key_event (supports custom keybindings)
			int key_result = tprompt_handle_key_event(handle, &event);
			if (key_result == 1) {
				break; // Confirm input
			} else if (key_result == -1) {
				return NULL; // Error
			}
			// key_result == 0: continue editing (newline was inserted)
		} else if (event.type == TERSE_EVENT_BACKSPACE) {
			tprompt_buffer_delete_before(&handle->buffer, 1);
			handle->input_state.has_goal_column = false;
		} else if (event.type == TERSE_EVENT_DELETE) {
			tprompt_buffer_delete_at(&handle->buffer, 1);
			handle->input_state.has_goal_column = false;
		} else if (event.type == TERSE_EVENT_ARROW_LEFT) {
			if (event.data.key.mods & TERSE_MOD_CTRL) {
				tprompt_cursor_move_word_backward(&handle->buffer);
			} else {
				tprompt_cursor_move_left(&handle->buffer, 1);
			}
			handle->input_state.has_goal_column = false;
		} else if (event.type == TERSE_EVENT_ARROW_RIGHT) {
			if (event.data.key.mods & TERSE_MOD_CTRL) {
				tprompt_cursor_move_word_forward(&handle->buffer);
			} else {
				tprompt_cursor_move_right(&handle->buffer, 1);
			}
			handle->input_state.has_goal_column = false;
		} else if (event.type == TERSE_EVENT_ARROW_UP || event.type == TERSE_EVENT_ARROW_DOWN) {
			// Handle UP/DOWN through tprompt_handle_key_event (supports history navigation)
			int key_result = tprompt_handle_key_event(handle, &event);
			if (key_result == 1) {
				break; // Confirm input (should not happen for arrows, but handle it)
			} else if (key_result == -1) {
				return NULL; // Error
			}
			// key_result == 0: continue editing
		} else if (event.type == TERSE_EVENT_HOME) {
			// Staged Home key behavior (Claude Code style)
			// First press: move to physical line start, second press: move to logical line start
			bool is_consecutive = (handle->input_state.last_key_type == TERSE_EVENT_HOME && handle->input_state.last_cursor_pos == handle->buffer.cursor);

			if (is_consecutive) {
				// Second press: move to logical line start
				tprompt_cursor_move_to_logical_line_start(handle);
			} else {
				// First press: move to physical line start
				tprompt_cursor_move_to_physical_line_start(handle);
			}

			// Update input state for next key press
			handle->input_state.last_key_type = event.type;
			handle->input_state.last_cursor_pos = handle->buffer.cursor;
			handle->input_state.has_goal_column = false;
		} else if (event.type == TERSE_EVENT_END) {
			// Staged End key behavior (Claude Code style)
			// First press: move to physical line end, second press: move to logical line end
			bool is_consecutive = (handle->input_state.last_key_type == TERSE_EVENT_END && handle->input_state.last_cursor_pos == handle->buffer.cursor);

			if (is_consecutive) {
				// Second press: move to logical line end
				tprompt_cursor_move_to_logical_line_end(handle);
			} else {
				// First press: move to physical line end
				tprompt_cursor_move_to_physical_line_end(handle);
			}

			// Update input state for next key press
			handle->input_state.last_key_type = event.type;
			handle->input_state.last_cursor_pos = handle->buffer.cursor;
			handle->input_state.has_goal_column = false;
		} else if (event.type == TERSE_EVENT_TAB) {
			// Handle Tab key through tprompt_handle_key_event (supports completion)
			int key_result = tprompt_handle_key_event(handle, &event);
			if (key_result == 1) {
				break; // Confirm input (shouldn't happen for Tab, but handle it)
			} else if (key_result == -1) {
				return NULL; // Error
			}
			// key_result == 0: Tab was handled (completion confirmed or tab inserted)
		}

		// Track last event for staging behavior (unless already tracked by Home/End handlers)
		// Only track if not already set by Home/End key handlers
		if (event.type != TERSE_EVENT_HOME && event.type != TERSE_EVENT_END) {
			handle->input_state.last_key_type = event.type;
			handle->input_state.last_cursor_pos = handle->buffer.cursor;
		}

		// Check if input confirmation is pending (before rendering)
		if (handle->pending_confirmation) {
			handle->pending_confirmation = false; // Reset flag

			// Call validation callback if configured
			if (handle->options.validation_callback) {
				tprompt_validation_result_t validation_result = handle->options.validation_callback(
					handle->buffer.data,
					handle->buffer.length,
					handle->options.validation_user_data);

				if (validation_result == TPROMPT_VALIDATION_REJECT) {
					// Validation rejected - play beep, render, and continue editing
					terse_write_text(handle->terse, "\x07"); // Bell character (beep)
					terse_flush(handle->terse);
					// TODO: Display validation error message if provided
					// Re-render display before continuing
					if (tprompt_display_render_buffered(handle) != 0) {
						// Restore terminal on error
#if defined(__unix__) || defined(__APPLE__)
						if (handle->raw_mode_active) {
							tcsetattr(STDIN_FILENO, TCSANOW, &handle->original_termios);
							handle->raw_mode_active = false;
						}
#endif
						return NULL;
					}
					continue; // Stay in editing loop

				} else if (validation_result == TPROMPT_VALIDATION_CONTINUE) {
					// Validation wants to continue editing with newline
					bool is_multiline = (handle->options.flags & TPROMPT_FLAG_MULTILINE) != 0;

					if (is_multiline) {
						// Insert newline and continue editing
						if (tprompt_buffer_insert(&handle->buffer, "\n", 1) != 0) {
							tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
								"Failed to insert newline after validation: buffer at %zu/%zu bytes",
								handle->buffer.length, handle->buffer.size);
							return NULL;
						}
						// Re-render display before continuing
						if (tprompt_display_render_buffered(handle) != 0) {
							// Restore terminal on error
#if defined(__unix__) || defined(__APPLE__)
							if (handle->raw_mode_active) {
								tcsetattr(STDIN_FILENO, TCSANOW, &handle->original_termios);
								handle->raw_mode_active = false;
							}
#endif
							return NULL;
						}
						continue; // Stay in editing loop
					} else {
						// Single-line mode: CONTINUE treated as REJECT (can't insert newline)
						terse_write_text(handle->terse, "\x07"); // Bell character (beep)
						terse_flush(handle->terse);
						// Re-render display before continuing
						if (tprompt_display_render_buffered(handle) != 0) {
							// Restore terminal on error
#if defined(__unix__) || defined(__APPLE__)
							if (handle->raw_mode_active) {
								tcsetattr(STDIN_FILENO, TCSANOW, &handle->original_termios);
								handle->raw_mode_active = false;
							}
#endif
							return NULL;
						}
						continue; // Stay in editing loop
					}
				}
				// TPROMPT_VALIDATION_ACCEPT: fall through to break from loop
			} else {
				// No validation callback - behavior depends on mode
				bool is_multiline = (handle->options.flags & TPROMPT_FLAG_MULTILINE) != 0;

				if (is_multiline) {
					// Multiline mode without validation: insert newline
					if (tprompt_buffer_insert(&handle->buffer, "\n", 1) != 0) {
						tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
							"Failed to insert newline: buffer at %zu/%zu bytes",
							handle->buffer.length, handle->buffer.size);
						return NULL;
					}
					// Re-render display before continuing
					if (tprompt_display_render_buffered(handle) != 0) {
						// Restore terminal on error
#if defined(__unix__) || defined(__APPLE__)
						if (handle->raw_mode_active) {
							tcsetattr(STDIN_FILENO, TCSANOW, &handle->original_termios);
							handle->raw_mode_active = false;
						}
#endif
						return NULL;
					}
					continue; // Stay in editing loop
				}
				// Single-line mode without validation: confirm input
			}

			// Validation passed or no validation callback in single-line mode - confirm input
			break;
		}

		// Re-render display after each event (if not already handled by validation logic)
		if (tprompt_display_render_buffered(handle) != 0) {
			// Restore terminal on error
#if defined(__unix__) || defined(__APPLE__)
			if (handle->raw_mode_active) {
				tcsetattr(STDIN_FILENO, TCSANOW, &handle->original_termios);
				handle->raw_mode_active = false;
			}
#endif
			return NULL;
		}
	}

	// Restore terminal mode before returning
#if defined(__unix__) || defined(__APPLE__)
	if (handle->raw_mode_active) {
		tcsetattr(STDIN_FILENO, TCSANOW, &handle->original_termios);
		handle->raw_mode_active = false;
	}
#endif

	// Move to new line after input is confirmed
	// In raw mode, we need \r\n to move to the beginning of the next line
	terse_write_text(handle->terse, "\r\n");
	terse_flush(handle->terse);

	// Update start_row for next readline call if cursor position is not available
	// This tracks where the cursor is after moving to the next line
	if (handle->display.start_row_known) {
		int lines_used = (int)handle->display.total_physical_lines;
		handle->display.start_row += lines_used + 1; // +1 for the \r\n we just wrote

		// Wrap around if we exceed terminal height
		if (handle->display.start_row >= (int)handle->display.terminal_height) {
			handle->display.start_row = handle->display.terminal_height - 1;
		}
	}

	// Add to history if not empty
	if (handle->buffer.length > 0 && !(handle->options.flags & TPROMPT_FLAG_DISABLE_HISTORY)) {
		tprompt_history_add_internal(&handle->history, handle->buffer.data);
	}

	// Return a copy of the buffer
	char *result = strdup(handle->buffer.data);
	if (!result) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
			"Failed to allocate result string");
		return NULL;
	}

	return result;
}

/* ========================================================================
 * Public API - Framework: History Management
 * ======================================================================== */

int tprompt_history_add(tprompt_handle_t handle, const char *entry)
{
	if (!handle || !entry) {
		return -1;
	}

	if (handle->options.flags & TPROMPT_FLAG_DISABLE_HISTORY) {
		return 0;
	}

	return tprompt_history_add_internal(&handle->history, entry);
}

int tprompt_history_load(tprompt_handle_t handle, const char *file_path)
{
	if (!handle || !file_path) {
		return -1;
	}

	return tprompt_history_load_internal(&handle->history, file_path);
}

int tprompt_history_save(tprompt_handle_t handle, const char *file_path)
{
	if (!handle || !file_path) {
		return -1;
	}

	return tprompt_history_save_internal(&handle->history, file_path);
}

void tprompt_history_clear(tprompt_handle_t handle)
{
	if (!handle) {
		return;
	}

	tprompt_history_free(&handle->history);
	tprompt_history_init(&handle->history, handle->options.max_history_size);
}

void tprompt_history_set_max_size(tprompt_handle_t handle, size_t max_size)
{
	if (!handle) {
		return;
	}

	handle->history.max_size = max_size;

	// Trim history if current count exceeds new max_size
	while (handle->history.max_size > 0 && handle->history.count > handle->history.max_size) {
		if (!handle->history.tail) {
			break; // Safety check
		}

		tprompt_history_entry_t *old_tail = handle->history.tail;
		handle->history.tail = old_tail->prev;

		if (handle->history.tail) {
			handle->history.tail->next = NULL;
		} else {
			// List became empty
			handle->history.head = NULL;
		}

		free(old_tail->text);
		free(old_tail);
		handle->history.count--;
	}

	// Reset navigation position if history became empty
	if (handle->history.count == 0) {
		handle->history.current = NULL;
	}
}

/* ========================================================================
 * Public API - Framework: Completion
 * ======================================================================== */

void tprompt_set_completion_callback(tprompt_handle_t handle,
	tprompt_completion_fn callback,
	void *user_data)
{
	if (!handle) {
		return;
	}

	handle->completion_callback = callback;
	handle->completion_user_data = user_data;
}

int tprompt_set_completion_prefixes(tprompt_handle_t handle, const char *prefixes)
{
	if (!handle) {
		return -1;
	}

	free(handle->completion_prefixes);
	handle->completion_prefixes = NULL;

	if (prefixes && prefixes[0] != '\0') {
		handle->completion_prefixes = strdup(prefixes);
		if (!handle->completion_prefixes) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to allocate completion prefixes");
			return -1;
		}
	}

	return 0;
}

void tprompt_free_completion_result(tprompt_completion_result_t *result)
{
	if (!result) {
		return;
	}

	if (result->candidates) {
		for (size_t i = 0; i < result->count; i++) {
			free(result->candidates[i]);
		}
		free(result->candidates);
		result->candidates = NULL;
	}

	result->count = 0;
}

/* ========================================================================
 * Public API - Framework: Validation
 * ======================================================================== */

void tprompt_set_validation_callback(tprompt_handle_t handle,
	tprompt_validation_fn callback,
	void *user_data)
{
	if (!handle) {
		return;
	}

	handle->options.validation_callback = callback;
	handle->options.validation_user_data = user_data;
}

/* ========================================================================
 * Public API - Framework: Keybindings
 * ======================================================================== */

int tprompt_set_keybindings(tprompt_handle_t handle,
	const tprompt_keybinding_t *bindings,
	size_t count)
{
	if (!handle) {
		return -1;
	}

	// Clear error
	tprompt_clear_error(&handle->last_error);

	// Validate keybindings
	if (tprompt_validate_keybindings(bindings, count, &handle->last_error) != 0) {
		// Critical validation error
		return -1;
	}

	// Free existing keybindings
	free(handle->keybindings);
	handle->keybindings = NULL;
	handle->keybinding_count = 0;

	// If NULL or count == 0, we're done (cleared custom bindings)
	if (!bindings || count == 0) {
		return 0;
	}

	// Allocate and copy new keybindings
	size_t bindings_size = sizeof(tprompt_keybinding_t) * count;
	handle->keybindings = malloc(bindings_size);
	if (!handle->keybindings) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
			"Failed to allocate keybindings array");
		return -1;
	}

	memcpy(handle->keybindings, bindings, bindings_size);
	handle->keybinding_count = count;

	return 0;
}

/* ========================================================================
 * Public API - Framework: Error Handling
 * ======================================================================== */

tprompt_error_info_t tprompt_get_last_error(tprompt_handle_t handle)
{
	if (handle) {
		return handle->last_error;
	}
	return tprompt_global_error;
}

/* ========================================================================
 * Phase 6 Keybinding Handlers
 * ======================================================================== */

int tprompt_key_handle_ctrl_w(tprompt_handle_t handle)
{
	if (!handle || handle->buffer.cursor == 0) {
		return 0; // Nothing to delete
	}

	const char *data = handle->buffer.data;
	size_t pos = handle->buffer.cursor;

	// Move back one character first
	size_t prev_pos = tprompt_utf8_prev_char(data, pos);

	// Skip trailing whitespace backwards
	while (prev_pos > 0 && (data[prev_pos] == ' ' || data[prev_pos] == '\t')) {
		pos = prev_pos;
		prev_pos = tprompt_utf8_prev_char(data, pos);
	}

	// If we're now at the start or hit a newline, delete from here
	if (prev_pos == 0 || data[prev_pos] == '\n') {
		if (prev_pos > 0 && data[prev_pos] == '\n') {
			pos = prev_pos + 1; // Don't delete the newline
		}
		size_t delete_len = handle->buffer.cursor - pos;
		handle->buffer.cursor = pos;
		if (delete_len > 0) {
			tprompt_buffer_delete_at(&handle->buffer, delete_len);
		}
		return 0;
	}

	// Delete backwards until whitespace, newline, or start
	while (pos > 0) {
		prev_pos = tprompt_utf8_prev_char(data, pos);
		if (data[prev_pos] == ' ' || data[prev_pos] == '\t' || data[prev_pos] == '\n') {
			break;
		}
		pos = prev_pos;
	}

	// Delete from pos to cursor
	size_t delete_len = handle->buffer.cursor - pos;
	handle->buffer.cursor = pos;
	if (delete_len > 0) {
		tprompt_buffer_delete_at(&handle->buffer, delete_len);
	}

	return 0;
}

int tprompt_key_handle_ctrl_k(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line boundaries
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
	size_t line_start, line_end;

	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &line_start, &line_end) != 0) {
		return -1;
	}

	// If cursor is already at end of logical line, do nothing
	if (handle->buffer.cursor >= line_end) {
		return 0;
	}

	// Delete from cursor to end of logical line
	size_t delete_len = line_end - handle->buffer.cursor;
	tprompt_buffer_delete_at(&handle->buffer, delete_len);

	return 0;
}

int tprompt_key_handle_ctrl_u(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line boundaries
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
	size_t line_start, line_end;

	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &line_start, &line_end) != 0) {
		return -1;
	}

	// If cursor is at the start of a logical line (not the first line),
	// delete the previous logical line and its newline
	if (handle->buffer.cursor == line_start && current_logical_line > 0) {
		size_t prev_line_start, prev_line_end;
		if (tprompt_get_logical_line_bounds(handle, current_logical_line - 1, &prev_line_start, &prev_line_end) != 0) {
			return -1;
		}
		// Delete from start of previous line to cursor (including the newline)
		size_t delete_len = handle->buffer.cursor - prev_line_start;
		handle->buffer.cursor = prev_line_start;
		tprompt_buffer_delete_at(&handle->buffer, delete_len);
		return 0;
	}

	// If cursor is already at start of first logical line, do nothing
	if (handle->buffer.cursor <= line_start) {
		return 0;
	}

	// Delete from start of logical line to cursor
	size_t delete_len = handle->buffer.cursor - line_start;
	handle->buffer.cursor = line_start;
	tprompt_buffer_delete_at(&handle->buffer, delete_len);

	return 0;
}

int tprompt_key_handle_ctrl_a(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line boundaries
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
	size_t line_start, line_end;

	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &line_start, &line_end) != 0) {
		return -1;
	}

	// If cursor is already at the start of a logical line (not the first line),
	// move to the start of the previous logical line
	if (handle->buffer.cursor == line_start && current_logical_line > 0) {
		size_t prev_line_start, prev_line_end;
		if (tprompt_get_logical_line_bounds(handle, current_logical_line - 1, &prev_line_start, &prev_line_end) != 0) {
			return -1;
		}
		handle->buffer.cursor = prev_line_start;
		return 0;
	}

	// Move cursor to start of logical line
	handle->buffer.cursor = line_start;

	return 0;
}

int tprompt_key_handle_ctrl_e(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line boundaries
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
	size_t line_start, line_end;

	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &line_start, &line_end) != 0) {
		return -1;
	}

	// Move cursor to end of logical line
	handle->buffer.cursor = line_end;

	return 0;
}

/* ========================================================================
 * Screen Buffer Management (Buffer-based Differential Rendering)
 * ======================================================================== */

int tprompt_screen_buffer_init(tprompt_screen_buffer_t *buffer, size_t rows, size_t cols)
{
	if (!buffer || rows == 0 || cols == 0) {
		return -1;
	}

	size_t total_cells = rows * cols;
	buffer->cells = (tprompt_screen_cell_t *)calloc(total_cells, sizeof(tprompt_screen_cell_t));
	if (!buffer->cells) {
		return -1;
	}

	buffer->rows = rows;
	buffer->cols = cols;

	// Initialize all cells to empty
	for (size_t i = 0; i < total_cells; i++) {
		buffer->cells[i].char_len = 0;
		buffer->cells[i].display_width = 1;
		buffer->cells[i].is_continuation = false;
		buffer->cells[i].utf8_char[0] = '\0';
	}

	return 0;
}

void tprompt_screen_buffer_free(tprompt_screen_buffer_t *buffer)
{
	if (!buffer) {
		return;
	}

	if (buffer->cells) {
		free(buffer->cells);
		buffer->cells = NULL;
	}

	buffer->rows = 0;
	buffer->cols = 0;
}

void tprompt_screen_buffer_clear(tprompt_screen_buffer_t *buffer)
{
	if (!buffer || !buffer->cells) {
		return;
	}

	size_t total_cells = buffer->rows * buffer->cols;
	for (size_t i = 0; i < total_cells; i++) {
		buffer->cells[i].char_len = 0;
		buffer->cells[i].display_width = 1;
		buffer->cells[i].is_continuation = false;
		buffer->cells[i].utf8_char[0] = '\0';
	}
}

int tprompt_screen_buffer_resize(tprompt_screen_buffer_t *buffer, size_t new_rows, size_t new_cols)
{
	if (!buffer || new_rows == 0 || new_cols == 0) {
		return -1;
	}

	// If size unchanged, just clear
	if (buffer->rows == new_rows && buffer->cols == new_cols) {
		tprompt_screen_buffer_clear(buffer);
		return 0;
	}

	// Reallocate
	size_t new_total = new_rows * new_cols;
	tprompt_screen_cell_t *new_cells = (tprompt_screen_cell_t *)calloc(new_total, sizeof(tprompt_screen_cell_t));
	if (!new_cells) {
		return -1;
	}

	// Initialize new cells
	for (size_t i = 0; i < new_total; i++) {
		new_cells[i].char_len = 0;
		new_cells[i].display_width = 1;
		new_cells[i].is_continuation = false;
		new_cells[i].utf8_char[0] = '\0';
	}

	// Free old buffer and update
	if (buffer->cells) {
		free(buffer->cells);
	}

	buffer->cells = new_cells;
	buffer->rows = new_rows;
	buffer->cols = new_cols;

	return 0;
}

int tprompt_screen_buffer_write_char(tprompt_screen_buffer_t *buffer,
	size_t row, size_t col,
	const char *utf8_char, size_t char_len, size_t display_width)
{
	if (!buffer || !buffer->cells || !utf8_char) {
		return -1;
	}

	if (row >= buffer->rows || col >= buffer->cols) {
		return -1; // Out of bounds
	}

	if (char_len > 4) {
		return -1; // Invalid UTF-8 length
	}

	size_t index = row * buffer->cols + col;

	// Copy UTF-8 character
	if (char_len > 0) {
		memcpy(buffer->cells[index].utf8_char, utf8_char, char_len);
	}
	buffer->cells[index].utf8_char[char_len] = '\0';
	buffer->cells[index].char_len = (uint8_t)char_len;
	buffer->cells[index].display_width = (uint8_t)display_width;
	buffer->cells[index].is_continuation = false;

	// Handle wide characters (2 columns)
	if (display_width == 2 && col + 1 < buffer->cols) {
		size_t next_index = index + 1;
		buffer->cells[next_index].char_len = 0;
		buffer->cells[next_index].utf8_char[0] = '\0';
		buffer->cells[next_index].display_width = 0;
		buffer->cells[next_index].is_continuation = true;
	}

	return 0;
}

int tprompt_screen_buffer_write_string(tprompt_handle_t handle,
	tprompt_screen_buffer_t *buffer,
	size_t row, size_t col,
	const char *str)
{
	if (!handle || !buffer || !str) {
		return -1;
	}

	size_t current_col = col;
	size_t i = 0;
	size_t str_len = strlen(str);

	while (i < str_len && current_col < buffer->cols) {
		// Get UTF-8 character length
		size_t char_len = tprompt_utf8_char_length((unsigned char)str[i]);
		if (char_len == 0 || i + char_len > str_len) {
			i++;
			continue;
		}

		// Decode character to get scalar value
		unsigned int scalar = tprompt_utf8_decode((const unsigned char *)&str[i], char_len);

		// Get display width
		int char_width = tprompt_get_char_width(scalar);
		if (char_width < 0) {
			char_width = 1;
		}

		// Check if character fits
		if (current_col + (size_t)char_width > buffer->cols) {
			break; // Would overflow line
		}

		// Write character to buffer
		if (tprompt_screen_buffer_write_char(buffer, row, current_col,
			&str[i], char_len, (size_t)char_width) != 0) {
			return -1;
		}

		current_col += (size_t)char_width;
		i += char_len;
	}

	return (int)(current_col - col); // Return number of columns advanced
}

void tprompt_screen_buffer_diff(const tprompt_screen_buffer_t *prev,
	const tprompt_screen_buffer_t *curr,
	bool *dirty_cells)
{
	if (!prev || !curr || !dirty_cells) {
		return;
	}

	// Buffers must have same dimensions
	if (prev->rows != curr->rows || prev->cols != curr->cols) {
		// Mark all as dirty if dimensions changed
		size_t total = curr->rows * curr->cols;
		for (size_t i = 0; i < total; i++) {
			dirty_cells[i] = true;
		}
		return;
	}

	size_t total_cells = prev->rows * prev->cols;

	for (size_t i = 0; i < total_cells; i++) {
		// Compare cell contents
		if (prev->cells[i].char_len != curr->cells[i].char_len ||
			prev->cells[i].display_width != curr->cells[i].display_width ||
			prev->cells[i].is_continuation != curr->cells[i].is_continuation) {
			dirty_cells[i] = true;
		} else if (prev->cells[i].char_len > 0 &&
				   memcmp(prev->cells[i].utf8_char, curr->cells[i].utf8_char,
					   prev->cells[i].char_len) != 0) {
			dirty_cells[i] = true;
		} else {
			dirty_cells[i] = false;
		}
	}
}

void tprompt_screen_buffer_swap(tprompt_screen_buffer_t *buffer1,
	tprompt_screen_buffer_t *buffer2)
{
	if (!buffer1 || !buffer2) {
		return;
	}

	// Swap pointers and dimensions
	tprompt_screen_cell_t *temp_cells = buffer1->cells;
	size_t temp_rows = buffer1->rows;
	size_t temp_cols = buffer1->cols;

	buffer1->cells = buffer2->cells;
	buffer1->rows = buffer2->rows;
	buffer1->cols = buffer2->cols;

	buffer2->cells = temp_cells;
	buffer2->rows = temp_rows;
	buffer2->cols = temp_cols;
}

int tprompt_screen_buffer_flush_diff(tprompt_handle_t handle)
{
	if (!handle || !handle->display.buffer_based_rendering_active) {
		return -1;
	}

	tprompt_screen_buffer_t *buf = &handle->display.current_buffer;
	bool *dirty = handle->display.dirty_cells;
	int base_row = handle->display.start_row;

	// Iterate through each row and find contiguous dirty regions
	for (size_t row = 0; row < buf->rows; row++) {
		size_t col = 0;
		while (col < buf->cols) {
			// Skip clean cells
			while (col < buf->cols && !dirty[row * buf->cols + col]) {
				col++;
			}

			if (col >= buf->cols) {
				break; // End of row
			}

			// Found start of dirty region
			size_t dirty_start = col;

			// Find end of contiguous dirty region
			while (col < buf->cols && dirty[row * buf->cols + col]) {
				col++;
			}

			size_t dirty_end = col;

			// Move cursor to start of dirty region
			terse_error_t terr = terse_move_to(handle->terse, base_row + (int)row, (int)dirty_start);
			if (terr != TERSE_OK) {
				return -1;
			}

			// Write all dirty cells in this region
			for (size_t c = dirty_start; c < dirty_end; c++) {
				size_t index = row * buf->cols + c;
				tprompt_screen_cell_t *cell = &buf->cells[index];

				// Skip continuation cells (already handled by wide char)
				if (cell->is_continuation) {
					continue;
				}

				// Write character or space for empty cell
				if (cell->char_len > 0) {
					terr = terse_write_text(handle->terse, cell->utf8_char);
				} else {
					terr = terse_write_text(handle->terse, " ");
				}

				if (terr != TERSE_OK) {
					return -1;
				}
			}
		}
	}

	return 0;
}

int tprompt_buffer_based_rendering_init(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Already initialized
	if (handle->display.buffer_based_rendering_active) {
		return 0;
	}

	// Determine buffer dimensions
	// Use current terminal dimensions or reasonable defaults
	size_t rows = handle->display.terminal_height > 0 ? handle->display.terminal_height : 24;
	size_t cols = handle->display.terminal_width > 0 ? handle->display.terminal_width : 80;

	// We only need a few rows for the prompt area (input + status line + completion)
	// Start with a reasonable size (20 rows covers most typical inputs, will grow dynamically if needed)
	rows = 20;

	// Initialize current buffer
	if (tprompt_screen_buffer_init(&handle->display.current_buffer, rows, cols) != 0) {
		return -1;
	}

	// Initialize previous buffer
	if (tprompt_screen_buffer_init(&handle->display.previous_buffer, rows, cols) != 0) {
		tprompt_screen_buffer_free(&handle->display.current_buffer);
		return -1;
	}

	// Allocate dirty cells array
	size_t total_cells = rows * cols;
	handle->display.dirty_cells = (bool *)calloc(total_cells, sizeof(bool));
	if (!handle->display.dirty_cells) {
		tprompt_screen_buffer_free(&handle->display.current_buffer);
		tprompt_screen_buffer_free(&handle->display.previous_buffer);
		return -1;
	}

	handle->display.buffer_based_rendering_active = true;

	return 0;
}

void tprompt_buffer_based_rendering_free(tprompt_handle_t handle)
{
	if (!handle) {
		return;
	}

	if (!handle->display.buffer_based_rendering_active) {
		return;
	}

	tprompt_screen_buffer_free(&handle->display.current_buffer);
	tprompt_screen_buffer_free(&handle->display.previous_buffer);

	if (handle->display.dirty_cells) {
		free(handle->display.dirty_cells);
		handle->display.dirty_cells = NULL;
	}

	handle->display.buffer_based_rendering_active = false;
}

int tprompt_display_resize_buffers(tprompt_handle_t handle, size_t new_rows, size_t new_cols)
{
	if (!handle || !handle->display.buffer_based_rendering_active) {
		return -1;
	}

	if (new_rows == 0 || new_cols == 0) {
		return -1;
	}

	// Check if resize is actually needed
	if (handle->display.current_buffer.rows >= new_rows &&
		handle->display.current_buffer.cols >= new_cols) {
		return 0; // Already large enough
	}

	// Resize current_buffer
	if (tprompt_screen_buffer_resize(&handle->display.current_buffer, new_rows, new_cols) != 0) {
		return -1;
	}

	// Resize previous_buffer
	if (tprompt_screen_buffer_resize(&handle->display.previous_buffer, new_rows, new_cols) != 0) {
		// Rollback: try to restore original size (best effort)
		// In practice, if this fails we're in trouble anyway
		return -1;
	}

	// Resize dirty_cells array
	size_t new_total = new_rows * new_cols;
	bool *new_dirty = (bool *)realloc(handle->display.dirty_cells, new_total * sizeof(bool));
	if (!new_dirty) {
		// Critical: can't resize dirty_cells
		// The buffers are already resized, so we're in an inconsistent state
		// Best we can do is fail and let caller handle it
		return -1;
	}

	// Initialize new dirty cells to false
	size_t old_total = handle->display.current_buffer.rows * handle->display.current_buffer.cols;
	if (new_total > old_total) {
		memset(&new_dirty[old_total], 0, (new_total - old_total) * sizeof(bool));
	}

	handle->display.dirty_cells = new_dirty;

	return 0;
}

/* ========================================================================
 * Buffer-based Rendering Functions (Phase 3)
 * ======================================================================== */

/**
 * @brief Render prompt string to buffer
 */
static int tprompt_render_to_buffer_prompt(tprompt_handle_t handle, size_t *out_row, size_t *out_col)
{
	if (!handle || !out_row || !out_col) {
		return -1;
	}

	tprompt_screen_buffer_t *buf = &handle->display.current_buffer;

	// Start at row 0, column 0
	size_t row = 0;
	size_t col = 0;

	// Write prompt if present
	if (handle->prompt) {
		int cols_written = tprompt_screen_buffer_write_string(handle, buf, row, col, handle->prompt);
		if (cols_written < 0) {
			return -1;
		}
		col += (size_t)cols_written;
	}

	*out_row = row;
	*out_col = col;
	return 0;
}

/**
 * @brief Render input text with wrapping to buffer
 */
static int tprompt_render_to_buffer_input(tprompt_handle_t handle, size_t start_row, size_t start_col,
	size_t *out_end_row, size_t *out_end_col)
{
	if (!handle || !out_end_row || !out_end_col) {
		return -1;
	}

	tprompt_screen_buffer_t *buf = &handle->display.current_buffer;
	size_t terminal_width = handle->display.terminal_width;

	size_t row = start_row;
	size_t col = start_col;

	// Iterate through buffer and write characters
	size_t i = 0;
	while (i < handle->buffer.length) {
		// Check for explicit newline
		if (handle->buffer.data[i] == '\n') {
			// Move to next row, column 0
			row++;
			col = 0;

			// Check if we need to resize buffer (leave 1 row margin)
			if (row >= buf->rows - 1) {
				size_t new_rows = buf->rows * 2; // Double the size
				if (tprompt_display_resize_buffers(handle, new_rows, buf->cols) != 0) {
					// Resize failed - cannot continue
					return -1;
				}
				buf = &handle->display.current_buffer; // Update pointer after resize
			}

			// Write continuation marker if configured
			size_t marker_width = tprompt_get_continuation_marker_width(handle);
			if (marker_width > 0 && marker_width < terminal_width) {
				// Build marker: spaces + '|' + space
				char marker[256];
				size_t spaces_before = marker_width >= 2 ? marker_width - 2 : 0;
				if (spaces_before + 2 >= sizeof(marker)) {
					spaces_before = sizeof(marker) - 3;
				}
				memset(marker, ' ', spaces_before);
				marker[spaces_before] = '|';
				marker[spaces_before + 1] = ' ';
				marker[spaces_before + 2] = '\0';

				int cols_written = tprompt_screen_buffer_write_string(handle, buf, row, col, marker);
				if (cols_written < 0) {
					return -1;
				}
				col += (size_t)cols_written;
			}

			i++;
			continue;
		}

		// Check if we need to wrap to next physical line
		if (col >= terminal_width) {
			row++;
			col = 0;

			// Check if we need to resize buffer (leave 1 row margin)
			if (row >= buf->rows - 1) {
				size_t new_rows = buf->rows * 2; // Double the size
				if (tprompt_display_resize_buffers(handle, new_rows, buf->cols) != 0) {
					// Resize failed - cannot continue
					return -1;
				}
				buf = &handle->display.current_buffer; // Update pointer after resize
			}
		}

		// Get character length
		size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[i]);
		if (char_len == 0 || i + char_len > handle->buffer.length) {
			i++;
			continue;
		}

		// Decode character to get scalar value
		unsigned int scalar = tprompt_utf8_decode((const unsigned char *)&handle->buffer.data[i], char_len);

		// Get display width
		int char_width = tprompt_get_char_width(scalar);
		if (char_width < 0) {
			char_width = 1;
		}

		// Check if character fits on current line
		if (col + (size_t)char_width > terminal_width) {
			// Wrap to next line
			row++;
			col = 0;

			// Check if we need to resize buffer (leave 1 row margin)
			if (row >= buf->rows - 1) {
				size_t new_rows = buf->rows * 2; // Double the size
				if (tprompt_display_resize_buffers(handle, new_rows, buf->cols) != 0) {
					// Resize failed - cannot continue
					return -1;
				}
				buf = &handle->display.current_buffer; // Update pointer after resize
			}
		}

		// Write character to buffer
		if (tprompt_screen_buffer_write_char(buf, row, col,
			&handle->buffer.data[i], char_len, (size_t)char_width) != 0) {
			return -1;
		}

		col += (size_t)char_width;
		i += char_len;
	}

	*out_end_row = row;
	*out_end_col = col;
	return 0;
}

/**
 * @brief Render debug status line to buffer
 */
static int tprompt_render_to_buffer_status_line(tprompt_handle_t handle, size_t row)
{
	if (!handle) {
		return -1;
	}

	tprompt_screen_buffer_t *buf = &handle->display.current_buffer;

	// Check buffer bounds
	if (row >= buf->rows) {
		return -1;
	}

	// Build debug info string
	char debug_info[128];
	int target_col = (int)handle->display.physical_column;
	if (handle->input_state.has_goal_column) {
		snprintf(debug_info, sizeof(debug_info), "x=%d y=%d goal=%zu",
			target_col, (int)handle->display.physical_line, handle->input_state.goal_column);
	} else {
		snprintf(debug_info, sizeof(debug_info), "x=%d y=%d goal=-",
			target_col, (int)handle->display.physical_line);
	}

	// Write debug info to buffer
	int cols_written = tprompt_screen_buffer_write_string(handle, buf, row, 0, debug_info);
	if (cols_written < 0) {
		return -1;
	}

	return 0;
}

/**
 * @brief Render completion list to buffer (basic implementation)
 */
static int tprompt_render_to_buffer_completion(tprompt_handle_t handle, size_t start_row)
{
	if (!handle) {
		return -1;
	}

	// Only render if completion is active
	if (!handle->completion_state.active) {
		return 0;
	}

	tprompt_screen_buffer_t *buf = &handle->display.current_buffer;
	size_t candidate_count = handle->completion_state.candidate_count;

	if (candidate_count == 0) {
		return 0;
	}

	char **candidates = handle->completion_state.candidates;
	size_t selected_index = handle->completion_state.selected_index;

	// Render each candidate on a separate row
	for (size_t i = 0; i < candidate_count; i++) {
		size_t row = start_row + i;
		size_t col = 0;

		// Check if we need to resize buffer for completion list
		if (row >= buf->rows - 1) {
			size_t new_rows = buf->rows * 2; // Double the size
			if (tprompt_display_resize_buffers(handle, new_rows, buf->cols) != 0) {
				// Resize failed - stop rendering completion list
				break;
			}
			buf = &handle->display.current_buffer; // Update pointer after resize
		}

		// Write selection indicator
		const char *prefix = (i == selected_index) ? "> " : "  ";
		int cols_written = tprompt_screen_buffer_write_string(handle, buf, row, col, prefix);
		if (cols_written < 0) {
			return -1;
		}
		col += (size_t)cols_written;

		// Write candidate text
		cols_written = tprompt_screen_buffer_write_string(handle, buf, row, col, candidates[i]);
		if (cols_written < 0) {
			return -1;
		}
	}

	return 0;
}

/**
 * @brief Main buffer-based rendering coordinator
 */
int tprompt_display_render_buffered(tprompt_handle_t handle)
{
	if (!handle || !handle->display.buffer_based_rendering_active) {
		return -1;
	}

	// Ensure we know our starting row
	if (!handle->display.start_row_known) {
		terse_cursor_position_t start_pos = terse_get_cursor_position(handle->terse);
		if (start_pos.known) {
			handle->display.start_row = start_pos.row;
			handle->display.start_row_known = true;
		} else {
			handle->display.start_row = 0;
			handle->display.start_row_known = true;
		}
	}

	// Calculate layout to get total_physical_lines
	tprompt_display_calculate_layout(handle);

	// Clear current buffer
	tprompt_screen_buffer_clear(&handle->display.current_buffer);

	// Render components to buffer
	size_t row = 0, col = 0;

	// 1. Render prompt
	if (tprompt_render_to_buffer_prompt(handle, &row, &col) != 0) {
		return -1;
	}

	// 2. Render input text (starts where prompt ended)
	size_t end_row = 0, end_col = 0;
	if (tprompt_render_to_buffer_input(handle, row, col, &end_row, &end_col) != 0) {
		return -1;
	}

	// 3. Render completion list if active (starts after input)
	if (handle->completion_state.active) {
		size_t completion_start_row = end_row + 1;
		if (tprompt_render_to_buffer_completion(handle, completion_start_row) != 0) {
			return -1;
		}
	}

	// 4. Render debug status line
	size_t status_row = handle->display.total_physical_lines;
	if (tprompt_render_to_buffer_status_line(handle, status_row) != 0) {
		// Non-fatal, continue
	}

	// 5. Detect differences with previous buffer
	tprompt_screen_buffer_diff(&handle->display.previous_buffer,
		&handle->display.current_buffer,
		handle->display.dirty_cells);

	// 6. Flush only dirty regions to terminal
	if (tprompt_screen_buffer_flush_diff(handle) != 0) {
		return -1;
	}

	// 7. Swap buffers (current becomes previous for next frame)
	tprompt_screen_buffer_swap(&handle->display.current_buffer,
		&handle->display.previous_buffer);

	// 8. Update cursor position
	size_t cursor_line, cursor_col;
	tprompt_calculate_physical_position(handle, handle->buffer.cursor,
		true, &cursor_line, &cursor_col);

	int base_row = handle->display.start_row;
	terse_error_t terr = terse_move_to(handle->terse, base_row + (int)cursor_line, (int)cursor_col);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to position cursor after buffered render");
		return -1;
	}

	// 9. Flush terminal output
	terr = terse_flush(handle->terse);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to flush output after buffered render");
		return -1;
	}

	// Update previous line count
	handle->display.prev_total_physical_lines = handle->display.total_physical_lines;

	// Clear dirty flags
	tprompt_display_clear_dirty(handle);

	return 0;
}
