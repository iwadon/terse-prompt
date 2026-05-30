/**
 * @file tprompt_display.h
 * @brief Display and rendering functions for terse-prompt
 *
 * Provides functions for display management, screen buffer operations,
 * and differential rendering.
 */

#ifndef TPROMPT_DISPLAY_H
#define TPROMPT_DISPLAY_H

#include "tprompt.h"
#include "tprompt_internal.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Physical Position Calculation
 * ======================================================================== */

/**
 * @brief Calculate physical line and column from byte offset
 * @param handle Prompt handle
 * @param byte_offset Byte offset in buffer (will be clamped to buffer length)
 * @param include_prompt Whether to include prompt width in first line calculation
 * @param out_physical_line Output: physical line number (0-based from start of input)
 * @param out_physical_col Output: physical column within current line (0-based)
 */
void tprompt_calculate_physical_position(tprompt_handle_t handle, size_t byte_offset,
	bool include_prompt,
	size_t *out_physical_line, size_t *out_physical_col);

/**
 * @brief Get the width of the continuation line marker
 * @param handle Prompt handle
 * @return Width in columns (same as prompt width), or 0 if no prompt
 */
size_t tprompt_get_continuation_marker_width(tprompt_handle_t handle);

/* ========================================================================
 * Display Layout and Rendering
 * ======================================================================== */

/**
 * @brief Calculate display layout (physical lines, columns)
 * @param handle Prompt handle
 */
void tprompt_display_calculate_layout(tprompt_handle_t handle);

/**
 * @brief Render the input buffer to terminal using buffer-based differential rendering
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_display_render_buffered(tprompt_handle_t handle);

/**
 * @brief Update cursor position on screen
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_display_update_cursor(tprompt_handle_t handle);

/**
 * @brief Clear the display area
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_display_clear(tprompt_handle_t handle);

/* ========================================================================
 * Dirty Region Tracking
 * ======================================================================== */

/**
 * @brief Mark a byte range as dirty (needs redrawing)
 * @param handle Prompt handle
 * @param start_byte Start byte offset (inclusive)
 * @param end_byte End byte offset (exclusive)
 */
void tprompt_display_mark_dirty_range(tprompt_handle_t handle,
	size_t start_byte,
	size_t end_byte);

/**
 * @brief Mark entire display as dirty (full redraw needed)
 * @param handle Prompt handle
 */
void tprompt_display_mark_all_dirty(tprompt_handle_t handle);

/**
 * @brief Clear dirty flags after successful render
 * @param handle Prompt handle
 */
void tprompt_display_clear_dirty(tprompt_handle_t handle);

/* ========================================================================
 * Screen Buffer Management
 *
 * The cell storage and frame diff now live in terse (TERSE_RENDER_BUFFERED).
 * terse-prompt keeps only the virtual rectangle dimensions and writes through
 * to terse via tprompt_screen_buffer_write_string().
 * ======================================================================== */

/**
 * @brief Write a UTF-8 string into the virtual rectangle via terse
 * @param handle Prompt handle (for terse handle and character width)
 * @param buffer Virtual rectangle dimensions (for the column bound)
 * @param row Starting row in rectangle-local coordinates (0-based)
 * @param col Starting column in rectangle-local coordinates (0-based)
 * @param str UTF-8 string to write
 * @return Number of columns advanced, or -1 on error
 */
int tprompt_screen_buffer_write_string(tprompt_handle_t handle,
	tprompt_screen_buffer_t *buffer,
	size_t row, size_t col,
	const char *str);

/* ========================================================================
 * Buffer-Based Rendering System
 * ======================================================================== */

/**
 * @brief Initialize buffer-based rendering system
 * @param handle Prompt handle
 * @return 0 on success, -1 on allocation failure
 */
int tprompt_buffer_based_rendering_init(tprompt_handle_t handle);

/**
 * @brief Free buffer-based rendering resources
 * @param handle Prompt handle
 */
void tprompt_buffer_based_rendering_free(tprompt_handle_t handle);

/**
 * @brief Grow the virtual rectangle, syncing the new size to terse
 * @param handle Prompt handle
 * @param new_rows New number of rows
 * @param new_cols New number of columns
 * @return 0 on success, -1 on failure
 */
int tprompt_display_resize_buffers(tprompt_handle_t handle, size_t new_rows, size_t new_cols);

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_DISPLAY_H */
