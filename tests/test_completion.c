/**
 * @file test_completion.c
 * @brief Completion functionality tests for terse-prompt Phase 3
 * @version 0.1
 * @date 2025-11-05
 *
 * Tests completion trigger detection, candidate filtering, navigation,
 * TAB confirmation, and ESC cancellation.
 */

#include "tprompt_internal.h"
#include <attest/attest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Mock Completion Callback
 * ======================================================================== */

static const char **mock_candidates = NULL;
static size_t mock_candidate_count = 0;

static tprompt_completion_result_t mock_completion_callback(
	const char *text,
	size_t cursor_pos,
	const char *prefix_char,
	void *user_data)
{
	(void)text;
	(void)cursor_pos;
	(void)prefix_char;
	(void)user_data;

	tprompt_completion_result_t result;
	result.candidates = (char **)mock_candidates;
	result.count = mock_candidate_count;
	return result;
}

/* ========================================================================
 * Completion State Tests
 * ======================================================================== */

TEST(Completion, StateInit)
{
	tprompt_completion_state_t state;
	tprompt_completion_init(&state);

	EXPECT_FALSE(state.active);
	EXPECT_EQ(state.trigger_char, '\0');
	EXPECT_EQ(state.trigger_offset, 0);
	EXPECT_EQ(state.candidate_count, 0);
	EXPECT_NULL(state.candidates);

	tprompt_completion_free(&state);
}

/* ========================================================================
 * Completion Candidate Navigation Tests
 * ======================================================================== */

TEST(Completion, SelectNext)
{
	tprompt_completion_state_t state;
	tprompt_completion_init(&state);

	const char *candidates[] = { "cmd1", "cmd2", "cmd3" };
	state.candidates = (char **)candidates;
	state.candidate_count = 3;
	state.selected_index = 0;
	state.active = true;

	// Navigate: 0 -> 1 -> 2 -> 0 (wrap around)
	tprompt_completion_select_next(&state);
	EXPECT_EQ(state.selected_index, 1);

	tprompt_completion_select_next(&state);
	EXPECT_EQ(state.selected_index, 2);

	tprompt_completion_select_next(&state);
	EXPECT_EQ(state.selected_index, 0);

	// Don't free candidates (they're static)
	state.candidates = NULL;
	tprompt_completion_free(&state);
}

TEST(Completion, SelectPrev)
{
	tprompt_completion_state_t state;
	tprompt_completion_init(&state);

	const char *candidates[] = { "cmd1", "cmd2", "cmd3" };
	state.candidates = (char **)candidates;
	state.candidate_count = 3;
	state.selected_index = 0;
	state.active = true;

	// Navigate: 0 -> 2 -> 1 -> 0 (wrap around)
	tprompt_completion_select_prev(&state);
	EXPECT_EQ(state.selected_index, 2);

	tprompt_completion_select_prev(&state);
	EXPECT_EQ(state.selected_index, 1);

	tprompt_completion_select_prev(&state);
	EXPECT_EQ(state.selected_index, 0);

	state.candidates = NULL;
	tprompt_completion_free(&state);
}

TEST(Completion, SingleCandidateNavigation)
{
	tprompt_completion_state_t state;
	tprompt_completion_init(&state);

	const char *candidates[] = { "only" };
	state.candidates = (char **)candidates;
	state.candidate_count = 1;
	state.selected_index = 0;
	state.active = true;

	// Navigation should stay at 0
	tprompt_completion_select_next(&state);
	EXPECT_EQ(state.selected_index, 0);

	tprompt_completion_select_prev(&state);
	EXPECT_EQ(state.selected_index, 0);

	state.candidates = NULL;
	tprompt_completion_free(&state);
}

TEST(Completion, EmptyCandidates)
{
	tprompt_completion_state_t state;
	tprompt_completion_init(&state);

	state.candidates = NULL;
	state.candidate_count = 0;
	state.selected_index = 0;

	// Should not crash
	tprompt_completion_select_next(&state);
	EXPECT_EQ(state.selected_index, 0);

	tprompt_completion_select_prev(&state);
	EXPECT_EQ(state.selected_index, 0);

	tprompt_completion_free(&state);
}

/* ========================================================================
 * Buffer and Completion Integration Tests
 * ======================================================================== */

TEST(Completion, BufferIntegration)
{
	tprompt_buffer_t buffer;
	tprompt_buffer_init(&buffer, 128);

	// Insert text: "/hel"
	EXPECT_EQ(tprompt_buffer_insert(&buffer, "/", 1), 0);
	EXPECT_EQ(tprompt_buffer_insert(&buffer, "hel", 3), 0);
	EXPECT_EQ(buffer.length, 4);
	EXPECT_EQ(buffer.cursor, 4);

	// Simulate completion replacement using memmove (like tprompt_completion_confirm does)
	size_t trigger_offset = 1; // After '/'
	size_t bytes_to_remove = buffer.cursor - trigger_offset;

	if (bytes_to_remove > 0) {
		memmove(buffer.data + trigger_offset,
			buffer.data + buffer.cursor,
			buffer.length - buffer.cursor + 1);
		buffer.length -= bytes_to_remove;
		buffer.cursor = trigger_offset;
	}

	// Insert completion
	EXPECT_EQ(tprompt_buffer_insert(&buffer, "hello", 5), 0);
	EXPECT_STREQ(buffer.data, "/hello");

	tprompt_buffer_free(&buffer);
}

TEST(Completion, UTF8BufferIntegration)
{
	tprompt_buffer_t buffer;
	tprompt_buffer_init(&buffer, 128);

	// Insert UTF-8 text: "/日本"
	EXPECT_EQ(tprompt_buffer_insert(&buffer, "/", 1), 0);
	EXPECT_EQ(tprompt_buffer_insert(&buffer, "日本", 6), 0); // 2 chars, 6 bytes
	EXPECT_EQ(buffer.length, 7);

	// Simulate completion replacement using memmove
	size_t trigger_offset = 1;
	size_t bytes_to_remove = buffer.cursor - trigger_offset;

	if (bytes_to_remove > 0) {
		memmove(buffer.data + trigger_offset,
			buffer.data + buffer.cursor,
			buffer.length - buffer.cursor + 1);
		buffer.length -= bytes_to_remove;
		buffer.cursor = trigger_offset;
	}

	EXPECT_EQ(tprompt_buffer_insert(&buffer, "日本語", 9), 0);
	EXPECT_STREQ(buffer.data, "/日本語");

	tprompt_buffer_free(&buffer);
}

/* ========================================================================
 * Memory Safety Tests
 * ======================================================================== */

TEST(Completion, MemoryCleanup)
{
	tprompt_completion_state_t state;
	tprompt_completion_init(&state);

	// Allocate candidates (simulating callback allocation)
	char **candidates = malloc(3 * sizeof(char *));
	candidates[0] = strdup("cmd1");
	candidates[1] = strdup("cmd2");
	candidates[2] = strdup("cmd3");

	state.candidates = candidates;
	state.candidate_count = 3;
	state.active = true;

	// Free should clean up
	tprompt_completion_free(&state);
	EXPECT_NULL(state.candidates);
	EXPECT_EQ(state.candidate_count, 0);
}

TEST(Completion, MultipleActivations)
{
	tprompt_completion_state_t state;
	tprompt_completion_init(&state);

	// Activate multiple times (simulating real usage)
	for (int i = 0; i < 5; i++) {
		char **candidates = malloc(2 * sizeof(char *));
		candidates[0] = strdup("a");
		candidates[1] = strdup("b");

		state.candidates = candidates;
		state.candidate_count = 2;
		state.active = true;

		// Deactivate and free
		tprompt_completion_free(&state);
		EXPECT_NULL(state.candidates);
	}

	tprompt_completion_free(&state);
}

/* ========================================================================
 * TAB-Cycling Completion Tests
 * ======================================================================== */

#include "test_helpers.h"

static tprompt_handle_t create_test_handle_with_completion(void)
{
	terse_handle_t terse_h = test_create_terse_handle();
	if (!terse_h) {
		return NULL;
	}

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse_h;
	opts.completion_prefixes = "/:";

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		terse_close(terse_h);
		return NULL;
	}
	return handle;
}

/* Mock extended completion callback for TAB-cycling tests */
static int mock_ex_callback(
	const char *text,
	size_t cursor_pos,
	const char *prefix_char,
	void *user_data,
	tprompt_completion_candidate_t **candidates,
	size_t *count)
{
	(void)text;
	(void)cursor_pos;
	(void)prefix_char;

	const char **words = (const char **)user_data;
	if (!words) {
		*candidates = NULL;
		*count = 0;
		return 0;
	}

	// Count words
	size_t n = 0;
	while (words[n]) n++;

	tprompt_completion_candidate_t *cands = malloc(sizeof(*cands) * n);
	for (size_t i = 0; i < n; i++) {
		cands[i].text = strdup(words[i]);
		cands[i].description = NULL;
	}
	*candidates = cands;
	*count = n;
	return 0;
}

TEST(CompletionCycling, ApplySelectionBasic)
{
	// Test tprompt_completion_apply_selection directly
	tprompt_handle_t handle = create_test_handle_with_completion();
	ASSERT_NOT_NULL(handle);

	// Set up extended callback with test candidates
	const char *words[] = { "help", "hello", "history", NULL };
	handle->completion_ex_callback = mock_ex_callback;
	handle->completion_user_data = (void *)words;

	// Simulate: user typed "/h"
	tprompt_buffer_insert(&handle->buffer, "/h", 2);

	// Activate completion at '/' position
	tprompt_completion_activate(handle, '/', 0);
	EXPECT_TRUE(handle->completion_state.active);
	EXPECT_EQ(handle->completion_state.candidate_count, 3);
	EXPECT_EQ(handle->completion_state.selected_index, 0);

	// Apply first candidate: should replace "/h" with "/help"
	EXPECT_EQ(tprompt_completion_apply_selection(handle), 0);
	EXPECT_STREQ(handle->buffer.data, "/help");
	EXPECT_TRUE(handle->completion_state.has_applied);

	// Select next and apply: should replace with "/hello"
	tprompt_completion_select_next(&handle->completion_state);
	EXPECT_EQ(handle->completion_state.selected_index, 1);
	EXPECT_EQ(tprompt_completion_apply_selection(handle), 0);
	EXPECT_STREQ(handle->buffer.data, "/hello");

	// Select next and apply: should replace with "/history"
	tprompt_completion_select_next(&handle->completion_state);
	EXPECT_EQ(handle->completion_state.selected_index, 2);
	EXPECT_EQ(tprompt_completion_apply_selection(handle), 0);
	EXPECT_STREQ(handle->buffer.data, "/history");

	tprompt_close(handle);
}

TEST(CompletionCycling, RestoreSaved)
{
	tprompt_handle_t handle = create_test_handle_with_completion();
	ASSERT_NOT_NULL(handle);

	const char *words[] = { "help", "hello", NULL };
	handle->completion_ex_callback = mock_ex_callback;
	handle->completion_user_data = (void *)words;

	// Simulate: user typed "/h"
	tprompt_buffer_insert(&handle->buffer, "/h", 2);

	// Activate and apply
	tprompt_completion_activate(handle, '/', 0);
	tprompt_completion_apply_selection(handle);
	EXPECT_STREQ(handle->buffer.data, "/help");

	// Restore should bring back "/h"
	EXPECT_EQ(tprompt_completion_restore_saved(handle), 0);
	EXPECT_STREQ(handle->buffer.data, "/h");
	EXPECT_EQ(handle->buffer.cursor, 2);

	tprompt_close(handle);
}

TEST(CompletionCycling, NoTrailingSpace)
{
	// Verify that confirm no longer adds a trailing space
	tprompt_handle_t handle = create_test_handle_with_completion();
	ASSERT_NOT_NULL(handle);

	const char *words[] = { "help", NULL };
	handle->completion_ex_callback = mock_ex_callback;
	handle->completion_user_data = (void *)words;

	// Simulate: user typed "/h"
	tprompt_buffer_insert(&handle->buffer, "/h", 2);

	// Activate and confirm
	tprompt_completion_activate(handle, '/', 0);
	tprompt_completion_confirm(handle);

	// Should be "/help" without trailing space
	EXPECT_STREQ(handle->buffer.data, "/help");

	tprompt_close(handle);
}

TEST(CompletionCycling, TabTriggeredApply)
{
	// Test TAB-triggered completion (identifier completion)
	tprompt_handle_t handle = create_test_handle_with_completion();
	ASSERT_NOT_NULL(handle);

	const char *words[] = { "mkdir", "make", NULL };
	handle->completion_ex_callback = mock_ex_callback;
	handle->completion_user_data = (void *)words;

	// Simulate: user typed "mk"
	tprompt_buffer_insert(&handle->buffer, "mk", 2);

	// Activate with TAB trigger (word start = 0)
	tprompt_completion_activate(handle, '\t', 0);
	EXPECT_TRUE(handle->completion_state.active);

	// Apply first candidate: "mkdir"
	EXPECT_EQ(tprompt_completion_apply_selection(handle), 0);
	EXPECT_STREQ(handle->buffer.data, "mkdir");

	// Cycle to next: "make"
	tprompt_completion_select_next(&handle->completion_state);
	EXPECT_EQ(tprompt_completion_apply_selection(handle), 0);
	EXPECT_STREQ(handle->buffer.data, "make");

	// Restore should bring back "mk"
	EXPECT_EQ(tprompt_completion_restore_saved(handle), 0);
	EXPECT_STREQ(handle->buffer.data, "mk");

	tprompt_close(handle);
}

/* ========================================================================
 * Main Entry Point
 * ======================================================================== */

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
