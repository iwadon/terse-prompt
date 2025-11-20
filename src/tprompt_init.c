/**
 * @file tprompt_init.c
 * @brief Initialization and cleanup functions
 * @version 0.1
 * @date 2025-11-20
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_keybinding.h"
#include "tprompt_internal.h"
#include "tprompt_buffer.h"
#include "tprompt_history.h"
#include "tprompt_completion.h"
#include "tprompt_display.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h>
#endif

#define DEFAULT_BUFFER_SIZE 256
#define DEFAULT_PROMPT "> "
#define DEFAULT_CONTINUATION_PROMPT "| "

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
	// In test mode, skip keyboard protocol setup to avoid escape sequences in test output
	unsigned int keyboard_supported = terse_keyboard_get_supported(handle->terse);
#ifndef TERSE_ENABLE_TEST_MODE
	if (keyboard_supported & TERSE_KEYBOARD_FEATURE_MODIFY_OTHER_KEYS) {
		terse_keyboard_enable(handle->terse, TERSE_KEYBOARD_FEATURE_MODIFY_OTHER_KEYS);
	}
	if (keyboard_supported & TERSE_KEYBOARD_FEATURE_KITTY_PROTOCOL) {
		terse_keyboard_enable(handle->terse, TERSE_KEYBOARD_FEATURE_KITTY_PROTOCOL);
	}
#else
	// In test mode, avoid enabling keyboard protocols to prevent escape sequences
	// from appearing in verbose test output (ctest -V)
	(void)keyboard_supported;
#endif

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
	handle->force_confirmation = false;
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

