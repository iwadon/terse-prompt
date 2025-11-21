/**
 * @file tprompt_session.c
 * @brief Editing session management
 * @version 0.1
 * @date 2025-11-20
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_session.h"
#include "tprompt_keybinding.h"
#include "tprompt_action.h"
#include "tprompt_internal.h"
#include "tprompt_buffer.h"
#include "tprompt_history.h"
#include "tprompt_completion.h"
#include "tprompt_display.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h>
#endif

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
 * Processes character input events through the action system:
 * - Ctrl+D special handling (EOF on empty buffer, delete-char on non-empty)
 * - All other character events dispatched via tprompt_resolve_action()
 * - Regular character insertion (UTF-8 encoded) for non-Ctrl characters
 *
 * @param handle Prompt handle
 * @param event The character event to process
 * @param should_break Output: set to true if input should be confirmed (e.g., Ctrl+D on empty buffer)
 * @return 0 on success, -1 on error
 */
int tprompt_handle_char_event(tprompt_handle_t handle, const terse_event_t *event, bool *should_break)
{
	if (!handle || !event || !should_break) {
		return -1;
	}

	*should_break = false;

	unsigned int scalar = event->data.ch.scalar;
	int mods = event->data.ch.mods;

	// Handle Ctrl+D (GNU readline-style behavior) - special case before general action handling
	// - If buffer is empty: EOF signal (end editing session)
	// - If buffer is non-empty: delete character at cursor (same as Delete key)
	// Note: terse transmits Ctrl+D as:
	//   - Uppercase 'D' on legacy terminals (control character 0x04 → 'A' + 3)
	//   - Lowercase 'd' on keyboard protocol terminals (kitty/iTerm2 CSI u)
	if ((scalar == 'd' || scalar == 'D') && (mods & TERSE_MOD_CTRL)) {
		if (handle->buffer.length == 0) {
			tprompt_clear_error(&handle->last_error); // EOF is not an error
			handle->eof_signaled = true; // Mark EOF
			*should_break = true; // Signal EOF
			return 0;
		}
		// Buffer is non-empty: delete character at cursor (delete-char)
		size_t old_length = handle->buffer.length;
		size_t deleted_bytes = tprompt_buffer_delete_at(&handle->buffer, 1);
		if (deleted_bytes > 0) {
			// Mark from cursor to old end as dirty (characters shift left)
			tprompt_display_mark_dirty_range(handle, handle->buffer.cursor, old_length);
		}
		return 0;
	}

	// Resolve action from keybindings (custom or default)
	tprompt_action_t action = tprompt_resolve_action(handle, event);

	// Execute the action through the action system
	if (action != TPROMPT_ACTION_NONE) {
		int result = tprompt_execute_action(handle, action, event);
		if (result == 1) {
			*should_break = true; // Confirm input
		} else if (result == -1) {
			return -1; // Error
		}
		// result == 0: continue editing
		return 0;
	}

	// Regular character input - convert scalar to UTF-8
	if (!(mods & TERSE_MOD_CTRL)) { // Ignore unhandled Ctrl combinations
		char utf8_buf[5];

		// Encode scalar to UTF-8
		int len = tprompt_utf8_encode(scalar, utf8_buf, sizeof(utf8_buf));
		if (len < 0) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_INVALID_ARGS, 0,
				"Invalid Unicode scalar value: 0x%X", scalar);
			return -1;
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
int tprompt_handle_pending_confirmation(tprompt_handle_t handle, bool *should_break)
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
		// No validation callback - behavior depends on mode and force_confirmation flag
		// If force_confirmation is set (from custom keybinding), always confirm
		if (handle->force_confirmation) {
			*should_break = true;
			return 0;
		}

		// No validation: confirm input immediately
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

	// Reset validation state for new readline session
	handle->pending_confirmation = false;
	handle->force_confirmation = false;

	// Reset EOF flag for new readline session
	handle->eof_signaled = false;

	// Reset display state for new readline session
	handle->display.start_row = 0; // Will be set on first render
	handle->display.start_row_known = false;

	// Clear previous buffer to ensure full redraw on first render
	// Without this, diff would detect no changes and skip rendering
	if (handle->display.buffer_based_rendering_active) {
		tprompt_screen_buffer_clear(&handle->display.previous_buffer);
	}

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
		} else {
			// Route all other events through tprompt_handle_key_event
			// This includes: ENTER, BACKSPACE, DELETE, ARROW_*, HOME, END, TAB
			int key_result = tprompt_handle_key_event(handle, &event);
			if (key_result == 1) {
				break; // Confirm input
			} else if (key_result == -1) {
				return NULL; // Error
			}
			// key_result == 0: continue editing
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
			// Reset force_confirmation flag after handling confirmation
			handle->force_confirmation = false;

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

	// Move cursor to end of buffer to ensure clean output positioning
	// This prevents output from appearing in the middle of multi-line editing area
	if (handle->buffer.cursor < handle->buffer.length) {
		handle->buffer.cursor = handle->buffer.length;
		// Re-render to position cursor at end (still in raw mode)
		tprompt_display_render_buffered(handle);
	}

	// Move to new line after input is confirmed
	// In raw mode, we need \r\n to move to the beginning of the next line
	terse_write_text(handle->terse, "\r\n");
	terse_flush(handle->terse);

	// Restore terminal mode before returning
	tprompt_readline_disable_raw_mode(handle);

	// Check for EOF: only return NULL if EOF was explicitly signaled (Ctrl+D on empty buffer)
	// This distinguishes between:
	// - Empty buffer + EOF flag = Ctrl+D on empty line → return NULL
	// - Empty buffer + no EOF flag = Enter on empty line → return ""
	if (handle->eof_signaled) {
		// Return NULL to signal EOF
		return NULL;
	}

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
