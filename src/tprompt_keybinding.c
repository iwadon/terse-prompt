/**
 * @file tprompt_keybinding.c
 * @brief Keybinding management and key event handlers
 * @version 0.1
 * @date 2025-11-20
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_keybinding.h"
#include "tprompt_action.h"
#include "tprompt_buffer.h"
#include "tprompt_completion.h"
#include "tprompt_internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Default Keybindings
 * ======================================================================== */

/**
 * @brief Default keybindings table
 *
 * This table defines the standard keyboard shortcuts for terse-prompt.
 * Users can override these with custom keybindings in tprompt_options_t.
 *
 * Priority order:
 * 1. Custom keybindings (checked first in tprompt_resolve_action)
 * 2. Default keybindings (checked if custom binding not found)
 * 3. Character insertion (fallback for TERSE_EVENT_CHAR without Ctrl)
 */
static const tprompt_keybinding_t default_keybindings[] = {
	/* Special keys */
	{ TERSE_EVENT_BACKSPACE, 0, { .scalar = 0 }, TPROMPT_ACTION_DELETE_BACKWARD },
	{ TERSE_EVENT_DELETE, 0, { .scalar = 0 }, TPROMPT_ACTION_DELETE_FORWARD },
	{ TERSE_EVENT_ARROW_LEFT, 0, { .scalar = 0 }, TPROMPT_ACTION_MOVE_LEFT },
	{ TERSE_EVENT_ARROW_RIGHT, 0, { .scalar = 0 }, TPROMPT_ACTION_MOVE_RIGHT },
	{ TERSE_EVENT_ARROW_UP, 0, { .scalar = 0 }, TPROMPT_ACTION_HISTORY_PREV },
	{ TERSE_EVENT_ARROW_DOWN, 0, { .scalar = 0 }, TPROMPT_ACTION_HISTORY_NEXT },
	{ TERSE_EVENT_HOME, 0, { .scalar = 0 }, TPROMPT_ACTION_MOVE_HOME },
	{ TERSE_EVENT_END, 0, { .scalar = 0 }, TPROMPT_ACTION_MOVE_END },
	{ TERSE_EVENT_TAB, 0, { .scalar = 0 }, TPROMPT_ACTION_COMPLETE },
	{ TERSE_EVENT_ENTER, 0, { .scalar = 0 }, TPROMPT_ACTION_CONFIRM_INPUT },

	/* Ctrl + Arrow keys */
	{ TERSE_EVENT_ARROW_LEFT, TERSE_MOD_CTRL, { .scalar = 0 }, TPROMPT_ACTION_MOVE_WORD_LEFT },
	{ TERSE_EVENT_ARROW_RIGHT, TERSE_MOD_CTRL, { .scalar = 0 }, TPROMPT_ACTION_MOVE_WORD_RIGHT },

	/* Ctrl shortcuts */
	{ TERSE_EVENT_CHAR, TERSE_MOD_CTRL, { .scalar = 'w' }, TPROMPT_ACTION_DELETE_WORD_BACKWARD },
	{ TERSE_EVENT_CHAR, TERSE_MOD_CTRL, { .scalar = 'k' }, TPROMPT_ACTION_DELETE_TO_END_OF_LINE },
	{ TERSE_EVENT_CHAR, TERSE_MOD_CTRL, { .scalar = 'u' }, TPROMPT_ACTION_DELETE_TO_START_OF_LINE },
	{ TERSE_EVENT_CHAR, TERSE_MOD_CTRL, { .scalar = 'a' }, TPROMPT_ACTION_MOVE_TO_LINE_START },
	{ TERSE_EVENT_CHAR, TERSE_MOD_CTRL, { .scalar = 'e' }, TPROMPT_ACTION_MOVE_TO_LINE_END },
	{ TERSE_EVENT_CHAR, TERSE_MOD_CTRL, { .scalar = 'p' }, TPROMPT_ACTION_HISTORY_PREV },
	{ TERSE_EVENT_CHAR, TERSE_MOD_CTRL, { .scalar = 'n' }, TPROMPT_ACTION_HISTORY_NEXT },
	/* Ctrl+D is handled specially in tprompt_handle_char_event() for EOF behavior */
};

static const size_t default_keybindings_count = sizeof(default_keybindings) / sizeof(default_keybindings[0]);

static int tprompt_insert_newline_at_cursor(tprompt_handle_t handle)
{
	if (!handle) {
		return -1;
	}

	size_t insert_pos = handle->buffer.cursor;
	if (tprompt_buffer_insert_limited(handle, "\n", 1) != 0) {
		if (handle->last_error.category == TPROMPT_ERROR_NONE) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_MEMORY, errno,
				"Failed to insert newline: buffer at %zu/%zu bytes",
				handle->buffer.length, handle->buffer.size);
		}
		return -1;
	}

	tprompt_display_mark_dirty_range(handle, insert_pos, handle->buffer.cursor);
	handle->input_state.has_goal_column = false;
	return 0;
}

const tprompt_keybinding_t *tprompt_get_default_keybindings(size_t *out_count)
{
	if (out_count) {
		*out_count = default_keybindings_count;
	}
	return default_keybindings;
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
			if (tprompt_buffer_insert_limited(handle, ch, ch_len) != 0) {
				return -1;
			}

			// Mark the inserted region as dirty
			tprompt_display_mark_dirty_range(handle, insert_pos, handle->buffer.cursor);

			// Activate completion at the current cursor position
			// Don't apply first candidate — let user type to filter first
			if (tprompt_completion_activate(handle, ch[0], handle->buffer.cursor - ch_len) != 0) {
				return -1;
			}

			return 0;
		}
	}

	// Normal character insertion
	size_t ch_len = strlen(ch);
	size_t insert_pos = handle->buffer.cursor;
	if (tprompt_buffer_insert_limited(handle, ch, ch_len) != 0) {
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
 * 4. Enter key mode: Enter submits; Shift/Ctrl variants insert newline
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

	// If completion is active, handle TAB-cycling and exit-on-other-key
	if (handle->completion_state.active) {
		// ESC: cancel completion and restore original text
		if (event->type == TERSE_EVENT_CHAR && event->data.ch.scalar == 27) {
			tprompt_completion_restore_saved(handle);
			tprompt_completion_deactivate(handle);
			tprompt_display_mark_all_dirty(handle);
			return 0;
		}

		// TAB: cycle to next candidate
		if (event->type == TERSE_EVENT_TAB && handle->completion_state.candidate_count > 0) {
			int mods = event->data.key.mods;
			if (mods & TERSE_MOD_SHIFT) {
				// Shift+TAB: previous candidate
				tprompt_completion_select_prev(&handle->completion_state);
			} else {
				// TAB: next candidate
				tprompt_completion_select_next(&handle->completion_state);
			}
			// Apply the selected candidate to the buffer immediately
			tprompt_completion_apply_selection(handle);
			tprompt_display_mark_all_dirty(handle);
			return 0;
		}

		// Backspace: if a candidate was applied, restore saved text first,
		// then delete normally and re-filter candidates
		if (event->type == TERSE_EVENT_BACKSPACE) {
			// If a candidate is currently applied, restore original text
			if (handle->completion_state.has_applied) {
				tprompt_completion_restore_saved(handle);
				handle->completion_state.has_applied = false;
			}

			size_t old_cursor = handle->buffer.cursor;
			size_t old_length = handle->buffer.length;
			size_t deleted_bytes = tprompt_buffer_delete_before(&handle->buffer, 1);
			if (deleted_bytes > 0) {
				tprompt_display_mark_dirty_range(handle, handle->buffer.cursor, old_length);

				// Check if trigger character was deleted
				size_t trigger_pos = handle->completion_state.trigger_offset;
				bool is_tab = (handle->completion_state.trigger_char == '\t');
				if (is_tab ? (handle->buffer.cursor <= trigger_pos)
						   : (old_cursor <= trigger_pos + 1)) {
					tprompt_completion_deactivate(handle);
				} else {
					// Update saved text/cursor to reflect the deletion
					free(handle->completion_state.saved_text);
					handle->completion_state.saved_text = strdup(handle->buffer.data ? handle->buffer.data : "");
					handle->completion_state.saved_cursor = handle->buffer.cursor;
					tprompt_completion_update(handle);
				}
			}
			handle->input_state.last_key_type = event->type;
			handle->input_state.last_cursor_pos = handle->buffer.cursor;
			handle->input_state.has_goal_column = false;
			return 0;
		}

		// Any other key: accept current state and exit completion,
		// then process the key normally (fall through below)
		tprompt_completion_deactivate(handle);
		tprompt_display_mark_all_dirty(handle);
		// Fall through to normal key handling
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
			// Set force_confirmation to bypass default multiline behavior
			handle->pending_confirmation = true;
			handle->force_confirmation = true;
			return 0;

		case TPROMPT_ACTION_INSERT_NEWLINE:
			// Insert newline at cursor position
			if (tprompt_insert_newline_at_cursor(handle) != 0) {
				return -1;
			}
			return 0;

		case TPROMPT_ACTION_CONFIRM_WITHOUT_VALIDATION:
			// Immediately confirm input without running validation callbacks
			return 1;

		// Future actions can be added here
		default:
			// Unknown action, fall through to default behavior
			break;
		}
	}

	// Handle Enter key behavior based on mode and modifiers (default behavior)
	if (event->type == TERSE_EVENT_ENTER) {
		int mods = event->data.key.mods;
		bool wants_newline = (mods & TERSE_MOD_SHIFT) || (mods & TERSE_MOD_CTRL);

		// Ctrl/Shift + Enter: insert newline instead of confirming
		if (wants_newline) {
			if (tprompt_insert_newline_at_cursor(handle) != 0) {
				return -1;
			}
			return 0; // Continue editing
		}

		// Plain Enter: validate/confirm through main loop
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
		size_t old_cursor = handle->buffer.cursor;
		size_t old_length = handle->buffer.length;
		size_t deleted_bytes = tprompt_buffer_delete_before(&handle->buffer, 1);
		if (deleted_bytes > 0) {
			// Mark from new cursor position to old end as dirty (characters shift left)
			tprompt_display_mark_dirty_range(handle, handle->buffer.cursor, old_length);

			// Check if completion was active and trigger character was deleted
			if (handle->completion_state.active) {
				// For Tab completion, trigger_offset is word start (no trigger char)
				// For prefix completion, trigger_offset is the prefix char position
				size_t trigger_pos = handle->completion_state.trigger_offset;
				bool is_tab = (handle->completion_state.trigger_char == '\t');
				// Tab: deactivate when cursor reaches or passes word start
				// Prefix: deactivate when the trigger char itself is deleted
				if (is_tab ? (handle->buffer.cursor <= trigger_pos)
						   : (old_cursor <= trigger_pos + 1)) {
					tprompt_completion_deactivate(handle);
				} else {
					// Update completion candidates with new input
					tprompt_completion_update(handle);
				}
			}
		}
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Handle delete key
	if (event->type == TERSE_EVENT_DELETE) {
		size_t old_cursor = handle->buffer.cursor;
		size_t old_length = handle->buffer.length;
		size_t deleted_bytes = tprompt_buffer_delete_at(&handle->buffer, 1);
		if (deleted_bytes > 0) {
			// Mark from cursor to old end as dirty (characters shift left)
			tprompt_display_mark_dirty_range(handle, handle->buffer.cursor, old_length);

			// Check if completion was active and trigger character was deleted
			if (handle->completion_state.active) {
				// If the deleted character was at the trigger offset, deactivate completion
				if (old_cursor == handle->completion_state.trigger_offset) {
					tprompt_completion_deactivate(handle);
				} else if (handle->buffer.cursor <= handle->completion_state.trigger_offset) {
					// Cursor moved before trigger, also deactivate
					tprompt_completion_deactivate(handle);
				} else {
					// Update completion candidates with new input
					tprompt_completion_update(handle);
				}
			}
		}
		handle->input_state.last_key_type = event->type;
		handle->input_state.last_cursor_pos = handle->buffer.cursor;
		handle->input_state.has_goal_column = false;
		return 0;
	}

	// Resolve action from default keybindings for unhandled key events
	{
		tprompt_action_t action = tprompt_resolve_action(handle, event);
		if (action != TPROMPT_ACTION_NONE) {
			int result = tprompt_execute_action(handle, action, event);
			if (result != 0) {
				return result;
			}
			handle->input_state.last_key_type = event->type;
			handle->input_state.last_cursor_pos = handle->buffer.cursor;
			handle->input_state.has_goal_column = false;
			return 0;
		}
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

/**
 * @brief Search for action in a keybindings array
 *
 * Generic helper function to search for a matching keybinding in any array.
 * Used by both tprompt_find_keybinding_action (custom bindings) and
 * tprompt_resolve_action (default bindings).
 *
 * @param bindings Keybindings array to search
 * @param count Number of keybindings in array
 * @param event Event to match
 * @return Action if found, TPROMPT_ACTION_NONE otherwise
 */
static tprompt_action_t tprompt_search_keybindings(
	const tprompt_keybinding_t *bindings,
	size_t count,
	const terse_event_t *event)
{
	if (!bindings || !event || count == 0) {
		return TPROMPT_ACTION_NONE;
	}

	for (size_t i = 0; i < count; i++) {
		const tprompt_keybinding_t *kb = &bindings[i];

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
			// For character events, match the scalar value
			// For Ctrl+letter combinations, do case-insensitive matching
			// This handles different keyboard protocols:
			// - Legacy terminals: send uppercase ('A'-'Z') with TERSE_MOD_CTRL
			// - Keyboard protocol terminals (kitty/iTerm2): send lowercase ('a'-'z') with TERSE_MOD_CTRL
			unsigned int kb_scalar = kb->data.scalar;
			unsigned int ev_scalar = event->data.ch.scalar;

			if (event_mods & TERSE_MOD_CTRL) {
				// Normalize both to uppercase for comparison
				if (kb_scalar >= 'a' && kb_scalar <= 'z') {
					kb_scalar = kb_scalar - 'a' + 'A';
				}
				if (ev_scalar >= 'a' && ev_scalar <= 'z') {
					ev_scalar = ev_scalar - 'a' + 'A';
				}
			}

			if (kb_scalar != ev_scalar) {
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

tprompt_action_t tprompt_find_keybinding_action(tprompt_handle_t handle, const terse_event_t *event)
{
	if (!handle || !event) {
		return TPROMPT_ACTION_NONE;
	}

	return tprompt_search_keybindings(handle->keybindings, handle->keybinding_count, event);
}

tprompt_action_t tprompt_resolve_action(tprompt_handle_t handle, const terse_event_t *event)
{
	if (!handle || !event) {
		return TPROMPT_ACTION_NONE;
	}

	// 1. Check custom keybindings first (highest priority)
	if (handle->keybindings && handle->keybinding_count > 0) {
		tprompt_action_t action = tprompt_search_keybindings(
			handle->keybindings,
			handle->keybinding_count,
			event);
		if (action != TPROMPT_ACTION_NONE) {
			return action;
		}
	}

	// 2. Check default keybindings
	tprompt_action_t action = tprompt_search_keybindings(
		default_keybindings,
		default_keybindings_count,
		event);
	if (action != TPROMPT_ACTION_NONE) {
		return action;
	}

	// 3. Fallback: regular character insertion for printable characters
	if (event->type == TERSE_EVENT_CHAR && !(event->data.ch.mods & TERSE_MOD_CTRL)) {
		return TPROMPT_ACTION_INSERT_CHAR;
	}

	// 4. No action for unrecognized input
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
		if (bindings[i].action < 0 || bindings[i].action > TPROMPT_ACTION_CONFIRM_WITHOUT_VALIDATION) {
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
