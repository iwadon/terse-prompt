/**
 * @file tprompt_api.c
 * @brief Public API implementations
 * @version 0.1
 * @date 2025-11-20
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_session.h"
#include "tprompt_keybinding.h"
#include "tprompt_status.h"
#include "tprompt_internal.h"
#include "tprompt_buffer.h"
#include "tprompt_history.h"
#include "tprompt_completion.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

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

	if (handle->options.max_input_size > 0) {
		size_t entry_len = strlen(entry);
		if (entry_len > handle->options.max_input_size) {
			tprompt_set_error(&handle->last_error, TPROMPT_ERROR_SIZE_LIMIT, 0,
				"History entry exceeds max_input_size (%zu bytes)",
				handle->options.max_input_size);
			return -1;
		}
	}

	return tprompt_history_add_internal(&handle->history, entry);
}

int tprompt_history_load(tprompt_handle_t handle, const char *file_path)
{
	if (!handle || !file_path) {
		return -1;
	}

	int result = tprompt_history_load_internal(&handle->history, file_path,
		handle->options.max_input_size, 0);
	if (result != 0) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_HISTORY_FILE, errno,
			"Failed to load history file");
	}
	return result;
}

int tprompt_history_save(tprompt_handle_t handle, const char *file_path)
{
	if (!handle || !file_path) {
		return -1;
	}

	int result = tprompt_history_save_internal(&handle->history, file_path);
	if (result != 0) {
		tprompt_set_error(&handle->last_error, TPROMPT_ERROR_HISTORY_FILE, errno,
			"Failed to save history file");
	}
	return result;
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

void tprompt_free_completion_candidates(tprompt_completion_candidate_t *candidates, size_t count)
{
	if (!candidates) {
		return;
	}

	for (size_t i = 0; i < count; i++) {
		free(candidates[i].text);
		free(candidates[i].description);
	}
	free(candidates);
}

void tprompt_set_completion_ex_callback(tprompt_handle_t handle,
	tprompt_completion_ex_fn callback,
	void *user_data)
{
	if (!handle) {
		return;
	}

	handle->completion_ex_callback = callback;
	handle->completion_user_data = user_data;
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
