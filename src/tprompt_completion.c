/**
 * @file tprompt_completion.c
 * @brief Implementation of completion functionality for terse-prompt
 * @version 0.1
 * @date 2025-11-13
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_internal.h"
#include "tprompt_completion.h"
#include "tprompt_buffer.h"
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Completion State Management
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

/* ========================================================================
 * Completion Activation and Deactivation
 * ======================================================================== */

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

/* ========================================================================
 * Completion Candidate Management
 * ======================================================================== */

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
	char prefix_str[2] = { handle->completion_state.trigger_char, '\0' };

	// Invoke callback
	tprompt_completion_result_t result = handle->completion_callback(
		text,
		cursor_pos,
		prefix_str,
		handle->completion_user_data);

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

/* ========================================================================
 * Completion Navigation
 * ======================================================================== */

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

/* ========================================================================
 * Completion Confirmation
 * ======================================================================== */

int tprompt_completion_confirm(tprompt_handle_t handle)
{
	if (!handle || !handle->completion_state.active) {
		return -1;
	}

	// Check if we have a valid selected candidate
	if (handle->completion_state.candidate_count == 0 || handle->completion_state.selected_index >= handle->completion_state.candidate_count) {
		return -1;
	}

	// Get the selected candidate text
	const char *selected = handle->completion_state.candidates[handle->completion_state.selected_index];
	if (!selected) {
		return -1;
	}

	// Delete text from AFTER trigger position to current cursor position
	// We keep the trigger character itself (e.g., '/' or '@')
	size_t trigger_pos = handle->completion_state.trigger_offset;
	size_t current_pos = handle->buffer.cursor;

	// Safety check: ensure trigger_pos is valid
	if (trigger_pos > current_pos || trigger_pos > handle->buffer.length) {
		return -1;
	}

	// Calculate trigger character length (assume single-byte for now)
	// Skip past the trigger character to get the start of user input
	size_t trigger_char_len = 1;
	size_t user_input_start = trigger_pos + trigger_char_len;

	// Move cursor to position after trigger character
	handle->buffer.cursor = user_input_start;

	// Delete characters from user_input_start to current_pos (byte-wise)
	size_t delete_count = current_pos - user_input_start;
	if (delete_count > 0) {
		// Shift remaining text left
		memmove(handle->buffer.data + user_input_start,
			handle->buffer.data + current_pos,
			handle->buffer.length - current_pos);

		handle->buffer.length -= delete_count;
		handle->buffer.data[handle->buffer.length] = '\0';
	}

	// Insert the selected candidate after the trigger character
	size_t selected_len = strlen(selected);
	if (tprompt_buffer_insert(&handle->buffer, selected, selected_len) != 0) {
		return -1;
	}

	// Insert a trailing space after the completed text
	if (tprompt_buffer_insert(&handle->buffer, " ", 1) != 0) {
		return -1;
	}

	return 0;
}

/* ========================================================================
 * Completion Trigger Detection
 * ======================================================================== */

bool tprompt_is_completion_trigger(tprompt_handle_t handle, char ch)
{
	if (!handle || !handle->completion_prefixes || !handle->completion_callback) {
		return false;
	}

	// Check if the character is in the completion_prefixes string
	return strchr(handle->completion_prefixes, ch) != NULL;
}
