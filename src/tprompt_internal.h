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
#include <stdbool.h>
#include <stddef.h>
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
    char *data;           /**< UTF-8 text buffer */
    size_t size;          /**< Current buffer size (allocated capacity) */
    size_t length;        /**< Current text length in bytes (excluding NUL) */
    size_t cursor;        /**< Cursor position in bytes (byte offset) */
} tprompt_buffer_t;

/**
 * @brief History entry node (linked list)
 */
typedef struct tprompt_history_entry {
    char *text;                           /**< History entry text */
    struct tprompt_history_entry *next;   /**< Next entry (older) */
    struct tprompt_history_entry *prev;   /**< Previous entry (newer) */
} tprompt_history_entry_t;

/**
 * @brief History management structure
 */
typedef struct tprompt_history {
    tprompt_history_entry_t *head;   /**< Most recent entry */
    tprompt_history_entry_t *tail;   /**< Oldest entry */
    size_t count;                    /**< Number of entries */
    size_t max_size;                 /**< Maximum number of entries (0 = unlimited) */
    tprompt_history_entry_t *current; /**< Current position during navigation */
} tprompt_history_t;

/**
 * @brief Completion state
 */
typedef struct tprompt_completion_state {
    bool active;                          /**< Whether completion is active */
    char **candidates;                    /**< Current candidate list (NULL-terminated) */
    size_t candidate_count;               /**< Number of candidates */
    size_t selected_index;                /**< Currently selected candidate index */
    size_t trigger_offset;                /**< Byte offset of completion trigger character */
    char trigger_char;                    /**< Character that triggered completion */
} tprompt_completion_state_t;

/**
 * @brief Display state for multi-line rendering
 */
typedef struct tprompt_display_state {
    size_t physical_line;       /**< Current physical line number */
    size_t physical_column;     /**< Current physical column number */
    size_t total_physical_lines; /**< Total number of physical lines */
    size_t terminal_width;      /**< Terminal width in columns */
    size_t terminal_height;     /**< Terminal height in rows */
    int start_row;              /**< Starting row for rendering (0-based absolute terminal row) */
    bool start_row_known;       /**< Whether start_row has been initialized */
} tprompt_display_state_t;

/**
 * @brief Input state for tracking key sequences
 */
typedef struct tprompt_input_state {
    int last_key_type;          /**< Last key event type (TERSE_EVENT_*) */
    size_t last_cursor_pos;     /**< Cursor position at last key press */
    size_t goal_column;         /**< Goal column for vertical navigation (0 = not set) */
    bool has_goal_column;       /**< Whether goal column is currently active */
} tprompt_input_state_t;

/**
 * @brief Main handle structure (opaque pointer implementation)
 */
struct tprompt_handle {
    /* Core dependencies */
    terse_handle_t terse;        /**< terse handle for terminal control */
    bool owns_terse;             /**< Whether this handle owns the terse handle */

    /* Terminal raw mode */
#if defined(__unix__) || defined(__APPLE__)
    struct termios original_termios;  /**< Original terminal settings */
    bool raw_mode_active;             /**< Whether raw mode is currently active */
#endif

    /* Input buffer */
    tprompt_buffer_t buffer;     /**< Dynamic input buffer */

    /* History */
    tprompt_history_t history;   /**< History management */
    char *history_file_path;     /**< History file path (NULL if none) */

    /* Completion */
    tprompt_completion_fn completion_callback;  /**< Completion callback function */
    void *completion_user_data;                 /**< User data for completion callback */
    char *completion_prefixes;                  /**< Completion trigger prefix characters */
    tprompt_completion_state_t completion_state; /**< Current completion state */

    /* Display */
    tprompt_display_state_t display;  /**< Display state for rendering */
    char *prompt;                     /**< Current prompt string */

    /* Input state */
    tprompt_input_state_t input_state; /**< Input state for key sequence tracking */

    /* Keybindings */
    tprompt_keybinding_t *keybindings;  /**< Custom keybindings array (dynamically allocated) */
    size_t keybinding_count;            /**< Number of custom keybindings */

    /* Configuration */
    tprompt_options_t options;   /**< Copy of options */

    /* Error tracking */
    tprompt_error_info_t last_error; /**< Last error information */
};

/**
 * @brief Global error information (for tprompt_open() failures)
 */
extern tprompt_error_info_t tprompt_global_error;

/* ========================================================================
 * Internal Helper Functions - Buffer Management
 * ======================================================================== */

/**
 * @brief Initialize a buffer
 * @param buffer Buffer to initialize
 * @param initial_size Initial buffer size (0 uses default)
 * @return 0 on success, -1 on failure
 */
int tprompt_buffer_init(tprompt_buffer_t *buffer, size_t initial_size);

/**
 * @brief Free buffer resources
 * @param buffer Buffer to free
 */
void tprompt_buffer_free(tprompt_buffer_t *buffer);

/**
 * @brief Clear buffer contents (reset to empty)
 * @param buffer Buffer to clear
 */
void tprompt_buffer_clear(tprompt_buffer_t *buffer);

/**
 * @brief Ensure buffer has at least the specified capacity
 * @param buffer Buffer to expand
 * @param required_size Required size in bytes
 * @return 0 on success, -1 on allocation failure
 */
int tprompt_buffer_ensure_capacity(tprompt_buffer_t *buffer, size_t required_size);

/**
 * @brief Insert text at cursor position
 * @param buffer Buffer to modify
 * @param text Text to insert (UTF-8)
 * @param len Length of text in bytes
 * @return 0 on success, -1 on failure
 */
int tprompt_buffer_insert(tprompt_buffer_t *buffer, const char *text, size_t len);

/**
 * @brief Delete characters before cursor (backspace)
 * @param buffer Buffer to modify
 * @param count Number of characters to delete (not bytes)
 * @return Number of bytes deleted
 */
size_t tprompt_buffer_delete_before(tprompt_buffer_t *buffer, size_t count);

/**
 * @brief Delete characters at cursor position (delete key)
 * @param buffer Buffer to modify
 * @param count Number of characters to delete (not bytes)
 * @return Number of bytes deleted
 */
size_t tprompt_buffer_delete_at(tprompt_buffer_t *buffer, size_t count);

/**
 * @brief Set buffer contents (replaces all text)
 * @param buffer Buffer to modify
 * @param text New text content (UTF-8)
 * @return 0 on success, -1 on failure
 */
int tprompt_buffer_set(tprompt_buffer_t *buffer, const char *text);

/* ========================================================================
 * Internal Helper Functions - Cursor Movement
 * ======================================================================== */

/**
 * @brief Move cursor left by n characters
 * @param buffer Buffer to operate on
 * @param count Number of characters to move
 * @return Number of bytes actually moved
 */
size_t tprompt_cursor_move_left(tprompt_buffer_t *buffer, size_t count);

/**
 * @brief Move cursor right by n characters
 * @param buffer Buffer to operate on
 * @param count Number of characters to move
 * @return Number of bytes actually moved
 */
size_t tprompt_cursor_move_right(tprompt_buffer_t *buffer, size_t count);

/**
 * @brief Move cursor to start of buffer
 * @param buffer Buffer to operate on
 */
void tprompt_cursor_move_to_start(tprompt_buffer_t *buffer);

/**
 * @brief Move cursor to end of buffer
 * @param buffer Buffer to operate on
 */
void tprompt_cursor_move_to_end(tprompt_buffer_t *buffer);

/**
 * @brief Move cursor to specific byte offset
 * @param buffer Buffer to operate on
 * @param offset Byte offset (clamped to valid range)
 */
void tprompt_cursor_move_to_offset(tprompt_buffer_t *buffer, size_t offset);

/**
 * @brief Move cursor forward by one word
 * @param buffer Buffer to operate on
 * @return Number of bytes moved
 */
size_t tprompt_cursor_move_word_forward(tprompt_buffer_t *buffer);

/**
 * @brief Move cursor backward by one word
 * @param buffer Buffer to operate on
 * @return Number of bytes moved
 */
size_t tprompt_cursor_move_word_backward(tprompt_buffer_t *buffer);

/**
 * @brief Move cursor to start of current logical line
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_cursor_move_to_logical_line_start(tprompt_handle_t handle);

/**
 * @brief Move cursor to end of current logical line
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_cursor_move_to_logical_line_end(tprompt_handle_t handle);

/**
 * @brief Move cursor up by one physical line
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_cursor_move_up(tprompt_handle_t handle);

/**
 * @brief Move cursor down by one physical line
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_cursor_move_down(tprompt_handle_t handle);

/* ========================================================================
 * Internal Helper Functions - History Management
 * ======================================================================== */

/**
 * @brief Initialize history structure
 * @param history History structure to initialize
 * @param max_size Maximum number of entries (0 = unlimited)
 */
void tprompt_history_init(tprompt_history_t *history, size_t max_size);

/**
 * @brief Free all history resources
 * @param history History structure to free
 */
void tprompt_history_free(tprompt_history_t *history);

/**
 * @brief Add entry to history (internal implementation)
 * @param history History structure
 * @param text Entry text
 * @return 0 on success, -1 on failure
 */
int tprompt_history_add_internal(tprompt_history_t *history, const char *text);

/**
 * @brief Get previous history entry (navigate backward)
 * @param history History structure
 * @return Previous entry text, or NULL if at beginning
 */
const char *tprompt_history_prev(tprompt_history_t *history);

/**
 * @brief Get next history entry (navigate forward)
 * @param history History structure
 * @return Next entry text, or NULL if at end
 */
const char *tprompt_history_next(tprompt_history_t *history);

/**
 * @brief Reset history navigation position
 * @param history History structure
 */
void tprompt_history_reset_position(tprompt_history_t *history);

/**
 * @brief Load history from file (internal implementation)
 * @param history History structure
 * @param file_path File path
 * @return 0 on success, -1 on failure
 */
int tprompt_history_load_internal(tprompt_history_t *history, const char *file_path);

/**
 * @brief Save history to file (internal implementation)
 * @param history History structure
 * @param file_path File path
 * @return 0 on success, -1 on failure
 */
int tprompt_history_save_internal(tprompt_history_t *history, const char *file_path);

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

/* ========================================================================
 * Internal Helper Functions - Completion
 * ======================================================================== */

/**
 * @brief Initialize completion state
 * @param state Completion state to initialize
 */
void tprompt_completion_init(tprompt_completion_state_t *state);

/**
 * @brief Free completion state resources
 * @param state Completion state to free
 */
void tprompt_completion_free(tprompt_completion_state_t *state);

/**
 * @brief Activate completion and fetch candidates
 * @param handle Prompt handle
 * @param trigger_char Character that triggered completion
 * @param trigger_offset Byte offset of trigger character
 * @return 0 on success, -1 on failure
 */
int tprompt_completion_activate(tprompt_handle_t handle, char trigger_char, size_t trigger_offset);

/**
 * @brief Deactivate completion and clean up
 * @param handle Prompt handle
 */
void tprompt_completion_deactivate(tprompt_handle_t handle);

/**
 * @brief Update completion candidates based on current input
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_completion_update(tprompt_handle_t handle);

/**
 * @brief Select next completion candidate
 * @param state Completion state
 */
void tprompt_completion_select_next(tprompt_completion_state_t *state);

/**
 * @brief Select previous completion candidate
 * @param state Completion state
 */
void tprompt_completion_select_prev(tprompt_completion_state_t *state);

/**
 * @brief Confirm selected completion candidate
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_completion_confirm(tprompt_handle_t handle);

/* ========================================================================
 * Internal Helper Functions - Display and Rendering
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
 * @brief Check if character is a completion trigger
 * @param handle Prompt handle
 * @param ch Character to check
 * @return true if trigger, false otherwise
 */
bool tprompt_is_completion_trigger(tprompt_handle_t handle, char ch);

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
int tprompt_find_keybinding_action(tprompt_handle_t handle, const terse_event_t *event);

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

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_INTERNAL_H */
