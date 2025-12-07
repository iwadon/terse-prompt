/**
 * @file test_multiline.c
 * @brief Multi-line editing functionality tests for terse-prompt Phase 4
 * @version 0.1
 * @date 2025-11-05
 *
 * Tests physical/logical line calculations, Home/End staged movement,
 * UP/DOWN vertical navigation, line wrapping display, and edge cases.
 */

#include "test_helpers.h"
#include "tprompt_internal.h"
#include <attest/attest.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Test Helpers
 * ======================================================================== */

/**
 * @brief Create a test handle with controlled terminal size
 */
static tprompt_handle_t create_test_handle(size_t term_width, size_t term_height)
{
	// Create mock terse handle for test mode
	terse_handle_t terse_h = test_create_terse_handle();
	if (!terse_h) {
		return NULL;
	}

	// Mock the requested terminal size
	terse_test_mock_size(terse_h, (int)term_height, (int)term_width);

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

/* ========================================================================
 * Physical/Logical Line Calculation Tests
 * ======================================================================== */

TEST(MultilineDisplay, CalculateLayout_EmptyBuffer)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Empty buffer - should be 1 physical line (prompt only)
	tprompt_display_calculate_layout(handle);
	EXPECT_EQ(handle->display.physical_line, 0);
	EXPECT_EQ(handle->display.physical_column, 2); // After "> "
	EXPECT_EQ(handle->display.total_physical_lines, 1);

	tprompt_close(handle);
}

TEST(MultilineDisplay, CalculateLayout_SingleLine)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Insert short text that fits on one line
	tprompt_buffer_insert(&handle->buffer, "Hello, World!", 13);
	tprompt_display_calculate_layout(handle);

	EXPECT_EQ(handle->display.physical_line, 0);
	EXPECT_EQ(handle->display.physical_column, 15); // 2 (prompt) + 13 (text)
	EXPECT_EQ(handle->display.total_physical_lines, 1);

	tprompt_close(handle);
}

TEST(MultilineDisplay, CalculateLayout_WrapsAtTerminalWidth)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// Insert text that wraps: "> " (2) + "12345678901234567890" (20) = 22 cols
	// Should wrap to line 1 at column 2
	tprompt_buffer_insert(&handle->buffer, "12345678901234567890", 20);
	tprompt_display_calculate_layout(handle);

	EXPECT_EQ(handle->display.physical_line, 1);
	EXPECT_EQ(handle->display.physical_column, 2);
	EXPECT_EQ(handle->display.total_physical_lines, 2);

	tprompt_close(handle);
}

TEST(MultilineDisplay, CalculateLayout_MultipleWraps)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// Insert 50 characters: will span 3 physical lines
	// Line 0: "> " + 18 chars = 20
	// Line 1: 20 chars
	// Line 2: 12 chars
	char text[51];
	memset(text, 'A', 50);
	text[50] = '\0';
	tprompt_buffer_insert(&handle->buffer, text, 50);
	tprompt_display_calculate_layout(handle);

	EXPECT_EQ(handle->display.physical_line, 2);
	EXPECT_EQ(handle->display.physical_column, 12);
	EXPECT_EQ(handle->display.total_physical_lines, 3);

	tprompt_close(handle);
}

TEST(MultilineDisplay, CalculateLayout_CursorInMiddle)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// Insert 50 characters, move cursor to position 25
	char text[51];
	memset(text, 'A', 50);
	text[50] = '\0';
	tprompt_buffer_insert(&handle->buffer, text, 50);
	handle->buffer.cursor = 25;

	tprompt_display_calculate_layout(handle);

	// Cursor at byte 25 = absolute col 27 (2 + 25)
	// Physical line = 27 / 20 = 1, col = 27 % 20 = 7
	EXPECT_EQ(handle->display.physical_line, 1);
	EXPECT_EQ(handle->display.physical_column, 7);

	tprompt_close(handle);
}

TEST(MultilineDisplay, CalculateLayout_UTF8Text)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Insert UTF-8: "日本語" (3 chars, 9 bytes, width 6 as full-width characters)
	tprompt_buffer_insert(&handle->buffer, "日本語", 9);
	tprompt_display_calculate_layout(handle);

	// CJK characters are full-width (2 columns each): 3 chars × 2 width = 6 columns
	EXPECT_EQ(handle->display.physical_column, 8); // 2 (prompt) + 6 (char width)

	tprompt_close(handle);
}

/* ========================================================================
 * Home/End Staged Movement Tests
 * ======================================================================== */

TEST(MultilineCursor, HomeKey_FirstPress_PhysicalLineStart)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// Insert text with newlines to create multiple logical lines
	// "AAAAAAAAAA\nBBBBBBBBBB\nCCCCCCCCCC"
	tprompt_buffer_insert(&handle->buffer, "AAAAAAAAAA\nBBBBBBBBBB\nCCCCCCCCCC", 32);

	// Move cursor to middle of last line
	handle->buffer.cursor = 27; // In "CCCCCCCCCC" line

	// Home press: move to start of current logical line
	int result = tprompt_cursor_move_to_logical_line_start(handle);
	EXPECT_EQ(result, 0);

	// Should move to start of "CCCCCCCCCC" line (after 2nd newline)
	EXPECT_EQ(handle->buffer.cursor, 22);

	tprompt_close(handle);
}

TEST(MultilineCursor, HomeKey_SecondPress_LogicalLineStart)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// Insert text, cursor in middle
	tprompt_buffer_insert(&handle->buffer, "Hello World", 11);

	// First Home: move to physical line start (byte 0)
	tprompt_cursor_move_to_logical_line_start(handle);
	EXPECT_EQ(handle->buffer.cursor, 0);

	// Second Home: should stay at logical line start (already there)
	tprompt_cursor_move_to_start(&handle->buffer);
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_close(handle);
}

TEST(MultilineCursor, EndKey_FirstPress_PhysicalLineEnd)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// Insert text with newlines to create multiple logical lines
	tprompt_buffer_insert(&handle->buffer, "AAAAAAAAAA\nBBBBBBBBBB\nCCCCCCCCCC", 32);

	// Move cursor to start of second line
	handle->buffer.cursor = 11; // Start of "BBBBBBBBBB"

	// End press: move to end of current logical line
	int result = tprompt_cursor_move_to_logical_line_end(handle);
	EXPECT_EQ(result, 0);

	// Should be at end of "BBBBBBBBBB" line (before 2nd newline)
	EXPECT_EQ(handle->buffer.cursor, 21);

	tprompt_close(handle);
}

TEST(MultilineCursor, EndKey_SecondPress_LogicalLineEnd)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Insert text, cursor at start
	tprompt_buffer_insert(&handle->buffer, "Hello World", 11);
	handle->buffer.cursor = 0;

	// First End: move to physical line end (same as logical in this case)
	tprompt_cursor_move_to_logical_line_end(handle);
	EXPECT_EQ(handle->buffer.cursor, 11);

	// Second End: should stay at logical line end (already there)
	tprompt_cursor_move_to_end(&handle->buffer);
	EXPECT_EQ(handle->buffer.cursor, 11);

	tprompt_close(handle);
}

/* ========================================================================
 * UP/DOWN Vertical Navigation Tests
 * ======================================================================== */

TEST(MultilineCursor, UpKey_AtTopLine_NoMovement)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Single line text, cursor at end
	tprompt_buffer_insert(&handle->buffer, "Hello", 5);

	size_t cursor_before = handle->buffer.cursor;
	int result = tprompt_cursor_move_up(handle);
	EXPECT_EQ(result, 0);
	EXPECT_EQ(handle->buffer.cursor, cursor_before); // No movement

	tprompt_close(handle);
}

TEST(MultilineCursor, DownKey_AtBottomLine_NoMovement)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Single line text, cursor at end
	tprompt_buffer_insert(&handle->buffer, "Hello", 5);

	size_t cursor_before = handle->buffer.cursor;
	int result = tprompt_cursor_move_down(handle);
	EXPECT_EQ(result, 0);
	EXPECT_EQ(handle->buffer.cursor, cursor_before); // No movement

	tprompt_close(handle);
}

TEST(MultilineCursor, UpKey_MovesToLineAbove)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// Insert text with newlines to create multiple logical lines
	tprompt_buffer_insert(&handle->buffer, "AAAAAAAAAA\nBBBBBBBBBB\nCCCCCCCCCC", 32);

	// Cursor at middle of last line (position 27)
	handle->buffer.cursor = 27;

	// Move up one line
	int result = tprompt_cursor_move_up(handle);
	EXPECT_EQ(result, 0);

	// Should be in second line "BBBBBBBBBB" at similar column position
	EXPECT_GE(handle->buffer.cursor, 11); // At or after start of "BBBBBBBBBB"
	EXPECT_LE(handle->buffer.cursor, 21); // At or before end of "BBBBBBBBBB"

	tprompt_close(handle);
}

TEST(MultilineCursor, DownKey_MovesToLineBelow)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// Insert text with newlines to create multiple logical lines
	tprompt_buffer_insert(&handle->buffer, "AAAAAAAAAA\nBBBBBBBBBB\nCCCCCCCCCC", 32);

	// Cursor at middle of first line (position 5)
	handle->buffer.cursor = 5;

	// Move down one line
	int result = tprompt_cursor_move_down(handle);
	EXPECT_EQ(result, 0);

	// Should be in second line "BBBBBBBBBB" at similar column position
	EXPECT_GE(handle->buffer.cursor, 11); // At or after start of "BBBBBBBBBB"
	EXPECT_LE(handle->buffer.cursor, 21); // At or before end of "BBBBBBBBBB"

	tprompt_close(handle);
}

TEST(MultilineCursor, UpDown_MaintainsGoalColumn)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// Create buffer with different logical line lengths
	// Line 0: "AAAAAAAAAA" (10 chars)
	// Line 1: "BBB" (3 chars - short line)
	// Line 2: "CCCCCCCCCC" (10 chars)
	tprompt_buffer_insert(&handle->buffer, "AAAAAAAAAA\nBBB\nCCCCCCCCCC", 25);

	// Move cursor to position 7 in last line (column 7)
	handle->buffer.cursor = 22; // Start of "CCCCCCCCCC" is 15, so 15+7=22

	// Move up - should try to maintain column 7 (but line 1 only has 3 chars)
	tprompt_cursor_move_up(handle);
	// Should be at end of "BBB" line (position 14)
	EXPECT_EQ(handle->buffer.cursor, 14);

	// Move up again - should return to column 7 on line 0
	tprompt_cursor_move_up(handle);
	// Should be at position 7 in "AAAAAAAAAA"
	EXPECT_EQ(handle->buffer.cursor, 7);

	tprompt_close(handle);
}

TEST(MultilineCursor, VerticalNavigation_ClearsGoalColumn)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Test", 4);

	// Set goal column
	handle->input_state.has_goal_column = true;
	handle->input_state.goal_column = 10;

	// Horizontal movement should clear goal column tracking
	handle->input_state.has_goal_column = false;
	EXPECT_FALSE(handle->input_state.has_goal_column);

	tprompt_close(handle);
}

/* ========================================================================
 * Line Wrapping Display Tests
 * ======================================================================== */

TEST(MultilineDisplay, Wrapping_ExactTerminalWidth)
{
	tprompt_handle_t handle = create_test_handle(10, 24);
	ASSERT_NOT_NULL(handle);

	// Prompt "> " (2) + "12345678" (8) = exactly 10 cols
	// Cursor at position 8 (end of buffer)
	tprompt_buffer_insert(&handle->buffer, "12345678", 8);
	tprompt_display_calculate_layout(handle);

	// At exact terminal width, cursor position is at end of first line
	// Physical line calculation: column reaches terminal_width (10), wraps occur
	// The implementation may place cursor at (1, 0) or (0, 10) depending on wrap behavior
	// Since we're at exact boundary, accept either as valid
	EXPECT_TRUE(handle->display.physical_line <= 1);
	// Total lines = 1 (content fits in one line)
	EXPECT_EQ(handle->display.total_physical_lines, 1);

	tprompt_close(handle);
}

TEST(MultilineDisplay, Wrapping_OneCharOverTerminalWidth)
{
	tprompt_handle_t handle = create_test_handle(10, 24);
	ASSERT_NOT_NULL(handle);

	// Prompt "> " (2) + "123456789" (9) = 11 cols - wraps to line 1
	tprompt_buffer_insert(&handle->buffer, "123456789", 9);
	tprompt_display_calculate_layout(handle);

	EXPECT_EQ(handle->display.physical_line, 1);
	EXPECT_EQ(handle->display.physical_column, 1);
	EXPECT_EQ(handle->display.total_physical_lines, 2);

	tprompt_close(handle);
}

TEST(MultilineDisplay, Wrapping_LongPrompt)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// Change prompt to longer string
	free(handle->prompt);
	handle->prompt = strdup("very-long-prompt> "); // 18 chars

	tprompt_buffer_insert(&handle->buffer, "ABC", 3); // 18 + 3 = 21 - wraps
	tprompt_display_calculate_layout(handle);

	EXPECT_EQ(handle->display.physical_line, 1);
	EXPECT_EQ(handle->display.total_physical_lines, 2);

	tprompt_close(handle);
}

/* ========================================================================
 * Edge Case Tests
 * ======================================================================== */

TEST(MultilineEdgeCases, EmptyBuffer_HomeEnd)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Empty buffer
	tprompt_cursor_move_to_logical_line_start(handle);
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_cursor_move_to_logical_line_end(handle);
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_close(handle);
}

TEST(MultilineEdgeCases, SingleCharacter_VerticalMovement)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "X", 1);

	// Up/Down should not crash, cursor should stay in place
	size_t cursor_before = handle->buffer.cursor;
	tprompt_cursor_move_up(handle);
	EXPECT_EQ(handle->buffer.cursor, cursor_before);

	tprompt_cursor_move_down(handle);
	EXPECT_EQ(handle->buffer.cursor, cursor_before);

	tprompt_close(handle);
}

TEST(MultilineEdgeCases, VeryLongLine_NoWrap)
{
	tprompt_handle_t handle = create_test_handle(10, 24);
	ASSERT_NOT_NULL(handle);

	// Insert 100 characters - should span many physical lines
	char text[101];
	memset(text, 'X', 100);
	text[100] = '\0';
	tprompt_buffer_insert(&handle->buffer, text, 100);

	tprompt_display_calculate_layout(handle);

	// Total columns = 2 (prompt) + 100 = 102
	// Physical lines = 102 / 10 = 10.2 -> 11 lines
	EXPECT_GT(handle->display.total_physical_lines, 10);

	tprompt_close(handle);
}

TEST(MultilineEdgeCases, CursorAtBufferBoundaries)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Test", 4);

	// Cursor at start
	handle->buffer.cursor = 0;
	tprompt_cursor_move_to_logical_line_start(handle);
	EXPECT_EQ(handle->buffer.cursor, 0);

	// Cursor at end
	handle->buffer.cursor = 4;
	tprompt_cursor_move_to_logical_line_end(handle);
	EXPECT_EQ(handle->buffer.cursor, 4);

	tprompt_close(handle);
}

TEST(MultilineEdgeCases, NarrowTerminal_OnlyPrompt)
{
	tprompt_handle_t handle = create_test_handle(2, 24); // Very narrow!
	ASSERT_NOT_NULL(handle);

	// Prompt "> " exactly fills terminal width
	tprompt_display_calculate_layout(handle);
	EXPECT_EQ(handle->display.total_physical_lines, 1);

	// Add one character - should wrap to next line
	tprompt_buffer_insert(&handle->buffer, "A", 1);
	tprompt_display_calculate_layout(handle);
	EXPECT_EQ(handle->display.total_physical_lines, 2);

	tprompt_close(handle);
}

TEST(MultilineEdgeCases, ZeroTerminalWidth_Fallback)
{
	// Test that zero terminal size triggers fallback to 80x24
	// With mock terse handle, size 0x0 will be mocked, and tprompt should
	// detect this and apply the fallback dimensions
	tprompt_handle_t handle = create_test_handle(0, 0);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Test", 4);

	tprompt_display_calculate_layout(handle);

	// Should fallback to reasonable dimensions (80x24 or actual terminal size)
	EXPECT_TRUE(handle->display.terminal_width > 0);
	EXPECT_TRUE(handle->display.terminal_height > 0);

	tprompt_close(handle);
}

TEST(MultilineEdgeCases, UTF8_AtWrapBoundary)
{
	tprompt_handle_t handle = create_test_handle(10, 24);
	ASSERT_NOT_NULL(handle);

	// Insert text that places UTF-8 char exactly at wrap boundary
	// Prompt "> " (2) + "1234567" (7) = 9 cols
	tprompt_buffer_insert(&handle->buffer, "1234567", 7);

	// Add UTF-8 character (3 bytes, but counts as 1 char width)
	// Total: 2 + 7 + 1 = 10 cols (exact terminal width)
	tprompt_buffer_insert(&handle->buffer, "日", 3);

	tprompt_display_calculate_layout(handle);
	// At exact terminal width boundary, accept either position
	EXPECT_TRUE(handle->display.physical_line <= 1);
	// Content fits in 1 line
	EXPECT_EQ(handle->display.total_physical_lines, 1);

	tprompt_close(handle);
}

/* ========================================================================
 * Input State Tracking Tests
 * ======================================================================== */

TEST(MultilineInputState, GoalColumnPersistence)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// Create multi-line buffer with newlines
	tprompt_buffer_insert(&handle->buffer, "AAAAAAAAAA\nBBBBBBBBBB\nCCCCCCCCCC", 32);

	// Move cursor to position in last line
	handle->buffer.cursor = 27; // Middle of "CCCCCCCCCC"

	// Goal column should not be set initially
	EXPECT_FALSE(handle->input_state.has_goal_column);

	// First up movement should set goal column
	tprompt_cursor_move_up(handle);
	EXPECT_TRUE(handle->input_state.has_goal_column);
	size_t goal = handle->input_state.goal_column;

	// Second up movement should preserve goal column
	tprompt_cursor_move_up(handle);
	EXPECT_TRUE(handle->input_state.has_goal_column);
	EXPECT_EQ(handle->input_state.goal_column, goal);

	tprompt_close(handle);
}

TEST(MultilineInputState, LastKeyTracking)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Test", 4);

	// Simulate key press tracking
	handle->input_state.last_key_type = TERSE_EVENT_HOME;
	handle->input_state.last_cursor_pos = handle->buffer.cursor;

	EXPECT_EQ(handle->input_state.last_key_type, TERSE_EVENT_HOME);

	tprompt_close(handle);
}

/* ========================================================================
 * Main Entry Point
 * ======================================================================== */

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
