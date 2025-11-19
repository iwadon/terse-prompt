/**
 * @file tprompt.c
 * @brief Implementation of terse-prompt library
 * @version 0.1
 * @date 2025-11-02
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_internal.h"
#include "tprompt_display.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h>
#endif

/* ========================================================================
 * Global Error Information
 * ======================================================================== */

tprompt_error_info_t tprompt_global_error = {
	.category = TPROMPT_ERROR_NONE,
	.code = 0,
	.message = { 0 }
};

/* ========================================================================
 * Constants
 * ======================================================================== */

#define DEFAULT_BUFFER_SIZE 256
#define DEFAULT_PROMPT "> "
#define DEFAULT_CONTINUATION_PROMPT "| "

/* ========================================================================
 * Continuation Prompt Helpers
 * ======================================================================== */

/**
 * @brief Calculate display width of a prompt string (UTF-8 aware)
 */
size_t tprompt_get_prompt_width(tprompt_handle_t handle, const char *prompt)
{
	if (!handle || !prompt) {
		return 0;
	}

	size_t width = 0;
	size_t len = strlen(prompt);
	size_t i = 0;

	while (i < len) {
		unsigned char byte = (unsigned char)prompt[i];
		size_t char_len = tprompt_utf8_char_length(byte);

		if (char_len == 0 || i + char_len > len) {
			// Invalid UTF-8 or truncated sequence, treat as single-byte
			width++;
			i++;
			continue;
		}

		// Decode UTF-8 character
		unsigned int scalar = tprompt_utf8_decode((const unsigned char *)&prompt[i], char_len);
		int char_width = tprompt_get_char_width(scalar);

		// Add character width (0 for control/combining, 1 for narrow, 2 for wide)
		width += (char_width > 0) ? char_width : 1;
		i += char_len;
	}

	return width;
}

/**
 * @brief Get the formatted continuation line prompt
 *
 * Returns the continuation prompt, padded or truncated to match the initial prompt width.
 * Thread-unsafe: uses static buffer.
 */
const char *tprompt_get_continuation_prompt(tprompt_handle_t handle)
{
	static char formatted_prompt[512];

	if (!handle || !handle->prompt) {
		return "";
	}

	// Use default continuation prompt if not set
	const char *cont_prompt = handle->continuation_prompt ? handle->continuation_prompt : DEFAULT_CONTINUATION_PROMPT;

	// Calculate widths
	size_t initial_width = tprompt_get_prompt_width(handle, handle->prompt);
	size_t cont_width = tprompt_get_prompt_width(handle, cont_prompt);

	if (cont_width == initial_width) {
		// Perfect match, return as-is
		return cont_prompt;
	} else if (cont_width < initial_width) {
		// Right-pad with spaces
		size_t padding = initial_width - cont_width;
		size_t cont_len = strlen(cont_prompt);

		if (padding + cont_len >= sizeof(formatted_prompt)) {
			// Truncate if too long
			padding = sizeof(formatted_prompt) - cont_len - 1;
		}

		// Build padded prompt: spaces + continuation_prompt
		memset(formatted_prompt, ' ', padding);
		memcpy(formatted_prompt + padding, cont_prompt, cont_len);
		formatted_prompt[padding + cont_len] = '\0';

		return formatted_prompt;
	} else {
		// Truncate (cont_width > initial_width)
		// Emit warning once (use static flag)
		static bool warning_emitted = false;
		if (!warning_emitted) {
			fprintf(stderr, "tprompt warning: continuation_prompt is longer than initial prompt, truncating\n");
			warning_emitted = true;
		}

		// Truncate to initial_width by copying bytes until we reach the width limit
		size_t byte_count = 0;
		size_t current_width = 0;
		size_t cont_len = strlen(cont_prompt);

		while (byte_count < cont_len && current_width < initial_width) {
			unsigned char byte = (unsigned char)cont_prompt[byte_count];
			size_t char_len = tprompt_utf8_char_length(byte);

			if (char_len == 0 || byte_count + char_len > cont_len) {
				// Invalid UTF-8, stop here
				break;
			}

			unsigned int scalar = tprompt_utf8_decode((const unsigned char *)&cont_prompt[byte_count], char_len);
			int char_width = tprompt_get_char_width(scalar);
			int actual_width = (char_width > 0) ? char_width : 1;

			// Check if adding this character would exceed width
			if (current_width + actual_width > initial_width) {
				break;
			}

			current_width += actual_width;
			byte_count += char_len;
		}

		// Copy truncated prompt
		if (byte_count >= sizeof(formatted_prompt)) {
			byte_count = sizeof(formatted_prompt) - 1;
		}
		memcpy(formatted_prompt, cont_prompt, byte_count);
		formatted_prompt[byte_count] = '\0';

		return formatted_prompt;
	}
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
size_t tprompt_calculate_cursor_col(tprompt_handle_t handle)
{
	return tprompt_calculate_column_at_offset(handle, handle->buffer.cursor, true);
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
			size_t insert_pos = handle->buffer.cursor;
			if (tprompt_buffer_insert(&handle->buffer, ch, ch_len) != 0) {
				return -1;
			}

			// Mark the inserted region as dirty
			tprompt_display_mark_dirty_range(handle, insert_pos, handle->buffer.cursor);

			// Activate completion at the current cursor position
			if (tprompt_completion_activate(handle, ch[0], handle->buffer.cursor - ch_len) != 0) {
				return -1;
			}

			return 0;
		}
	}

	// Normal character insertion
	size_t ch_len = strlen(ch);
	size_t insert_pos = handle->buffer.cursor;
	if (tprompt_buffer_insert(&handle->buffer, ch, ch_len) != 0) {
		return -1;
	}

	// Mark the inserted region as dirty
	tprompt_display_mark_dirty_range(handle, insert_pos, handle->buffer.cursor);

	// If completion is active, update candidates with new input
	if (handle->completion_state.active) {
		if (tprompt_completion_update(handle) != 0) {
			return -1;
		}
	}

	return 0;
}

/**
 * @brief Handle keyboard events during editing
 *
 * Main event handler that processes all keyboard input. This is called
 * by tprompt_readline() for each key event.
 *
 * Key behavior priorities:
 * 1. Completion navigation (if active): UP/DOWN navigate candidates, TAB confirms, ESC cancels
 * 2. Multi-line navigation vs. history: UP/DOWN behavior depends on whether buffer has newlines
 * 3. Home/End staged movement: First press moves to logical line boundary, second press to buffer boundary
 * 4. Enter key mode: Single-line submits, multi-line inserts newline (Ctrl/Alt+Enter always submits)
 *
 * @param handle Prompt handle
 * @param event Terse event to process
 * @return 1 to submit input, 0 to continue editing, -1 on error
 */
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
			return 0; // Continue editing (don't return input)

		case TERSE_EVENT_ARROW_DOWN:
			// Navigate to next candidate
			tprompt_completion_select_next(&handle->completion_state);
			return 0; // Continue editing

		case TERSE_EVENT_TAB:
			// Confirm selected completion
			if (tprompt_completion_confirm(handle) == 0) {
				tprompt_completion_deactivate(handle);
			}
			return 0; // Continue editing

		default:
			break;
		}
	}

	// Handle ESC to cancel completion (check regardless of candidate count)
	if (handle->completion_state.active && event->type == TERSE_EVENT_CHAR) {
		if (event->data.ch.scalar == 27) { // ESC
			tprompt_completion_deactivate(handle);
			return 0;
		}
	}

	// If completion is not active, handle UP/DOWN for either line navigation or history
	if (!handle->completion_state.active) {
		// Check if buffer has multiple lines
		bool has_multiple_lines = tprompt_buffer_has_newlines(handle);

		switch (event->type) {
		case TERSE_EVENT_ARROW_UP:
			if (has_multiple_lines) {
				// In multi-line mode: check if at first line
				size_t current_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
				if (current_line == 0) {
					// At first line, navigate to previous history entry
					// Save current input if not already in history navigation
					if (!handle->history.current) {
						if (handle->history.saved_input) {
							free(handle->history.saved_input);
						}
						handle->history.saved_input = strdup(handle->buffer.data);
					}
					const char *entry = tprompt_history_prev(&handle->history);
					if (entry) {
						tprompt_buffer_set(&handle->buffer, entry);
						tprompt_display_mark_all_dirty(handle);
					}
				} else {
					// Not at first line, navigate up within buffer
					tprompt_cursor_move_up(handle);
				}
			} else {
				// Single line mode: always navigate history
				// Save current input if not already in history navigation
				if (!handle->history.current) {
					if (handle->history.saved_input) {
						free(handle->history.saved_input);
					}
					handle->history.saved_input = strdup(handle->buffer.data);
				}
				const char *entry = tprompt_history_prev(&handle->history);
				if (entry) {
					tprompt_buffer_set(&handle->buffer, entry);
				}
			}
			return 0; // Continue editing

		case TERSE_EVENT_ARROW_DOWN:
			if (has_multiple_lines) {
				// In multi-line mode: check if at last line
				size_t current_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
				size_t total_lines = tprompt_count_logical_lines(handle);
				if (current_line >= total_lines - 1) {
					// At last line, navigate to next history entry
					const char *entry = tprompt_history_next(&handle->history);
					if (entry) {
						tprompt_buffer_set(&handle->buffer, entry);
					} else {
						// At the end of history, restore saved input
						if (handle->history.saved_input) {
							tprompt_buffer_set(&handle->buffer, handle->history.saved_input);
						} else {
							tprompt_buffer_clear(&handle->buffer);
						}
					}
				} else {
					// Not at last line, navigate down within buffer
					tprompt_cursor_move_down(handle);
				}
			} else {
				// Single line mode: always navigate history
				const char *entry = tprompt_history_next(&handle->history);
				if (entry) {
					tprompt_buffer_set(&handle->buffer, entry);
				} else {
					// At the end of history, restore saved input
					if (handle->history.saved_input) {
						tprompt_buffer_set(&handle->buffer, handle->history.saved_input);
					} else {
						tprompt_buffer_clear(&handle->buffer);
					}
				}
			}
			return 0; // Continue editing

		case TERSE_EVENT_ARROW_LEFT:
			// Move cursor left by one character
			tprompt_cursor_move_left(&handle->buffer, 1);
			handle->input_state.has_goal_column = false;
			return 0; // Continue editing

		case TERSE_EVENT_ARROW_RIGHT:
			// Move cursor right by one character
			tprompt_cursor_move_right(&handle->buffer, 1);
			handle->input_state.has_goal_column = false;
			return 0; // Continue editing

		default:
			break;
		}
	}

	// Handle Home key - staged movement (Claude Code style)
	// First press: move to physical line start, second press: move to logical line start
	if (event->type == TERSE_EVENT_HOME) {
		// Check if this is a consecutive Home press
		bool is_consecutive = (handle->input_state.last_key_type == TERSE_EVENT_HOME && handle->input_state.last_cursor_pos == handle->buffer.cursor);

		if (is_consecutive) {
			// Second press: move to logical line start
			tprompt_cursor_move_to_logical_line_start(handle);
		} else {
			// First press: move to physical line start
			tprompt_cursor_move_to_physical_line_start(handle);
		}

		// Update input state for next key press
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0; // Continue editing
	}

	// Handle End key - staged movement (Claude Code style)
	// First press: move to physical line end, second press: move to logical line end
	if (event->type == TERSE_EVENT_END) {
		// Check if this is a consecutive End press
		bool is_consecutive = (handle->input_state.last_key_type == TERSE_EVENT_END && handle->input_state.last_cursor_pos == handle->buffer.cursor);

		if (is_consecutive) {
			// Second press: move to logical line end
			tprompt_cursor_move_to_logical_line_end(handle);
		} else {
			// First press: move to physical line end
			tprompt_cursor_move_to_physical_line_end(handle);
		}

		// Update input state for next key press
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0; // Continue editing
	}

	// Check custom keybindings first (takes precedence over default behavior)
	tprompt_action_t custom_action = tprompt_find_keybinding_action(handle, event);
	if (custom_action != TPROMPT_ACTION_NONE) {
		switch (custom_action) {
		case TPROMPT_ACTION_CONFIRM_INPUT:
			// Mark confirmation as pending (will be validated in main loop)
			handle->pending_confirmation = true;
			return 0;

		case TPROMPT_ACTION_INSERT_NEWLINE:
			// Insert newline at cursor position
			if (tprompt_buffer_insert(&handle->buffer, "\n", 1) != 0) {
				tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
					"Failed to insert newline: buffer at %zu/%zu bytes",
					handle->buffer.length, handle->buffer.size);
				return -1;
			}
			return 0; // Continue editing

		// Future actions can be added here
		default:
			// Unknown action, fall through to default behavior
			break;
		}
	}

	// Handle Enter key behavior based on mode and modifiers (default behavior)
	if (event->type == TERSE_EVENT_ENTER) {
		int mods = event->data.key.mods;
		bool is_multiline = (handle->options.flags & TPROMPT_FLAG_MULTILINE) != 0;

		// Ctrl+Enter or Alt+Enter: confirm input (always accept, but call validation for logging/statistics)
		if ((mods & TERSE_MOD_CTRL) || (mods & TERSE_MOD_ALT)) {
			// Call validation callback if configured (for logging/statistics), but always accept
			if (handle->options.validation_callback) {
				handle->options.validation_callback(
					handle->buffer.data,
					handle->buffer.length,
					handle->options.validation_user_data);
				// Ignore result - always confirm
			}
			return 1; // Immediately confirm input, regardless of validation result
		}

		// Plain Enter: behavior depends on mode and validation callback
		// Mark confirmation as pending (will be validated in main loop)
		handle->pending_confirmation = true;
		return 0;
	}

	// Handle Ctrl+D as EOF
	if (event->type == TERSE_EVENT_CHAR && event->data.ch.scalar == 4 && // Ctrl+D
		(event->data.ch.mods & TERSE_MOD_CTRL)) {
		// Mark confirmation as pending (will be validated in main loop)
		handle->pending_confirmation = true;
		return 0;
	}

	// Handle Ctrl+P (previous history) - Emacs-style binding
	if (event->type == TERSE_EVENT_CHAR && event->data.ch.scalar == 'p' && // Ctrl+P
		(event->data.ch.mods & TERSE_MOD_CTRL)) {
		// Check if buffer has multiple lines
		bool has_multiple_lines = tprompt_buffer_has_newlines(handle);

		if (has_multiple_lines) {
			// In multi-line mode: check if at first line
			size_t current_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
			if (current_line == 0) {
				// At first line, navigate to previous history entry
				// Save current input if not already in history navigation
				if (!handle->history.current) {
					if (handle->history.saved_input) {
						free(handle->history.saved_input);
					}
					handle->history.saved_input = strdup(handle->buffer.data);
				}
				const char *entry = tprompt_history_prev(&handle->history);
				if (entry) {
					tprompt_buffer_set(&handle->buffer, entry);
					tprompt_display_mark_all_dirty(handle);
				}
			} else {
				// Not at first line, navigate up within buffer
				tprompt_cursor_move_up(handle);
			}
		} else {
			// Single line mode: always navigate history
			// Save current input if not already in history navigation
			if (!handle->history.current) {
				if (handle->history.saved_input) {
					free(handle->history.saved_input);
				}
				handle->history.saved_input = strdup(handle->buffer.data);
			}
			const char *entry = tprompt_history_prev(&handle->history);
			if (entry) {
				tprompt_buffer_set(&handle->buffer, entry);
				tprompt_display_mark_all_dirty(handle);
			}
		}
		return 0;
	}

	// Handle Ctrl+N (next history) - Emacs-style binding
	if (event->type == TERSE_EVENT_CHAR && event->data.ch.scalar == 'n' && // Ctrl+N
		(event->data.ch.mods & TERSE_MOD_CTRL)) {
		// Check if buffer has multiple lines
		bool has_multiple_lines = tprompt_buffer_has_newlines(handle);

		if (has_multiple_lines) {
			// In multi-line mode: check if at last line
			size_t current_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
			size_t total_lines = tprompt_count_logical_lines(handle);
			if (current_line >= total_lines - 1) {
				// At last line, navigate to next history entry
				const char *entry = tprompt_history_next(&handle->history);
				if (entry) {
					tprompt_buffer_set(&handle->buffer, entry);
					tprompt_display_mark_all_dirty(handle);
				} else {
					// At the end of history, restore saved input
					if (handle->history.saved_input) {
						tprompt_buffer_set(&handle->buffer, handle->history.saved_input);
					} else {
						tprompt_buffer_clear(&handle->buffer);
					}
					tprompt_display_mark_all_dirty(handle);
				}
			} else {
				// Not at last line, navigate down within buffer
				tprompt_cursor_move_down(handle);
			}
		} else {
			// Single line mode: always navigate history
			const char *entry = tprompt_history_next(&handle->history);
			if (entry) {
				tprompt_buffer_set(&handle->buffer, entry);
				tprompt_display_mark_all_dirty(handle);
			} else {
				// At the end of history, restore saved input
				if (handle->history.saved_input) {
					tprompt_buffer_set(&handle->buffer, handle->history.saved_input);
				} else {
					tprompt_buffer_clear(&handle->buffer);
				}
				tprompt_display_mark_all_dirty(handle);
			}
		}
		return 0;
	}

	// Handle left/right arrow keys for cursor movement
	if (event->type == TERSE_EVENT_ARROW_LEFT) {
		tprompt_cursor_move_left(&handle->buffer, 1);
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	if (event->type == TERSE_EVENT_ARROW_RIGHT) {
		tprompt_cursor_move_right(&handle->buffer, 1);
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle backspace key
	if (event->type == TERSE_EVENT_BACKSPACE) {
		size_t old_length = handle->buffer.length;
		size_t deleted_bytes = tprompt_buffer_delete_before(&handle->buffer, 1);
		if (deleted_bytes > 0) {
			// Mark from new cursor position to old end as dirty (characters shift left)
			tprompt_display_mark_dirty_range(handle, handle->buffer.cursor, old_length);
		}
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle delete key
	if (event->type == TERSE_EVENT_DELETE) {
		size_t old_length = handle->buffer.length;
		size_t deleted_bytes = tprompt_buffer_delete_at(&handle->buffer, 1);
		if (deleted_bytes > 0) {
			// Mark from cursor to old end as dirty (characters shift left)
			tprompt_display_mark_dirty_range(handle, handle->buffer.cursor, old_length);
		}
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle Tab key (when completion is not active)
	if (event->type == TERSE_EVENT_TAB) {
		// Insert tab character
		if (tprompt_buffer_insert(&handle->buffer, "\t", 1) != 0) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to insert tab character: buffer at %zu/%zu bytes",
				handle->buffer.length, handle->buffer.size);
			return -1;
		}
		// Tab affects display width, force full redraw
		tprompt_display_mark_all_dirty(handle);
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// For any other key, clear the last key tracking and goal column
	handle->input_state.last_key_type = event->type;
	handle->input_state.last_cursor_pos = handle->buffer.cursor;

	// Clear goal column on non-vertical movements
	handle->input_state.has_goal_column = false;

	return 0; // Continue editing
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
 * Custom Keybindings - Internal Helpers
 * ======================================================================== */

tprompt_action_t tprompt_find_keybinding_action(tprompt_handle_t handle, const terse_event_t *event)
{
	if (!handle || !event || !handle->keybindings) {
		return TPROMPT_ACTION_NONE;
	}

	for (size_t i = 0; i < handle->keybinding_count; i++) {
		const tprompt_keybinding_t *kb = &handle->keybindings[i];

		// Check key type match
		if (kb->key != event->type) {
			continue;
		}

		// Extract modifiers from event based on type
		int event_mods = 0;
		if (event->type == TERSE_EVENT_CHAR) {
			event_mods = event->data.ch.mods;
		} else if (event->type == TERSE_EVENT_FUNCTION) {
			event_mods = event->data.function.mods;
		} else {
			// For other key types (ENTER, BACKSPACE, arrows, etc.)
			event_mods = event->data.key.mods;
		}

		// Check modifier match
		if (kb->modifiers != event_mods) {
			continue;
		}

		// Event-type-specific matching
		if (event->type == TERSE_EVENT_CHAR) {
			// For character events, also match the scalar value
			if (kb->data.scalar != event->data.ch.scalar) {
				continue;
			}
		} else if (event->type == TERSE_EVENT_FUNCTION) {
			// For function keys, match the function number
			if (kb->data.function_num != event->data.function.number) {
				continue;
			}
		}

		// Match found!
		return kb->action;
	}

	return TPROMPT_ACTION_NONE;
}

int tprompt_validate_keybindings(const tprompt_keybinding_t *bindings,
	size_t count,
	tprompt_error_info_t *error)
{
	// NULL bindings with count > 0 is a critical error
	if (!bindings && count > 0) {
		tprompt_set_error(error, TPROMPT_ERROR_INVALID_ARGS, 0,
			"NULL keybindings array with count > 0");
		return -1;
	}

	// NULL or count == 0 is valid (means no custom bindings)
	if (!bindings || count == 0) {
		return 0;
	}

	int warning_count = 0;
	char warning_msg[256] = { 0 };

	for (size_t i = 0; i < count; i++) {
		// Check for unknown/invalid action values
		if (bindings[i].action < 0 || bindings[i].action > TPROMPT_ACTION_INSERT_NEWLINE) {
			warning_count++;
			snprintf(warning_msg, sizeof(warning_msg),
				"Warning: keybinding %zu has unknown action %d (will be ignored)",
				i, bindings[i].action);
			// Just record warning, don't fail
		}

		// Check for duplicates
		for (size_t j = i + 1; j < count; j++) {
			if (bindings[i].key != bindings[j].key) {
				continue;
			}
			if (bindings[i].modifiers != bindings[j].modifiers) {
				continue;
			}

			// For CHAR and FUNCTION events, also check data field
			bool is_duplicate = false;
			if (bindings[i].key == TERSE_EVENT_CHAR) {
				is_duplicate = (bindings[i].data.scalar == bindings[j].data.scalar);
			} else if (bindings[i].key == TERSE_EVENT_FUNCTION) {
				is_duplicate = (bindings[i].data.function_num == bindings[j].data.function_num);
			} else {
				// For other key types, key+modifiers is enough to determine duplicate
				is_duplicate = true;
			}

			if (is_duplicate) {
				warning_count++;
				snprintf(warning_msg, sizeof(warning_msg),
					"Warning: duplicate keybinding at indices %zu and %zu", i, j);
			}
		}
	}

	// Record warnings in error info if any
	if (warning_count > 0 && error) {
		tprompt_set_error(error, TPROMPT_ERROR_NONE, 0,
			"%s (total %d warnings)", warning_msg, warning_count);
	}

	return 0; // Success (warnings don't cause failure)
}

/* ========================================================================
 * Logical Line Navigation - Internal Helpers (Phase 5)
 * ======================================================================== */

/**
 * @brief Count total number of logical lines in buffer
 *
 * A logical line is delimited by explicit newline characters (\n).
 * Empty buffer has 1 logical line.
 *
 * @param handle Prompt handle
 * @return Number of logical lines (minimum 1)
 */
size_t tprompt_count_logical_lines(tprompt_handle_t handle)
{
	if (!handle || !handle->buffer.data) {
		return 1;
	}

	// Count newline characters in buffer
	size_t line_count = 1; // Always at least 1 logical line
	const char *data = handle->buffer.data;
	size_t length = handle->buffer.length;

	for (size_t i = 0; i < length; i++) {
		if (data[i] == '\n') {
			line_count++;
		}
	}

	return line_count;
}

/**
 * @brief Get logical line number containing given byte offset
 *
 * Lines are 0-indexed. Counts number of newlines before the offset.
 *
 * @param handle Prompt handle
 * @param byte_offset Byte offset in buffer (clamped to buffer length)
 * @return Logical line number (0-based)
 */
size_t tprompt_get_logical_line_at_offset(tprompt_handle_t handle, size_t byte_offset)
{
	if (!handle || !handle->buffer.data) {
		return 0;
	}

	// Clamp offset to buffer length
	if (byte_offset > handle->buffer.length) {
		byte_offset = handle->buffer.length;
	}

	// Count newlines before the offset
	size_t line_number = 0;
	const char *data = handle->buffer.data;

	for (size_t i = 0; i < byte_offset; i++) {
		if (data[i] == '\n') {
			line_number++;
		}
	}

	return line_number;
}

/**
 * @brief Get byte range (start and end offsets) for a logical line
 *
 * Finds the start and end byte offsets of the specified logical line.
 * The end offset is exclusive (points to the newline or end of buffer).
 *
 * Algorithm:
 * 1. Iterate through buffer counting newlines until we reach the target line
 * 2. Record the start offset (byte after previous newline, or 0 for first line)
 * 3. Continue iterating until next newline or end of buffer
 * 4. Record the end offset (byte at newline or end of buffer)
 *
 * @param handle Prompt handle
 * @param logical_line Logical line number (0-based)
 * @param out_start Output: byte offset of line start (inclusive)
 * @param out_end Output: byte offset of line end (exclusive, at \n or buffer end)
 * @return 0 on success, -1 if line number is out of range
 */
int tprompt_get_logical_line_bounds(tprompt_handle_t handle, size_t logical_line,
	size_t *out_start, size_t *out_end)
{
	if (!handle || !handle->buffer.data || !out_start || !out_end) {
		return -1;
	}

	const char *data = handle->buffer.data;
	size_t length = handle->buffer.length;
	size_t current_line = 0;
	size_t line_start = 0;

	// Find the start of the requested logical line
	for (size_t i = 0; i < length; i++) {
		if (current_line == logical_line) {
			line_start = i;
			break;
		}
		if (data[i] == '\n') {
			current_line++;
			line_start = i + 1; // Start of next line is after newline
		}
	}

	// Check if we found the requested line
	if (current_line != logical_line) {
		// Line number is beyond buffer content
		return -1;
	}

	// Find the end of the logical line (next newline or end of buffer)
	size_t line_end = line_start;
	for (size_t i = line_start; i < length; i++) {
		if (data[i] == '\n') {
			line_end = i; // End is at newline (exclusive)
			break;
		}
		line_end = i + 1; // If no newline found, end is at buffer end
	}

	*out_start = line_start;
	*out_end = line_end;
	return 0;
}

size_t tprompt_get_logical_line_length(tprompt_handle_t handle, size_t logical_line)
{
	size_t start, end;
	if (tprompt_get_logical_line_bounds(handle, logical_line, &start, &end) != 0) {
		return 0;
	}

	return end - start;
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
		.flags = 0
	};

	// Open temporary handle
	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		return NULL;
	}

	// Read one line
	char *result = tprompt_readline(handle, NULL);

	// If readline failed, copy the error to global error before closing handle
	if (result == NULL && handle->last_error.category != TPROMPT_ERROR_NONE) {
		tprompt_global_error = handle->last_error;
	}

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
		.continuation_prompt = DEFAULT_CONTINUATION_PROMPT,
		.history_file = NULL,
		.max_input_size = 1024 * 1024,
		.max_history_size = 100,
		.completion_callback = NULL,
		.completion_user_data = NULL,
		.completion_prefixes = NULL,
		.terse_handle = NULL,
		.flags = TPROMPT_FLAG_MULTILINE,
		.custom_keybindings = NULL,
		.keybinding_count = 0,
		.validation_callback = NULL,
		.validation_user_data = NULL
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

	// Initialize raw mode flag
#if defined(__unix__) || defined(__APPLE__)
	handle->raw_mode_active = false;
#endif

	// Enable enhanced keyboard features if supported
	unsigned int keyboard_supported = terse_keyboard_get_supported(handle->terse);
	if (keyboard_supported & TERSE_KEYBOARD_FEATURE_MODIFY_OTHER_KEYS) {
		terse_keyboard_enable(handle->terse, TERSE_KEYBOARD_FEATURE_MODIFY_OTHER_KEYS);
	}
	if (keyboard_supported & TERSE_KEYBOARD_FEATURE_KITTY_PROTOCOL) {
		terse_keyboard_enable(handle->terse, TERSE_KEYBOARD_FEATURE_KITTY_PROTOCOL);
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
	handle->display.prev_total_physical_lines = 0;
	handle->display.terminal_width = 80; // Will be updated from terse
	handle->display.terminal_height = 24;
	handle->display.start_row = 0;
	handle->display.start_row_known = false;

	// Initialize input state
	handle->input_state.last_key_type = 0;
	handle->input_state.last_cursor_pos = 0;
	handle->input_state.goal_column = 0;
	handle->input_state.has_goal_column = false;

	// Initialize validation state
	handle->pending_confirmation = false;
	handle->validation_error_msg = NULL;

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

	// Store continuation prompt (use default if NULL)
	const char *cont_prompt = options->continuation_prompt ? options->continuation_prompt : DEFAULT_CONTINUATION_PROMPT;
	handle->continuation_prompt = strdup(cont_prompt);
	if (!handle->continuation_prompt) {
		tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
			"Failed to allocate continuation prompt string");
		free(handle->prompt);
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

	// Initialize status line callback based on flags and options
	handle->status_line_callback = NULL;
	handle->status_line_user_data = NULL;

	if (options->flags & TPROMPT_FLAG_SHOW_DEBUG_STATUS) {
		// Debug mode takes precedence
		handle->status_line_callback = tprompt_internal_debug_status_callback;
		handle->status_line_user_data = NULL;
	} else if (options->status_line_callback) {
		// Use custom callback if provided
		handle->status_line_callback = options->status_line_callback;
		handle->status_line_user_data = options->status_line_user_data;
	}

	// Copy and validate custom keybindings
	handle->keybindings = NULL;
	handle->keybinding_count = 0;
	if (options->custom_keybindings && options->keybinding_count > 0) {
		// Validate keybindings first
		tprompt_error_info_t validation_error = { 0 };
		if (tprompt_validate_keybindings(options->custom_keybindings,
				options->keybinding_count,
				&validation_error)
			!= 0) {
			// Critical validation error
			tprompt_set_error(&tprompt_global_error, validation_error.category,
				validation_error.code, "%s", validation_error.message);
			free(handle->prompt);
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

		// Copy keybindings array
		size_t bindings_size = sizeof(tprompt_keybinding_t) * options->keybinding_count;
		handle->keybindings = malloc(bindings_size);
		if (!handle->keybindings) {
			tprompt_set_error(&tprompt_global_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to allocate keybindings array");
			free(handle->prompt);
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
		memcpy(handle->keybindings, options->custom_keybindings, bindings_size);
		handle->keybinding_count = options->keybinding_count;

		// Record any validation warnings in handle error info (non-fatal)
		if (validation_error.category != TPROMPT_ERROR_NONE) {
			handle->last_error = validation_error;
		}
	}

	// Clear error if no warnings
	if (handle->last_error.category == TPROMPT_ERROR_NONE) {
		tprompt_clear_error(&handle->last_error);
	}

	// Initialize buffer-based rendering (Phase 1-3)
	if (tprompt_buffer_based_rendering_init(handle) != 0) {
		// Non-fatal: buffer-based rendering is optional optimization
		// Continue without it
	}

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

	// Free buffer-based rendering resources (Phase 1-3)
	tprompt_buffer_based_rendering_free(handle);

	// Free resources
	free(handle->history_file_path);
	free(handle->prompt);
	free(handle->continuation_prompt);
	free(handle->completion_prefixes);
	free(handle->keybindings);
	free(handle->validation_error_msg);

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
 * Public API - Framework: Editing Session - Helper Functions
 * ======================================================================== */

/**
 * @brief Enable raw terminal mode for editing
 *
 * @param handle Prompt handle
 * @return 0 on success, -1 on error
 */
static int tprompt_readline_enable_raw_mode(tprompt_handle_t handle)
{
#if defined(__unix__) || defined(__APPLE__)
	if (!handle) {
		return -1;
	}

	if (tcgetattr(STDIN_FILENO, &handle->original_termios) == 0) {
		struct termios raw = handle->original_termios;
		raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
		raw.c_oflag &= ~(OPOST);
		raw.c_cflag |= CS8;
		raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
		raw.c_cc[VMIN] = 1;
		raw.c_cc[VTIME] = 0;
		if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
			handle->raw_mode_active = true;
			return 0;
		}
	}
	return -1;
#else
	(void)handle; // Unused on non-POSIX platforms
	return 0;
#endif
}

/**
 * @brief Disable raw terminal mode and restore original settings
 *
 * @param handle Prompt handle
 */
static void tprompt_readline_disable_raw_mode(tprompt_handle_t handle)
{
#if defined(__unix__) || defined(__APPLE__)
	if (handle && handle->raw_mode_active) {
		tcsetattr(STDIN_FILENO, TCSANOW, &handle->original_termios);
		handle->raw_mode_active = false;
	}
#else
	(void)handle; // Unused on non-POSIX platforms
#endif
}

/**
 * @brief Handle TERSE_EVENT_CHAR events in the main editing loop
 *
 * Processes character input events, including:
 * - Custom keybindings (checked first)
 * - Built-in Ctrl shortcuts (Ctrl+W, Ctrl+K, Ctrl+U, Ctrl+A, Ctrl+E, Ctrl+P, Ctrl+N, Ctrl+D)
 * - Regular character insertion (UTF-8 encoded)
 *
 * @param handle Prompt handle
 * @param event The character event to process
 * @param should_break Output: set to true if input should be confirmed (e.g., Ctrl+D on empty buffer)
 * @return 0 on success, -1 on error
 */
static int tprompt_handle_char_event(tprompt_handle_t handle, const terse_event_t *event, bool *should_break)
{
	if (!handle || !event || !should_break) {
		return -1;
	}

	*should_break = false;

	unsigned int scalar = event->data.ch.scalar;
	int mods = event->data.ch.mods;

	// Check custom keybindings first (before built-in Ctrl shortcuts)
	tprompt_action_t custom_action = tprompt_find_keybinding_action(handle, event);
	if (custom_action != TPROMPT_ACTION_NONE) {
		int key_result = tprompt_handle_key_event(handle, event);
		if (key_result == 1) {
			*should_break = true; // Confirm input
		} else if (key_result == -1) {
			return -1; // Error
		}
		// key_result == 0: continue editing
		return 0;
	}

	// Handle Ctrl+W - delete word backward
	if (scalar == 'w' && (mods & TERSE_MOD_CTRL)) {
		tprompt_key_handle_ctrl_w(handle);
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle Ctrl+K - delete to end of line
	if (scalar == 'k' && (mods & TERSE_MOD_CTRL)) {
		tprompt_key_handle_ctrl_k(handle);
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle Ctrl+U - delete to start of line
	if (scalar == 'u' && (mods & TERSE_MOD_CTRL)) {
		tprompt_key_handle_ctrl_u(handle);
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle Ctrl+A - move to start of line
	if (scalar == 'a' && (mods & TERSE_MOD_CTRL)) {
		tprompt_key_handle_ctrl_a(handle);
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle Ctrl+E - move to end of line
	if (scalar == 'e' && (mods & TERSE_MOD_CTRL)) {
		tprompt_key_handle_ctrl_e(handle);
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle Ctrl+P - previous history (Emacs-style)
	if (scalar == 'p' && (mods & TERSE_MOD_CTRL)) {
		int key_result = tprompt_handle_key_event(handle, event);
		if (key_result == 1) {
			*should_break = true; // Confirm input
		} else if (key_result == -1) {
			return -1; // Error
		}
		// key_result == 0: continue editing
		return 0;
	}

	// Handle Ctrl+N - next history (Emacs-style)
	if (scalar == 'n' && (mods & TERSE_MOD_CTRL)) {
		int key_result = tprompt_handle_key_event(handle, event);
		if (key_result == 1) {
			*should_break = true; // Confirm input
		} else if (key_result == -1) {
			return -1; // Error
		}
		// key_result == 0: continue editing
		return 0;
	}

	// Handle Ctrl+D (EOF-like behavior)
	if (scalar == 'd' && (mods & TERSE_MOD_CTRL)) {
		if (handle->buffer.length == 0) {
			tprompt_clear_error(&handle->last_error); // EOF is not an error
			*should_break = true; // Signal EOF
		}
		// If buffer is not empty, ignore Ctrl+D
		return 0;
	}

	// Regular character input - convert scalar to UTF-8
	if (!(mods & TERSE_MOD_CTRL)) { // Ignore unhandled Ctrl combinations
		char utf8_buf[5];
		int len = 0;

		// Simple UTF-8 encoding
		if (scalar < 0x80) {
			utf8_buf[len++] = (char)scalar;
		} else if (scalar < 0x800) {
			utf8_buf[len++] = (char)(0xC0 | (scalar >> 6));
			utf8_buf[len++] = (char)(0x80 | (scalar & 0x3F));
		} else if (scalar < 0x10000) {
			utf8_buf[len++] = (char)(0xE0 | (scalar >> 12));
			utf8_buf[len++] = (char)(0x80 | ((scalar >> 6) & 0x3F));
			utf8_buf[len++] = (char)(0x80 | (scalar & 0x3F));
		} else if (scalar < 0x110000) {
			utf8_buf[len++] = (char)(0xF0 | (scalar >> 18));
			utf8_buf[len++] = (char)(0x80 | ((scalar >> 12) & 0x3F));
			utf8_buf[len++] = (char)(0x80 | ((scalar >> 6) & 0x3F));
			utf8_buf[len++] = (char)(0x80 | (scalar & 0x3F));
		}
		utf8_buf[len] = '\0';

		// Use char input handler for completion trigger detection
		if (tprompt_handle_char_input(handle, utf8_buf, event->data.ch.width) != 0) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to insert character (scalar=0x%X) into buffer", scalar);
			return -1;
		}
		handle->input_state.has_goal_column = false;
	}

	return 0;
}

/**
 * @brief Handle pending input confirmation with validation
 *
 * Called when user attempts to confirm input (e.g., Enter key).
 * Runs validation callback if configured and handles the result.
 *
 * @param handle Prompt handle
 * @param should_break Output: set to true if input should be confirmed and loop exited
 * @return 0 on success (continue or break), -1 on error
 */
static int tprompt_handle_pending_confirmation(tprompt_handle_t handle, bool *should_break)
{
	if (!handle || !should_break) {
		return -1;
	}

	*should_break = false;

	// Call validation callback if configured
	if (handle->options.validation_callback) {
		tprompt_validation_result_t validation_result = handle->options.validation_callback(
			handle->buffer.data,
			handle->buffer.length,
			handle->options.validation_user_data);

		if (validation_result == TPROMPT_VALIDATION_REJECT) {
			// Validation rejected - play beep, render, and continue editing
			terse_write_text(handle->terse, "\x07"); // Bell character (beep)
			terse_flush(handle->terse);
			// TODO: Display validation error message if provided
			// Re-render display before continuing
			if (tprompt_display_render_buffered(handle) != 0) {
				return -1;
			}
			return 0; // Continue editing

		} else if (validation_result == TPROMPT_VALIDATION_CONTINUE) {
			// Validation wants to continue editing with newline
			bool is_multiline = (handle->options.flags & TPROMPT_FLAG_MULTILINE) != 0;

			if (is_multiline) {
				// Insert newline and continue editing
				if (tprompt_buffer_insert(&handle->buffer, "\n", 1) != 0) {
					tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
						"Failed to insert newline after validation: buffer at %zu/%zu bytes",
						handle->buffer.length, handle->buffer.size);
					return -1;
				}
				// Re-render display before continuing
				if (tprompt_display_render_buffered(handle) != 0) {
					return -1;
				}
				return 0; // Continue editing
			} else {
				// Single-line mode: CONTINUE treated as REJECT (can't insert newline)
				terse_write_text(handle->terse, "\x07"); // Bell character (beep)
				terse_flush(handle->terse);
				// Re-render display before continuing
				if (tprompt_display_render_buffered(handle) != 0) {
					return -1;
				}
				return 0; // Continue editing
			}
		}
		// TPROMPT_VALIDATION_ACCEPT: fall through to confirm input
		*should_break = true;
		return 0;

	} else {
		// No validation callback - behavior depends on mode
		bool is_multiline = (handle->options.flags & TPROMPT_FLAG_MULTILINE) != 0;

		if (is_multiline) {
			// Multiline mode without validation: insert newline
			if (tprompt_buffer_insert(&handle->buffer, "\n", 1) != 0) {
				tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
					"Failed to insert newline: buffer at %zu/%zu bytes",
					handle->buffer.length, handle->buffer.size);
				return -1;
			}
			// Re-render display before continuing
			if (tprompt_display_render_buffered(handle) != 0) {
				return -1;
			}
			return 0; // Continue editing
		}
		// Single-line mode without validation: confirm input
		*should_break = true;
		return 0;
	}
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

	// Clear buffer for new input
	tprompt_buffer_clear(&handle->buffer);

	// Reset history navigation
	tprompt_history_reset_position(&handle->history);

	// Reset display state for new readline session
	handle->display.start_row = 0; // Will be set on first render
	handle->display.start_row_known = false;

	// Mark display as needing full render for initial draw
	tprompt_display_mark_all_dirty(handle);

	// Update prompt if override is provided
	if (prompt_override) {
		free(handle->prompt);
		handle->prompt = strdup(prompt_override);
		if (!handle->prompt) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to allocate prompt override");
			return NULL;
		}
	}

	// Enable raw mode
	tprompt_readline_enable_raw_mode(handle);

	// Initial render
	if (tprompt_display_render_buffered(handle) != 0) {
		// Restore terminal on error
		tprompt_readline_disable_raw_mode(handle);
		return NULL;
	}

	// Main event loop
	while (1) {
		// Wait for next event
		terse_event_t event;
		terse_error_t result = terse_read_event(handle->terse, -1, &event); // -1 = wait indefinitely
		if (result != TERSE_OK) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_TERSE, (int)result,
				"Failed to read event");
			return NULL;
		}

		// Handle different event types
		if (event.type == TERSE_EVENT_CHAR) {
			bool should_break = false;
			if (tprompt_handle_char_event(handle, &event, &should_break) != 0) {
				// Error occurred
				tprompt_readline_disable_raw_mode(handle);
				return NULL;
			}
			if (should_break) {
				break; // Confirm input (e.g., Ctrl+D on empty buffer)
			}
		} else if (event.type == TERSE_EVENT_ENTER) {
			// Handle Enter key through tprompt_handle_key_event (supports custom keybindings)
			int key_result = tprompt_handle_key_event(handle, &event);
			if (key_result == 1) {
				break; // Confirm input
			} else if (key_result == -1) {
				return NULL; // Error
			}
			// key_result == 0: continue editing (newline was inserted)
		} else if (event.type == TERSE_EVENT_BACKSPACE) {
			tprompt_buffer_delete_before(&handle->buffer, 1);
			handle->input_state.has_goal_column = false;
		} else if (event.type == TERSE_EVENT_DELETE) {
			tprompt_buffer_delete_at(&handle->buffer, 1);
			handle->input_state.has_goal_column = false;
		} else if (event.type == TERSE_EVENT_ARROW_LEFT) {
			if (event.data.key.mods & TERSE_MOD_CTRL) {
				tprompt_cursor_move_word_backward(&handle->buffer);
			} else {
				tprompt_cursor_move_left(&handle->buffer, 1);
			}
			handle->input_state.has_goal_column = false;
		} else if (event.type == TERSE_EVENT_ARROW_RIGHT) {
			if (event.data.key.mods & TERSE_MOD_CTRL) {
				tprompt_cursor_move_word_forward(&handle->buffer);
			} else {
				tprompt_cursor_move_right(&handle->buffer, 1);
			}
			handle->input_state.has_goal_column = false;
		} else if (event.type == TERSE_EVENT_ARROW_UP || event.type == TERSE_EVENT_ARROW_DOWN) {
			// Handle UP/DOWN through tprompt_handle_key_event (supports history navigation)
			int key_result = tprompt_handle_key_event(handle, &event);
			if (key_result == 1) {
				break; // Confirm input (should not happen for arrows, but handle it)
			} else if (key_result == -1) {
				return NULL; // Error
			}
			// key_result == 0: continue editing
		} else if (event.type == TERSE_EVENT_HOME || event.type == TERSE_EVENT_END) {
			// Handle Home/End keys through tprompt_handle_key_event (supports staged movement)
			int key_result = tprompt_handle_key_event(handle, &event);
			if (key_result == 1) {
				break; // Confirm input (shouldn't happen for Home/End, but handle it)
			} else if (key_result == -1) {
				return NULL; // Error
			}
			// key_result == 0: continue editing
		} else if (event.type == TERSE_EVENT_TAB) {
			// Handle Tab key through tprompt_handle_key_event (supports completion)
			int key_result = tprompt_handle_key_event(handle, &event);
			if (key_result == 1) {
				break; // Confirm input (shouldn't happen for Tab, but handle it)
			} else if (key_result == -1) {
				return NULL; // Error
			}
			// key_result == 0: Tab was handled (completion confirmed or tab inserted)
		}

		// Track last event for staging behavior (unless already tracked by Home/End handlers)
		// Only track if not already set by Home/End key handlers
		if (event.type != TERSE_EVENT_HOME && event.type != TERSE_EVENT_END) {
			handle->input_state.last_key_type = event.type;
			handle->input_state.last_cursor_pos = handle->buffer.cursor;
		}

		// Check if input confirmation is pending (before rendering)
		if (handle->pending_confirmation) {
			handle->pending_confirmation = false; // Reset flag

			bool should_break = false;
			int result = tprompt_handle_pending_confirmation(handle, &should_break);
			if (result == -1) {
				// Error occurred
				tprompt_readline_disable_raw_mode(handle);
				return NULL;
			}

			if (should_break) {
				break; // Confirm input
			}
			// Continue editing (validation handled rendering)
			continue;
		}

		// Re-render display after each event (if not already handled by validation logic)
		if (tprompt_display_render_buffered(handle) != 0) {
			// Restore terminal on error
			tprompt_readline_disable_raw_mode(handle);
			return NULL;
		}
	}

	// Restore terminal mode before returning
	tprompt_readline_disable_raw_mode(handle);

	// Move cursor to end of buffer to ensure clean output positioning
	// This prevents output from appearing in the middle of multi-line editing area
	if (handle->buffer.cursor < handle->buffer.length) {
		handle->buffer.cursor = handle->buffer.length;
		// Render one final time to position cursor at end
		tprompt_display_render_buffered(handle);
	}

	// Move to new line after input is confirmed
	// In raw mode, we need \r\n to move to the beginning of the next line
	terse_write_text(handle->terse, "\r\n");
	terse_flush(handle->terse);

	// Update start_row for next readline call if cursor position is not available
	// This tracks where the cursor is after moving to the next line
	if (handle->display.start_row_known) {
		int lines_used = (int)handle->display.total_physical_lines;
		handle->display.start_row += lines_used + 1; // +1 for the \r\n we just wrote

		// Wrap around if we exceed terminal height
		if (handle->display.start_row >= (int)handle->display.terminal_height) {
			handle->display.start_row = handle->display.terminal_height - 1;
		}
	}

	// Add to history if not empty
	if (handle->buffer.length > 0 && !(handle->options.flags & TPROMPT_FLAG_DISABLE_HISTORY)) {
		tprompt_history_add_internal(&handle->history, handle->buffer.data);
	}

	// Return a copy of the buffer
	char *result = strdup(handle->buffer.data);
	if (!result) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
			"Failed to allocate result string");
		return NULL;
	}

	return result;
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

	// Trim history if current count exceeds new max_size
	while (handle->history.max_size > 0 && handle->history.count > handle->history.max_size) {
		if (!handle->history.tail) {
			break; // Safety check
		}

		tprompt_history_entry_t *old_tail = handle->history.tail;
		handle->history.tail = old_tail->prev;

		if (handle->history.tail) {
			handle->history.tail->next = NULL;
		} else {
			// List became empty
			handle->history.head = NULL;
		}

		free(old_tail->text);
		free(old_tail);
		handle->history.count--;
	}

	// Reset navigation position if history became empty
	if (handle->history.count == 0) {
		handle->history.current = NULL;
	}
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
 * Public API - Framework: Validation
 * ======================================================================== */

void tprompt_set_validation_callback(tprompt_handle_t handle,
	tprompt_validation_fn callback,
	void *user_data)
{
	if (!handle) {
		return;
	}

	handle->options.validation_callback = callback;
	handle->options.validation_user_data = user_data;
}

/* ========================================================================
 * Public API - Framework: Keybindings
 * ======================================================================== */

int tprompt_set_keybindings(tprompt_handle_t handle,
	const tprompt_keybinding_t *bindings,
	size_t count)
{
	if (!handle) {
		return -1;
	}

	// Clear error
	tprompt_clear_error(&handle->last_error);

	// Validate keybindings
	if (tprompt_validate_keybindings(bindings, count, &handle->last_error) != 0) {
		// Critical validation error
		return -1;
	}

	// Free existing keybindings
	free(handle->keybindings);
	handle->keybindings = NULL;
	handle->keybinding_count = 0;

	// If NULL or count == 0, we're done (cleared custom bindings)
	if (!bindings || count == 0) {
		return 0;
	}

	// Allocate and copy new keybindings
	size_t bindings_size = sizeof(tprompt_keybinding_t) * count;
	handle->keybindings = malloc(bindings_size);
	if (!handle->keybindings) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
			"Failed to allocate keybindings array");
		return -1;
	}

	memcpy(handle->keybindings, bindings, bindings_size);
	handle->keybinding_count = count;

	return 0;
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

/* ========================================================================
 * Phase 6 Keybinding Handlers
 * ======================================================================== */

int tprompt_key_handle_ctrl_w(tprompt_handle_t handle)
{
	if (!handle || handle->buffer.cursor == 0) {
		return 0; // Nothing to delete
	}

	const char *data = handle->buffer.data;
	size_t pos = handle->buffer.cursor;

	// Move back one character first
	size_t prev_pos = tprompt_utf8_prev_char(data, pos);

	// Skip trailing whitespace backwards
	while (prev_pos > 0 && (data[prev_pos] == ' ' || data[prev_pos] == '\t')) {
		pos = prev_pos;
		prev_pos = tprompt_utf8_prev_char(data, pos);
	}

	// If we're now at the start or hit a newline, delete from here
	if (prev_pos == 0 || data[prev_pos] == '\n') {
		if (prev_pos > 0 && data[prev_pos] == '\n') {
			pos = prev_pos + 1; // Don't delete the newline
		}
		size_t delete_len = handle->buffer.cursor - pos;
		handle->buffer.cursor = pos;
		if (delete_len > 0) {
			tprompt_buffer_delete_at(&handle->buffer, delete_len);
		}
		return 0;
	}

	// Delete backwards until whitespace, newline, or start
	while (pos > 0) {
		prev_pos = tprompt_utf8_prev_char(data, pos);
		if (data[prev_pos] == ' ' || data[prev_pos] == '\t' || data[prev_pos] == '\n') {
			break;
		}
		pos = prev_pos;
	}

	// Delete from pos to cursor
	size_t delete_len = handle->buffer.cursor - pos;
	handle->buffer.cursor = pos;
	if (delete_len > 0) {
		tprompt_buffer_delete_at(&handle->buffer, delete_len);
	}

	return 0;
}

int tprompt_key_handle_ctrl_k(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line boundaries
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
	size_t line_start, line_end;

	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &line_start, &line_end) != 0) {
		return -1;
	}

	// If cursor is already at end of logical line, do nothing
	if (handle->buffer.cursor >= line_end) {
		return 0;
	}

	// Delete from cursor to end of logical line
	size_t delete_len = line_end - handle->buffer.cursor;
	tprompt_buffer_delete_at(&handle->buffer, delete_len);

	return 0;
}

int tprompt_key_handle_ctrl_u(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line boundaries
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
	size_t line_start, line_end;

	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &line_start, &line_end) != 0) {
		return -1;
	}

	// If cursor is at the start of a logical line (not the first line),
	// delete the previous logical line and its newline
	if (handle->buffer.cursor == line_start && current_logical_line > 0) {
		size_t prev_line_start, prev_line_end;
		if (tprompt_get_logical_line_bounds(handle, current_logical_line - 1, &prev_line_start, &prev_line_end) != 0) {
			return -1;
		}
		// Delete from start of previous line to cursor (including the newline)
		size_t delete_len = handle->buffer.cursor - prev_line_start;
		handle->buffer.cursor = prev_line_start;
		tprompt_buffer_delete_at(&handle->buffer, delete_len);
		return 0;
	}

	// If cursor is already at start of first logical line, do nothing
	if (handle->buffer.cursor <= line_start) {
		return 0;
	}

	// Delete from start of logical line to cursor
	size_t delete_len = handle->buffer.cursor - line_start;
	handle->buffer.cursor = line_start;
	tprompt_buffer_delete_at(&handle->buffer, delete_len);

	return 0;
}

int tprompt_key_handle_ctrl_a(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line boundaries
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
	size_t line_start, line_end;

	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &line_start, &line_end) != 0) {
		return -1;
	}

	// If cursor is already at the start of a logical line (not the first line),
	// move to the start of the previous logical line
	if (handle->buffer.cursor == line_start && current_logical_line > 0) {
		size_t prev_line_start, prev_line_end;
		if (tprompt_get_logical_line_bounds(handle, current_logical_line - 1, &prev_line_start, &prev_line_end) != 0) {
			return -1;
		}
		handle->buffer.cursor = prev_line_start;
		return 0;
	}

	// Move cursor to start of logical line
	handle->buffer.cursor = line_start;

	return 0;
}

int tprompt_key_handle_ctrl_e(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	// Get current logical line boundaries
	size_t current_logical_line = tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor);
	size_t line_start, line_end;

	if (tprompt_get_logical_line_bounds(handle, current_logical_line, &line_start, &line_end) != 0) {
		return -1;
	}

	// Move cursor to end of logical line
	handle->buffer.cursor = line_end;

	return 0;
}

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

/* ========================================================================
 * Screen Buffer Management (Buffer-based Differential Rendering)
 * ======================================================================== */

