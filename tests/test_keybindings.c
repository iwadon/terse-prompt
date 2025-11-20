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

/**
 * @brief Execute a Ctrl+character keybinding
 * @param handle Prompt handle
 * @param ch Character (lowercase letter)
 */
static void execute_ctrl_char(tprompt_handle_t handle, char ch)
{
	terse_event_t event = {
		.type = TERSE_EVENT_CHAR,
		.data = {
			.ch = {
				.scalar = (unsigned int)ch,
				.mods = TERSE_MOD_CTRL,
				.width = 1
			}
		}
	};

	tprompt_action_t action = tprompt_resolve_action(handle, &event);
	tprompt_execute_action(handle, action, &event);
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
	execute_ctrl_char(handle, 'w');

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

	execute_ctrl_char(handle, 'w');

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
	execute_ctrl_char(handle, 'w');
	EXPECT_STREQ(handle->buffer.data, "one two ");

	// Delete "two"
	execute_ctrl_char(handle, 'w');
	EXPECT_STREQ(handle->buffer.data, "one ");

	// Delete "one"
	execute_ctrl_char(handle, 'w');
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

	execute_ctrl_char(handle, 'w');

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

	execute_ctrl_char(handle, 'w');

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

	execute_ctrl_char(handle, 'k');

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

	execute_ctrl_char(handle, 'k');

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

	execute_ctrl_char(handle, 'k');

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

	execute_ctrl_char(handle, 'k');

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

	execute_ctrl_char(handle, 'u');

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

	execute_ctrl_char(handle, 'u');

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

	execute_ctrl_char(handle, 'u');

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

	execute_ctrl_char(handle, 'u');

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

	execute_ctrl_char(handle, 'a');

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

	execute_ctrl_char(handle, 'a');

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

	execute_ctrl_char(handle, 'a');

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

	execute_ctrl_char(handle, 'e');

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

	execute_ctrl_char(handle, 'e');

	// Should move to end of line2 (before \n)
	EXPECT_EQ(handle->buffer.cursor, 11); // After "line2"

	tprompt_close(handle);
}

TEST(KeybindingsCtrlE, MoveToEndAlreadyAtEnd)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "test", 4);

	execute_ctrl_char(handle, 'e');

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
	execute_ctrl_char(handle, 'k');
	EXPECT_STREQ(handle->buffer.data, "hello ");

	// Delete to start
	execute_ctrl_char(handle, 'u');
	EXPECT_STREQ(handle->buffer.data, "");

	tprompt_close(handle);
}

TEST(KeybindingsCombined, CtrlAThenCtrlE)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "test line", 9);
	handle->buffer.cursor = 5; // Middle

	execute_ctrl_char(handle, 'a');
	EXPECT_EQ(handle->buffer.cursor, 0);

	execute_ctrl_char(handle, 'e');
	EXPECT_EQ(handle->buffer.cursor, 9);

	tprompt_close(handle);
}

TEST(KeybindingsCombined, CtrlWMultipleWordsUTF8)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "hello 世界 test", 16);

	// Delete "test"
	execute_ctrl_char(handle, 'w');
	EXPECT_STREQ(handle->buffer.data, "hello 世界 ");

	// Delete "世界"
	execute_ctrl_char(handle, 'w');
	EXPECT_STREQ(handle->buffer.data, "hello ");

	tprompt_close(handle);
}

/* ========================================================================
 * Ctrl+D Tests (Delete Char / EOF)
 * ======================================================================== */

/**
 * @brief Execute Ctrl+D and return whether it signaled EOF
 * @param handle Prompt handle
 * @return true if EOF signaled, false if delete-char performed
 */
static bool execute_ctrl_d(tprompt_handle_t handle)
{
	terse_event_t event = {
		.type = TERSE_EVENT_CHAR,
		.data = {
			.ch = {
				.scalar = 'd',
				.mods = TERSE_MOD_CTRL,
				.width = 1
			}
		}
	};

	bool should_break = false;
	int result = tprompt_handle_char_event(handle, &event, &should_break);

	// result should be 0 (success), should_break indicates EOF
	(void)result; // Suppress unused warning
	return should_break;
}

TEST(KeybindingsCtrlD, DeleteCharAtCursor)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	// Insert "hello" and position cursor at 'e' (index 1)
	tprompt_buffer_insert(&handle->buffer, "hello", 5);
	handle->buffer.cursor = 1;

	bool eof = execute_ctrl_d(handle);

	// Should delete 'e', not signal EOF
	EXPECT_FALSE(eof);
	EXPECT_STREQ(handle->buffer.data, "hllo");
	EXPECT_EQ(handle->buffer.length, 4);
	EXPECT_EQ(handle->buffer.cursor, 1); // Cursor stays at same position

	tprompt_close(handle);
}

TEST(KeybindingsCtrlD, DeleteCharAtEnd)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	// Insert "test" and position cursor at end
	tprompt_buffer_insert(&handle->buffer, "test", 4);

	bool eof = execute_ctrl_d(handle);

	// Should do nothing (cursor at end, nothing to delete)
	EXPECT_FALSE(eof);
	EXPECT_STREQ(handle->buffer.data, "test");
	EXPECT_EQ(handle->buffer.length, 4);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlD, EOFOnEmptyBuffer)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	// Buffer is empty
	EXPECT_EQ(handle->buffer.length, 0);

	bool eof = execute_ctrl_d(handle);

	// Should signal EOF
	EXPECT_TRUE(eof);
	EXPECT_EQ(handle->buffer.length, 0); // Buffer still empty

	tprompt_close(handle);
}

TEST(KeybindingsCtrlD, DeleteMultipleChars)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	// Insert "abcde"
	tprompt_buffer_insert(&handle->buffer, "abcde", 5);
	handle->buffer.cursor = 0; // Start

	// Delete 'a'
	bool eof1 = execute_ctrl_d(handle);
	EXPECT_FALSE(eof1);
	EXPECT_STREQ(handle->buffer.data, "bcde");

	// Delete 'b'
	bool eof2 = execute_ctrl_d(handle);
	EXPECT_FALSE(eof2);
	EXPECT_STREQ(handle->buffer.data, "cde");

	// Delete 'c'
	bool eof3 = execute_ctrl_d(handle);
	EXPECT_FALSE(eof3);
	EXPECT_STREQ(handle->buffer.data, "de");

	tprompt_close(handle);
}

TEST(KeybindingsCtrlD, DeleteUTF8Character)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	// Insert "hello世界"
	tprompt_buffer_insert(&handle->buffer, "hello世界", 11);
	handle->buffer.cursor = 5; // Before '世'

	bool eof = execute_ctrl_d(handle);

	// Should delete '世' (3 bytes)
	EXPECT_FALSE(eof);
	EXPECT_STREQ(handle->buffer.data, "hello界");
	EXPECT_EQ(handle->buffer.cursor, 5);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlD, DeleteCharThenEOF)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	// Insert single character "x"
	tprompt_buffer_insert(&handle->buffer, "x", 1);
	handle->buffer.cursor = 0;

	// First Ctrl+D: delete 'x'
	bool eof1 = execute_ctrl_d(handle);
	EXPECT_FALSE(eof1);
	EXPECT_STREQ(handle->buffer.data, "");
	EXPECT_EQ(handle->buffer.length, 0);

	// Second Ctrl+D: signal EOF (buffer now empty)
	bool eof2 = execute_ctrl_d(handle);
	EXPECT_TRUE(eof2);

	tprompt_close(handle);
}

TEST(KeybindingsCtrlD, DeleteInMultilineBuffer)
{
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	// Insert multi-line text
	tprompt_buffer_insert(&handle->buffer, "line1\nline2\nline3", 17);
	handle->buffer.cursor = 6; // At 'l' of "line2"

	bool eof = execute_ctrl_d(handle);

	// Should delete 'l', not signal EOF
	EXPECT_FALSE(eof);
	EXPECT_STREQ(handle->buffer.data, "line1\nine2\nline3");
	EXPECT_EQ(handle->buffer.cursor, 6);

	tprompt_close(handle);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
