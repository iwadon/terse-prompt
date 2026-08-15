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

#if defined(_WIN32)
#include <windows.h>
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
 * @brief Logical dimensions of the virtual screen rectangle
 *
 * The actual cell storage and frame diffing now live in terse's buffered
 * rendering (TERSE_RENDER_BUFFERED). terse-prompt keeps only the logical
 * rows/cols here so the layout/wrapping code can decide when text must wrap
 * or the rectangle must grow; the rendering itself goes straight to terse.
 */
typedef struct tprompt_screen_buffer {
	size_t rows; /**< Number of rows in the virtual rectangle */
	size_t cols; /**< Number of columns in the virtual rectangle */
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
	bool is_dirty;			 /**< Whether any region needs redrawing */
	size_t dirty_start_byte; /**< Start byte offset of dirty region */
	size_t dirty_end_byte;	 /**< End byte offset of dirty region (exclusive) */
	bool force_full_redraw;	 /**< Force full redraw on next render */

	/* Virtual screen rectangle. The cell storage and frame diff now live in
	 * terse (TERSE_RENDER_BUFFERED); current_buffer holds only the logical
	 * rows/cols used by layout/wrapping. */
	tprompt_screen_buffer_t current_buffer; /**< Logical rectangle dimensions */
	bool buffer_based_rendering_active;		/**< Whether buffer-based rendering is initialized */
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
#if defined(_WIN32)
	DWORD original_input_mode;	/**< Original console input mode */
	DWORD original_output_mode; /**< Original console output mode */
	bool raw_mode_active;		/**< Whether raw mode is currently active */
#endif

	/* Input buffer */
	tprompt_buffer_t buffer; /**< Dynamic input buffer */

	/* History */
	tprompt_history_t history; /**< History management */
	char *history_file_path;   /**< History file path (NULL if none) */

	/* Completion */
	tprompt_completion_fn completion_callback;		 /**< Completion callback function (legacy) */
	tprompt_completion_ex_fn completion_ex_callback; /**< Extended completion callback (with descriptions) */
	void *completion_user_data;						 /**< User data for completion callback */
	char *completion_prefixes;						 /**< Completion trigger prefix characters */
	tprompt_completion_state_t completion_state;	 /**< Current completion state */

	/* Display */
	tprompt_display_state_t display; /**< Display state for rendering */
	char *prompt;					 /**< Current prompt string */
	char *continuation_prompt;		 /**< Continuation line prompt string */

	/* Input state */
	tprompt_input_state_t input_state; /**< Input state for key sequence tracking */

	/* Keybindings */
	tprompt_keybinding_t *keybindings; /**< Custom keybindings array (dynamically allocated) */
	size_t keybinding_count;		   /**< Number of custom keybindings */

	/* Validation */
	bool pending_confirmation;	/**< Whether input confirmation is pending validation */
	bool force_confirmation;	/**< Whether confirmation is forced by custom keybinding (bypass default multiline behavior) */
	char *validation_error_msg; /**< Validation error message to display (NULL if none) */

	/* EOF tracking */
	bool eof_signaled; /**< Whether EOF (Ctrl+D on empty buffer) was signaled */

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

/**
 * @brief Encode Unicode scalar value to UTF-8 bytes
 * @param scalar Unicode scalar value (0x0 to 0x10FFFF, excluding surrogates)
 * @param out_buf Output buffer for UTF-8 bytes (must be at least 4 bytes)
 * @param max_buf_size Size of output buffer
 * @return Number of bytes written (1-4), or -1 on error
 */
int tprompt_utf8_encode(unsigned int scalar, char *out_buf, size_t max_buf_size);

/* ========================================================================
 * Internal Helper Functions - String Utilities
 * ======================================================================== */

/**
 * @brief Duplicate a NUL-terminated string (ISO C11-only strdup replacement)
 *
 * Unlike POSIX strdup(), this function accepts a NULL argument and returns
 * NULL instead of invoking undefined behavior. Implemented with only
 * malloc()/memcpy() so callers do not need any POSIX feature test macro.
 *
 * @param s String to duplicate, or NULL
 * @return Newly allocated copy of s, NULL if s is NULL, or NULL on allocation failure
 */
char *tprompt_strdup(const char *s);

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
 * @brief Calculate display width of a prompt string (UTF-8 aware)
 *
 * Calculates the visual width of a prompt string, accounting for wide characters.
 *
 * @param handle Prompt handle (for UTF-8 processing)
 * @param prompt Prompt string (UTF-8 encoded)
 * @return Display width in columns
 */
size_t tprompt_get_prompt_width(tprompt_handle_t handle, const char *prompt);

/**
 * @brief Get the formatted continuation line prompt
 *
 * Returns the continuation prompt, padded or truncated to match the initial prompt width.
 * If continuation_prompt is shorter, it's right-padded with spaces.
 * If continuation_prompt is longer, it's truncated with a warning.
 *
 * @param handle Prompt handle
 * @return Formatted continuation prompt (statically allocated, do not free)
 */
const char *tprompt_get_continuation_prompt(tprompt_handle_t handle);

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
 * @brief Write a UTF-8 string into the virtual rectangle via terse
 * @param handle Prompt handle (for terse handle and character width)
 * @param buffer Virtual rectangle dimensions (for the column bound)
 * @param row Starting row in rectangle-local coordinates (0-based)
 * @param col Starting column in rectangle-local coordinates (0-based)
 * @param str UTF-8 string to write
 * @return Number of columns advanced, or -1 on error
 *
 * Positions the terse cursor at (row, col) in buffered-mode local coordinates
 * and writes the string. terse projects local coordinates onto the terminal
 * (absolute = origin + local) and performs the frame diff at flush time.
 */
int tprompt_screen_buffer_write_string(tprompt_handle_t handle,
	tprompt_screen_buffer_t *buffer,
	size_t row, size_t col,
	const char *str);

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
 * @brief Handle character input events (TERSE_EVENT_CHAR)
 *
 * Special handling for Ctrl+D (EOF/delete-char) and action system routing.
 *
 * @param handle Prompt handle
 * @param event Character event
 * @param should_break Output parameter: set to true if EOF signaled
 * @return 0 on success, -1 on error
 */
int tprompt_handle_char_event(tprompt_handle_t handle, const terse_event_t *event, bool *should_break);

/**
 * @brief Handle pending input confirmation (validation and mode-dependent behavior)
 * @param handle Prompt handle
 * @param should_break Output parameter: set to true if input should be confirmed
 * @return 0 on success, -1 on error
 */
int tprompt_handle_pending_confirmation(tprompt_handle_t handle, bool *should_break);

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
 * Internal Helper Functions - Action System
 * ======================================================================== */

/**
 * @brief Resolve keyboard event to action
 *
 * Checks custom keybindings first, then default keybindings, then falls back
 * to character insertion for regular character input.
 *
 * @param handle Prompt handle
 * @param event Keyboard event from terse
 * @return Action to execute (TPROMPT_ACTION_NONE if no action)
 */
tprompt_action_t tprompt_resolve_action(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Execute an action
 *
 * Dispatches to the appropriate action handler function based on the action type.
 * Context-dependent logic (e.g., multiline mode, validation) is handled within
 * the action handlers.
 *
 * @param handle Prompt handle
 * @param action Action to execute
 * @param event Original keyboard event (may be needed by some handlers)
 * @return 1 to confirm input, 0 to continue editing, -1 on error
 */
int tprompt_execute_action(tprompt_handle_t handle, tprompt_action_t action, const terse_event_t *event);

/* ========================================================================
 * Individual Action Handlers
 * ======================================================================== */

/**
 * @brief Handle MOVE_LEFT action
 *
 * @param handle Prompt handle
 * @param event Original event (unused)
 * @return 0 to continue editing, -1 on error
 */
int tprompt_action_move_left(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Handle MOVE_RIGHT action
 *
 * @param handle Prompt handle
 * @param event Original event (unused)
 * @return 0 to continue editing, -1 on error
 */
int tprompt_action_move_right(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Handle DELETE_BACKWARD action (backspace)
 *
 * @param handle Prompt handle
 * @param event Original event (unused)
 * @return 0 to continue editing, -1 on error
 */
int tprompt_action_delete_backward(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Handle DELETE_FORWARD action (delete)
 *
 * @param handle Prompt handle
 * @param event Original event (unused)
 * @return 0 to continue editing, -1 on error
 */
int tprompt_action_delete_forward(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Handle MOVE_HOME action
 *
 * @param handle Prompt handle
 * @param event Original event (unused)
 * @return 0 to continue editing, -1 on error
 */
int tprompt_action_move_home(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Handle MOVE_END action
 *
 * @param handle Prompt handle
 * @param event Original event (unused)
 * @return 0 to continue editing, -1 on error
 */
int tprompt_action_move_end(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Handle HISTORY_PREV action (up arrow)
 *
 * @param handle Prompt handle
 * @param event Original event (unused)
 * @return 0 to continue editing, -1 on error
 */
int tprompt_action_history_prev(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Handle HISTORY_NEXT action (down arrow)
 *
 * @param handle Prompt handle
 * @param event Original event (unused)
 * @return 0 to continue editing, -1 on error
 */
int tprompt_action_history_next(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Handle MOVE_UP action (up arrow in multiline)
 *
 * @param handle Prompt handle
 * @param event Original event (unused)
 * @return 0 to continue editing, -1 on error
 */
int tprompt_action_move_up(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Handle MOVE_DOWN action (down arrow in multiline)
 *
 * @param handle Prompt handle
 * @param event Original event (unused)
 * @return 0 to continue editing, -1 on error
 */
int tprompt_action_move_down(tprompt_handle_t handle, const terse_event_t *event);

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

/**
 * @brief Estimate how many status lines will be rendered
 *
 * Used to calculate required display space before actual rendering.
 * Follows same priority rules as tprompt_render_status_line().
 *
 * @param handle Prompt handle
 * @return Estimated number of status line rows (0 if no status line)
 */
int tprompt_estimate_status_lines(tprompt_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_INTERNAL_H */
