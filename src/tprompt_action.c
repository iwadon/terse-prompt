/**
 * @file tprompt_action.c
 * @brief Action execution system and logical line navigation
 * @version 0.1
 * @date 2025-11-20
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_action.h"
#include "tprompt_keybinding.h"
#include "tprompt_internal.h"
#include "tprompt_buffer.h"
#include "tprompt_history.h"
#include "tprompt_completion.h"
#include "tprompt_display.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

/* ========================================================================
 * Action Execution - Internal Helpers
 * ======================================================================== */

/* ------------------------------------------------------------------------
 * Individual Action Handlers
 * ------------------------------------------------------------------------ */

/**
 * @brief Handle MOVE_LEFT action
 */
int tprompt_action_move_left(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle) {
		return -1;
	}

	tprompt_cursor_move_left(&handle->buffer, 1);
	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/**
 * @brief Handle MOVE_RIGHT action
 */
int tprompt_action_move_right(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle) {
		return -1;
	}

	tprompt_cursor_move_right(&handle->buffer, 1);
	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/**
 * @brief Handle DELETE_BACKWARD action (backspace)
 */
int tprompt_action_delete_backward(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle) {
		return -1;
	}

	size_t old_cursor = handle->buffer.cursor;
	size_t old_length = handle->buffer.length;
	size_t deleted_bytes = tprompt_buffer_delete_before(&handle->buffer, 1);
	if (deleted_bytes > 0) {
		// Mark from new cursor position to old end as dirty (characters shift left)
		tprompt_display_mark_dirty_range(handle, handle->buffer.cursor, old_length);

		// If completion is active, check if we deleted the trigger character
		if (handle->completion_state.active) {
			size_t trigger_pos = handle->completion_state.trigger_offset;
			// If we deleted at or before the trigger position, deactivate completion
			if (old_cursor <= trigger_pos + 1) {
				tprompt_completion_deactivate(handle);
			} else {
				// Otherwise, update completion candidates with new input
				tprompt_completion_update(handle);
			}
		}
	}
	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/**
 * @brief Handle DELETE_FORWARD action (delete)
 */
int tprompt_action_delete_forward(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle) {
		return -1;
	}

	size_t old_length = handle->buffer.length;
	size_t deleted_bytes = tprompt_buffer_delete_at(&handle->buffer, 1);
	if (deleted_bytes > 0) {
		// Mark from cursor to old end as dirty (characters shift left)
		tprompt_display_mark_dirty_range(handle, handle->buffer.cursor, old_length);
	}
	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/**
 * @brief Handle MOVE_HOME action
 *
 * Implements Claude Code-style staged navigation:
 * - First press: move to physical line start
 * - Second press: move to logical line start
 */
int tprompt_action_move_home(tprompt_handle_t handle, const terse_event_t *event)
{
	if (!handle) {
		return -1;
	}

	// Check if this is a consecutive Home press
	bool is_consecutive = (handle->input_state.last_key_type == event->type &&
	                       handle->input_state.last_cursor_pos == handle->buffer.cursor);

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

/**
 * @brief Handle MOVE_END action
 *
 * Implements Claude Code-style staged navigation:
 * - First press: move to physical line end
 * - Second press: move to logical line end
 */
int tprompt_action_move_end(tprompt_handle_t handle, const terse_event_t *event)
{
	if (!handle) {
		return -1;
	}

	// Check if this is a consecutive End press
	bool is_consecutive = (handle->input_state.last_key_type == event->type &&
	                       handle->input_state.last_cursor_pos == handle->buffer.cursor);

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

/**
 * @brief Handle HISTORY_PREV action (up arrow)
 *
 * In multiline mode: only navigates history if at first line
 * In single-line mode: always navigates history
 */
int tprompt_action_history_prev(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle) {
		return -1;
	}

	bool has_multiple_lines = tprompt_buffer_has_newlines(handle);

	if (has_multiple_lines) {
		// In multi-line mode: check if at first line
		size_t current_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
		if (current_line > 0) {
			// Not at first line, should use MOVE_UP instead
			return tprompt_cursor_move_up(handle);
		}
	}

	// At first line or single-line mode: navigate to previous history entry
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

	return 0; // Continue editing
}

/**
 * @brief Handle HISTORY_NEXT action (down arrow)
 *
 * In multiline mode: only navigates history if at last line
 * In single-line mode: always navigates history
 */
int tprompt_action_history_next(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle) {
		return -1;
	}

	bool has_multiple_lines = tprompt_buffer_has_newlines(handle);

	if (has_multiple_lines) {
		// In multi-line mode: check if at last line
		size_t current_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
		size_t total_lines = tprompt_count_logical_lines(handle);
		if (current_line < total_lines - 1) {
			// Not at last line, should use MOVE_DOWN instead
			return tprompt_cursor_move_down(handle);
		}
	}

	// At last line or single-line mode: navigate to next history entry
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

	return 0; // Continue editing
}

/**
 * @brief Handle MOVE_UP action (up arrow in multiline)
 */
int tprompt_action_move_up(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle) {
		return -1;
	}

	tprompt_cursor_move_up(handle);
	return 0; // Continue editing
}

/**
 * @brief Handle MOVE_DOWN action (down arrow in multiline)
 */
int tprompt_action_move_down(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle) {
		return -1;
	}

	tprompt_cursor_move_down(handle);
	return 0; // Continue editing
}

/**
 * @brief Handle MOVE_WORD_LEFT action (Ctrl+Left)
 *
 * Move cursor to the beginning of the previous word.
 * Words are delimited by whitespace.
 */
int tprompt_action_move_word_left(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle || handle->buffer.cursor == 0) {
		return 0; // Nothing to do
	}

	const char *data = handle->buffer.data;
	size_t pos = handle->buffer.cursor;

	// Move back one character first
	size_t prev_pos = tprompt_utf8_prev_char(data, pos);

	// Skip trailing whitespace backwards
	while (prev_pos > 0 && (data[prev_pos] == ' ' || data[prev_pos] == '\t' || data[prev_pos] == '\n')) {
		pos = prev_pos;
		prev_pos = tprompt_utf8_prev_char(data, pos);
	}

	// If we're at the start, stop here
	if (prev_pos == 0) {
		handle->buffer.cursor = 0;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Move backwards until whitespace or start
	while (pos > 0) {
		prev_pos = tprompt_utf8_prev_char(data, pos);
		if (data[prev_pos] == ' ' || data[prev_pos] == '\t' || data[prev_pos] == '\n') {
			break;
		}
		pos = prev_pos;
		if (prev_pos == 0) {
			break; // Reached start
		}
	}

	handle->buffer.cursor = pos;
	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/**
 * @brief Handle MOVE_WORD_RIGHT action (Ctrl+Right)
 *
 * Move cursor to the end of the next word.
 * Words are delimited by whitespace.
 */
int tprompt_action_move_word_right(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle || handle->buffer.cursor >= handle->buffer.length) {
		return 0; // Nothing to do
	}

	const char *data = handle->buffer.data;
	size_t pos = handle->buffer.cursor;
	size_t max_length = handle->buffer.length;

	// Skip current whitespace
	while (pos < max_length && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\n')) {
		pos = tprompt_utf8_next_char(data, pos, max_length);
	}

	// Move forward until whitespace or end
	while (pos < max_length && data[pos] != ' ' && data[pos] != '\t' && data[pos] != '\n') {
		pos = tprompt_utf8_next_char(data, pos, max_length);
	}

	handle->buffer.cursor = pos;
	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/**
 * @brief Handle DELETE_WORD_BACKWARD action (Ctrl+W)
 *
 * Delete from cursor backwards to the start of the current/previous word.
 * Stops at logical line boundaries (doesn't delete newlines).
 */
int tprompt_action_delete_word_backward(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

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
		handle->input_state.has_goal_column = false;
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

	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/**
 * @brief Handle DELETE_TO_END_OF_LINE action (Ctrl+K)
 *
 * Delete from cursor to the end of the current logical line.
 */
int tprompt_action_delete_to_end_of_line(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

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

	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/**
 * @brief Handle DELETE_TO_START_OF_LINE action (Ctrl+U)
 *
 * Delete from cursor to the start of the current logical line.
 * If at the start of a line (not first), deletes the previous line.
 */
int tprompt_action_delete_to_start_of_line(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

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

		// Delete from start of previous line to current cursor (includes newline)
		size_t delete_len = handle->buffer.cursor - prev_line_start;
		handle->buffer.cursor = prev_line_start;
		if (delete_len > 0) {
			tprompt_buffer_delete_at(&handle->buffer, delete_len);
		}

		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Otherwise, delete from start of current line to cursor
	if (handle->buffer.cursor > line_start) {
		size_t delete_len = handle->buffer.cursor - line_start;
		handle->buffer.cursor = line_start;
		tprompt_buffer_delete_at(&handle->buffer, delete_len);
	}

	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/**
 * @brief Handle MOVE_TO_LINE_START action (Ctrl+A)
 *
 * Move cursor to the start of the current logical line.
 * If already at start, move to start of previous line.
 */
int tprompt_action_move_to_line_start(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

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
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Move cursor to start of logical line
	handle->buffer.cursor = line_start;

	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/**
 * @brief Handle MOVE_TO_LINE_END action (Ctrl+E)
 *
 * Move cursor to the end of the current logical line.
 */
int tprompt_action_move_to_line_end(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

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

	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/**
 * @brief Handle COMPLETE action (Tab)
 *
 * If completion is active, confirm selection.
 * Otherwise, do nothing (TAB is reserved for completion only, following REPL conventions).
 */
int tprompt_action_complete(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle) {
		return -1;
	}

	// If completion is active, confirm selected completion
	if (handle->completion_state.active) {
		if (tprompt_completion_confirm(handle) == 0) {
			tprompt_completion_deactivate(handle);
		}
		return 0; // Continue editing
	}

	// Otherwise, do nothing (TAB is reserved for completion)
	// This follows REPL conventions (Python, Ruby, Node.js, IPython, etc.)
	// where TAB is dedicated to completion and does not insert literal tab characters
	return 0; // Continue editing
}

/**
 * @brief Handle INSERT_NEWLINE action
 *
 * Insert a newline character at the current cursor position.
 */
int tprompt_action_insert_newline(tprompt_handle_t handle, const terse_event_t *event)
{
	(void)event; // Unused

	if (!handle) {
		return -1;
	}

	// Insert newline at cursor position
	if (tprompt_buffer_insert_limited(handle, "\n", 1) != 0) {
		if (handle->last_error.category == TPROMPT_ERROR_NONE) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to insert newline: buffer at %zu/%zu bytes",
				handle->buffer.length, handle->buffer.size);
		}
		return -1;
	}

	handle->input_state.has_goal_column = false;
	return 0; // Continue editing
}

/* ------------------------------------------------------------------------
 * Action Dispatcher
 * ------------------------------------------------------------------------ */

/**
 * @brief Execute an action
 *
 * Dispatches actions to individual action handler functions. All editing
 * actions now have dedicated handlers following the table-driven action system.
 * Only CONFIRM_INPUT and NONE fall back to tprompt_handle_key_event() for
 * special handling.
 *
 * @param handle Prompt handle
 * @param action Action to execute
 * @param event Original event (needed for some handlers)
 * @return 1 to confirm input, 0 to continue editing, -1 on error
 */
int tprompt_execute_action(tprompt_handle_t handle, tprompt_action_t action, const terse_event_t *event)
{
	if (!handle || !event) {
		return -1;
	}

	// Dispatch to individual action handlers
	switch (action) {
	case TPROMPT_ACTION_MOVE_LEFT:
		return tprompt_action_move_left(handle, event);

	case TPROMPT_ACTION_MOVE_RIGHT:
		return tprompt_action_move_right(handle, event);

	case TPROMPT_ACTION_DELETE_BACKWARD:
		return tprompt_action_delete_backward(handle, event);

	case TPROMPT_ACTION_DELETE_FORWARD:
		return tprompt_action_delete_forward(handle, event);

	case TPROMPT_ACTION_MOVE_HOME:
		return tprompt_action_move_home(handle, event);

	case TPROMPT_ACTION_MOVE_END:
		return tprompt_action_move_end(handle, event);

	case TPROMPT_ACTION_HISTORY_PREV:
		return tprompt_action_history_prev(handle, event);

	case TPROMPT_ACTION_HISTORY_NEXT:
		return tprompt_action_history_next(handle, event);

	case TPROMPT_ACTION_MOVE_UP:
		return tprompt_action_move_up(handle, event);

	case TPROMPT_ACTION_MOVE_DOWN:
		return tprompt_action_move_down(handle, event);

	case TPROMPT_ACTION_MOVE_WORD_LEFT:
		return tprompt_action_move_word_left(handle, event);

	case TPROMPT_ACTION_MOVE_WORD_RIGHT:
		return tprompt_action_move_word_right(handle, event);

	case TPROMPT_ACTION_DELETE_WORD_BACKWARD:
		return tprompt_action_delete_word_backward(handle, event);

	case TPROMPT_ACTION_DELETE_TO_END_OF_LINE:
		return tprompt_action_delete_to_end_of_line(handle, event);

	case TPROMPT_ACTION_DELETE_TO_START_OF_LINE:
		return tprompt_action_delete_to_start_of_line(handle, event);

	case TPROMPT_ACTION_MOVE_TO_LINE_START:
		return tprompt_action_move_to_line_start(handle, event);

	case TPROMPT_ACTION_MOVE_TO_LINE_END:
		return tprompt_action_move_to_line_end(handle, event);

	case TPROMPT_ACTION_COMPLETE:
		return tprompt_action_complete(handle, event);

	case TPROMPT_ACTION_INSERT_NEWLINE:
		return tprompt_action_insert_newline(handle, event);

	case TPROMPT_ACTION_INSERT_CHAR:
		// Insert character at cursor (delegates to char input handler for completion trigger detection)
		{
			char utf8_buf[5];
			unsigned int scalar = event->data.ch.scalar;

			// Encode scalar to UTF-8
			int len = tprompt_utf8_encode(scalar, utf8_buf, sizeof(utf8_buf));
			if (len < 0) {
				tprompt_set_error(&handle->last_error, TPROMPT_ERROR_INVALID_ARGS, 0,
					"Invalid Unicode scalar value: 0x%X", scalar);
				return -1;
			}
			utf8_buf[len] = '\0';

			// Use char input handler for completion trigger detection and insertion
			if (tprompt_handle_char_input(handle, utf8_buf, event->data.ch.width) != 0) {
				tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
					"Failed to insert character (scalar=0x%X) into buffer", scalar);
				return -1;
			}

			handle->input_state.has_goal_column = false;
			return 0;
		}

	case TPROMPT_ACTION_CONFIRM_WITHOUT_VALIDATION:
		// Immediately confirm input without running validation callback
		return 1;

	case TPROMPT_ACTION_CONFIRM_INPUT:
		// Mark confirmation as pending (will be validated in main loop)
		// Set force_confirmation to bypass default multiline behavior
		handle->pending_confirmation = true;
		handle->force_confirmation = true;
		return 0;

	case TPROMPT_ACTION_NONE:
	default:
		// For other actions not yet extracted, fall back to existing handler
		return tprompt_handle_key_event(handle, event);
	}
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
