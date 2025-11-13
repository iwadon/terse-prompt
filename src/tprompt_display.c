/**
 * @file tprompt_display.c
 * @brief Display and rendering implementation for terse-prompt
 */

#include "tprompt_display.h"
#include "tprompt_internal.h"
#include "terse.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * Internal Helper Functions
 * ======================================================================== */

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


/* ========================================================================
 * Physical Position Calculation
 * ======================================================================== */

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
