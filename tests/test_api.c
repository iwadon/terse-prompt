/**
 * @file test_api.c
 * @brief Public API coverage tests for terse-prompt
 *
 * Exercises history, completion, and keybinding APIs that aren't covered
 * by the feature-focused suites.
 */

#include "test_helpers.h"
#include "tprompt_internal.h"
#include <attest/attest.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ========================================================================
 * Test Helpers
 * ======================================================================== */

static tprompt_handle_t create_test_handle_with_flags(terse_handle_t *out_terse, int flags)
{
	terse_handle_t terse = test_create_terse_handle();
	if (!terse) {
		return NULL;
	}

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.flags = flags;

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		terse_close(terse);
		return NULL;
	}

	*out_terse = terse;
	return handle;
}

static tprompt_completion_result_t empty_completion_callback(
	const char *text,
	size_t cursor_pos,
	const char *prefix_char,
	void *user_data)
{
	(void)text;
	(void)cursor_pos;
	(void)prefix_char;
	(void)user_data;

	tprompt_completion_result_t result = { 0 };
	return result;
}

static int mock_completion_ex_callback(
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
	(void)user_data;

	tprompt_completion_candidate_t *items = malloc(2 * sizeof(*items));
	if (!items) {
		return -1;
	}

	items[0].text = strdup("alpha");
	items[0].description = strdup("first");
	items[1].text = strdup("beta");
	items[1].description = NULL;

	if (!items[0].text || !items[0].description || !items[1].text) {
		tprompt_free_completion_candidates(items, 2);
		return -1;
	}

	*candidates = items;
	*count = 2;
	return 0;
}

/* ========================================================================
 * History API Tests
 * ======================================================================== */

TEST(HistoryApi, AddRespectsDisableFlag)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse, TPROMPT_FLAG_DISABLE_HISTORY);
	ASSERT_NOT_NULL(handle);

	EXPECT_EQ(tprompt_history_add(handle, "cmd1"), 0);
	EXPECT_EQ(handle->history.count, 0u);

	tprompt_close(handle);
	terse_close(terse);
}

TEST(HistoryApi, AddAndClear)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse, 0);
	ASSERT_NOT_NULL(handle);

	EXPECT_EQ(tprompt_history_add(handle, "cmd1"), 0);
	EXPECT_EQ(tprompt_history_add(handle, "cmd2"), 0);
	EXPECT_EQ(handle->history.count, 2u);

	tprompt_history_clear(handle);
	EXPECT_EQ(handle->history.count, 0u);
	EXPECT_NULL(handle->history.head);
	EXPECT_NULL(handle->history.tail);

	tprompt_close(handle);
	terse_close(terse);
}

TEST(HistoryApi, SetMaxSizeTrims)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse, 0);
	ASSERT_NOT_NULL(handle);

	EXPECT_EQ(tprompt_history_add(handle, "one"), 0);
	EXPECT_EQ(tprompt_history_add(handle, "two"), 0);
	EXPECT_EQ(tprompt_history_add(handle, "three"), 0);

	tprompt_history_set_max_size(handle, 2);
	EXPECT_EQ(handle->history.count, 2u);

	const char *entry = tprompt_history_prev(&handle->history);
	EXPECT_STREQ(entry, "three");
	entry = tprompt_history_prev(&handle->history);
	EXPECT_STREQ(entry, "two");
	entry = tprompt_history_prev(&handle->history);
	EXPECT_NULL(entry);

	tprompt_close(handle);
	terse_close(terse);
}

TEST(HistoryApi, SaveLoadRoundTrip)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse, 0);
	ASSERT_NOT_NULL(handle);

	char temp_file[] = "/tmp/tprompt_api_history_XXXXXX";
	int fd = mkstemp(temp_file);
	ASSERT_TRUE(fd >= 0);
	close(fd);

	EXPECT_EQ(tprompt_history_add(handle, "alpha"), 0);
	EXPECT_EQ(tprompt_history_add(handle, "beta"), 0);
	EXPECT_EQ(tprompt_history_save(handle, temp_file), 0);

	tprompt_history_clear(handle);
	EXPECT_EQ(tprompt_history_load(handle, temp_file), 0);

	const char *entry = tprompt_history_prev(&handle->history);
	EXPECT_STREQ(entry, "beta");
	entry = tprompt_history_prev(&handle->history);
	EXPECT_STREQ(entry, "alpha");

	unlink(temp_file);
	tprompt_close(handle);
	terse_close(terse);
}

/* ========================================================================
 * Completion API Tests
 * ======================================================================== */

TEST(CompletionApi, PrefixesRequireCallback)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse, 0);
	ASSERT_NOT_NULL(handle);

	ASSERT_EQ(tprompt_set_completion_prefixes(handle, "/@"), 0);
	EXPECT_FALSE(tprompt_is_completion_trigger(handle, '/'));

	tprompt_set_completion_callback(handle, empty_completion_callback, NULL);
	EXPECT_TRUE(tprompt_is_completion_trigger(handle, '/'));
	EXPECT_TRUE(tprompt_is_completion_trigger(handle, '@'));
	EXPECT_FALSE(tprompt_is_completion_trigger(handle, '#'));

	ASSERT_EQ(tprompt_set_completion_prefixes(handle, ""), 0);
	EXPECT_FALSE(tprompt_is_completion_trigger(handle, '/'));

	tprompt_close(handle);
	terse_close(terse);
}

TEST(CompletionApi, ExtendedCallbackUsed)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse, 0);
	ASSERT_NOT_NULL(handle);

	tprompt_set_completion_ex_callback(handle, mock_completion_ex_callback, NULL);
	ASSERT_EQ(tprompt_completion_activate(handle, '@', handle->buffer.cursor), 0);

	EXPECT_TRUE(handle->completion_state.use_extended);
	EXPECT_EQ(handle->completion_state.candidate_count, 2u);
	EXPECT_STREQ(handle->completion_state.candidates_ex[0].text, "alpha");
	EXPECT_STREQ(handle->completion_state.candidates_ex[0].description, "first");
	EXPECT_STREQ(handle->completion_state.candidates_ex[1].text, "beta");

	tprompt_completion_deactivate(handle);
	tprompt_close(handle);
	terse_close(terse);
}

TEST(CompletionApi, FreeHelpers)
{
	tprompt_completion_result_t result = { 0 };
	result.count = 2;
	result.candidates = malloc(2 * sizeof(*result.candidates));
	result.candidates[0] = strdup("one");
	result.candidates[1] = strdup("two");

	tprompt_free_completion_result(&result);
	EXPECT_NULL(result.candidates);
	EXPECT_EQ(result.count, 0u);

	tprompt_completion_candidate_t *candidates = malloc(2 * sizeof(*candidates));
	candidates[0].text = strdup("alpha");
	candidates[0].description = strdup("first");
	candidates[1].text = strdup("beta");
	candidates[1].description = NULL;

	tprompt_free_completion_candidates(candidates, 2);
}

/* ========================================================================
 * Keybinding API Tests
 * ======================================================================== */

TEST(KeybindingsApi, RejectsNullArrayWithCount)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse, 0);
	ASSERT_NOT_NULL(handle);

	EXPECT_EQ(tprompt_set_keybindings(handle, NULL, 1), -1);

	tprompt_error_info_t err = tprompt_get_last_error(handle);
	EXPECT_EQ(err.category, TPROMPT_ERROR_INVALID_ARGS);
	EXPECT_TRUE(strstr(err.message, "NULL") != NULL);

	tprompt_close(handle);
	terse_close(terse);
}

TEST(KeybindingsApi, SetAndClear)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse, 0);
	ASSERT_NOT_NULL(handle);

	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_CHAR('x', TERSE_MOD_CTRL, TPROMPT_ACTION_DELETE_TO_END_OF_LINE)
	};

	ASSERT_EQ(tprompt_set_keybindings(handle, bindings, 1), 0);
	EXPECT_EQ(handle->keybinding_count, 1u);
	EXPECT_EQ(handle->keybindings[0].data.scalar, 'x');

	bindings[0].data.scalar = 'y';
	EXPECT_EQ(handle->keybindings[0].data.scalar, 'x');

	ASSERT_EQ(tprompt_set_keybindings(handle, NULL, 0), 0);
	EXPECT_EQ(handle->keybinding_count, 0u);
	EXPECT_NULL(handle->keybindings);

	tprompt_close(handle);
	terse_close(terse);
}

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
