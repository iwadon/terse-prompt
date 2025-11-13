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
 * @brief Render the input buffer to terminal
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_display_render(tprompt_handle_t handle);

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

/**
 * @brief Render completion candidate list
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_display_render_completion(tprompt_handle_t handle);

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

/**
 * @brief Check if differential rendering is feasible
 * @param handle Prompt handle
 * @return true if differential rendering can be used, false for full redraw
 */
bool tprompt_display_can_use_differential(tprompt_handle_t handle);

/* ========================================================================
 * Screen Buffer Management
 * ======================================================================== */

/**
 * @brief Initialize screen buffer with given dimensions
 * @param buffer Buffer to initialize
 * @param rows Number of rows
 * @param cols Number of columns
 * @return 0 on success, -1 on allocation failure
 */
int tprompt_screen_buffer_init(tprompt_screen_buffer_t *buffer, size_t rows, size_t cols);

/**
 * @brief Free screen buffer resources
 * @param buffer Buffer to free
 */
void tprompt_screen_buffer_free(tprompt_screen_buffer_t *buffer);

/**
 * @brief Clear all cells in buffer (set to empty)
 * @param buffer Buffer to clear
 */
void tprompt_screen_buffer_clear(tprompt_screen_buffer_t *buffer);

/**
 * @brief Resize screen buffer (reallocates if needed)
 * @param buffer Buffer to resize
 * @param new_rows New number of rows
 * @param new_cols New number of columns
 * @return 0 on success, -1 on allocation failure
 */
int tprompt_screen_buffer_resize(tprompt_screen_buffer_t *buffer, size_t new_rows, size_t new_cols);

/**
 * @brief Write a UTF-8 character to buffer at specified position
 * @param buffer Target buffer
 * @param row Row position (0-based)
 * @param col Column position (0-based)
 * @param utf8_char UTF-8 character bytes
 * @param char_len Length of UTF-8 character in bytes
 * @param display_width Display width (1 or 2 for wide chars)
 * @return 0 on success, -1 if position out of bounds
 */
int tprompt_screen_buffer_write_char(tprompt_screen_buffer_t *buffer,
	size_t row, size_t col,
	const char *utf8_char, size_t char_len, size_t display_width);

/**
 * @brief Write a string to buffer starting at specified position
 * @param handle Prompt handle (for character width calculation)
 * @param buffer Target buffer
 * @param row Starting row (0-based)
 * @param col Starting column (0-based)
 * @param str UTF-8 string to write
 * @return Number of columns advanced, or -1 on error
 */
int tprompt_screen_buffer_write_string(tprompt_handle_t handle,
	tprompt_screen_buffer_t *buffer,
	size_t row, size_t col,
	const char *str);

/**
 * @brief Compare two buffers and mark differences in dirty_cells array
 * @param prev Previous buffer
 * @param curr Current buffer
 * @param dirty_cells Output array of dirty flags (must be rows * cols in size)
 */
void tprompt_screen_buffer_diff(const tprompt_screen_buffer_t *prev,
	const tprompt_screen_buffer_t *curr,
	bool *dirty_cells);

/**
 * @brief Swap current and previous buffers (avoids reallocation)
 * @param buffer1 First buffer
 * @param buffer2 Second buffer
 */
void tprompt_screen_buffer_swap(tprompt_screen_buffer_t *buffer1,
	tprompt_screen_buffer_t *buffer2);

/**
 * @brief Flush dirty cells from buffer to terminal
 * @param handle Prompt handle
 * @return 0 on success, -1 on error
 */
int tprompt_screen_buffer_flush_diff(tprompt_handle_t handle);

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
 * @brief Resize all display buffers (current, previous, dirty_cells) synchronously
 * @param handle Prompt handle
 * @param new_rows New number of rows
 * @param new_cols New number of columns
 * @return 0 on success, -1 on allocation failure
 */
int tprompt_display_resize_buffers(tprompt_handle_t handle, size_t new_rows, size_t new_cols);

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_DISPLAY_H */
