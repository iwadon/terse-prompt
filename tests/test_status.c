/**
 * @file test_status.c
 * @brief Status line and cursor position tests
 *
 * Validates status line rendering priority and cursor position APIs.
 */

#include "test_helpers.h"
#include "tprompt_internal.h"
#include <attest/attest.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static tprompt_handle_t create_test_handle_with_flags(terse_handle_t *out_terse, int flags)
{
	terse_handle_t terse = test_create_terse_handle_sized(10, 40);
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

/* Read a rectangle row back from terse's displayed frame (the cell storage now
 * lives in terse). terse_get_cell returns the content committed by the last
 * flush, which is exactly what tprompt_display_render_buffered() produced. */
static void buffer_row_to_string(tprompt_handle_t handle,
	size_t row, char *out, size_t out_size)
{
	if (!handle || !out || out_size == 0 || row >= handle->display.current_buffer.rows) {
		if (out && out_size > 0) {
			out[0] = '\0';
		}
		return;
	}

	size_t cols = handle->display.current_buffer.cols;
	size_t out_pos = 0;
	for (size_t col = 0; col < cols && out_pos + 1 < out_size; col++) {
		terse_cell_t cell;
		if (terse_get_cell(handle->terse, (int)row, (int)col, &cell) != TERSE_OK) {
			out[out_pos++] = ' ';
			continue;
		}
		if (cell.is_continuation) {
			continue; // 2nd column of a wide char; already emitted
		}
		if (cell.char_len == 0) {
			out[out_pos++] = ' ';
			continue;
		}

		size_t copy_len = cell.char_len;
		if (out_pos + copy_len >= out_size) {
			copy_len = out_size - out_pos - 1;
		}
		memcpy(out + out_pos, cell.utf8_char, copy_len);
		out_pos += copy_len;
	}

	out[out_pos] = '\0';
}

static void rtrim_spaces(char *text)
{
	size_t len = strlen(text);
	while (len > 0 && text[len - 1] == ' ') {
		len--;
	}
	text[len] = '\0';
}

static int status_callback_calls = 0;

static int status_line_callback(tprompt_handle_t handle, char *buffer, size_t buffer_size, void *user_data)
{
	(void)handle;
	(void)user_data;
	status_callback_calls++;
	snprintf(buffer, buffer_size, "status: ready");
	return 1;
}

static int status_line_custom_callback(tprompt_handle_t handle, char *buffer, size_t buffer_size, void *user_data)
{
	(void)handle;
	(void)user_data;
	status_callback_calls++;
	snprintf(buffer, buffer_size, "CUSTOM");
	return 1;
}

TEST(StatusLine, CustomCallbackRendersLine)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse, TPROMPT_FLAG_MULTILINE);
	ASSERT_NOT_NULL(handle);

	status_callback_calls = 0;
	tprompt_set_status_line_callback(handle, status_line_callback, NULL);

	ASSERT_EQ(tprompt_display_render_buffered(handle), 0);
	EXPECT_EQ(status_callback_calls, 2);

	char row_text[128];
	buffer_row_to_string(handle, 1, row_text, sizeof(row_text));
	rtrim_spaces(row_text);
	EXPECT_TRUE(strncmp(row_text, "status: ready", strlen("status: ready")) == 0);

	tprompt_close(handle);
	terse_close(terse);
}

TEST(StatusLine, HideFlagSuppressesCallback)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse,
		TPROMPT_FLAG_MULTILINE | TPROMPT_FLAG_HIDE_STATUS_LINE);
	ASSERT_NOT_NULL(handle);

	status_callback_calls = 0;
	tprompt_set_status_line_callback(handle, status_line_callback, NULL);

	ASSERT_EQ(tprompt_display_render_buffered(handle), 0);
	EXPECT_EQ(status_callback_calls, 0);

	char row_text[128];
	buffer_row_to_string(handle, 1, row_text, sizeof(row_text));
	rtrim_spaces(row_text);
	EXPECT_TRUE(row_text[0] == '\0');

	tprompt_close(handle);
	terse_close(terse);
}

TEST(StatusLine, DebugFlagOverridesCustomCallback)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse,
		TPROMPT_FLAG_MULTILINE | TPROMPT_FLAG_SHOW_DEBUG_STATUS);
	ASSERT_NOT_NULL(handle);

	status_callback_calls = 0;
	tprompt_set_status_line_callback(handle, status_line_custom_callback, NULL);

	ASSERT_EQ(tprompt_display_render_buffered(handle), 0);
	EXPECT_EQ(status_callback_calls, 0);

	char row_text[256];
	buffer_row_to_string(handle, 1, row_text, sizeof(row_text));
	rtrim_spaces(row_text);
	EXPECT_TRUE(strstr(row_text, "x=") != NULL);
	EXPECT_TRUE(strstr(row_text, "y=") != NULL);

	tprompt_close(handle);
	terse_close(terse);
}

TEST(CursorPosition, SingleLineColumn)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse, TPROMPT_FLAG_MULTILINE);
	ASSERT_NOT_NULL(handle);

	ASSERT_EQ(tprompt_handle_char_input(handle, "a", 1), 0);
	ASSERT_EQ(tprompt_handle_char_input(handle, "b", 1), 0);
	ASSERT_EQ(tprompt_handle_char_input(handle, "c", 1), 0);

	ASSERT_EQ(tprompt_display_render_buffered(handle), 0);
	EXPECT_EQ(tprompt_get_cursor_line(handle), 0u);
	EXPECT_EQ(tprompt_get_cursor_column(handle), 5u);

	tprompt_close(handle);
	terse_close(terse);
}

TEST(CursorPosition, MultiLineColumn)
{
	terse_handle_t terse = NULL;
	tprompt_handle_t handle = create_test_handle_with_flags(&terse, TPROMPT_FLAG_MULTILINE);
	ASSERT_NOT_NULL(handle);

	ASSERT_EQ(tprompt_handle_char_input(handle, "a", 1), 0);
	ASSERT_EQ(tprompt_handle_char_input(handle, "\n", 1), 0);
	ASSERT_EQ(tprompt_handle_char_input(handle, "b", 1), 0);

	ASSERT_EQ(tprompt_display_render_buffered(handle), 0);
	EXPECT_EQ(tprompt_get_cursor_line(handle), 1u);
	EXPECT_EQ(tprompt_get_cursor_column(handle), 3u);

	tprompt_close(handle);
	terse_close(terse);
}

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
