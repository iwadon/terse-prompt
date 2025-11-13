/**
 * @file tprompt_buffer.c
 * @brief Buffer and cursor operation implementations for terse-prompt
 *
 * Implements buffer management (initialization, allocation, insertion,
 * deletion) and cursor movement operations.
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_internal.h"
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Constants
 * ======================================================================== */

#define DEFAULT_BUFFER_SIZE 256

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
