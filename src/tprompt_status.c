/**
 * @file tprompt_status.c
 * @brief Status line rendering
 * @version 0.1
 * @date 2025-11-20
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_status.h"
#include "tprompt_session.h"
#include "tprompt_internal.h"
#include "tprompt_display.h"
#include <stdio.h>
#include <string.h>

/* ========================================================================
 * Status Line - Internal Debug Callback
 * ======================================================================== */

/**
 * @brief Internal debug status line callback
 *
 * Generates debug information for display in the status line.
 * Shows cursor position (physical x/y) and goal column if active.
 */
int tprompt_internal_debug_status_callback(
	tprompt_handle_t handle,
	char *buffer,
	size_t buffer_size,
	void *user_data)
{
	(void)user_data; // Unused

	if (!handle || !buffer || buffer_size == 0) {
		return -1;
	}

	int target_col = (int)handle->display.physical_column;
	if (handle->input_state.has_goal_column) {
		snprintf(buffer, buffer_size,
			"x=%d y=%d goal=%zu term=%zux%zu virt=%zux%zu",
			target_col,
			(int)handle->display.physical_line,
			handle->input_state.goal_column,
			handle->display.terminal_width,
			handle->display.terminal_height,
			handle->display.current_buffer.cols,
			handle->display.current_buffer.rows);
	} else {
		snprintf(buffer, buffer_size,
			"x=%d y=%d goal=- term=%zux%zu virt=%zux%zu",
			target_col,
			(int)handle->display.physical_line,
			handle->display.terminal_width,
			handle->display.terminal_height,
			handle->display.current_buffer.cols,
			handle->display.current_buffer.rows);
	}

	return 1; // 1 line written
}

/* ========================================================================
 * Status Line - Public API
 * ======================================================================== */

void tprompt_set_status_line_callback(
	tprompt_handle_t handle,
	tprompt_status_line_fn callback,
	void *user_data)
{
	if (!handle) {
		return;
	}

	handle->status_line_callback = callback;
	handle->status_line_user_data = user_data;
}

size_t tprompt_get_cursor_line(tprompt_handle_t handle)
{
	if (!handle) {
		return 0;
	}

	return handle->display.physical_line;
}

size_t tprompt_get_cursor_column(tprompt_handle_t handle)
{
	if (!handle) {
		return 0;
	}

	return handle->display.physical_column;
}

