/**
 * @file tprompt_display.c
 * @brief Display and rendering implementation for terse-prompt
 */

#include "tprompt_display.h"
#include "terse.h"
#include "tprompt_internal.h"
#include "tprompt_status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Internal Helper Functions
 * ======================================================================== */

/**
 * @brief Calculate total physical lines needed for buffer content
 */
static size_t tprompt_calculate_total_physical_lines(tprompt_handle_t handle)
{
	if (!handle) {
		return 1;
	}

	size_t wrap_width = handle->display.current_buffer.cols;
	if (wrap_width == 0) {
		wrap_width = 79;
	}

	size_t current_col = 0;
	if (handle->prompt) {
		current_col = strlen(handle->prompt);
	}
	size_t total_physical_lines = 1;

	const char *data = handle->buffer.data;
	size_t length = handle->buffer.length;
	size_t i = 0;

	while (i < length) {
		if (data[i] == '\n') {
			total_physical_lines++;
			current_col = 0;

			size_t marker_width = tprompt_get_continuation_marker_width(handle);
			current_col += marker_width;

			i++;
			continue;
		}

		if (current_col >= wrap_width) {
			total_physical_lines++;
			current_col = 0;
		}

		size_t char_len = tprompt_utf8_char_length((unsigned char)data[i]);
		if (char_len == 0) {
			char_len = 1;
			i++;
			current_col++;
		} else {
			unsigned int scalar = tprompt_utf8_decode((const unsigned char *)&data[i], char_len);
			int char_width = tprompt_get_char_width(scalar);
			i += char_len;
			current_col += char_width;
		}
	}

	return total_physical_lines;
}

/**
 * @brief Estimate how many status lines will be rendered
 */
int tprompt_estimate_status_lines(tprompt_handle_t handle)
{
	if (!handle) {
		return 0;
	}

	if (handle->options.flags & TPROMPT_FLAG_HIDE_STATUS_LINE) {
		return 0;
	}

	tprompt_status_line_fn callback = NULL;
	void *user_data = NULL;

	if (handle->options.flags & TPROMPT_FLAG_SHOW_DEBUG_STATUS) {
		callback = tprompt_internal_debug_status_callback;
		user_data = NULL;
	} else if (handle->status_line_callback) {
		callback = handle->status_line_callback;
		user_data = handle->status_line_user_data;
	}

	if (!callback) {
		return 0;
	}

	char status_buffer[TPROMPT_STATUS_LINE_BUFFER_SIZE];
	int lines = callback(handle, status_buffer, sizeof(status_buffer), user_data);

	if (lines < 0) {
		return 0;
	}

	if (lines > TPROMPT_STATUS_LINE_MAX_ROWS) {
		lines = TPROMPT_STATUS_LINE_MAX_ROWS;
	}

	return lines;
}

/**
 * @brief Determine how many rows are required for the current frame
 */
static size_t tprompt_calculate_required_rows(tprompt_handle_t handle)
{
	if (!handle) {
		return 0;
	}

	size_t required_rows = handle->display.total_physical_lines;

	int status_lines = tprompt_estimate_status_lines(handle);
	if (status_lines > 0) {
		required_rows += (size_t)status_lines;
	}

	if (handle->completion_state.active) {
		size_t candidate_count = handle->completion_state.candidate_count;
		if (candidate_count > 0) {
			size_t max_visible = TPROMPT_MAX_VISIBLE_COMPLETION_ROWS;
			size_t visible_start = handle->completion_state.display_offset;
			size_t visible_end = candidate_count < visible_start + max_visible
				? candidate_count
				: visible_start + max_visible;

			size_t rows_for_candidates = visible_end - visible_start;
			size_t rows_for_indicators = 0;

			if (visible_start > 0) {
				rows_for_indicators++;
			}
			if (visible_end < candidate_count) {
				rows_for_indicators++;
			}

			required_rows += rows_for_candidates + rows_for_indicators;
		}
	}

	if (required_rows == 0) {
		required_rows = 1;
	}

	return required_rows;
}

/**
 * @brief Ensure there is enough vertical space for the prompt/status/completions
 */
static int tprompt_ensure_vertical_space(tprompt_handle_t handle, size_t required_rows)
{
	if (!handle || required_rows == 0) {
		return -1;
	}

	size_t term_height = handle->display.terminal_height > 0 ? handle->display.terminal_height : 24;

	if (required_rows >= term_height) {
		handle->display.start_row = 0;
		handle->display.start_row_known = true;
		return 0;
	}

	int available_rows = (int)term_height - handle->display.start_row;
	if (available_rows >= (int)required_rows) {
		return 0;
	}

	size_t missing_rows = (size_t)((int)required_rows - available_rows);

	// Scrolling the terminal to make room is manipulation outside the virtual
	// rectangle, so it is emitted as raw escapes (terse_write_raw): move to the
	// last terminal row (CUP is 1-based) and write a newline per missing row.
	char seq[16];
	int len = snprintf(seq, sizeof(seq), "\x1b[%d;1H", (int)term_height);
	if (len > 0 && terse_write_raw(handle->terse, seq, (size_t)len) != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, 0,
			"Failed to move cursor before making space for prompt");
		return -1;
	}

	for (size_t i = 0; i < missing_rows; i++) {
		if (terse_write_raw(handle->terse, "\r\n", 2) != TERSE_OK) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, 0,
				"Failed to scroll terminal to fit prompt");
			return -1;
		}
	}

	int new_start_row = (int)term_height - (int)required_rows;
	if (new_start_row < 0) {
		new_start_row = 0;
	}

	handle->display.start_row = new_start_row;
	handle->display.start_row_known = true;

	return 0;
}

/* ========================================================================
 * Physical Position Calculation
 * ======================================================================== */

/**
 * @brief Calculate physical line and column from byte offset
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

	size_t wrap_width = handle->display.current_buffer.cols;
	if (wrap_width == 0) {
		wrap_width = 79;
	}

	if (byte_offset > handle->buffer.length) {
		byte_offset = handle->buffer.length;
	}

	size_t current_col = 0;
	if (include_prompt && handle->prompt) {
		current_col = strlen(handle->prompt);
	}
	size_t current_physical_line = 0;

	const char *data = handle->buffer.data;
	size_t i = 0;

	while (i < byte_offset) {
		if (data[i] == '\n') {
			current_physical_line++;
			current_col = 0;

			size_t marker_width = tprompt_get_continuation_marker_width(handle);
			current_col += marker_width;

			i++;
			continue;
		}

		if (current_col >= wrap_width) {
			current_physical_line++;
			current_col = 0;
		}

		size_t char_len = tprompt_utf8_char_length((unsigned char)data[i]);
		if (char_len == 0) {
			char_len = 1;
			i++;
			current_col++;
		} else {
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
 */
size_t tprompt_get_continuation_marker_width(tprompt_handle_t handle)
{
	if (!handle || !handle->prompt) {
		return 0;
	}
	return tprompt_get_prompt_width(handle, handle->prompt);
}

void tprompt_display_calculate_layout(tprompt_handle_t handle)
{
	if (!handle) {
		return;
	}

	terse_size_t size = terse_get_size(handle->terse);
	if (size.known && size.cols > 0 && size.rows > 0) {
		handle->display.terminal_width = (size_t)size.cols;
		handle->display.terminal_height = (size_t)size.rows;
	}

	if (handle->display.terminal_width == 0) {
		handle->display.terminal_width = 80;
	}
	if (handle->display.terminal_height == 0) {
		handle->display.terminal_height = 24;
	}

	tprompt_calculate_physical_position(handle, handle->buffer.cursor, true,
		&handle->display.physical_line,
		&handle->display.physical_column);

	handle->display.total_physical_lines = tprompt_calculate_total_physical_lines(handle);
}

int tprompt_display_update_cursor(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

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

	int target_row = base_row + (int)handle->display.physical_line;

	int target_col = (int)handle->display.physical_column;
	terse_error_t terr = terse_move_to(handle->terse, target_row, target_col);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to move cursor to position (row=%d, col=%zu)",
			target_row, handle->display.physical_column);
		return -1;
	}

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

	terse_error_t terr = terse_clear_line(handle->terse, TERSE_CLEAR_ALL);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to clear current display line");
		return -1;
	}

	terse_cursor_position_t pos = terse_get_cursor_position(handle->terse);
	int row = pos.known ? pos.row : 0;
	terr = terse_move_to(handle->terse, row, 0);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to move cursor to line start");
		return -1;
	}

	terr = terse_flush(handle->terse);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to flush output after display clear");
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

	if (end_byte > handle->buffer.length) {
		end_byte = handle->buffer.length;
	}
	if (start_byte > end_byte) {
		start_byte = end_byte;
	}

	if (!handle->display.is_dirty) {
		handle->display.is_dirty = true;
		handle->display.dirty_start_byte = start_byte;
		handle->display.dirty_end_byte = end_byte;
	} else {
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

/* ========================================================================
 * Screen Buffer Management (delegated to terse's TERSE_RENDER_BUFFERED)
 *
 * The cell storage and frame diff now live in terse. terse-prompt keeps only
 * the virtual rectangle dimensions (current_buffer.rows/cols) for layout and
 * writes characters through to terse via tprompt_screen_buffer_write_string().
 * ======================================================================== */

int tprompt_screen_buffer_write_string(tprompt_handle_t handle,
	tprompt_screen_buffer_t *buffer,
	size_t row, size_t col,
	const char *str)
{
	if (!handle || !buffer || !str || !handle->terse) {
		return -1;
	}

	size_t current_col = col;
	size_t i = 0;
	size_t str_len = strlen(str);

	while (i < str_len && current_col < buffer->cols) {
		size_t char_len = tprompt_utf8_char_length((unsigned char)str[i]);
		if (char_len == 0 || i + char_len > str_len) {
			i++;
			continue;
		}

		unsigned int scalar = tprompt_utf8_decode((const unsigned char *)&str[i], char_len);

		int char_width = tprompt_get_char_width(scalar);
		if (char_width < 0) {
			char_width = 1;
		}

		if (current_col + (size_t)char_width > buffer->cols) {
			break;
		}

		if (terse_move_to(handle->terse, (int)row, (int)current_col) != TERSE_OK) {
			return -1;
		}

		char ch[5];
		memcpy(ch, &str[i], char_len);
		ch[char_len] = '\0';
		if (terse_write_text(handle->terse, ch) != TERSE_OK) {
			return -1;
		}

		current_col += (size_t)char_width;
		i += char_len;
	}

	return (int)(current_col - col);
}

int tprompt_buffer_based_rendering_init(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	if (handle->display.buffer_based_rendering_active) {
		return 0;
	}

	if (handle->terse) {
		terse_size_t size = terse_get_size(handle->terse);
		if (size.known && size.cols > 0 && size.rows > 0) {
			handle->display.terminal_width = (size_t)size.cols;
			handle->display.terminal_height = (size_t)size.rows;
		}
	}

	// Rectangle width spans the terminal minus one column reserved for the
	// cursor at end of line, so the cursor always has a dedicated cell.
	size_t cols = handle->display.terminal_width > 0 ? handle->display.terminal_width : 80;
	if (cols > 1) {
		cols -= 1;
	}

	// Logical height of the prompt area. It grows dynamically via
	// tprompt_display_resize_buffers() when input wraps past it.
	size_t rows = 20;

	// terse projects each rectangle row to an absolute terminal row at flush
	// time and rejects a move past the terminal bottom, so the rectangle must
	// fit within the terminal height. Clamp it; on a short terminal the prompt
	// area simply uses every row available.
	if (handle->display.terminal_height > 0 && rows > handle->display.terminal_height) {
		rows = handle->display.terminal_height;
	}

	// The cell storage lives in terse. Size terse's virtual rectangle to
	// rows x cols at origin (0,0); the per-frame origin (start_row) is applied
	// in tprompt_display_render_buffered() via terse_buffer_set_region().
	if (terse_buffer_set_region(handle->terse, 0, 0, (int)rows, (int)cols) != TERSE_OK) {
		return -1;
	}

	handle->display.current_buffer.rows = rows;
	handle->display.current_buffer.cols = cols;
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

	// The cell storage is owned by terse and freed when the terse handle is
	// closed; terse-prompt only tracked logical dimensions, so just reset them.
	handle->display.current_buffer.rows = 0;
	handle->display.current_buffer.cols = 0;
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

	// terse rejects a flush that would move past the terminal bottom, so the
	// rectangle (placed at start_row) must fit in the rows available below it.
	if (handle->display.terminal_height > 0) {
		size_t available_rows = (size_t)handle->display.start_row < handle->display.terminal_height
			? handle->display.terminal_height - (size_t)handle->display.start_row
			: 1;
		if (new_rows > available_rows) {
			new_rows = available_rows;
		}
	}

	// Only grow; never shrink the rectangle mid-session (matches prior behavior).
	if (handle->display.current_buffer.rows >= new_rows && handle->display.current_buffer.cols >= new_cols) {
		return 0;
	}

	if (terse_buffer_set_region(handle->terse, handle->display.start_row, 0,
			(int)new_rows, (int)new_cols)
		!= TERSE_OK) {
		return -1;
	}

	handle->display.current_buffer.rows = new_rows;
	handle->display.current_buffer.cols = new_cols;
	return 0;
}

/* ========================================================================
 * Buffer-based Rendering Functions
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

	size_t row = 0;
	size_t col = 0;

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
	size_t wrap_width = buf->cols;

	size_t row = start_row;
	size_t col = start_col;

	size_t i = 0;
	while (i < handle->buffer.length) {
		if (handle->buffer.data[i] == '\n') {
			row++;
			col = 0;

			if (row >= buf->rows - 1) {
				size_t new_rows = buf->rows * 2;
				if (tprompt_display_resize_buffers(handle, new_rows, buf->cols) != 0) {
					return -1;
				}
				buf = &handle->display.current_buffer;
			}

			const char *cont_prompt = tprompt_get_continuation_prompt(handle);
			if (cont_prompt && cont_prompt[0] != '\0') {
				int cols_written = tprompt_screen_buffer_write_string(handle, buf, row, col, cont_prompt);
				if (cols_written < 0) {
					return -1;
				}
				col += (size_t)cols_written;
			}

			i++;
			continue;
		}

		if (col >= wrap_width) {
			row++;
			col = 0;

			if (row >= buf->rows - 1) {
				size_t new_rows = buf->rows * 2;
				if (tprompt_display_resize_buffers(handle, new_rows, buf->cols) != 0) {
					return -1;
				}
				buf = &handle->display.current_buffer;
			}
		}

		size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[i]);
		if (char_len == 0 || i + char_len > handle->buffer.length) {
			i++;
			continue;
		}

		unsigned int scalar = tprompt_utf8_decode((const unsigned char *)&handle->buffer.data[i], char_len);

		int char_width = tprompt_get_char_width(scalar);
		if (char_width < 0) {
			char_width = 1;
		}

		if (col + (size_t)char_width > wrap_width) {
			row++;
			col = 0;

			if (row >= buf->rows - 1) {
				size_t new_rows = buf->rows * 2;
				if (tprompt_display_resize_buffers(handle, new_rows, buf->cols) != 0) {
					return -1;
				}
				buf = &handle->display.current_buffer;
			}
		}

		// Write this character into terse's virtual buffer at the local cell.
		if (terse_move_to(handle->terse, (int)row, (int)col) != TERSE_OK) {
			return -1;
		}
		char ch[5];
		memcpy(ch, &handle->buffer.data[i], char_len);
		ch[char_len] = '\0';
		if (terse_write_text(handle->terse, ch) != TERSE_OK) {
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
 * @brief Render status line to screen buffer (callback-based, multi-line supported)
 */
int tprompt_render_status_line(tprompt_handle_t handle, size_t start_row)
{
	if (!handle) {
		return -1;
	}

	if (handle->options.flags & TPROMPT_FLAG_HIDE_STATUS_LINE) {
		return 0;
	}

	tprompt_status_line_fn callback = NULL;
	void *user_data = NULL;

	if (handle->options.flags & TPROMPT_FLAG_SHOW_DEBUG_STATUS) {
		callback = tprompt_internal_debug_status_callback;
		user_data = NULL;
	} else if (handle->status_line_callback) {
		callback = handle->status_line_callback;
		user_data = handle->status_line_user_data;
	}

	if (!callback) {
		return 0;
	}

	char status_buffer[TPROMPT_STATUS_LINE_BUFFER_SIZE];
	status_buffer[0] = '\0';

	int num_lines = callback(handle, status_buffer, sizeof(status_buffer), user_data);

	if (num_lines < 0) {
		return -1;
	}

	if (num_lines == 0) {
		return 0;
	}

	if (num_lines > TPROMPT_STATUS_LINE_MAX_ROWS) {
		num_lines = TPROMPT_STATUS_LINE_MAX_ROWS;
	}

	tprompt_screen_buffer_t *buf = &handle->display.current_buffer;
	const char *line_start = status_buffer;
	size_t current_row = start_row;
	int lines_rendered = 0;

	for (int i = 0; i < num_lines && current_row < buf->rows; i++) {
		const char *line_end = strchr(line_start, '\n');
		size_t line_len;

		if (line_end) {
			line_len = (size_t)(line_end - line_start);
		} else {
			line_len = strlen(line_start);
		}

		char line_buffer[TPROMPT_STATUS_LINE_BUFFER_SIZE];
		if (line_len >= sizeof(line_buffer)) {
			line_len = sizeof(line_buffer) - 1;
		}
		memcpy(line_buffer, line_start, line_len);
		line_buffer[line_len] = '\0';

		int cols_written = tprompt_screen_buffer_write_string(handle, buf, current_row, 0, line_buffer);
		if (cols_written < 0) {
			break;
		}

		lines_rendered++;
		current_row++;

		if (line_end) {
			line_start = line_end + 1;
		} else {
			break;
		}
	}

	return lines_rendered;
}

/**
 * @brief Render completion list to buffer (basic implementation)
 */
static int tprompt_render_to_buffer_completion(tprompt_handle_t handle, size_t start_row)
{
	if (!handle) {
		return -1;
	}

	if (!handle->completion_state.active) {
		return 0;
	}

	tprompt_screen_buffer_t *buf = &handle->display.current_buffer;
	size_t candidate_count = handle->completion_state.candidate_count;

	if (candidate_count == 0) {
		return 0;
	}

	bool use_extended = handle->completion_state.use_extended;
	tprompt_completion_candidate_t *candidates_ex = handle->completion_state.candidates_ex;
	char **candidates = handle->completion_state.candidates;
	size_t selected_index = handle->completion_state.selected_index;

	size_t max_candidate_width = 0;
	if (use_extended && candidates_ex) {
		for (size_t i = 0; i < candidate_count; i++) {
			if (candidates_ex[i].text) {
				size_t text_width = tprompt_utf8_char_count(candidates_ex[i].text, strlen(candidates_ex[i].text));
				if (text_width > max_candidate_width) {
					max_candidate_width = text_width;
				}
			}
		}
	} else if (candidates) {
		for (size_t i = 0; i < candidate_count; i++) {
			if (candidates[i]) {
				size_t text_width = tprompt_utf8_char_count(candidates[i], strlen(candidates[i]));
				if (text_width > max_candidate_width) {
					max_candidate_width = text_width;
				}
			}
		}
	}

	size_t display_offset = handle->completion_state.display_offset;
	size_t max_visible = TPROMPT_MAX_VISIBLE_COMPLETION_ROWS;
	size_t visible_start = display_offset;
	size_t visible_end = (candidate_count < display_offset + max_visible)
		? candidate_count
		: display_offset + max_visible;
	size_t visible_count = visible_end - visible_start;

	size_t current_row = start_row;
	if (visible_start > 0) {
		if (current_row >= buf->rows - 1) {
			size_t new_rows = buf->rows * 2;
			if (tprompt_display_resize_buffers(handle, new_rows, buf->cols) != 0) {
				return -1;
			}
			buf = &handle->display.current_buffer;
		}

		char indicator[64];
		snprintf(indicator, sizeof(indicator), "  ↑ %zu more above", visible_start);
		tprompt_screen_buffer_write_string(handle, buf, current_row, 0, indicator);
		current_row++;
	}

	for (size_t i = visible_start; i < visible_end; i++) {
		size_t row = current_row + (i - visible_start);
		size_t col = 0;

		if (row >= buf->rows - 1) {
			size_t new_rows = buf->rows * 2;
			if (tprompt_display_resize_buffers(handle, new_rows, buf->cols) != 0) {
				break;
			}
			buf = &handle->display.current_buffer;
		}

		const char *prefix = (i == selected_index) ? "> " : "  ";
		int cols_written = tprompt_screen_buffer_write_string(handle, buf, row, col, prefix);
		if (cols_written < 0) {
			return -1;
		}
		col += (size_t)cols_written;

		const char *candidate_text = NULL;
		const char *description_text = NULL;

		if (use_extended && candidates_ex) {
			candidate_text = candidates_ex[i].text;
			description_text = candidates_ex[i].description;
		} else if (candidates) {
			candidate_text = candidates[i];
		}

		if (candidate_text) {
			cols_written = tprompt_screen_buffer_write_string(handle, buf, row, col, candidate_text);
			if (cols_written < 0) {
				return -1;
			}
			col += (size_t)cols_written;

			if (description_text && description_text[0] != '\0') {
				size_t text_width = tprompt_utf8_char_count(candidate_text, strlen(candidate_text));
				size_t padding = max_candidate_width > text_width ? (max_candidate_width - text_width) : 0;

				for (size_t j = 0; j < padding + 2; j++) {
					cols_written = tprompt_screen_buffer_write_string(handle, buf, row, col, " ");
					if (cols_written < 0) {
						return -1;
					}
					col += (size_t)cols_written;
				}

				cols_written = tprompt_screen_buffer_write_string(handle, buf, row, col, description_text);
				if (cols_written < 0) {
					return -1;
				}
			}
		}
	}

	if (visible_end < candidate_count) {
		current_row = start_row + (visible_start > 0 ? 1 : 0) + visible_count;

		if (current_row >= buf->rows - 1) {
			size_t new_rows = buf->rows * 2;
			if (tprompt_display_resize_buffers(handle, new_rows, buf->cols) != 0) {
				return -1;
			}
			buf = &handle->display.current_buffer;
		}

		char indicator[64];
		size_t hidden_below = candidate_count - visible_end;
		snprintf(indicator, sizeof(indicator), "  ↓ %zu more below", hidden_below);
		tprompt_screen_buffer_write_string(handle, buf, current_row, 0, indicator);
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

	tprompt_display_calculate_layout(handle);

	size_t required_rows = tprompt_calculate_required_rows(handle);
	if (required_rows > 0 && tprompt_ensure_vertical_space(handle, required_rows) != 0) {
		return -1;
	}

	// Position terse's virtual rectangle so its top-left maps to start_row,
	// column 0. terse projects each local cell to the terminal as
	// absolute = origin + local.
	//
	// The rectangle bottom (start_row + rows) must not exceed the terminal
	// height, or terse_buffer_flush() will move the cursor past the terminal
	// bottom and fail. Clamp the height handed to terse to the rows available
	// below start_row. current_buffer.rows is the logical height used for layout
	// and wrapping and is left unchanged; only the rectangle handed to terse is
	// clamped.
	size_t region_rows = handle->display.current_buffer.rows;
	if (handle->display.terminal_height > 0) {
		size_t available_rows = (size_t)handle->display.start_row < handle->display.terminal_height
			? handle->display.terminal_height - (size_t)handle->display.start_row
			: 1;
		if (region_rows > available_rows) {
			region_rows = available_rows;
		}
	}
	if (terse_buffer_set_region(handle->terse, handle->display.start_row, 0,
			(int)region_rows,
			(int)handle->display.current_buffer.cols)
		!= TERSE_OK) {
		return -1;
	}

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

	// 3. Render status line (starts after input)
	size_t status_start_row = end_row + 1;
	int status_lines_rendered = tprompt_render_status_line(handle, status_start_row);
	if (status_lines_rendered < 0) {
		status_lines_rendered = 0;
	}

	// 4. Render completion list if active (starts after status line)
	if (handle->completion_state.active) {
		size_t completion_start_row = status_start_row + (size_t)status_lines_rendered;
		if (tprompt_render_to_buffer_completion(handle, completion_start_row) != 0) {
			return -1;
		}
	}

	// 5. Request the cursor's final resting place in local coordinates.
	size_t cursor_line, cursor_col;
	tprompt_calculate_physical_position(handle, handle->buffer.cursor,
		true, &cursor_line, &cursor_col);
	if (terse_buffer_set_cursor(handle->terse, (int)cursor_line, (int)cursor_col) != TERSE_OK) {
		return -1;
	}

	// 6. Flush: terse diffs against the previously displayed frame, emits only
	// the changed cells (projected through the origin), then positions the cursor.
	terse_error_t terr = terse_flush(handle->terse);
	if (terr != TERSE_OK) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)terr,
			"Failed to flush output after buffered render");
		return -1;
	}

	handle->display.prev_total_physical_lines = handle->display.total_physical_lines;

	tprompt_display_clear_dirty(handle);

	return 0;
}
