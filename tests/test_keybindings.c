/**
 * @file test_keybindings.c
 * @brief Tests for Phase 6 keybindings (Ctrl+W, Ctrl+K, Ctrl+U, Ctrl+A, Ctrl+E)
 * @version 0.1
 * @date 2025-11-05
 *
 * Tests the five additional keybindings added in Phase 6.
 */

#include "tprompt_internal.h"
#include <attest/attest.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

/**
 * @brief Create a test handle with pre-configured options
 */
static tprompt_handle_t create_test_handle(void)
{
	tprompt_options_t opts = {
		.prompt = "> ",
		.history_file = NULL,
		.max_input_size = 1024,
		.max_history_size = 100,
		.completion_callback = NULL,
		.completion_user_data = NULL,
		.completion_prefixes = NULL,
		.terse_handle = NULL,
		.flags = TPROMPT_FLAG_MULTILINE
	};

	return tprompt_open(&opts);
}

/* ========================================================================
 * Ctrl+W Tests (Delete Word Backward)
 * ======================================================================== */

TEST(KeybindingsCtrlW, DeleteSingleWord)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	// Insert "hello world" and position cursor after "world"
	tprompt_buffer_insert(&handle->buffer, "hello world", 11);

	// Call Ctrl+W handler (should delete "world")
	tprompt_key_handle_ctrl_w(handle);

	EXPECT_STREQ(handle->buffer.data, "hello ");
	EXPECT_EQ(handle->buffer.length, 6);
	EXPECT_EQ(handle->buffer.cursor, 6);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlW, DeleteWordWithTrailingSpace)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	// Insert "hello world  " with trailing spaces
	tprompt_buffer_insert(&handle->buffer, "hello world  ", 13);

	tprompt_key_handle_ctrl_w(handle);

	// Should delete "world" and trailing spaces
	EXPECT_STREQ(handle->buffer.data, "hello ");
	EXPECT_EQ(handle->buffer.cursor, 6);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlW, DeleteMultipleWords)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "one two three", 13);

	// Delete "three"
	tprompt_key_handle_ctrl_w(handle);
	EXPECT_STREQ(handle->buffer.data, "one two ");

	// Delete "two"
	tprompt_key_handle_ctrl_w(handle);
	EXPECT_STREQ(handle->buffer.data, "one ");

	// Delete "one"
	tprompt_key_handle_ctrl_w(handle);
	EXPECT_STREQ(handle->buffer.data, "");
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlW, DeleteAtStart)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "word", 4);
	handle->buffer.cursor = 0;

	tprompt_key_handle_ctrl_w(handle);

	// Should do nothing when cursor at start
	EXPECT_STREQ(handle->buffer.data, "word");
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlW, DeleteWithNewlines)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "line1\nline2 word", 16);

	tprompt_key_handle_ctrl_w(handle);

	// Should delete "word"
	EXPECT_STREQ(handle->buffer.data, "line1\nline2 ");

	tprompt_close(handle);
}

/* ========================================================================
 * Ctrl+K Tests (Delete to End of Line)
 * ======================================================================== */

TEST(KeybindingsCtrlK, DeleteToEndOfLine)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "hello world", 11);
	handle->buffer.cursor = 6; // Position after "hello "

	tprompt_key_handle_ctrl_k(handle);

	EXPECT_STREQ(handle->buffer.data, "hello ");
	EXPECT_EQ(handle->buffer.length, 6);
	EXPECT_EQ(handle->buffer.cursor, 6);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlK, DeleteToEndMultiline)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "line1\nline2\nline3", 17);
	handle->buffer.cursor = 6; // Position at start of "line2"

	tprompt_key_handle_ctrl_k(handle);

	// Should only delete "line2" (up to newline)
	EXPECT_STREQ(handle->buffer.data, "line1\n\nline3");
	EXPECT_EQ(handle->buffer.cursor, 6);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlK, DeleteAtEndOfLine)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "test\n", 5);
	handle->buffer.cursor = 4; // Position before newline

	tprompt_key_handle_ctrl_k(handle);

	// Should delete nothing (already at end of logical line)
	EXPECT_STREQ(handle->buffer.data, "test\n");
	EXPECT_EQ(handle->buffer.cursor, 4);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlK, DeleteEntireLine)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "delete this", 11);
	handle->buffer.cursor = 0;

	tprompt_key_handle_ctrl_k(handle);

	EXPECT_STREQ(handle->buffer.data, "");
	EXPECT_EQ(handle->buffer.length, 0);
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_close(handle);
}

/* ========================================================================
 * Ctrl+U Tests (Delete to Start of Line)
 * ======================================================================== */

TEST(KeybindingsCtrlU, DeleteToStartOfLine)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "hello world", 11);

	tprompt_key_handle_ctrl_u(handle);

	EXPECT_STREQ(handle->buffer.data, "");
	EXPECT_EQ(handle->buffer.length, 0);
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlU, DeleteToStartMiddle)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "hello world", 11);
	handle->buffer.cursor = 6; // After "hello "

	tprompt_key_handle_ctrl_u(handle);

	EXPECT_STREQ(handle->buffer.data, "world");
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlU, DeleteToStartMultiline)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "line1\nline2\nline3", 17);
	handle->buffer.cursor = 12; // In middle of "line2"

	tprompt_key_handle_ctrl_u(handle);

	// Should only delete from start of line2
	EXPECT_STREQ(handle->buffer.data, "line1\nline3");
	EXPECT_EQ(handle->buffer.cursor, 6);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlU, DeleteAtStartOfLine)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "test", 4);
	handle->buffer.cursor = 0;

	tprompt_key_handle_ctrl_u(handle);

	// Should do nothing
	EXPECT_STREQ(handle->buffer.data, "test");
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_close(handle);
}

/* ========================================================================
 * Ctrl+A Tests (Move to Start of Line)
 * ======================================================================== */

TEST(KeybindingsCtrlA, MoveToStartSingleLine)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "hello world", 11);

	tprompt_key_handle_ctrl_a(handle);

	EXPECT_EQ(handle->buffer.cursor, 0);
	EXPECT_STREQ(handle->buffer.data, "hello world"); // Content unchanged

	tprompt_close(handle);
}

TEST(KeybindingsCtrlA, MoveToStartMultiline)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "line1\nline2\nline3", 17);
	handle->buffer.cursor = 12; // In middle of line2

	tprompt_key_handle_ctrl_a(handle);

	// Should move to start of line2
	EXPECT_EQ(handle->buffer.cursor, 6); // After first \n

	tprompt_close(handle);
}

TEST(KeybindingsCtrlA, MoveToStartAlreadyAtStart)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "test", 4);
	handle->buffer.cursor = 0;

	tprompt_key_handle_ctrl_a(handle);

	// Should stay at 0
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_close(handle);
}

/* ========================================================================
 * Ctrl+E Tests (Move to End of Line)
 * ======================================================================== */

TEST(KeybindingsCtrlE, MoveToEndSingleLine)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "hello world", 11);
	handle->buffer.cursor = 0;

	tprompt_key_handle_ctrl_e(handle);

	EXPECT_EQ(handle->buffer.cursor, 11);
	EXPECT_STREQ(handle->buffer.data, "hello world");

	tprompt_close(handle);
}

TEST(KeybindingsCtrlE, MoveToEndMultiline)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "line1\nline2\nline3", 17);
	handle->buffer.cursor = 6; // Start of line2

	tprompt_key_handle_ctrl_e(handle);

	// Should move to end of line2 (before \n)
	EXPECT_EQ(handle->buffer.cursor, 11); // After "line2"

	tprompt_close(handle);
}

TEST(KeybindingsCtrlE, MoveToEndAlreadyAtEnd)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "test", 4);

	tprompt_key_handle_ctrl_e(handle);

	// Should stay at end
	EXPECT_EQ(handle->buffer.cursor, 4);

	tprompt_close(handle);
}

/* ========================================================================
 * Combined Tests
 * ======================================================================== */

TEST(KeybindingsCombined, CtrlKThenCtrlU)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "hello world test", 16);
	handle->buffer.cursor = 6; // After "hello "

	// Delete to end
	tprompt_key_handle_ctrl_k(handle);
	EXPECT_STREQ(handle->buffer.data, "hello ");

	// Delete to start
	tprompt_key_handle_ctrl_u(handle);
	EXPECT_STREQ(handle->buffer.data, "");

	tprompt_close(handle);
}

TEST(KeybindingsCombined, CtrlAThenCtrlE)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "test line", 9);
	handle->buffer.cursor = 5; // Middle

	tprompt_key_handle_ctrl_a(handle);
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_key_handle_ctrl_e(handle);
	EXPECT_EQ(handle->buffer.cursor, 9);

	tprompt_close(handle);
}

TEST(KeybindingsCombined, CtrlWMultipleWordsUTF8)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "hello 世界 test", 16);

	// Delete "test"
	tprompt_key_handle_ctrl_w(handle);
	EXPECT_STREQ(handle->buffer.data, "hello 世界 ");

	// Delete "世界"
	tprompt_key_handle_ctrl_w(handle);
	EXPECT_STREQ(handle->buffer.data, "hello ");

	tprompt_close(handle);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
