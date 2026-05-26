/**
 * @file test_display.c
 * @brief Rendering and cursor positioning regression tests
 *
 * Ensures the buffered renderer keeps the terminal cursor in sync
 * after edits such as backspace/delete.
 */

#include "test_helpers.h"
#include "tprompt_internal.h"
#include <attest/attest.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Create a handle wired to a mock terse instance.
 */
static tprompt_handle_t create_test_handle(terse_handle_t *out_terse)
{
	// Create mock terse handle for test mode
	terse_handle_t terse = test_create_terse_handle();
	if (!terse) {
		return NULL;
	}

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		terse_close(terse);
		return NULL;
	}

	*out_terse = terse;
	return handle;
}

TEST(DisplayBackspace, RestoresCursorAfterDeletion)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle(&terse);
	ASSERT_NOT_NULL(handle);

	ASSERT_EQ(tprompt_display_render_buffered(handle), 0);

	ASSERT_EQ(tprompt_handle_char_input(handle, "a", 1), 0);
	ASSERT_EQ(tprompt_display_render_buffered(handle), 0);
	ASSERT_EQ(tprompt_handle_char_input(handle, "b", 1), 0);
	ASSERT_EQ(tprompt_display_render_buffered(handle), 0);
	ASSERT_EQ(tprompt_handle_char_input(handle, "c", 1), 0);
	ASSERT_EQ(tprompt_display_render_buffered(handle), 0);

	test_clear_calls(terse);
	test_start_recording(terse);

	terse_event_t backspace = { .type = TERSE_EVENT_BACKSPACE };
	ASSERT_EQ(tprompt_handle_key_event(handle, &backspace), 0);
	ASSERT_EQ(tprompt_display_render_buffered(handle), 0);

	test_stop_recording(terse);

	size_t cursor_line = 0;
	size_t cursor_col = 0;
	tprompt_calculate_physical_position(handle, handle->buffer.cursor,
		true, &cursor_line, &cursor_col);
	int expected_row = handle->display.start_row + (int)cursor_line;
	int expected_col = (int)cursor_col;

	int count = 0;
	const terse_call_record_t *calls = test_get_calls(terse, &count);
	ASSERT_NOT_NULL(calls);

	bool blank_seen = false;
	bool reposition_seen = false;
	for (int i = 0; i < count; i++) {
		const terse_call_record_t *call = &calls[i];

		if (!blank_seen && call->type == TERSE_CALL_WRITE_TEXT && strcmp(call->data.write_text.text, " ") == 0) {
			blank_seen = true;
			continue;
		}

		if (blank_seen && call->type == TERSE_CALL_MOVE_TO) {
			if (call->data.move_to.row == expected_row && call->data.move_to.col == expected_col) {
				reposition_seen = true;
				break;
			}
		}
	}

	EXPECT_TRUE(blank_seen);
	EXPECT_TRUE(reposition_seen);

	tprompt_close(handle);
	terse_close(terse);
}

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
