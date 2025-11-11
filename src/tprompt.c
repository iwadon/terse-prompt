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
 * Forward Declarations for Static Helper Functions
 * ======================================================================== */

static size_t tprompt_calculate_cursor_col(tprompt_handle_t handle);
static void tprompt_calculate_physical_position(tprompt_handle_t handle, size_t byte_offset,
	bool include_prompt, size_t *out_physical_line, size_t *out_physical_col);

/* ========================================================================
 * Error Handling - Internal Helpers
 * ======================================================================== */

void tprompt_set_error(tprompt_error_info_t *error,
	tprompt_error_category_t category,
	int code,
	const char *message, ...)
{
	if (!error) {
		return;
	}

	error->category = category;
	error->code = code;

	if (message) {
		va_list args;
		va_start(args, message);
		vsnprintf(error->message, sizeof(error->message), message, args);
		va_end(args);
	} else {
		error->message[0] = '\0';
	}
}

void tprompt_clear_error(tprompt_error_info_t *error)
{
	if (!error) {
		return;
	}

	error->category = TPROMPT_ERROR_NONE;
	error->code = 0;
	error->message[0] = '\0';
}

/* ========================================================================
 * Buffer Management - Internal Helpers
 * ======================================================================== */

int tprompt_buffer_init(tprompt_buffer_t *buffer, size_t initial_size)
{
	if (!buffer) {
		return -1;
	}

	if (initial_size == 0) {
		initial_size = DEFAULT_BUFFER_SIZE;
	}

	buffer->data = malloc(initial_size);
	if (!buffer->data) {
		return -1;
	}

	buffer->data[0] = '\0';
	buffer->size = initial_size;
	buffer->length = 0;
	buffer->cursor = 0;

	return 0;
}

void tprompt_buffer_free(tprompt_buffer_t *buffer)
{
	if (!buffer) {
		return;
	}

	free(buffer->data);
	buffer->data = NULL;
	buffer->size = 0;
	buffer->length = 0;
	buffer->cursor = 0;
}

void tprompt_buffer_clear(tprompt_buffer_t *buffer)
{
	if (!buffer || !buffer->data) {
		return;
	}

	buffer->data[0] = '\0';
	buffer->length = 0;
	buffer->cursor = 0;
}

int tprompt_buffer_ensure_capacity(tprompt_buffer_t *buffer, size_t required_size)
{
	if (!buffer) {
		return -1;
	}

	// Need space for null terminator
	required_size++;

	if (buffer->size >= required_size) {
		return 0;
	}

	// Double until we have enough space
	size_t new_size = buffer->size;
	while (new_size < required_size) {
		new_size *= 2;
	}

	char *new_data = realloc(buffer->data, new_size);
	if (!new_data) {
		return -1;
	}

	buffer->data = new_data;
	buffer->size = new_size;

	return 0;
}

int tprompt_buffer_insert(tprompt_buffer_t *buffer, const char *text, size_t len)
{
	if (!buffer || !text || len == 0) {
		return -1;
	}

	// Ensure we have enough capacity
	if (tprompt_buffer_ensure_capacity(buffer, buffer->length + len) != 0) {
		return -1;
	}

	// Move existing text after cursor to make room
	if (buffer->cursor < buffer->length) {
		memmove(buffer->data + buffer->cursor + len,
			buffer->data + buffer->cursor,
			buffer->length - buffer->cursor);
	}

	// Insert new text
	memcpy(buffer->data + buffer->cursor, text, len);
	buffer->cursor += len;
	buffer->length += len;
	buffer->data[buffer->length] = '\0';

	return 0;
}

size_t tprompt_buffer_delete_before(tprompt_buffer_t *buffer, size_t count)
{
	if (!buffer || buffer->cursor == 0) {
		return 0;
	}

	size_t bytes_deleted = 0;
	for (size_t i = 0; i < count && buffer->cursor > 0; i++) {
		// Find previous character boundary
		size_t prev_pos = tprompt_utf8_prev_char(buffer->data, buffer->cursor);
		size_t char_bytes = buffer->cursor - prev_pos;

		// Shift remaining text left to delete the character
		if (buffer->cursor < buffer->length) {
			memmove(buffer->data + prev_pos,
				buffer->data + buffer->cursor,
				buffer->length - buffer->cursor);
		}

		buffer->length -= char_bytes;
		buffer->cursor = prev_pos;
		buffer->data[buffer->length] = '\0';
		bytes_deleted += char_bytes;
	}

	return bytes_deleted;
}

size_t tprompt_buffer_delete_at(tprompt_buffer_t *buffer, size_t count)
{
	if (!buffer || buffer->cursor >= buffer->length) {
		return 0;
	}

	size_t bytes_deleted = 0;
	for (size_t i = 0; i < count && buffer->cursor < buffer->length; i++) {
		// Find next character boundary
		size_t next_pos = tprompt_utf8_next_char(buffer->data, buffer->cursor, buffer->length);
		size_t char_bytes = next_pos - buffer->cursor;

		// Shift remaining text left to delete the character
		if (next_pos < buffer->length) {
			memmove(buffer->data + buffer->cursor,
				buffer->data + next_pos,
				buffer->length - next_pos);
		}

		buffer->length -= char_bytes;
		buffer->data[buffer->length] = '\0';
		bytes_deleted += char_bytes;
	}

	return bytes_deleted;
}

int tprompt_buffer_set(tprompt_buffer_t *buffer, const char *text)
{
	if (!buffer || !text) {
		return -1;
	}

	size_t len = strlen(text);
	if (tprompt_buffer_ensure_capacity(buffer, len) != 0) {
		return -1;
	}

	memcpy(buffer->data, text, len + 1);
	buffer->length = len;
	buffer->cursor = len;

	return 0;
}

/* ========================================================================
 * Cursor Movement - Internal Helpers
 * ======================================================================== */

size_t tprompt_cursor_move_left(tprompt_buffer_t *buffer, size_t count)
{
	if (!buffer || buffer->cursor == 0) {
		return 0;
	}

	size_t bytes_moved = 0;
	for (size_t i = 0; i < count && buffer->cursor > 0; i++) {
		size_t prev_pos = tprompt_utf8_prev_char(buffer->data, buffer->cursor);
		size_t char_bytes = buffer->cursor - prev_pos;
		buffer->cursor = prev_pos;
		bytes_moved += char_bytes;
	}

	return bytes_moved;
}

size_t tprompt_cursor_move_right(tprompt_buffer_t *buffer, size_t count)
{
	if (!buffer || buffer->cursor >= buffer->length) {
		return 0;
	}

	size_t bytes_moved = 0;
	for (size_t i = 0; i < count && buffer->cursor < buffer->length; i++) {
		size_t next_pos = tprompt_utf8_next_char(buffer->data, buffer->cursor, buffer->length);
		size_t char_bytes = next_pos - buffer->cursor;
		buffer->cursor = next_pos;
		bytes_moved += char_bytes;
	}

	return bytes_moved;
}

void tprompt_cursor_move_to_start(tprompt_buffer_t *buffer)
{
	if (buffer) {
		buffer->cursor = 0;
	}
}

void tprompt_cursor_move_to_end(tprompt_buffer_t *buffer)
{
	if (buffer) {
		buffer->cursor = buffer->length;
	}
}

void tprompt_cursor_move_to_offset(tprompt_buffer_t *buffer, size_t offset)
{
	if (!buffer) {
		return;
	}

	if (offset > buffer->length) {
		offset = buffer->length;
	}

	buffer->cursor = offset;
}

size_t tprompt_cursor_move_word_forward(tprompt_buffer_t *buffer)
{
	if (!buffer || buffer->cursor >= buffer->length) {
		return 0;
	}

	size_t bytes_moved = 0;
	const char *data = buffer->data;
	size_t pos = buffer->cursor;

	// Skip any whitespace at current position
	while (pos < buffer->length && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\n')) {
		size_t char_len = tprompt_utf8_char_length((unsigned char)data[pos]);
		if (char_len == 0)
			char_len = 1;
		pos += char_len;
		bytes_moved += char_len;
	}

	// Move forward until we hit whitespace or end
	while (pos < buffer->length && data[pos] != ' ' && data[pos] != '\t' && data[pos] != '\n') {
		size_t char_len = tprompt_utf8_char_length((unsigned char)data[pos]);
		if (char_len == 0)
			char_len = 1;
		pos += char_len;
		bytes_moved += char_len;
	}

	buffer->cursor = pos;
	return bytes_moved;
}

size_t tprompt_cursor_move_word_backward(tprompt_buffer_t *buffer)
{
	if (!buffer || buffer->cursor == 0) {
		return 0;
	}

	size_t bytes_moved = 0;
	const char *data = buffer->data;
	size_t pos = buffer->cursor;

	// Move back one character first to get onto the previous word
	pos = tprompt_utf8_prev_char(data, pos);
	bytes_moved += (buffer->cursor - pos);

	// Skip any whitespace backwards
	while (pos > 0 && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\n')) {
		size_t prev_pos = tprompt_utf8_prev_char(data, pos);
		bytes_moved += (pos - prev_pos);
		pos = prev_pos;
	}

	// Move backward until we hit whitespace or start
	while (pos > 0) {
		size_t prev_pos = tprompt_utf8_prev_char(data, pos);
		if (data[prev_pos] == ' ' || data[prev_pos] == '\t' || data[prev_pos] == '\n') {
			break;
		}
		bytes_moved += (pos - prev_pos);
		pos = prev_pos;
	}

	buffer->cursor = pos;
	return bytes_moved;
}

int tprompt_cursor_move_to_logical_line_start(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
	size_t line_start, line_end;

	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &line_start, &line_end) != 0) {
		return -1;
	}

	// Move cursor to start of current logical line
	handle->buffer.cursor = line_start;
	return 0;
}

int tprompt_cursor_move_to_logical_line_end(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
	size_t line_start, line_end;

	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &line_start, &line_end) != 0) {
		return -1;
	}

	// Move cursor to end of current logical line
	handle->buffer.cursor = line_end;
	return 0;
}

/**
 * @brief Move cursor up one logical line
 *
 * Implements vertical cursor movement with goal column tracking:
 * - Maintains horizontal column position across multiple up/down movements
 * - If target line is shorter, cursor moves to end of that line
 * - Goal column persists until a horizontal movement occurs
 *
 * Algorithm:
 * 1. Determine current logical line and column position
 * 2. Set or retrieve goal column (persists across vertical movements)
 * 3. Find previous logical line boundaries
 * 4. Move to goal column in previous line (or end if line is shorter)
 *
 * @param handle Prompt handle
 * @return 0 on success (may do nothing if at first line), -1 on error
 */
int tprompt_cursor_move_up(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line number
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);

	// If at first logical line, can't move up
	if (current_logical_line == 0) {
		return 0;
	}

	// Get bounds of current line to calculate column position
	size_t current_line_start, current_line_end;
	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &current_line_start, &current_line_end) != 0) {
		return -1;
	}

	// Calculate column within current logical line (in characters)
	size_t column = 0;
	for (size_t i = current_line_start; i < handle->buffer.cursor;) {
		size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[i]);
		if (char_len == 0) {
			char_len = 1;
		}
		i += char_len;
		column++;
	}

	// Set or use goal column
	if (!handle->input_state.has_goal_column) {
		handle->input_state.goal_column = column;
		handle->input_state.has_goal_column = true;
	} else {
		column = handle->input_state.goal_column;
	}

	// Get bounds of previous logical line
	size_t prev_line_start, prev_line_end;
	if (tprompt_get_logical_line_bounds(handle, current_logical_line - 1, &prev_line_start, &prev_line_end) != 0) {
		return -1;
	}

	// Move to goal column in previous line (or end of line if shorter)
	size_t target_offset = prev_line_start;
	size_t current_col = 0;

	while (target_offset < prev_line_end && current_col < column) {
		size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[target_offset]);
		if (char_len == 0) {
			char_len = 1;
		}
		target_offset += char_len;
		current_col++;
	}

	handle->buffer.cursor = target_offset;
	return 0;
}

/**
 * @brief Move cursor down one logical line
 *
 * Implements vertical cursor movement with goal column tracking.
 * Behavior is symmetric to tprompt_cursor_move_up().
 *
 * @param handle Prompt handle
 * @return 0 on success (may do nothing if at last line), -1 on error
 */
int tprompt_cursor_move_down(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line number
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
	size_t total_logical_lines = tprompt_count_logical_lines(handle);

	// If at last logical line, can't move down
	if (current_logical_line >= total_logical_lines - 1) {
		return 0;
	}

	// Get bounds of current line to calculate column position
	size_t current_line_start, current_line_end;
	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &current_line_start, &current_line_end) != 0) {
		return -1;
	}

	// Calculate column within current logical line (in characters)
	size_t column = 0;
	for (size_t i = current_line_start; i < handle->buffer.cursor;) {
		size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[i]);
		if (char_len == 0) {
			char_len = 1;
		}
		i += char_len;
		column++;
	}

	// Set or use goal column
	if (!handle->input_state.has_goal_column) {
		handle->input_state.goal_column = column;
		handle->input_state.has_goal_column = true;
	} else {
		column = handle->input_state.goal_column;
	}

	// Get bounds of next logical line
	size_t next_line_start, next_line_end;
	if (tprompt_get_logical_line_bounds(handle, current_logical_line + 1, &next_line_start, &next_line_end) != 0) {
		return -1;
	}

	// Move to goal column in next line (or end of line if shorter)
	size_t target_offset = next_line_start;
	size_t current_col = 0;

	while (target_offset < next_line_end && current_col < column) {
		size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[target_offset]);
		if (char_len == 0) {
			char_len = 1;
		}
		target_offset += char_len;
		current_col++;
	}

	handle->buffer.cursor = target_offset;
	return 0;
}

/**
 * @brief Move cursor to start of current physical line
 *
 * Finds the byte offset where the current physical (screen) line starts,
 * accounting for both terminal wrapping and explicit newlines.
 *
 * Algorithm:
 * 1. Calculate current physical line and column position
 * 2. Walk backward through buffer to find start of physical line:
 *    - Stop at explicit newline (next char is start of physical line)
 *    - Stop when column reaches 0 (wrapped to this line)
 * 3. Move cursor to that position
 *
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_cursor_move_to_physical_line_start(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current physical position
	size_t current_phys_line, current_phys_col;
	tprompt_calculate_physical_position(handle, handle->buffer.cursor, true,
		&current_phys_line, &current_phys_col);

	// If already at column 0, we're at the start of the physical line
	if (current_phys_col == 0) {
		return 0;
	}

	// Walk backward through buffer to find where this physical line starts
	size_t terminal_width = handle->display.terminal_width;
	if (terminal_width == 0) {
		terminal_width = 80;
	}

	const char *data = handle->buffer.data;
	size_t target_offset = handle->buffer.cursor;

	// Move backward character by character, recalculating position each time
	while (target_offset > 0) {
		size_t prev_offset = tprompt_utf8_prev_char(data, target_offset);

		// Check physical position at prev_offset
		size_t phys_line, phys_col;
		tprompt_calculate_physical_position(handle, prev_offset, true,
			&phys_line, &phys_col);

		// If we moved to a different physical line, stop
		if (phys_line < current_phys_line) {
			break;
		}

		target_offset = prev_offset;
	}

	handle->buffer.cursor = target_offset;
	return 0;
}

/**
 * @brief Move cursor to end of current physical line
 *
 * Finds the byte offset where the current physical (screen) line ends,
 * accounting for both terminal wrapping and explicit newlines.
 *
 * Algorithm:
 * 1. Calculate current physical line position
 * 2. Walk forward through buffer until:
 *    - Hit explicit newline (end of physical line)
 *    - Column reaches terminal width (end before wrap)
 *    - Reach end of buffer
 * 3. Move cursor to that position
 *
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_cursor_move_to_physical_line_end(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current physical position
	size_t current_phys_line, current_phys_col;
	tprompt_calculate_physical_position(handle, handle->buffer.cursor, true,
		&current_phys_line, &current_phys_col);

	// Walk forward through buffer to find where this physical line ends
	size_t terminal_width = handle->display.terminal_width;
	if (terminal_width == 0) {
		terminal_width = 80;
	}

	const char *data = handle->buffer.data;
	size_t target_offset = handle->buffer.cursor;
	size_t buffer_length = handle->buffer.length;

	// Move forward character by character
	while (target_offset < buffer_length) {
		// Check if next character is newline (end of physical line)
		if (data[target_offset] == '\n') {
			break;
		}

		// Move to next character
		size_t char_len = tprompt_utf8_char_length((unsigned char)data[target_offset]);
		if (char_len == 0) {
			char_len = 1;
		}
		size_t next_offset = target_offset + char_len;

		// Check physical position at next_offset
		size_t phys_line, phys_col;
		tprompt_calculate_physical_position(handle, next_offset, true,
			&phys_line, &phys_col);

		// If we moved to a different physical line, stop (don't include wrap point)
		if (phys_line > current_phys_line) {
			break;
		}

		target_offset = next_offset;
	}

	handle->buffer.cursor = target_offset;
	return 0;
}

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
static unsigned int tprompt_utf8_decode(const unsigned char *bytes, size_t len)
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
static int tprompt_get_char_width(unsigned int scalar)
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
static size_t tprompt_calculate_cursor_col(tprompt_handle_t handle)
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
static void tprompt_calculate_physical_position(tprompt_handle_t handle, size_t byte_offset,
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
			if (tprompt_buffer_insert(&handle->buffer, ch, ch_len) != 0) {
				return -1;
			}

			// Activate completion at the current cursor position
			if (tprompt_completion_activate(handle, ch[0], handle->buffer.cursor - ch_len) != 0) {
				return -1;
			}

			return 0;
		}
	}

	// Normal character insertion
	size_t ch_len = strlen(ch);
	if (tprompt_buffer_insert(&handle->buffer, ch, ch_len) != 0) {
		return -1;
	}

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
		tprompt_buffer_delete_before(&handle->buffer, 1);
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle delete key
	if (event->type == TERSE_EVENT_DELETE) {
		tprompt_buffer_delete_at(&handle->buffer, 1);
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
	if (tprompt_display_render(handle) != 0) {
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
					if (tprompt_display_render(handle) != 0) {
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
						if (tprompt_display_render(handle) != 0) {
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
						if (tprompt_display_render(handle) != 0) {
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
					if (tprompt_display_render(handle) != 0) {
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
		if (tprompt_display_render(handle) != 0) {
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
