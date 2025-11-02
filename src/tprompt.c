/**
 * @file tprompt.c
 * @brief Implementation of terse-prompt library
 * @version 0.1
 * @date 2025-11-02
 */

#include "tprompt_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

/* ========================================================================
 * Global Error Information
 * ======================================================================== */

tprompt_error_info_t tprompt_global_error = {
    .category = TPROMPT_ERROR_NONE,
    .code = 0,
    .message = {0}
};

/* ========================================================================
 * Constants
 * ======================================================================== */

#define DEFAULT_BUFFER_SIZE 256
#define DEFAULT_PROMPT "> "

/* ========================================================================
 * Error Handling - Internal Helpers
 * ======================================================================== */

void tprompt_set_error(tprompt_error_info_t *error,
                       tprompt_error_category_t category,
                       int code,
                       const char *message, ...)
{
    if (!error) {
        return;
    }

    error->category = category;
    error->code = code;

    if (message) {
        va_list args;
        va_start(args, message);
        vsnprintf(error->message, sizeof(error->message), message, args);
        va_end(args);
    } else {
        error->message[0] = '\0';
    }
}

void tprompt_clear_error(tprompt_error_info_t *error)
{
    if (!error) {
        return;
    }

    error->category = TPROMPT_ERROR_NONE;
    error->code = 0;
    error->message[0] = '\0';
}

/* ========================================================================
 * Buffer Management - Internal Helpers
 * ======================================================================== */

int tprompt_buffer_init(tprompt_buffer_t *buffer, size_t initial_size)
{
    if (!buffer) {
        return -1;
    }

    if (initial_size == 0) {
        initial_size = DEFAULT_BUFFER_SIZE;
    }

    buffer->data = malloc(initial_size);
    if (!buffer->data) {
        return -1;
    }

    buffer->data[0] = '\0';
    buffer->size = initial_size;
    buffer->length = 0;
    buffer->cursor = 0;

    return 0;
}

void tprompt_buffer_free(tprompt_buffer_t *buffer)
{
    if (!buffer) {
        return;
    }

    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
    buffer->length = 0;
    buffer->cursor = 0;
}

void tprompt_buffer_clear(tprompt_buffer_t *buffer)
{
    if (!buffer || !buffer->data) {
        return;
    }

    buffer->data[0] = '\0';
    buffer->length = 0;
    buffer->cursor = 0;
}

int tprompt_buffer_ensure_capacity(tprompt_buffer_t *buffer, size_t required_size)
{
    if (!buffer) {
        return -1;
    }

    // Need space for null terminator
    required_size++;

    if (buffer->size >= required_size) {
        return 0;
    }

    // Double until we have enough space
    size_t new_size = buffer->size;
    while (new_size < required_size) {
        new_size *= 2;
    }

    char *new_data = realloc(buffer->data, new_size);
    if (!new_data) {
        return -1;
    }

    buffer->data = new_data;
    buffer->size = new_size;

    return 0;
}

int tprompt_buffer_insert(tprompt_buffer_t *buffer, const char *text, size_t len)
{
    if (!buffer || !text || len == 0) {
        return -1;
    }

    // Ensure we have enough capacity
    if (tprompt_buffer_ensure_capacity(buffer, buffer->length + len) != 0) {
        return -1;
    }

    // Move existing text after cursor to make room
    if (buffer->cursor < buffer->length) {
        memmove(buffer->data + buffer->cursor + len,
                buffer->data + buffer->cursor,
                buffer->length - buffer->cursor);
    }

    // Insert new text
    memcpy(buffer->data + buffer->cursor, text, len);
    buffer->cursor += len;
    buffer->length += len;
    buffer->data[buffer->length] = '\0';

    return 0;
}

size_t tprompt_buffer_delete_before(tprompt_buffer_t *buffer, size_t count)
{
    // TODO: Implement with UTF-8 character counting
    return 0;
}

size_t tprompt_buffer_delete_at(tprompt_buffer_t *buffer, size_t count)
{
    // TODO: Implement with UTF-8 character counting
    return 0;
}

int tprompt_buffer_set(tprompt_buffer_t *buffer, const char *text)
{
    if (!buffer || !text) {
        return -1;
    }

    size_t len = strlen(text);
    if (tprompt_buffer_ensure_capacity(buffer, len) != 0) {
        return -1;
    }

    memcpy(buffer->data, text, len + 1);
    buffer->length = len;
    buffer->cursor = len;

    return 0;
}

/* ========================================================================
 * Cursor Movement - Internal Helpers
 * ======================================================================== */

size_t tprompt_cursor_move_left(tprompt_buffer_t *buffer, size_t count)
{
    // TODO: Implement with UTF-8 character counting
    return 0;
}

size_t tprompt_cursor_move_right(tprompt_buffer_t *buffer, size_t count)
{
    // TODO: Implement with UTF-8 character counting
    return 0;
}

void tprompt_cursor_move_to_start(tprompt_buffer_t *buffer)
{
    if (buffer) {
        buffer->cursor = 0;
    }
}

void tprompt_cursor_move_to_end(tprompt_buffer_t *buffer)
{
    if (buffer) {
        buffer->cursor = buffer->length;
    }
}

void tprompt_cursor_move_to_offset(tprompt_buffer_t *buffer, size_t offset)
{
    if (!buffer) {
        return;
    }

    if (offset > buffer->length) {
        offset = buffer->length;
    }

    buffer->cursor = offset;
}

size_t tprompt_cursor_move_word_forward(tprompt_buffer_t *buffer)
{
    // TODO: Implement word boundary detection
    return 0;
}

size_t tprompt_cursor_move_word_backward(tprompt_buffer_t *buffer)
{
    // TODO: Implement word boundary detection
    return 0;
}

/* ========================================================================
 * History Management - Internal Helpers
 * ======================================================================== */

void tprompt_history_init(tprompt_history_t *history, size_t max_size)
{
    if (!history) {
        return;
    }

    history->head = NULL;
    history->tail = NULL;
    history->count = 0;
    history->max_size = max_size;
    history->current = NULL;
}

void tprompt_history_free(tprompt_history_t *history)
{
    if (!history) {
        return;
    }

    tprompt_history_entry_t *entry = history->head;
    while (entry) {
        tprompt_history_entry_t *next = entry->next;
        free(entry->text);
        free(entry);
        entry = next;
    }

    history->head = NULL;
    history->tail = NULL;
    history->count = 0;
    history->current = NULL;
}

int tprompt_history_add_internal(tprompt_history_t *history, const char *text)
{
    // TODO: Implement history addition with deduplication
    return 0;
}

const char *tprompt_history_prev(tprompt_history_t *history)
{
    // TODO: Implement history navigation
    return NULL;
}

const char *tprompt_history_next(tprompt_history_t *history)
{
    // TODO: Implement history navigation
    return NULL;
}

void tprompt_history_reset_position(tprompt_history_t *history)
{
    if (history) {
        history->current = NULL;
    }
}

int tprompt_history_load_internal(tprompt_history_t *history, const char *file_path)
{
    // TODO: Implement history file loading
    return 0;
}

int tprompt_history_save_internal(tprompt_history_t *history, const char *file_path)
{
    // TODO: Implement history file saving
    return 0;
}

/* ========================================================================
 * UTF-8 Utilities - Internal Helpers
 * ======================================================================== */

size_t tprompt_utf8_char_length(unsigned char byte)
{
    if ((byte & 0x80) == 0x00) {
        return 1;
    } else if ((byte & 0xE0) == 0xC0) {
        return 2;
    } else if ((byte & 0xF0) == 0xE0) {
        return 3;
    } else if ((byte & 0xF8) == 0xF0) {
        return 4;
    }
    return 0; // Invalid
}

size_t tprompt_utf8_prev_char(const char *text, size_t offset)
{
    if (!text || offset == 0) {
        return 0;
    }

    // Move backward from current position
    size_t pos = offset - 1;

    // Skip backward over continuation bytes (10xxxxxx)
    while (pos > 0 && (text[pos] & 0xC0) == 0x80) {
        pos--;
    }

    return pos;
}

size_t tprompt_utf8_next_char(const char *text, size_t offset, size_t max_length)
{
    if (!text || offset >= max_length) {
        return max_length;
    }

    // Get character length at current position
    size_t char_len = tprompt_utf8_char_length((unsigned char)text[offset]);
    if (char_len == 0) {
        // Invalid UTF-8, skip one byte
        return offset + 1;
    }

    // Move forward by character length
    size_t new_offset = offset + char_len;
    if (new_offset > max_length) {
        new_offset = max_length;
    }

    return new_offset;
}

bool tprompt_utf8_validate(const char *text, size_t length)
{
    if (!text) {
        return false;
    }

    size_t i = 0;
    while (i < length) {
        unsigned char byte = (unsigned char)text[i];
        size_t char_len = tprompt_utf8_char_length(byte);

        // Invalid lead byte
        if (char_len == 0) {
            return false;
        }

        // Check if we have enough bytes remaining
        if (i + char_len > length) {
            return false;
        }

        // Validate continuation bytes
        for (size_t j = 1; j < char_len; j++) {
            unsigned char cont = (unsigned char)text[i + j];
            if ((cont & 0xC0) != 0x80) {
                return false;
            }
        }

        // Additional validation for overlong encodings and invalid ranges
        if (char_len == 2) {
            // 2-byte: 0xC2-0xDF (must be >= 0x80)
            if (byte < 0xC2) {
                return false;
            }
        } else if (char_len == 3) {
            // 3-byte: Check for overlong and surrogates
            if (byte == 0xE0 && (unsigned char)text[i + 1] < 0xA0) {
                return false; // Overlong
            }
            if (byte == 0xED && (unsigned char)text[i + 1] >= 0xA0) {
                return false; // UTF-16 surrogate
            }
        } else if (char_len == 4) {
            // 4-byte: Check for overlong and out of range
            if (byte == 0xF0 && (unsigned char)text[i + 1] < 0x90) {
                return false; // Overlong
            }
            if (byte > 0xF4) {
                return false; // Beyond U+10FFFF
            }
            if (byte == 0xF4 && (unsigned char)text[i + 1] > 0x8F) {
                return false; // Beyond U+10FFFF
            }
        }

        i += char_len;
    }

    return true;
}

size_t tprompt_utf8_char_count(const char *text, size_t length)
{
    if (!text) {
        return 0;
    }

    size_t count = 0;
    size_t i = 0;

    while (i < length) {
        size_t char_len = tprompt_utf8_char_length((unsigned char)text[i]);
        if (char_len == 0) {
            // Invalid byte, skip it
            i++;
        } else {
            i += char_len;
            count++;
        }
    }

    return count;
}

/* ========================================================================
 * Completion - Internal Helpers
 * ======================================================================== */

void tprompt_completion_init(tprompt_completion_state_t *state)
{
    if (!state) {
        return;
    }

    state->active = false;
    state->candidates = NULL;
    state->candidate_count = 0;
    state->selected_index = 0;
    state->trigger_offset = 0;
    state->trigger_char = '\0';
}

void tprompt_completion_free(tprompt_completion_state_t *state)
{
    if (!state) {
        return;
    }

    if (state->candidates) {
        for (size_t i = 0; i < state->candidate_count; i++) {
            free(state->candidates[i]);
        }
        free(state->candidates);
    }

    tprompt_completion_init(state);
}

int tprompt_completion_activate(tprompt_handle_t handle, char trigger_char, size_t trigger_offset)
{
    // TODO: Implement completion activation
    return -1;
}

void tprompt_completion_deactivate(tprompt_handle_t handle)
{
    // TODO: Implement completion deactivation
}

int tprompt_completion_update(tprompt_handle_t handle)
{
    // TODO: Implement completion update
    return -1;
}

void tprompt_completion_select_next(tprompt_completion_state_t *state)
{
    // TODO: Implement completion candidate selection
}

void tprompt_completion_select_prev(tprompt_completion_state_t *state)
{
    // TODO: Implement completion candidate selection
}

int tprompt_completion_confirm(tprompt_handle_t handle)
{
    // TODO: Implement completion confirmation
    return -1;
}

/* ========================================================================
 * Display and Rendering - Internal Helpers
 * ======================================================================== */

/**
 * @brief Calculate cursor column position including prompt width
 * @param handle Prompt handle
 * @return Physical column position (0-based)
 */
static size_t tprompt_calculate_cursor_col(tprompt_handle_t handle)
{
    if (!handle) {
        return 0;
    }

    // Start with prompt length (assuming single-width characters)
    size_t prompt_len = handle->prompt ? strlen(handle->prompt) : 0;
    size_t col = prompt_len;

    // Add character widths from buffer start to cursor position
    // For now, assume 1 column per character (will be enhanced with terse width info)
    size_t char_count = tprompt_utf8_char_count(handle->buffer.data, handle->buffer.cursor);
    col += char_count;

    return col;
}

void tprompt_display_calculate_layout(tprompt_handle_t handle)
{
    if (!handle) {
        return;
    }

    // Get terminal dimensions from terse
    terse_size_t size = terse_get_size(handle->terse);
    if (size.known) {
        handle->display.terminal_width = (size_t)size.cols;
        handle->display.terminal_height = (size_t)size.rows;
    }

    // Calculate cursor column position
    size_t cursor_col = tprompt_calculate_cursor_col(handle);

    // For single-line mode (current implementation), physical line is always 0
    handle->display.physical_line = 0;
    handle->display.physical_column = cursor_col;
    handle->display.total_physical_lines = 1;

    // Future: Multi-line wrapping logic will be added here
}

int tprompt_display_render(tprompt_handle_t handle)
{
    if (!handle) {
        return -1;
    }

    // Clear current line first
    if (terse_clear_line(handle->terse, TERSE_CLEAR_ALL) < 0) {
        tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                         "Failed to clear line");
        return -1;
    }

    // Move cursor to start of line
    if (terse_move_to(handle->terse, -1, 0) < 0) {
        tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                         "Failed to move cursor to line start");
        return -1;
    }

    // Write prompt
    if (handle->prompt) {
        if (terse_write_text(handle->terse, handle->prompt) < 0) {
            tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                             "Failed to write prompt");
            return -1;
        }
    }

    // Write buffer contents
    if (handle->buffer.length > 0) {
        if (terse_write_text(handle->terse, handle->buffer.data) < 0) {
            tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                             "Failed to write buffer");
            return -1;
        }
    }

    // Update cursor position
    return tprompt_display_update_cursor(handle);
}

int tprompt_display_update_cursor(tprompt_handle_t handle)
{
    if (!handle) {
        return -1;
    }

    // Calculate cursor position
    tprompt_display_calculate_layout(handle);

    // Move cursor to calculated position
    if (terse_move_to(handle->terse, -1, (int)handle->display.physical_column) < 0) {
        tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                         "Failed to update cursor position");
        return -1;
    }

    // Flush to ensure cursor is visible
    if (terse_flush(handle->terse) < 0) {
        tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                         "Failed to flush output");
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
    if (terse_clear_line(handle->terse, TERSE_CLEAR_ALL) < 0) {
        tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                         "Failed to clear display");
        return -1;
    }

    // Move cursor to start of line
    if (terse_move_to(handle->terse, -1, 0) < 0) {
        tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                         "Failed to move cursor");
        return -1;
    }

    // Flush
    if (terse_flush(handle->terse) < 0) {
        tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                         "Failed to flush output");
        return -1;
    }

    return 0;
}

/* ========================================================================
 * Key Event Handlers - Internal Helpers
 * ======================================================================== */

int tprompt_handle_char_input(tprompt_handle_t handle, const char *ch, int width)
{
    // TODO: Implement character input handling
    return -1;
}

int tprompt_handle_key_event(tprompt_handle_t handle, const terse_event_t *event)
{
    // TODO: Implement key event handling
    return -1;
}

bool tprompt_is_completion_trigger(tprompt_handle_t handle, char ch)
{
    // TODO: Implement completion trigger detection
    return false;
}

/* ========================================================================
 * Public API - Simple Wrapper
 * ======================================================================== */

char *tprompt(const char *prompt_text)
{
    // Create options with defaults
    tprompt_options_t opts = {
        .prompt = prompt_text ? prompt_text : DEFAULT_PROMPT,
        .history_file = NULL,
        .max_input_size = 1024 * 1024,
        .max_history_size = 100,
        .completion_callback = NULL,
        .completion_user_data = NULL,
        .completion_prefixes = NULL,
        .terse_handle = NULL,
        .flags = TPROMPT_FLAG_MULTILINE
    };

    // Open temporary handle
    tprompt_handle_t handle = tprompt_open(&opts);
    if (!handle) {
        return NULL;
    }

    // Read one line
    char *result = tprompt_readline(handle, NULL);

    // Close handle
    tprompt_close(handle);

    return result;
}

/* ========================================================================
 * Public API - Framework: Initialization and Cleanup
 * ======================================================================== */

tprompt_handle_t tprompt_open(const tprompt_options_t *options)
{
    tprompt_clear_error(&tprompt_global_error);

    // Allocate handle
    tprompt_handle_t handle = calloc(1, sizeof(struct tprompt_handle));
    if (!handle) {
        tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
                         "Failed to allocate handle");
        return NULL;
    }

    // Use default options if none provided
    tprompt_options_t default_opts = {
        .prompt = DEFAULT_PROMPT,
        .history_file = NULL,
        .max_input_size = 1024 * 1024,
        .max_history_size = 100,
        .completion_callback = NULL,
        .completion_user_data = NULL,
        .completion_prefixes = NULL,
        .terse_handle = NULL,
        .flags = TPROMPT_FLAG_MULTILINE
    };

    if (!options) {
        options = &default_opts;
    }

    // Copy options
    handle->options = *options;
    if (handle->options.prompt == NULL) {
        handle->options.prompt = DEFAULT_PROMPT;
    }

    // Initialize terse handle
    if (options->terse_handle) {
        handle->terse = options->terse_handle;
        handle->owns_terse = false;
    } else {
        // Try TERSE_PROFILE_AUTO first, fall back to TERSE_P0 for non-TTY environments
        handle->terse = terse_open(TERSE_PROFILE_AUTO, NULL);
        if (!handle->terse) {
            // Fall back to P0 (basic profile) which works in non-TTY environments
            handle->terse = terse_open(TERSE_P0, NULL);
        }
        if (!handle->terse) {
            tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_TERSE, errno,
                             "Failed to create terse handle");
            free(handle);
            return NULL;
        }
        handle->owns_terse = true;
    }

    // Initialize buffer
    if (tprompt_buffer_init(&handle->buffer, DEFAULT_BUFFER_SIZE) != 0) {
        tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
                         "Failed to initialize buffer");
        if (handle->owns_terse) {
            terse_close(handle->terse);
        }
        free(handle);
        return NULL;
    }

    // Initialize history
    tprompt_history_init(&handle->history, options->max_history_size);

    // Load history from file if specified
    if (options->history_file && !(options->flags & TPROMPT_FLAG_DISABLE_HISTORY)) {
        handle->history_file_path = strdup(options->history_file);
        if (!handle->history_file_path) {
            tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
                             "Failed to allocate history file path");
            tprompt_history_free(&handle->history);
            tprompt_buffer_free(&handle->buffer);
            if (handle->owns_terse) {
                terse_close(handle->terse);
            }
            free(handle);
            return NULL;
        }
        tprompt_history_load_internal(&handle->history, handle->history_file_path);
        // Ignore errors on load
    }

    // Initialize completion
    tprompt_completion_init(&handle->completion_state);
    handle->completion_callback = options->completion_callback;
    handle->completion_user_data = options->completion_user_data;
    if (options->completion_prefixes) {
        handle->completion_prefixes = strdup(options->completion_prefixes);
        if (!handle->completion_prefixes) {
            tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
                             "Failed to allocate completion prefixes");
            free(handle->history_file_path);
            tprompt_history_free(&handle->history);
            tprompt_buffer_free(&handle->buffer);
            if (handle->owns_terse) {
                terse_close(handle->terse);
            }
            free(handle);
            return NULL;
        }
    }

    // Initialize display state
    handle->display.physical_line = 0;
    handle->display.physical_column = 0;
    handle->display.total_physical_lines = 0;
    handle->display.terminal_width = 80;  // Will be updated from terse
    handle->display.terminal_height = 24;

    // Store prompt
    handle->prompt = strdup(options->prompt);
    if (!handle->prompt) {
        tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
                         "Failed to allocate prompt string");
        free(handle->completion_prefixes);
        tprompt_completion_free(&handle->completion_state);
        free(handle->history_file_path);
        tprompt_history_free(&handle->history);
        tprompt_buffer_free(&handle->buffer);
        if (handle->owns_terse) {
            terse_close(handle->terse);
        }
        free(handle);
        return NULL;
    }

    // Clear error
    tprompt_clear_error(&handle->last_error);

    return handle;
}

void tprompt_close(tprompt_handle_t handle)
{
    if (!handle) {
        return;
    }

    // Save history if needed
    if (handle->history_file_path && !(handle->options.flags & TPROMPT_FLAG_NO_AUTO_SAVE)) {
        tprompt_history_save_internal(&handle->history, handle->history_file_path);
        // Ignore errors on save
    }

    // Free resources
    free(handle->history_file_path);
    free(handle->prompt);
    free(handle->completion_prefixes);

    tprompt_completion_free(&handle->completion_state);
    tprompt_history_free(&handle->history);
    tprompt_buffer_free(&handle->buffer);

    // Close terse handle if we own it
    if (handle->owns_terse && handle->terse) {
        terse_close(handle->terse);
    }

    free(handle);
}

/* ========================================================================
 * Public API - Framework: Editing Session
 * ======================================================================== */

char *tprompt_readline(tprompt_handle_t handle, const char *prompt_override)
{
    if (!handle) {
        errno = EINVAL;
        return NULL;
    }

    tprompt_clear_error(&handle->last_error);

    // TODO: Implement readline logic
    // For now, return NULL to indicate not implemented
    tprompt_set_error(&handle->last_error, TPROMPT_ERROR_NONE, 0,
                     "Not yet implemented");

    return NULL;
}

/* ========================================================================
 * Public API - Framework: History Management
 * ======================================================================== */

int tprompt_history_add(tprompt_handle_t handle, const char *entry)
{
    if (!handle || !entry) {
        return -1;
    }

    if (handle->options.flags & TPROMPT_FLAG_DISABLE_HISTORY) {
        return 0;
    }

    return tprompt_history_add_internal(&handle->history, entry);
}

int tprompt_history_load(tprompt_handle_t handle, const char *file_path)
{
    if (!handle || !file_path) {
        return -1;
    }

    return tprompt_history_load_internal(&handle->history, file_path);
}

int tprompt_history_save(tprompt_handle_t handle, const char *file_path)
{
    if (!handle || !file_path) {
        return -1;
    }

    return tprompt_history_save_internal(&handle->history, file_path);
}

void tprompt_history_clear(tprompt_handle_t handle)
{
    if (!handle) {
        return;
    }

    tprompt_history_free(&handle->history);
    tprompt_history_init(&handle->history, handle->options.max_history_size);
}

void tprompt_history_set_max_size(tprompt_handle_t handle, size_t max_size)
{
    if (!handle) {
        return;
    }

    handle->history.max_size = max_size;

    // TODO: Trim history if current count exceeds new max
}

/* ========================================================================
 * Public API - Framework: Completion
 * ======================================================================== */

void tprompt_set_completion_callback(tprompt_handle_t handle,
                                     tprompt_completion_fn callback,
                                     void *user_data)
{
    if (!handle) {
        return;
    }

    handle->completion_callback = callback;
    handle->completion_user_data = user_data;
}

int tprompt_set_completion_prefixes(tprompt_handle_t handle, const char *prefixes)
{
    if (!handle) {
        return -1;
    }

    free(handle->completion_prefixes);
    handle->completion_prefixes = NULL;

    if (prefixes && prefixes[0] != '\0') {
        handle->completion_prefixes = strdup(prefixes);
        if (!handle->completion_prefixes) {
            tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
                             "Failed to allocate completion prefixes");
            return -1;
        }
    }

    return 0;
}

void tprompt_free_completion_result(tprompt_completion_result_t *result)
{
    if (!result) {
        return;
    }

    if (result->candidates) {
        for (size_t i = 0; i < result->count; i++) {
            free(result->candidates[i]);
        }
        free(result->candidates);
        result->candidates = NULL;
    }

    result->count = 0;
}

/* ========================================================================
 * Public API - Framework: Error Handling
 * ======================================================================== */

tprompt_error_info_t tprompt_get_last_error(tprompt_handle_t handle)
{
    if (handle) {
        return handle->last_error;
    }
    return tprompt_global_error;
}
