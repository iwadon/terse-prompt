/**
 * @file test_keybindings.c
 * @brief Tests for Phase 6 keybindings (Ctrl+W, Ctrl+K, Ctrl+U, Ctrl+A, Ctrl+E)
 * @version 0.1
 * @date 2025-11-05
 *
 * Tests the five additional keybindings added in Phase 6.
 */

#include "test_helpers.h"
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
	// Create mock terse handle for test mode
	terse_handle_t terse_h = test_create_terse_handle();
	if (!terse_h) {
		return NULL;
	}

	tprompt_options_t opts = {
		.prompt = "> ",
		.history_file = NULL,
		.max_input_size = 1024,
		.max_history_size = 100,
		.completion_callback = NULL,
		.completion_user_data = NULL,
		.completion_prefixes = NULL,
		.terse_handle = terse_h,  // Use mock terse handle
		.flags = TPROMPT_FLAG_MULTILINE
	};

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		terse_close(terse_h);
		return NULL;
	}

	return handle;
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
 * Custom Keybinding Tests (Regression tests for custom Enter behavior)
 * ======================================================================== */

/**
 * @brief Create a test handle with custom keybindings
 * @param bindings Array of custom keybindings
 * @param count Number of bindings
 * @return Prompt handle, or NULL on failure
 */
static tprompt_handle_t create_test_handle_with_keybindings(
	const tprompt_keybinding_t *bindings,
	size_t count)
{
	// Create mock terse handle for test mode
	terse_handle_t terse_h = test_create_terse_handle();
	if (!terse_h) {
		return NULL;
	}

	tprompt_options_t opts = {
		.prompt = "> ",
		.history_file = NULL,
		.max_input_size = 1024,
		.max_history_size = 100,
		.completion_callback = NULL,
		.completion_user_data = NULL,
		.completion_prefixes = NULL,
		.terse_handle = terse_h,  // Use mock terse handle
		.flags = TPROMPT_FLAG_MULTILINE,
		.custom_keybindings = (tprompt_keybinding_t *)bindings,
		.keybinding_count = count
	};

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		terse_close(terse_h);
		return NULL;
	}

	return handle;
}

/**
 * @brief Execute a custom key event through the full event handling pipeline
 * @param handle Prompt handle
 * @param event Event to execute
 * @return Result from tprompt_handle_key_event
 */
static int execute_key_event(tprompt_handle_t handle, const terse_event_t *event)
{
	return tprompt_handle_key_event(handle, event);
}

TEST(CustomKeybindings, EnterConfirmsInMultilineMode)
{
	// Regression test: Custom keybinding to confirm with Enter in multiline mode
	// Previously, Enter would insert newline regardless of custom keybinding

	// Define custom keybinding: Enter = confirm
	tprompt_keybinding_t custom_bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_test_handle_with_keybindings(custom_bindings, 1);
	ASSERT_NE(handle, NULL);

	// Insert some text
	tprompt_buffer_insert(&handle->buffer, "test input", 10);

	// Simulate Enter key (no modifiers)
	terse_event_t enter_event = {
		.type = TERSE_EVENT_ENTER,
		.data = {
			.key = {
				.mods = 0
			}
		}
	};

	// Execute Enter key event
	int result = execute_key_event(handle, &enter_event);

	// Should set pending_confirmation and force_confirmation
	EXPECT_EQ(result, 0); // Continue editing (confirmation pending)
	EXPECT_TRUE(handle->pending_confirmation);
	EXPECT_TRUE(handle->force_confirmation);

	// Buffer should be unchanged (no newline inserted)
	EXPECT_STREQ(handle->buffer.data, "test input");
	EXPECT_EQ(handle->buffer.length, 10);

	tprompt_close(handle);
}

TEST(CustomKeybindings, ShiftEnterInsertsNewline)
{
	// Test that Shift+Enter inserts newline as configured

	tprompt_keybinding_t custom_bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, TERSE_MOD_SHIFT, TPROMPT_ACTION_INSERT_NEWLINE),
	};

	tprompt_handle_t handle = create_test_handle_with_keybindings(custom_bindings, 2);
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "line1", 5);

	// Simulate Shift+Enter
	terse_event_t shift_enter = {
		.type = TERSE_EVENT_ENTER,
		.data = {
			.key = {
				.mods = TERSE_MOD_SHIFT
			}
		}
	};

	int result = execute_key_event(handle, &shift_enter);

	// Should insert newline and continue editing
	EXPECT_EQ(result, 0);
	EXPECT_STREQ(handle->buffer.data, "line1\n");
	EXPECT_EQ(handle->buffer.length, 6);
	EXPECT_FALSE(handle->pending_confirmation); // No confirmation pending

	tprompt_close(handle);
}

TEST(CustomKeybindings, NoValidationCallbackStillConfirms)
{
	// Regression test: With custom CONFIRM_INPUT binding and no validation callback,
	// input should still be confirmed (not insert newline)
	//
	// Historical note: This test originally failed to detect a bug where force_confirmation
	// was reset BEFORE calling tprompt_handle_pending_confirmation, causing the flag to be
	// lost. The test now explicitly simulates the correct main loop behavior to ensure
	// force_confirmation is checked properly. For full integration testing of the main loop,
	// a separate integration test suite would be needed.

	tprompt_keybinding_t custom_bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_test_handle_with_keybindings(custom_bindings, 1);
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "test", 4);

	// Simulate Enter
	terse_event_t enter_event = {
		.type = TERSE_EVENT_ENTER,
		.data = {
			.key = {
				.mods = 0
			}
		}
	};

	execute_key_event(handle, &enter_event);

	// Should have pending_confirmation and force_confirmation set
	EXPECT_TRUE(handle->pending_confirmation);
	EXPECT_TRUE(handle->force_confirmation);

	// Simulate the main loop's confirmation handling logic
	// CRITICAL: Must match exact order in tprompt_readline (src/tprompt.c:2486-2505)
	// This test verifies that force_confirmation works correctly with the main loop's reset logic
	if (handle->pending_confirmation) {
		// Save force_confirmation state before any resets (for assertion)
		bool force_was_set = handle->force_confirmation;
		EXPECT_TRUE(force_was_set); // Verify it was set by custom keybinding

		handle->pending_confirmation = false; // Reset flag (main loop does this)
		// NOTE: Main loop should NOT reset force_confirmation here (that's the bug we're testing for)
		// The correct behavior is to reset AFTER calling tprompt_handle_pending_confirmation

		bool should_break = false;
		int result = tprompt_handle_pending_confirmation(handle, &should_break);
		handle->force_confirmation = false; // Reset after handling (correct behavior)

		// Should confirm input (break=true), not insert newline
		EXPECT_EQ(result, 0);
		EXPECT_TRUE(should_break);
		EXPECT_STREQ(handle->buffer.data, "test"); // No newline added
	}

	tprompt_close(handle);
}

TEST(CustomKeybindings, DefaultBehaviorWithoutCustomBinding)
{
	// Test that without custom keybinding, default behavior applies
	// (multiline mode: Enter inserts newline when no validation callback)

	// Use existing helper without custom keybindings
	tprompt_handle_t handle = create_test_handle();
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "test", 4);

	// Simulate Enter
	terse_event_t enter_event = {
		.type = TERSE_EVENT_ENTER,
		.data = {
			.key = {
				.mods = 0
			}
		}
	};

	execute_key_event(handle, &enter_event);

	// Should have pending_confirmation but NOT force_confirmation
	EXPECT_TRUE(handle->pending_confirmation);
	EXPECT_FALSE(handle->force_confirmation);

	// Simulate pending confirmation handling
	bool should_break = false;
	int result = tprompt_handle_pending_confirmation(handle, &should_break);

	// Default multiline behavior: insert newline
	EXPECT_EQ(result, 0);
	EXPECT_FALSE(should_break); // Continue editing
	EXPECT_STREQ(handle->buffer.data, "test\n"); // Newline added

	tprompt_close(handle);
}

TEST(CustomKeybindings, CtrlJInsertsNewline)
{
	// Test Ctrl+J custom binding (character-based keybinding)

	tprompt_keybinding_t custom_bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
		TPROMPT_BIND_CHAR('j', TERSE_MOD_CTRL, TPROMPT_ACTION_INSERT_NEWLINE),
	};

	tprompt_handle_t handle = create_test_handle_with_keybindings(custom_bindings, 2);
	ASSERT_NE(handle, NULL);

	tprompt_buffer_insert(&handle->buffer, "line1", 5);

	// Simulate Ctrl+J (CHAR event with scalar='j' and CTRL modifier)
	terse_event_t ctrl_j = {
		.type = TERSE_EVENT_CHAR,
		.data = {
			.ch = {
				.scalar = 'j',
				.mods = TERSE_MOD_CTRL,
				.width = 1
			}
		}
	};

	int result = execute_key_event(handle, &ctrl_j);

	// Should insert newline
	EXPECT_EQ(result, 0);
	EXPECT_STREQ(handle->buffer.data, "line1\n");
	EXPECT_FALSE(handle->pending_confirmation);

	tprompt_close(handle);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
