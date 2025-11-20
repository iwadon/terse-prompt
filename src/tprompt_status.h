/**
 * @file tprompt_status.h
 * @brief Status line rendering
 * @version 0.1
 * @date 2025-11-20
 */

#ifndef TPROMPT_STATUS_H
#define TPROMPT_STATUS_H

#include "tprompt_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal debug status line callback
 *
 * Generates debug information (cursor position, goal column, etc.) for display
 * in the status line. Used when TPROMPT_FLAG_SHOW_DEBUG_STATUS is set.
 *
 * @param handle Prompt handle
 * @param buffer Output buffer for status line text
 * @param buffer_size Size of output buffer
 * @param user_data User data (unused for debug status)
 * @return Number of lines written (1), or -1 on error
 */
int tprompt_internal_debug_status_callback(
	tprompt_handle_t handle,
	char *buffer,
	size_t buffer_size,
	void *user_data);

/**
 * @brief Render status line to screen buffer
 *
 * Calls the active status line callback (if any) and renders the result
 * to the screen buffer starting at the specified row.
 *
 * Priority order:
 * 1. TPROMPT_FLAG_HIDE_STATUS_LINE → skip rendering
 * 2. TPROMPT_FLAG_SHOW_DEBUG_STATUS → use internal debug callback
 * 3. Custom callback (handle->status_line_callback) → use custom callback
 * 4. No callback → skip rendering
 *
 * @param handle Prompt handle
 * @param start_row Starting row for status line rendering
 * @return Number of rows rendered, or -1 on error
 */
int tprompt_render_status_line(tprompt_handle_t handle, size_t start_row);

/**
 * @brief Set status line callback
 * @param handle Prompt handle
 * @param callback Status line callback function
 * @param user_data User data to pass to callback
 */
void tprompt_set_status_line_callback(tprompt_handle_t handle,
	tprompt_status_line_fn callback,
	void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_STATUS_H */
