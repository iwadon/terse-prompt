/**
 * @file tprompt.c
 * @brief Implementation of terse-prompt library
 * @version 0.1
 * @date 2025-11-02
 */

#define _POSIX_C_SOURCE 200809L

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
 * Forward Declarations for Static Helper Functions
 * ======================================================================== */

static size_t tprompt_calculate_cursor_col(tprompt_handle_t handle);

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

int tprompt_cursor_move_to_physical_line_start(tprompt_handle_t handle)
{
    if (!handle) {
        return -1;
    }

    // Calculate current layout
    tprompt_display_calculate_layout(handle);

    size_t terminal_width = handle->display.terminal_width;
    if (terminal_width == 0) {
        terminal_width = 80;
    }

    // Calculate absolute column at cursor (including prompt)
    size_t cursor_col = tprompt_calculate_cursor_col(handle);

    // Calculate which physical line we're on
    size_t current_physical_line = cursor_col / terminal_width;

    // Calculate target column: start of current physical line
    size_t target_col = current_physical_line * terminal_width;

    // Find byte offset corresponding to target column
    // We need to iterate through the buffer to find the position
    size_t prompt_len = handle->prompt ? strlen(handle->prompt) : 0;
    size_t current_col = prompt_len;
    size_t byte_offset = 0;

    // If target column is before/at prompt, move to buffer start
    if (target_col <= prompt_len) {
        handle->buffer.cursor = 0;
        return 0;
    }

    // Find the byte offset where we reach target column
    while (byte_offset < handle->buffer.length) {
        if (current_col >= target_col) {
            break;
        }

        size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[byte_offset]);
        if (char_len == 0) {
            char_len = 1; // Invalid UTF-8, skip one byte
        }

        byte_offset += char_len;
        current_col++;
    }

    handle->buffer.cursor = byte_offset;
    return 0;
}

int tprompt_cursor_move_to_physical_line_end(tprompt_handle_t handle)
{
    if (!handle) {
        return -1;
    }

    // Calculate current layout
    tprompt_display_calculate_layout(handle);

    size_t terminal_width = handle->display.terminal_width;
    if (terminal_width == 0) {
        terminal_width = 80;
    }

    // Calculate absolute column at cursor (including prompt)
    size_t cursor_col = tprompt_calculate_cursor_col(handle);

    // Calculate which physical line we're on
    size_t current_physical_line = cursor_col / terminal_width;

    // Calculate target column: end of current physical line (one before next line start)
    size_t target_col = (current_physical_line + 1) * terminal_width - 1;

    // Find byte offset corresponding to target column
    size_t prompt_len = handle->prompt ? strlen(handle->prompt) : 0;
    size_t current_col = prompt_len;
    size_t byte_offset = 0;
    size_t last_valid_offset = 0;

    // Find the byte offset where we reach or exceed target column
    while (byte_offset < handle->buffer.length) {
        if (current_col > target_col) {
            // We've gone past target, use previous position
            handle->buffer.cursor = last_valid_offset;
            return 0;
        }

        if (current_col == target_col) {
            // Exact match
            handle->buffer.cursor = byte_offset;
            return 0;
        }

        last_valid_offset = byte_offset;

        size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[byte_offset]);
        if (char_len == 0) {
            char_len = 1; // Invalid UTF-8, skip one byte
        }

        byte_offset += char_len;
        current_col++;
    }

    // If we reached end of buffer before target column, use end of buffer
    handle->buffer.cursor = handle->buffer.length;
    return 0;
}

int tprompt_cursor_move_up(tprompt_handle_t handle)
{
    if (!handle) {
        return -1;
    }

    // Calculate current layout
    tprompt_display_calculate_layout(handle);

    size_t terminal_width = handle->display.terminal_width;
    if (terminal_width == 0) {
        terminal_width = 80;
    }

    // Calculate current absolute column (including prompt)
    size_t current_col = tprompt_calculate_cursor_col(handle);

    // Calculate which physical line we're currently on
    size_t current_physical_line = current_col / terminal_width;

    // If at top line, can't move up
    if (current_physical_line == 0) {
        return 0;
    }

    // Set goal column if not already set
    if (!handle->input_state.has_goal_column) {
        handle->input_state.goal_column = current_col % terminal_width;
        handle->input_state.has_goal_column = true;
    }

    // Calculate target column: goal column on line above
    size_t target_physical_line = current_physical_line - 1;
    size_t target_col = target_physical_line * terminal_width + handle->input_state.goal_column;

    // Find byte offset corresponding to target column
    size_t prompt_len = handle->prompt ? strlen(handle->prompt) : 0;
    size_t col = prompt_len;
    size_t byte_offset = 0;

    // If target column is before/at prompt, move to buffer start
    if (target_col <= prompt_len) {
        handle->buffer.cursor = 0;
        return 0;
    }

    // Find the byte offset where we reach or exceed target column
    while (byte_offset < handle->buffer.length && col < target_col) {
        size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[byte_offset]);
        if (char_len == 0) {
            char_len = 1; // Invalid UTF-8, skip one byte
        }

        byte_offset += char_len;
        col++;
    }

    // Clamp to buffer length
    if (byte_offset > handle->buffer.length) {
        byte_offset = handle->buffer.length;
    }

    handle->buffer.cursor = byte_offset;
    return 0;
}

int tprompt_cursor_move_down(tprompt_handle_t handle)
{
    if (!handle) {
        return -1;
    }

    // Calculate current layout
    tprompt_display_calculate_layout(handle);

    size_t terminal_width = handle->display.terminal_width;
    if (terminal_width == 0) {
        terminal_width = 80;
    }

    // Calculate current absolute column (including prompt)
    size_t current_col = tprompt_calculate_cursor_col(handle);

    // Calculate which physical line we're currently on
    size_t current_physical_line = current_col / terminal_width;
    size_t total_lines = handle->display.total_physical_lines;

    // If at bottom line, can't move down
    if (current_physical_line >= total_lines - 1) {
        return 0;
    }

    // Set goal column if not already set
    if (!handle->input_state.has_goal_column) {
        handle->input_state.goal_column = current_col % terminal_width;
        handle->input_state.has_goal_column = true;
    }

    // Calculate target column: goal column on line below
    size_t target_physical_line = current_physical_line + 1;
    size_t target_col = target_physical_line * terminal_width + handle->input_state.goal_column;

    // Find byte offset corresponding to target column
    size_t prompt_len = handle->prompt ? strlen(handle->prompt) : 0;
    size_t col = prompt_len;
    size_t byte_offset = 0;

    // Find the byte offset where we reach or exceed target column
    while (byte_offset < handle->buffer.length && col < target_col) {
        size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[byte_offset]);
        if (char_len == 0) {
            char_len = 1; // Invalid UTF-8, skip one byte
        }

        byte_offset += char_len;
        col++;
    }

    // Clamp to buffer length
    if (byte_offset > handle->buffer.length) {
        byte_offset = handle->buffer.length;
    }

    handle->buffer.cursor = byte_offset;
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
    if (!history || !text) {
        return -1;
    }

    // Skip empty entries
    if (text[0] == '\0') {
        return 0;
    }

    // Duplicate check: don't add if it's the same as the most recent entry
    if (history->head && history->head->text) {
        if (strcmp(history->head->text, text) == 0) {
            return 0;  // Skip duplicate
        }
    }

    // Allocate new entry
    tprompt_history_entry_t *entry = malloc(sizeof(tprompt_history_entry_t));
    if (!entry) {
        return -1;
    }

    // Duplicate text
    entry->text = strdup(text);
    if (!entry->text) {
        free(entry);
        return -1;
    }

    entry->next = NULL;
    entry->prev = NULL;

    // Insert at head (most recent position)
    if (!history->head) {
        // Empty history
        history->head = entry;
        history->tail = entry;
    } else {
        // Add to head
        entry->next = history->head;
        history->head->prev = entry;
        history->head = entry;
    }

    history->count++;

    // LRU eviction: remove from tail if we exceed max_size
    while (history->max_size > 0 && history->count > history->max_size) {
        if (!history->tail) {
            break;  // Safety check
        }

        tprompt_history_entry_t *old_tail = history->tail;
        history->tail = old_tail->prev;

        if (history->tail) {
            history->tail->next = NULL;
        } else {
            // List became empty
            history->head = NULL;
        }

        free(old_tail->text);
        free(old_tail);
        history->count--;
    }

    // Reset navigation position to head (most recent)
    history->current = NULL;

    return 0;
}

const char *tprompt_history_prev(tprompt_history_t *history)
{
    if (!history) {
        return NULL;
    }

    // If current is NULL, start from head (most recent)
    if (!history->current) {
        history->current = history->head;
        return history->current ? history->current->text : NULL;
    }

    // Try to move to next (older) entry
    // Note: 'next' points toward older entries, 'prev' points toward newer entries
    if (history->current->next) {
        history->current = history->current->next;
        return history->current->text;
    }

    // Already at the oldest entry, return NULL but keep current position
    return NULL;
}

const char *tprompt_history_next(tprompt_history_t *history)
{
    if (!history || !history->current) {
        return NULL;
    }

    // Move to prev (newer) entry
    // Note: 'next' points toward older entries, 'prev' points toward newer entries
    history->current = history->current->prev;

    // Return current entry's text (or NULL if at end - returning to current input)
    return history->current ? history->current->text : NULL;
}

void tprompt_history_reset_position(tprompt_history_t *history)
{
    if (history) {
        history->current = NULL;
    }
}

int tprompt_history_load_internal(tprompt_history_t *history, const char *file_path)
{
    if (!history || !file_path) {
        return -1;
    }

    // Open file for reading
    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        // File not found is not an error (first time use)
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    // Read line by line
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;

    while ((line_len = getline(&line, &line_cap, fp)) != -1) {
        // Remove trailing newline if present
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0';
            line_len--;
        }

        // Remove trailing carriage return if present (CRLF handling)
        if (line_len > 0 && line[line_len - 1] == '\r') {
            line[line_len - 1] = '\0';
            line_len--;
        }

        // Add to history (skip empty lines handled by add_internal)
        tprompt_history_add_internal(history, line);
    }

    free(line);
    fclose(fp);

    return 0;
}

int tprompt_history_save_internal(tprompt_history_t *history, const char *file_path)
{
    if (!history || !file_path) {
        return -1;
    }

    // Open file for writing (truncate)
    FILE *fp = fopen(file_path, "w");
    if (!fp) {
        return -1;
    }

    // Write entries from tail to head (oldest to most recent)
    // This ensures that when we load and insert at head, the order is preserved
    tprompt_history_entry_t *entry = history->tail;
    while (entry) {
        // Write entry with newline
        if (fprintf(fp, "%s\n", entry->text) < 0) {
            fclose(fp);
            return -1;
        }
        entry = entry->prev;
    }

    if (fclose(fp) != 0) {
        return -1;
    }

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
    if (!handle) {
        return -1;
    }

    // Set completion state
    handle->completion_state.active = true;
    handle->completion_state.trigger_char = trigger_char;
    handle->completion_state.trigger_offset = trigger_offset;

    // Fetch initial candidates
    return tprompt_completion_update(handle);
}

void tprompt_completion_deactivate(tprompt_handle_t handle)
{
    if (!handle) {
        return;
    }

    // Free candidates
    if (handle->completion_state.candidates) {
        for (size_t i = 0; i < handle->completion_state.candidate_count; i++) {
            free(handle->completion_state.candidates[i]);
        }
        free(handle->completion_state.candidates);
    }

    // Reset state
    tprompt_completion_init(&handle->completion_state);
}

int tprompt_completion_update(tprompt_handle_t handle)
{
    if (!handle || !handle->completion_state.active) {
        return -1;
    }

    // Free previous candidates
    if (handle->completion_state.candidates) {
        for (size_t i = 0; i < handle->completion_state.candidate_count; i++) {
            free(handle->completion_state.candidates[i]);
        }
        free(handle->completion_state.candidates);
        handle->completion_state.candidates = NULL;
        handle->completion_state.candidate_count = 0;
        handle->completion_state.selected_index = 0;
    }

    // Call the completion callback
    if (!handle->completion_callback) {
        return -1;
    }

    // Prepare parameters for callback
    const char *text = handle->buffer.data;
    size_t cursor_pos = handle->buffer.cursor;
    char prefix_str[2] = {handle->completion_state.trigger_char, '\0'};

    // Invoke callback
    tprompt_completion_result_t result = handle->completion_callback(
        text,
        cursor_pos,
        prefix_str,
        handle->completion_user_data
    );

    // Store the results
    handle->completion_state.candidates = result.candidates;
    handle->completion_state.candidate_count = result.count;
    handle->completion_state.selected_index = 0;

    // If no candidates, deactivate completion
    if (result.count == 0) {
        tprompt_completion_deactivate(handle);
        return 0;
    }

    return 0;
}

void tprompt_completion_select_next(tprompt_completion_state_t *state)
{
    if (!state || !state->active || state->candidate_count == 0) {
        return;
    }

    // Move to next candidate (wrap around at end)
    state->selected_index = (state->selected_index + 1) % state->candidate_count;
}

void tprompt_completion_select_prev(tprompt_completion_state_t *state)
{
    if (!state || !state->active || state->candidate_count == 0) {
        return;
    }

    // Move to previous candidate (wrap around at beginning)
    if (state->selected_index == 0) {
        state->selected_index = state->candidate_count - 1;
    } else {
        state->selected_index--;
    }
}

int tprompt_completion_confirm(tprompt_handle_t handle)
{
    if (!handle || !handle->completion_state.active) {
        return -1;
    }

    // Check if we have a valid selected candidate
    if (handle->completion_state.candidate_count == 0 ||
        handle->completion_state.selected_index >= handle->completion_state.candidate_count) {
        return -1;
    }

    // Get the selected candidate text
    const char *selected = handle->completion_state.candidates[handle->completion_state.selected_index];
    if (!selected) {
        return -1;
    }

    // Delete text from trigger position to current cursor position
    size_t trigger_pos = handle->completion_state.trigger_offset;
    size_t current_pos = handle->buffer.cursor;

    // Safety check: ensure trigger_pos is valid
    if (trigger_pos > current_pos || trigger_pos > handle->buffer.length) {
        return -1;
    }

    // Move cursor to trigger position
    handle->buffer.cursor = trigger_pos;

    // Delete characters from trigger_pos to current_pos (byte-wise)
    size_t delete_count = current_pos - trigger_pos;
    if (delete_count > 0) {
        // Shift remaining text left
        memmove(handle->buffer.data + trigger_pos,
                handle->buffer.data + current_pos,
                handle->buffer.length - current_pos);

        handle->buffer.length -= delete_count;
        handle->buffer.data[handle->buffer.length] = '\0';
    }

    // Insert the selected candidate at trigger position
    size_t selected_len = strlen(selected);
    if (tprompt_buffer_insert(&handle->buffer, selected, selected_len) != 0) {
        return -1;
    }

    return 0;
}

/* ========================================================================
 * Display and Rendering - Internal Helpers
 * ======================================================================== */

/**
 * @brief Calculate absolute column position from byte offset
 * @param handle Prompt handle
 * @param byte_offset Byte offset in buffer
 * @param include_prompt Whether to include prompt width
 * @return Absolute column position (0-based)
 */
static size_t tprompt_calculate_column_at_offset(tprompt_handle_t handle, size_t byte_offset, bool include_prompt)
{
    if (!handle) {
        return 0;
    }

    // Start with prompt length if requested
    size_t col = 0;
    if (include_prompt && handle->prompt) {
        col = strlen(handle->prompt);
    }

    // Add character widths from buffer start to given offset
    size_t char_count = tprompt_utf8_char_count(handle->buffer.data, byte_offset);
    col += char_count;

    return col;
}

/**
 * @brief Calculate cursor column position including prompt width
 * @param handle Prompt handle
 * @return Physical column position (0-based)
 */
static size_t tprompt_calculate_cursor_col(tprompt_handle_t handle)
{
    return tprompt_calculate_column_at_offset(handle, handle->buffer.cursor, true);
}

/**
 * @brief Calculate physical line and column from absolute column position
 * @param absolute_col Absolute column position (including prompt)
 * @param terminal_width Terminal width in columns
 * @param out_physical_line Output: physical line number (0-based)
 * @param out_physical_col Output: physical column within line (0-based)
 */
static void tprompt_calculate_physical_position(size_t absolute_col, size_t terminal_width,
                                                 size_t *out_physical_line, size_t *out_physical_col)
{
    if (terminal_width == 0) {
        terminal_width = 80; // Fallback default
    }

    *out_physical_line = absolute_col / terminal_width;
    *out_physical_col = absolute_col % terminal_width;
}

/**
 * @brief Calculate total physical lines needed for buffer content
 * @param handle Prompt handle
 * @return Total number of physical lines
 */
static size_t tprompt_calculate_total_physical_lines(tprompt_handle_t handle)
{
    if (!handle) {
        return 1;
    }

    size_t terminal_width = handle->display.terminal_width;
    if (terminal_width == 0) {
        terminal_width = 80; // Fallback default
    }

    // Calculate absolute column at end of buffer
    size_t end_col = tprompt_calculate_column_at_offset(handle, handle->buffer.length, true);

    // Calculate how many physical lines are needed
    // If end_col is 0, we need 1 line
    // If end_col > 0 and divides evenly, we're at the end of a line (don't add extra)
    // If end_col > 0 and has remainder, we need the quotient + 1
    if (end_col == 0) {
        return 1;
    }
    return ((end_col - 1) / terminal_width) + 1;
}

void tprompt_display_calculate_layout(tprompt_handle_t handle)
{
    if (!handle) {
        return;
    }

    // Get terminal dimensions from terse (only update if known)
    terse_size_t size = terse_get_size(handle->terse);
    if (size.known && size.cols > 0 && size.rows > 0) {
        handle->display.terminal_width = (size_t)size.cols;
        handle->display.terminal_height = (size_t)size.rows;
    }

    // Use fallback if terminal size still unknown
    if (handle->display.terminal_width == 0) {
        handle->display.terminal_width = 80;
    }
    if (handle->display.terminal_height == 0) {
        handle->display.terminal_height = 24;
    }

    // Calculate absolute column position at cursor (including prompt)
    size_t cursor_col = tprompt_calculate_cursor_col(handle);

    // Calculate physical line and column from absolute position
    tprompt_calculate_physical_position(cursor_col, handle->display.terminal_width,
                                        &handle->display.physical_line,
                                        &handle->display.physical_column);

    // Calculate total number of physical lines needed
    handle->display.total_physical_lines = tprompt_calculate_total_physical_lines(handle);
}

int tprompt_display_render(tprompt_handle_t handle)
{
    if (!handle) {
        return -1;
    }

    // Calculate layout first to know how many physical lines we need
    tprompt_display_calculate_layout(handle);

    // Get current cursor position to know where we started
    terse_cursor_position_t start_pos = terse_get_cursor_position(handle->terse);
    handle->display.start_row = start_pos.known ? start_pos.row : 0;

    // Clear all physical lines used by the input
    for (size_t i = 0; i < handle->display.total_physical_lines; i++) {
        if (terse_move_to(handle->terse, handle->display.start_row + (int)i, 0) < 0) {
            tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                             "Failed to move cursor");
            return -1;
        }
        if (terse_clear_line(handle->terse, TERSE_CLEAR_ALL) < 0) {
            tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                             "Failed to clear line");
            return -1;
        }
    }

    // Move back to start position
    if (terse_move_to(handle->terse, handle->display.start_row, 0) < 0) {
        tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                         "Failed to move cursor to start");
        return -1;
    }

    // Render text with wrapping
    size_t current_col = 0;
    size_t terminal_width = handle->display.terminal_width;

    // Write prompt on first line
    if (handle->prompt) {
        size_t prompt_len = strlen(handle->prompt);
        if (terse_write_text(handle->terse, handle->prompt) < 0) {
            tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                             "Failed to write prompt");
            return -1;
        }
        current_col += prompt_len;
    }

    // Write buffer contents character by character, wrapping at terminal width
    size_t i = 0;
    while (i < handle->buffer.length) {
        // Check if we need to wrap to next line
        if (current_col >= terminal_width) {
            // Move to next physical line
            if (terse_write_text(handle->terse, "\n") < 0) {
                tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                                 "Failed to write newline");
                return -1;
            }
            current_col = 0;
        }

        // Get current character length
        size_t char_len = tprompt_utf8_char_length((unsigned char)handle->buffer.data[i]);
        if (char_len == 0) {
            // Invalid byte, skip it
            i++;
            continue;
        }

        // Write the character
        char temp[5] = {0};
        if (char_len <= 4) {
            memcpy(temp, &handle->buffer.data[i], char_len);
            temp[char_len] = '\0';

            if (terse_write_text(handle->terse, temp) < 0) {
                tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                                 "Failed to write character");
                return -1;
            }

            // Assume each character takes 1 column (could be enhanced with width tracking)
            current_col++;
        }

        i += char_len;
    }

    // Update cursor position to where the cursor should be
    if (tprompt_display_update_cursor(handle) != 0) {
        return -1;
    }

    // Render completion list if active
    if (handle->completion_state.active) {
        if (tprompt_display_render_completion(handle) != 0) {
            return -1;
        }
    }

    return 0;
}

int tprompt_display_update_cursor(tprompt_handle_t handle)
{
    if (!handle) {
        return -1;
    }

    // Calculate cursor position
    tprompt_display_calculate_layout(handle);

    // Calculate target row (start_row + physical_line offset)
    int target_row = handle->display.start_row + (int)handle->display.physical_line;

    // Move cursor to calculated position (row and column)
    if (terse_move_to(handle->terse, target_row, (int)handle->display.physical_column) < 0) {
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

int tprompt_display_render_completion(tprompt_handle_t handle)
{
    if (!handle || !handle->completion_state.active) {
        return 0;  // Not an error, just nothing to render
    }

    size_t candidate_count = handle->completion_state.candidate_count;
    if (candidate_count == 0) {
        return 0;  // No candidates to display
    }

    char **candidates = handle->completion_state.candidates;
    size_t selected_index = handle->completion_state.selected_index;

    // Get current cursor position (save it for later restoration)
    terse_cursor_position_t saved_pos = terse_get_cursor_position(handle->terse);

    // Move cursor to next line after input
    if (terse_move_by(handle->terse, 1, 0) < 0) {
        tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                         "Failed to move cursor for completion list");
        return -1;
    }

    // Render each candidate
    for (size_t i = 0; i < candidate_count; i++) {
        // Clear the line first
        if (terse_clear_line(handle->terse, TERSE_CLEAR_ALL) < 0) {
            return -1;
        }

        // Move to start of line
        if (terse_move_to(handle->terse, -1, 0) < 0) {
            return -1;
        }

        // Render selection indicator and candidate text
        char prefix[8] = "  ";
        if (i == selected_index) {
            snprintf(prefix, sizeof(prefix), "> ");
        }

        // Write prefix
        if (terse_write_text(handle->terse, prefix) < 0) {
            tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                             "Failed to write completion prefix");
            return -1;
        }

        // Write candidate text
        if (terse_write_text(handle->terse, candidates[i]) < 0) {
            tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                             "Failed to write completion candidate");
            return -1;
        }

        // Move to next line if not the last candidate
        if (i < candidate_count - 1) {
            if (terse_move_by(handle->terse, 1, 0) < 0) {
                return -1;
            }
        }
    }

    // Restore cursor to input position
    if (saved_pos.known) {
        if (terse_move_to(handle->terse, saved_pos.row, saved_pos.col) < 0) {
            return -1;
        }
    }

    // Flush output
    if (terse_flush(handle->terse) < 0) {
        tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, errno,
                         "Failed to flush completion display");
        return -1;
    }

    return 0;
}

/* ========================================================================
 * Key Event Handlers - Internal Helpers
 * ======================================================================== */

int tprompt_handle_char_input(tprompt_handle_t handle, const char *ch, int width)
{
    if (!handle || !ch) {
        return -1;
    }

    // Check if this is a completion trigger character
    if (ch[0] != '\0' && tprompt_is_completion_trigger(handle, ch[0])) {
        // Activate completion if not already active
        if (!handle->completion_state.active) {
            // Insert the character first
            size_t ch_len = strlen(ch);
            if (tprompt_buffer_insert(&handle->buffer, ch, ch_len) != 0) {
                return -1;
            }

            // Activate completion at the current cursor position
            if (tprompt_completion_activate(handle, ch[0], handle->buffer.cursor - ch_len) != 0) {
                return -1;
            }

            return 0;
        }
    }

    // Normal character insertion
    size_t ch_len = strlen(ch);
    if (tprompt_buffer_insert(&handle->buffer, ch, ch_len) != 0) {
        return -1;
    }

    // If completion is active, update candidates with new input
    if (handle->completion_state.active) {
        if (tprompt_completion_update(handle) != 0) {
            return -1;
        }
    }

    return 0;
}

int tprompt_handle_key_event(tprompt_handle_t handle, const terse_event_t *event)
{
    if (!handle || !event) {
        return -1;
    }

    // If completion is active, handle UP/DOWN for candidate navigation
    if (handle->completion_state.active && handle->completion_state.candidate_count > 0) {
        switch (event->type) {
            case TERSE_EVENT_ARROW_UP:
                // Navigate to previous candidate
                tprompt_completion_select_prev(&handle->completion_state);
                return 0;  // Continue editing (don't return input)

            case TERSE_EVENT_ARROW_DOWN:
                // Navigate to next candidate
                tprompt_completion_select_next(&handle->completion_state);
                return 0;  // Continue editing

            case TERSE_EVENT_TAB:
                // Confirm selected completion
                if (tprompt_completion_confirm(handle) == 0) {
                    tprompt_completion_deactivate(handle);
                }
                return 0;  // Continue editing

            default:
                break;
        }
    }

    // Handle ESC to cancel completion (check regardless of candidate count)
    if (handle->completion_state.active && event->type == TERSE_EVENT_CHAR) {
        if (event->data.ch.scalar == 27) {  // ESC
            tprompt_completion_deactivate(handle);
            return 0;
        }
    }

    // If completion is not active, handle UP/DOWN for either line navigation or history
    if (!handle->completion_state.active) {
        // Decide between multi-line navigation and history navigation
        // Use line navigation if: buffer has newlines OR multi-line flag is set
        bool use_line_navigation = tprompt_buffer_has_newlines(handle) ||
                                   (handle->options.flags & TPROMPT_FLAG_MULTILINE);

        switch (event->type) {
            case TERSE_EVENT_ARROW_UP:
                if (use_line_navigation) {
                    // Navigate up one physical line
                    tprompt_cursor_move_up(handle);
                } else {
                    // Navigate to previous history entry
                    const char *entry = tprompt_history_prev(&handle->history);
                    if (entry) {
                        tprompt_buffer_set(&handle->buffer, entry);
                    }
                }
                return 0;  // Continue editing

            case TERSE_EVENT_ARROW_DOWN:
                if (use_line_navigation) {
                    // Navigate down one physical line
                    tprompt_cursor_move_down(handle);
                } else {
                    // Navigate to next history entry
                    const char *entry = tprompt_history_next(&handle->history);
                    if (entry) {
                        tprompt_buffer_set(&handle->buffer, entry);
                    } else {
                        // At the end of history, clear buffer (return to current input)
                        tprompt_buffer_clear(&handle->buffer);
                    }
                }
                return 0;  // Continue editing

            default:
                break;
        }
    }

    // Handle Home key - staged movement
    if (event->type == TERSE_EVENT_HOME) {
        // Check if this is a consecutive Home press
        bool is_consecutive = (handle->input_state.last_key_type == TERSE_EVENT_HOME &&
                              handle->input_state.last_cursor_pos == handle->buffer.cursor);

        if (is_consecutive || handle->buffer.cursor == 0) {
            // Second press OR already at start: move to logical line start
            tprompt_cursor_move_to_start(&handle->buffer);
        } else {
            // First press: move to physical line start
            tprompt_cursor_move_to_physical_line_start(handle);
        }

        // Update input state for next key press
        handle->input_state.last_key_type = event->type;
        handle->input_state.last_cursor_pos = handle->buffer.cursor;
        return 0;  // Continue editing
    }

    // Handle End key - staged movement
    if (event->type == TERSE_EVENT_END) {
        // Check if this is a consecutive End press
        bool is_consecutive = (handle->input_state.last_key_type == TERSE_EVENT_END &&
                              handle->input_state.last_cursor_pos == handle->buffer.cursor);

        if (is_consecutive || handle->buffer.cursor == handle->buffer.length) {
            // Second press OR already at end: move to logical line end
            tprompt_cursor_move_to_end(&handle->buffer);
        } else {
            // First press: move to physical line end
            tprompt_cursor_move_to_physical_line_end(handle);
        }

        // Update input state for next key press
        handle->input_state.last_key_type = event->type;
        handle->input_state.last_cursor_pos = handle->buffer.cursor;
        return 0;  // Continue editing
    }

    // Handle ENTER - confirm input
    if (event->type == TERSE_EVENT_ENTER) {
        return 1;
    }

    // Handle Ctrl+D as EOF
    if (event->type == TERSE_EVENT_CHAR &&
        event->data.ch.scalar == 4 &&  // Ctrl+D
        (event->data.ch.mods & TERSE_MOD_CTRL)) {
        return 1;
    }

    // For any other key, clear the last key tracking and goal column
    handle->input_state.last_key_type = event->type;
    handle->input_state.last_cursor_pos = handle->buffer.cursor;

    // Clear goal column on non-vertical movements
    handle->input_state.has_goal_column = false;

    return 0;  // Continue editing
}

bool tprompt_is_completion_trigger(tprompt_handle_t handle, char ch)
{
    if (!handle || !handle->completion_prefixes || !handle->completion_callback) {
        return false;
    }

    // Check if the character is in the completion_prefixes string
    return strchr(handle->completion_prefixes, ch) != NULL;
}

bool tprompt_buffer_has_newlines(tprompt_handle_t handle)
{
    if (!handle || !handle->buffer.data) {
        return false;
    }

    // Check if buffer contains any newline characters
    return memchr(handle->buffer.data, '\n', handle->buffer.length) != NULL;
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
    handle->display.start_row = 0;

    // Initialize input state
    handle->input_state.last_key_type = 0;
    handle->input_state.last_cursor_pos = 0;
    handle->input_state.goal_column = 0;
    handle->input_state.has_goal_column = false;

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
