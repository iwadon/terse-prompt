/**
 * @file tprompt_buffer.h
 * @brief Buffer and cursor operation functions for terse-prompt
 *
 * Provides functions for buffer management (initialization, allocation,
 * insertion, deletion) and cursor movement operations.
 */

#ifndef TPROMPT_BUFFER_H
#define TPROMPT_BUFFER_H

#include "tprompt.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations from tprompt_internal.h */
typedef struct tprompt_buffer tprompt_buffer_t;

/* ========================================================================
 * Buffer Management Functions
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
 * @brief Insert text with max_input_size enforcement
 * @param handle Prompt handle
 * @param text Text to insert (UTF-8)
 * @param len Length of text in bytes
 * @return 0 on success, -1 on failure
 */
int tprompt_buffer_insert_limited(tprompt_handle_t handle, const char *text, size_t len);

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
 * Cursor Movement Functions
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

/**
 * @brief Move cursor to start of current physical line
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_cursor_move_to_physical_line_start(tprompt_handle_t handle);

/**
 * @brief Move cursor to end of current physical line
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_cursor_move_to_physical_line_end(tprompt_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_BUFFER_H */
