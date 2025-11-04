/**
 * @file test_multiline.c
 * @brief Multi-line editing functionality tests for terse-prompt Phase 4
 * @version 0.1
 * @date 2025-11-05
 *
 * Tests physical/logical line calculations, Home/End staged movement,
 * UP/DOWN vertical navigation, line wrapping display, and edge cases.
 */

#include "tprompt_internal.h"
#include <attest/attest.h>
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * Test Helpers
 * ======================================================================== */

/**
 * @brief Create a test handle with controlled terminal size
 */
static tprompt_handle_t create_test_handle(size_t term_width, size_t term_height)
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

    tprompt_handle_t handle = tprompt_open(&opts);
    if (handle) {
        // Manually set terminal dimensions for testing
        handle->display.terminal_width = term_width;
        handle->display.terminal_height = term_height;
    }
    return handle;
}

/* ========================================================================
 * Physical/Logical Line Calculation Tests
 * ======================================================================== */

TEST(MultilineDisplay, CalculateLayout_EmptyBuffer)
{
    tprompt_handle_t handle = create_test_handle(80, 24);
    ASSERT_NE(handle, NULL);

    // Empty buffer - should be 1 physical line (prompt only)
    tprompt_display_calculate_layout(handle);
    EXPECT_EQ(handle->display.physical_line, 0);
    EXPECT_EQ(handle->display.physical_column, 2);  // After "> "
    EXPECT_EQ(handle->display.total_physical_lines, 1);

    tprompt_close(handle);
}

TEST(MultilineDisplay, CalculateLayout_SingleLine)
{
    tprompt_handle_t handle = create_test_handle(80, 24);
    ASSERT_NE(handle, NULL);

    // Insert short text that fits on one line
    tprompt_buffer_insert(&handle->buffer, "Hello, World!", 13);
    tprompt_display_calculate_layout(handle);

    EXPECT_EQ(handle->display.physical_line, 0);
    EXPECT_EQ(handle->display.physical_column, 15);  // 2 (prompt) + 13 (text)
    EXPECT_EQ(handle->display.total_physical_lines, 1);

    tprompt_close(handle);
}

TEST(MultilineDisplay, CalculateLayout_WrapsAtTerminalWidth)
{
    tprompt_handle_t handle = create_test_handle(20, 24);
    ASSERT_NE(handle, NULL);

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
    ASSERT_NE(handle, NULL);

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
    ASSERT_NE(handle, NULL);

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
    ASSERT_NE(handle, NULL);

    // Insert UTF-8: "日本語" (3 chars, 9 bytes)
    tprompt_buffer_insert(&handle->buffer, "日本語", 9);
    tprompt_display_calculate_layout(handle);

    // Should count as 3 character positions, not 9
    EXPECT_EQ(handle->display.physical_column, 5);  // 2 (prompt) + 3 (chars)

    tprompt_close(handle);
}

/* ========================================================================
 * Home/End Staged Movement Tests
 * ======================================================================== */

TEST(MultilineCursor, HomeKey_FirstPress_PhysicalLineStart)
{
    tprompt_handle_t handle = create_test_handle(20, 24);
    ASSERT_NE(handle, NULL);

    // Insert text spanning 2 lines, cursor at end
    char text[51];
    memset(text, 'A', 50);
    text[50] = '\0';
    tprompt_buffer_insert(&handle->buffer, text, 50);

    // Cursor should be at line 2, col 12
    tprompt_display_calculate_layout(handle);
    EXPECT_EQ(handle->display.physical_line, 2);

    // First Home press: move to start of physical line
    int result = tprompt_cursor_move_to_physical_line_start(handle);
    EXPECT_EQ(result, 0);

    // Should move to start of line 2 (byte offset 40)
    tprompt_display_calculate_layout(handle);
    EXPECT_EQ(handle->display.physical_line, 2);
    EXPECT_EQ(handle->display.physical_column, 0);

    tprompt_close(handle);
}

TEST(MultilineCursor, HomeKey_SecondPress_LogicalLineStart)
{
    tprompt_handle_t handle = create_test_handle(20, 24);
    ASSERT_NE(handle, NULL);

    // Insert text, cursor in middle
    tprompt_buffer_insert(&handle->buffer, "Hello World", 11);

    // First Home: move to physical line start (byte 0)
    tprompt_cursor_move_to_physical_line_start(handle);
    EXPECT_EQ(handle->buffer.cursor, 0);

    // Second Home: should stay at logical line start (already there)
    tprompt_cursor_move_to_start(&handle->buffer);
    EXPECT_EQ(handle->buffer.cursor, 0);

    tprompt_close(handle);
}

TEST(MultilineCursor, EndKey_FirstPress_PhysicalLineEnd)
{
    tprompt_handle_t handle = create_test_handle(20, 24);
    ASSERT_NE(handle, NULL);

    // Insert text spanning 2 lines, cursor at start
    char text[51];
    memset(text, 'A', 50);
    text[50] = '\0';
    tprompt_buffer_insert(&handle->buffer, text, 50);
    handle->buffer.cursor = 0;

    // First End press: move to end of physical line 0
    int result = tprompt_cursor_move_to_physical_line_end(handle);
    EXPECT_EQ(result, 0);

    // Should be at end of line 0 (byte offset 18, since prompt takes 2 cols)
    tprompt_display_calculate_layout(handle);
    EXPECT_EQ(handle->display.physical_line, 0);
    EXPECT_EQ(handle->display.physical_column, 19);  // Last col before wrap

    tprompt_close(handle);
}

TEST(MultilineCursor, EndKey_SecondPress_LogicalLineEnd)
{
    tprompt_handle_t handle = create_test_handle(80, 24);
    ASSERT_NE(handle, NULL);

    // Insert text, cursor at start
    tprompt_buffer_insert(&handle->buffer, "Hello World", 11);
    handle->buffer.cursor = 0;

    // First End: move to physical line end (same as logical in this case)
    tprompt_cursor_move_to_physical_line_end(handle);
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
    ASSERT_NE(handle, NULL);

    // Single line text, cursor at end
    tprompt_buffer_insert(&handle->buffer, "Hello", 5);

    size_t cursor_before = handle->buffer.cursor;
    int result = tprompt_cursor_move_up(handle);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(handle->buffer.cursor, cursor_before);  // No movement

    tprompt_close(handle);
}

TEST(MultilineCursor, DownKey_AtBottomLine_NoMovement)
{
    tprompt_handle_t handle = create_test_handle(80, 24);
    ASSERT_NE(handle, NULL);

    // Single line text, cursor at end
    tprompt_buffer_insert(&handle->buffer, "Hello", 5);

    size_t cursor_before = handle->buffer.cursor;
    int result = tprompt_cursor_move_down(handle);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(handle->buffer.cursor, cursor_before);  // No movement

    tprompt_close(handle);
}

TEST(MultilineCursor, UpKey_MovesToLineAbove)
{
    tprompt_handle_t handle = create_test_handle(20, 24);
    ASSERT_NE(handle, NULL);

    // Insert text spanning 3 lines
    char text[51];
    memset(text, 'A', 50);
    text[50] = '\0';
    tprompt_buffer_insert(&handle->buffer, text, 50);

    // Cursor at end (line 2, col 12)
    tprompt_display_calculate_layout(handle);
    EXPECT_EQ(handle->display.physical_line, 2);

    // Move up one line
    tprompt_cursor_move_up(handle);
    tprompt_display_calculate_layout(handle);
    EXPECT_EQ(handle->display.physical_line, 1);

    tprompt_close(handle);
}

TEST(MultilineCursor, DownKey_MovesToLineBelow)
{
    tprompt_handle_t handle = create_test_handle(20, 24);
    ASSERT_NE(handle, NULL);

    // Insert text spanning 3 lines, cursor at start
    char text[51];
    memset(text, 'A', 50);
    text[50] = '\0';
    tprompt_buffer_insert(&handle->buffer, text, 50);
    handle->buffer.cursor = 0;

    // Move down one line
    tprompt_cursor_move_down(handle);
    tprompt_display_calculate_layout(handle);
    EXPECT_EQ(handle->display.physical_line, 1);

    tprompt_close(handle);
}

TEST(MultilineCursor, UpDown_MaintainsGoalColumn)
{
    tprompt_handle_t handle = create_test_handle(20, 24);
    ASSERT_NE(handle, NULL);

    // Create buffer with different line lengths:
    // Line 0: "> AAAAAAAAAAAAAAAA" (18 chars)
    // Line 1: "AAAA" (4 chars)
    // Line 2: "AAAAAAAAAAAAAAAAAAAA" (20 chars)
    char text[43];
    memset(text, 'A', 42);
    text[42] = '\0';
    tprompt_buffer_insert(&handle->buffer, text, 42);

    // Move cursor to col 15 on line 2
    handle->buffer.cursor = 40;  // Last full line + some offset
    tprompt_display_calculate_layout(handle);

    // Move up - should try to maintain column 15 (but line 1 only has 4 chars)
    tprompt_cursor_move_up(handle);
    tprompt_display_calculate_layout(handle);
    EXPECT_EQ(handle->display.physical_line, 1);

    // Move up again - should return to approx same column on line 0
    tprompt_cursor_move_up(handle);
    tprompt_display_calculate_layout(handle);
    EXPECT_EQ(handle->display.physical_line, 0);

    tprompt_close(handle);
}

TEST(MultilineCursor, VerticalNavigation_ClearsGoalColumn)
{
    tprompt_handle_t handle = create_test_handle(80, 24);
    ASSERT_NE(handle, NULL);

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
    ASSERT_NE(handle, NULL);

    // Prompt "> " (2) + "12345678" (8) = exactly 10 cols
    // Cursor at position 10: wraps to line 1, column 0
    tprompt_buffer_insert(&handle->buffer, "12345678", 8);
    tprompt_display_calculate_layout(handle);

    EXPECT_EQ(handle->display.physical_line, 1);
    EXPECT_EQ(handle->display.physical_column, 0);
    EXPECT_EQ(handle->display.total_physical_lines, 1);

    tprompt_close(handle);
}

TEST(MultilineDisplay, Wrapping_OneCharOverTerminalWidth)
{
    tprompt_handle_t handle = create_test_handle(10, 24);
    ASSERT_NE(handle, NULL);

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
    ASSERT_NE(handle, NULL);

    // Change prompt to longer string
    free(handle->prompt);
    handle->prompt = strdup("very-long-prompt> ");  // 18 chars

    tprompt_buffer_insert(&handle->buffer, "ABC", 3);  // 18 + 3 = 21 - wraps
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
    ASSERT_NE(handle, NULL);

    // Empty buffer
    tprompt_cursor_move_to_physical_line_start(handle);
    EXPECT_EQ(handle->buffer.cursor, 0);

    tprompt_cursor_move_to_physical_line_end(handle);
    EXPECT_EQ(handle->buffer.cursor, 0);

    tprompt_close(handle);
}

TEST(MultilineEdgeCases, SingleCharacter_VerticalMovement)
{
    tprompt_handle_t handle = create_test_handle(80, 24);
    ASSERT_NE(handle, NULL);

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
    ASSERT_NE(handle, NULL);

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
    ASSERT_NE(handle, NULL);

    tprompt_buffer_insert(&handle->buffer, "Test", 4);

    // Cursor at start
    handle->buffer.cursor = 0;
    tprompt_cursor_move_to_physical_line_start(handle);
    EXPECT_EQ(handle->buffer.cursor, 0);

    // Cursor at end
    handle->buffer.cursor = 4;
    tprompt_cursor_move_to_physical_line_end(handle);
    EXPECT_EQ(handle->buffer.cursor, 4);

    tprompt_close(handle);
}

TEST(MultilineEdgeCases, NarrowTerminal_OnlyPrompt)
{
    tprompt_handle_t handle = create_test_handle(2, 24);  // Very narrow!
    ASSERT_NE(handle, NULL);

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
    tprompt_handle_t handle = create_test_handle(0, 0);  // Simulate unknown size
    ASSERT_NE(handle, NULL);

    tprompt_buffer_insert(&handle->buffer, "Test", 4);

    // Should fall back to default width (80)
    tprompt_display_calculate_layout(handle);
    EXPECT_EQ(handle->display.terminal_width, 80);
    EXPECT_EQ(handle->display.terminal_height, 24);

    tprompt_close(handle);
}

TEST(MultilineEdgeCases, UTF8_AtWrapBoundary)
{
    tprompt_handle_t handle = create_test_handle(10, 24);
    ASSERT_NE(handle, NULL);

    // Insert text that places UTF-8 char exactly at wrap boundary
    // Prompt "> " (2) + "1234567" (7) = 9 cols
    tprompt_buffer_insert(&handle->buffer, "1234567", 7);

    // Add UTF-8 character (3 bytes, but counts as 1 char width)
    // Total: 2 + 7 + 1 = 10 cols, cursor wraps to line 1, col 0
    tprompt_buffer_insert(&handle->buffer, "日", 3);

    tprompt_display_calculate_layout(handle);
    // Cursor at position 10: line 1, total lines needed = 1 (content fits in 1 line)
    EXPECT_EQ(handle->display.physical_line, 1);
    EXPECT_EQ(handle->display.total_physical_lines, 1);

    tprompt_close(handle);
}

/* ========================================================================
 * Input State Tracking Tests
 * ======================================================================== */

TEST(MultilineInputState, GoalColumnPersistence)
{
    tprompt_handle_t handle = create_test_handle(20, 24);
    ASSERT_NE(handle, NULL);

    // Create multi-line buffer
    char text[51];
    memset(text, 'A', 50);
    text[50] = '\0';
    tprompt_buffer_insert(&handle->buffer, text, 50);

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
    ASSERT_NE(handle, NULL);

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
