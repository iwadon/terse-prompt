/**
 * @file tprompt_internal.h
 * @brief Internal structures and helper functions for terse-prompt
 * @version 0.1
 * @date 2025-11-02
 *
 * This header defines the internal implementation details of terse-prompt.
 * It should not be exposed to library users.
 */

#ifndef TPROMPT_INTERNAL_H
#define TPROMPT_INTERNAL_H

#include "tprompt.h"
#include "tprompt_buffer.h"
#include "tprompt_completion.h"
#include "tprompt_history.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <terse.h>

#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Internal Data Structures
 * ======================================================================== */

/**
 * @brief Dynamic buffer for input text
 */
typedef struct tprompt_buffer {
	char *data;	   /**< UTF-8 text buffer */
	size_t size;   /**< Current buffer size (allocated capacity) */
	size_t length; /**< Current text length in bytes (excluding NUL) */
	size_t cursor; /**< Cursor position in bytes (byte offset) */
} tprompt_buffer_t;


/**
 * @brief Screen cell for virtual buffer-based rendering
 *
 * Represents a single character cell on the terminal screen.
 * Each cell can hold one UTF-8 character (up to 4 bytes).
 * Reserved fields for future color/attribute support.
 */
typedef struct tprompt_screen_cell {
	char utf8_char[5];		/**< UTF-8 character (max 4 bytes + NUL terminator) */
	uint8_t char_len;		/**< Byte length of UTF-8 character (0 = empty cell) */
	uint8_t display_width;	/**< Display width in columns (1 or 2 for wide chars) */
	bool is_continuation;	/**< True if this cell is the 2nd column of a wide char */

	/* Reserved for future extensions */
	uint8_t reserved1;		/**< Reserved for fg_color */
	uint8_t reserved2;		/**< Reserved for bg_color */
	uint8_t reserved3;		/**< Reserved for attributes (bold, underline, etc.) */
	uint8_t reserved4;		/**< Reserved for future use */
} tprompt_screen_cell_t;

/**
 * @brief Virtual screen buffer for differential rendering
 *
 * Holds a 2D grid of screen cells representing the terminal display area.
 * Used for detecting changes between frames and rendering only differences.
 */
typedef struct tprompt_screen_buffer {
	tprompt_screen_cell_t *cells;	/**< 2D array of cells (rows * cols) */
	size_t rows;					/**< Number of rows in buffer */
	size_t cols;					/**< Number of columns in buffer */
} tprompt_screen_buffer_t;

/**
 * @brief Display state for multi-line rendering
 */
typedef struct tprompt_display_state {
	size_t physical_line;			  /**< Current physical line number */
	size_t physical_column;			  /**< Current physical column number */
	size_t total_physical_lines;	  /**< Total number of physical lines */
	size_t prev_total_physical_lines; /**< Previous total_physical_lines (for clearing orphaned lines) */
	size_t terminal_width;			  /**< Terminal width in columns */
	size_t terminal_height;			  /**< Terminal height in rows */
	int start_row;					  /**< Starting row for rendering (0-based absolute terminal row) */
	bool start_row_known;			  /**< Whether start_row has been initialized */

	/* Dirty region tracking for differential rendering (legacy) */
	bool is_dirty;					  /**< Whether any region needs redrawing */
	size_t dirty_start_byte;		  /**< Start byte offset of dirty region */
	size_t dirty_end_byte;			  /**< End byte offset of dirty region (exclusive) */
	bool force_full_redraw;			  /**< Force full redraw on next render */

	/* Virtual screen buffer for new buffer-based differential rendering */
	tprompt_screen_buffer_t current_buffer;	 /**< Current frame buffer */
	tprompt_screen_buffer_t previous_buffer; /**< Previous frame buffer for diff */
	bool *dirty_cells;						 /**< Per-cell dirty flags (rows * cols) */
	bool buffer_based_rendering_active;		 /**< Whether buffer-based rendering is initialized */
} tprompt_display_state_t;

/**
 * @brief Input state for tracking key sequences
 */
typedef struct tprompt_input_state {
	int last_key_type;		/**< Last key event type (TERSE_EVENT_*) */
	size_t last_cursor_pos; /**< Cursor position at last key press */
	size_t goal_column;		/**< Goal column for vertical navigation (0 = not set) */
	bool has_goal_column;	/**< Whether goal column is currently active */
} tprompt_input_state_t;

/**
 * @brief Main handle structure (opaque pointer implementation)
 */
struct tprompt_handle {
	/* Core dependencies */
	terse_handle_t terse; /**< terse handle for terminal control */
	bool owns_terse;	  /**< Whether this handle owns the terse handle */

	/* Terminal raw mode */
#if defined(__unix__) || defined(__APPLE__)
	struct termios original_termios; /**< Original terminal settings */
	bool raw_mode_active;			 /**< Whether raw mode is currently active */
#endif

	/* Input buffer */
	tprompt_buffer_t buffer; /**< Dynamic input buffer */

	/* History */
	tprompt_history_t history; /**< History management */
	char *history_file_path;   /**< History file path (NULL if none) */

	/* Completion */
	tprompt_completion_fn completion_callback;	 /**< Completion callback function */
	void *completion_user_data;					 /**< User data for completion callback */
	char *completion_prefixes;					 /**< Completion trigger prefix characters */
	tprompt_completion_state_t completion_state; /**< Current completion state */

	/* Display */
	tprompt_display_state_t display; /**< Display state for rendering */
	char *prompt;					 /**< Current prompt string */

	/* Input state */
	tprompt_input_state_t input_state; /**< Input state for key sequence tracking */

	/* Keybindings */
	tprompt_keybinding_t *keybindings; /**< Custom keybindings array (dynamically allocated) */
	size_t keybinding_count;		   /**< Number of custom keybindings */

	/* Validation */
	bool pending_confirmation;	 /**< Whether input confirmation is pending validation */
	char *validation_error_msg;	 /**< Validation error message to display (NULL if none) */

	/* Status line */
	tprompt_status_line_fn status_line_callback; /**< Status line callback function */
	void *status_line_user_data;				 /**< User data for status line callback */

	/* Configuration */
	tprompt_options_t options; /**< Copy of options */

	/* Error tracking */
	tprompt_error_info_t last_error; /**< Last error information */
};

/**
 * @brief Global error information (for tprompt_open() failures)
 */
extern tprompt_error_info_t tprompt_global_error;

/* ========================================================================
 * Internal Helper Functions - Display Position Calculation
 * ======================================================================== */

/**
 * @brief Calculate cursor column position including prompt width
 * @param handle Prompt handle
 * @return Physical column position (0-based)
 */
size_t tprompt_calculate_cursor_col(tprompt_handle_t handle);

/**
 * @brief Calculate physical line and column from byte offset
 * @param handle Prompt handle
 * @param byte_offset Byte offset in buffer (will be clamped to buffer length)
 * @param include_prompt Whether to include prompt width in first line calculation
 * @param out_physical_line Output: physical line number (0-based from start of input)
 * @param out_physical_col Output: physical column within current line (0-based)
 */
void tprompt_calculate_physical_position(tprompt_handle_t handle, size_t byte_offset,
	bool include_prompt, size_t *out_physical_line, size_t *out_physical_col);

/* ========================================================================
 * Internal Helper Functions - UTF-8 Utilities
 * ======================================================================== */

/**
 * @brief Get the length of a UTF-8 character (in bytes)
 * @param byte First byte of UTF-8 character
 * @return Length in bytes (1-4), or 0 if invalid
 */
size_t tprompt_utf8_char_length(unsigned char byte);

/**
 * @brief Get the byte offset of the previous character
 * @param text UTF-8 text buffer
 * @param offset Current byte offset
 * @return Byte offset of previous character, or offset if at start
 */
size_t tprompt_utf8_prev_char(const char *text, size_t offset);

/**
 * @brief Get the byte offset of the next character
 * @param text UTF-8 text buffer
 * @param offset Current byte offset
 * @param max_length Maximum text length in bytes
 * @return Byte offset of next character, or offset if at end
 */
size_t tprompt_utf8_next_char(const char *text, size_t offset, size_t max_length);

/**
 * @brief Validate UTF-8 sequence
 * @param text Text to validate
 * @param length Length in bytes
 * @return true if valid UTF-8, false otherwise
 */
bool tprompt_utf8_validate(const char *text, size_t length);

/**
 * @brief Count number of UTF-8 characters in text
 * @param text UTF-8 text
 * @param length Length in bytes
 * @return Number of characters (code points)
 */
size_t tprompt_utf8_char_count(const char *text, size_t length);

/**
 * @brief Decode UTF-8 bytes to Unicode scalar value
 * @param bytes UTF-8 byte sequence
 * @param len Number of bytes in sequence
 * @return Unicode scalar value (code point)
 */
unsigned int tprompt_utf8_decode(const unsigned char *bytes, size_t len);

/**
 * @brief Get display width of a Unicode character
 * @param scalar Unicode scalar value
 * @return Display width (0 for control/combining, 1 for narrow, 2 for wide)
 */
int tprompt_get_char_width(unsigned int scalar);


/* ========================================================================
 * Internal Helper Functions - Display and Rendering
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

/**
 * @brief Get the width (in columns) of the continuation line marker
 *
 * The continuation marker is displayed on lines after the first logical line.
 * Format: (prompt_width - 2) spaces + '|' + space
 * The '|' aligns with the second-to-last character of the prompt.
 * Example: "tprompt> " (9 chars) -> "       | " (7 spaces + | + space)
 *
 * @param handle Prompt handle
 * @return Width of continuation marker in columns (same as prompt width, 0 if no prompt)
 */
size_t tprompt_get_continuation_marker_width(tprompt_handle_t handle);

/**
 * @brief Render completion candidate list
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_display_render_completion(tprompt_handle_t handle);

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
 * Internal Helper Functions - Screen Buffer Management
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

/* ========================================================================
 * Internal Helper Functions - Error Handling
 * ======================================================================== */

/**
 * @brief Set error information
 * @param error Error info structure to populate
 * @param category Error category
 * @param code Detailed error code
 * @param message Error message format string
 * @param ... Format arguments
 */
void tprompt_set_error(tprompt_error_info_t *error,
	tprompt_error_category_t category,
	int code,
	const char *message, ...);

/**
 * @brief Clear error information (reset to NONE)
 * @param error Error info structure to clear
 */
void tprompt_clear_error(tprompt_error_info_t *error);

/* ========================================================================
 * Internal Helper Functions - Keybinding Handlers
 * ======================================================================== */

/**
 * @brief Handle character input event
 * @param handle Prompt handle
 * @param ch Character to insert (UTF-8 sequence)
 * @param width Display width of character
 * @return 0 on success, -1 on failure
 */
int tprompt_handle_char_input(tprompt_handle_t handle, const char *ch, int width);

/**
 * @brief Handle special key event
 * @param handle Prompt handle
 * @param event Terse key event
 * @return 0 to continue editing, 1 to confirm input, -1 on error
 */
int tprompt_handle_key_event(tprompt_handle_t handle, const terse_event_t *event);


/**
 * @brief Check if buffer contains newline characters
 * @param handle Prompt handle
 * @return true if buffer contains newlines, false otherwise
 */
bool tprompt_buffer_has_newlines(tprompt_handle_t handle);

/* ========================================================================
 * Internal Helper Functions - Custom Keybindings
 * ======================================================================== */

/**
 * @brief Find keybinding action for a given event
 *
 * Searches the custom keybindings array for a matching event.
 * Returns the action if found, or TPROMPT_ACTION_NONE if not found.
 *
 * @param handle Prompt handle
 * @param event Terse event to match
 * @return Action code (TPROMPT_ACTION_*) or TPROMPT_ACTION_NONE
 */
tprompt_action_t tprompt_find_keybinding_action(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Validate keybindings array
 *
 * Checks for:
 * - NULL bindings with count > 0 (error)
 * - Duplicate bindings (warning)
 * - Unknown action values (warning)
 *
 * Warnings are recorded in error info but do not cause failure.
 *
 * @param bindings Keybindings array to validate
 * @param count Number of keybindings
 * @param error Error info structure to populate
 * @return 0 on success, -1 on critical error (NULL bindings with count > 0)
 */
int tprompt_validate_keybindings(const tprompt_keybinding_t *bindings,
	size_t count,
	tprompt_error_info_t *error);

/* ========================================================================
 * Internal Helper Functions - Phase 6 Keybinding Handlers
 * ======================================================================== */

/**
 * @brief Handle Ctrl+W - delete word before cursor
 * @param handle Prompt handle
 * @return 0 on success, -1 on error
 */
int tprompt_key_handle_ctrl_w(tprompt_handle_t handle);

/**
 * @brief Handle Ctrl+K - delete from cursor to end of logical line
 * @param handle Prompt handle
 * @return 0 on success, -1 on error
 */
int tprompt_key_handle_ctrl_k(tprompt_handle_t handle);

/**
 * @brief Handle Ctrl+U - delete from start of logical line to cursor
 * @param handle Prompt handle
 * @return 0 on success, -1 on error
 */
int tprompt_key_handle_ctrl_u(tprompt_handle_t handle);

/**
 * @brief Handle Ctrl+A - move cursor to start of logical line
 * @param handle Prompt handle
 * @return 0 on success, -1 on error
 */
int tprompt_key_handle_ctrl_a(tprompt_handle_t handle);

/**
 * @brief Handle Ctrl+E - move cursor to end of logical line
 * @param handle Prompt handle
 * @return 0 on success, -1 on error
 */
int tprompt_key_handle_ctrl_e(tprompt_handle_t handle);

/* ========================================================================
 * Internal Helper Functions - Logical Line Navigation (Phase 5)
 * ======================================================================== */

/**
 * @brief Count total number of logical lines in buffer
 * @param handle Prompt handle
 * @return Number of logical lines (minimum 1)
 */
size_t tprompt_count_logical_lines(tprompt_handle_t handle);

/**
 * @brief Get logical line number at specified byte offset
 * @param handle Prompt handle
 * @param byte_offset Byte offset in buffer
 * @return Logical line number (0-based)
 */
size_t tprompt_get_logical_line_at_offset(tprompt_handle_t handle, size_t byte_offset);

/**
 * @brief Get start and end byte offsets of a logical line
 * @param handle Prompt handle
 * @param logical_line Logical line number (0-based)
 * @param out_start Output: Start byte offset of line
 * @param out_end Output: End byte offset of line (exclusive)
 * @return 0 on success, -1 if line number is invalid
 */
int tprompt_get_logical_line_bounds(tprompt_handle_t handle, size_t logical_line,
	size_t *out_start, size_t *out_end);

/**
 * @brief Get byte length of a logical line
 * @param handle Prompt handle
 * @param logical_line Logical line number (0-based)
 * @return Byte length of line (excluding newline), or 0 if invalid
 */
size_t tprompt_get_logical_line_length(tprompt_handle_t handle, size_t logical_line);

/* ========================================================================
 * Internal Helper Functions - Status Line
 * ======================================================================== */

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

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_INTERNAL_H */
