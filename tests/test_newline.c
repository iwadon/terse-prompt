/**
 * @file test_newline.c
 * @brief Newline character support tests for terse-prompt Phase 5
 * @version 0.1
 * @date 2025-11-05
 *
 * Tests logical line calculations, newline character handling,
 * navigation with newlines (UP/DOWN/Home/End), and Enter vs modifier Enter.
 */

#include "tprompt_internal.h"
#include "test_helpers.h"
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
	// Create mock terse handle with custom size
	terse_handle_t terse_h = test_create_terse_handle_sized((int)term_height, (int)term_width);
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
		.terse_handle = terse_h,
		.flags = TPROMPT_FLAG_MULTILINE
	};

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		terse_close(terse_h);
	}
	return handle;
}

/* ========================================================================
 * Logical Line Calculation Tests
 * ======================================================================== */

TEST(NewlineLogicalLine, CountLines_EmptyBuffer)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Empty buffer - should be 1 logical line
	size_t count = tprompt_count_logical_lines(handle);
	EXPECT_EQ(count, 1);

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, CountLines_NoNewlines)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Hello, World!", 13);
	size_t count = tprompt_count_logical_lines(handle);
	EXPECT_EQ(count, 1);

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, CountLines_OneNewline)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2", 11);
	size_t count = tprompt_count_logical_lines(handle);
	EXPECT_EQ(count, 2);

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, CountLines_MultipleNewlines)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2\nLine3\nLine4", 23);
	size_t count = tprompt_count_logical_lines(handle);
	EXPECT_EQ(count, 4);

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, CountLines_ConsecutiveNewlines)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Empty lines (consecutive newlines)
	tprompt_buffer_insert(&handle->buffer, "Line1\n\n\nLine4", 13);
	size_t count = tprompt_count_logical_lines(handle);
	EXPECT_EQ(count, 4); // 3 newlines = 4 logical lines

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, CountLines_TrailingNewline)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\n", 6);
	size_t count = tprompt_count_logical_lines(handle);
	EXPECT_EQ(count, 2); // Trailing newline creates empty line

	tprompt_close(handle);
}

/* ========================================================================
 * Logical Line At Offset Tests
 * ======================================================================== */

TEST(NewlineLogicalLine, GetLineAtOffset_NoNewlines)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Hello", 5);

	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 0), 0);
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 2), 0);
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 5), 0);

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, GetLineAtOffset_WithNewlines)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// "Line1\nLine2\nLine3" (offsets: 0-5, 6-11, 12-17)
	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2\nLine3", 17);

	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 0), 0);  // In "Line1"
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 5), 0);  // At first '\n'
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 6), 1);  // Start of "Line2"
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 11), 1); // At second '\n'
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 12), 2); // Start of "Line3"
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 17), 2); // End of buffer

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, GetLineAtOffset_EmptyLines)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// "A\n\nC" (offsets: 0, 1, 2, 3)
	tprompt_buffer_insert(&handle->buffer, "A\n\nC", 4);

	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 0), 0); // "A"
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 1), 0); // First '\n'
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 2), 1); // Empty line
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, 3), 2); // "C"

	tprompt_close(handle);
}

/* ========================================================================
 * Logical Line Bounds Tests
 * ======================================================================== */

TEST(NewlineLogicalLine, GetLineBounds_FirstLine)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2", 11);

	size_t start, end;
	int result = tprompt_get_logical_line_bounds(handle, 0, &start, &end);

	EXPECT_EQ(result, 0);
	EXPECT_EQ(start, 0);
	EXPECT_EQ(end, 5); // "Line1" is 5 chars, newline at index 5

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, GetLineBounds_MiddleLine)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2\nLine3", 17);

	size_t start, end;
	int result = tprompt_get_logical_line_bounds(handle, 1, &start, &end);

	EXPECT_EQ(result, 0);
	EXPECT_EQ(start, 6); // After first '\n'
	EXPECT_EQ(end, 11);	 // Before second '\n'

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, GetLineBounds_LastLine)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2", 11);

	size_t start, end;
	int result = tprompt_get_logical_line_bounds(handle, 1, &start, &end);

	EXPECT_EQ(result, 0);
	EXPECT_EQ(start, 6);
	EXPECT_EQ(end, 11); // End of buffer

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, GetLineBounds_EmptyLine)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "A\n\nC", 4);

	size_t start, end;
	int result = tprompt_get_logical_line_bounds(handle, 1, &start, &end);

	EXPECT_EQ(result, 0);
	EXPECT_EQ(start, 2); // After first '\n'
	EXPECT_EQ(end, 2);	 // Before second '\n' (empty line)

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, GetLineBounds_InvalidLine)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2", 11);

	size_t start, end;
	int result = tprompt_get_logical_line_bounds(handle, 10, &start, &end);

	EXPECT_EQ(result, -1); // Line 10 doesn't exist

	tprompt_close(handle);
}

/* ========================================================================
 * Logical Line Length Tests
 * ======================================================================== */

TEST(NewlineLogicalLine, GetLineLength_Simple)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "ABC\nDEFGH\nXY", 12);

	EXPECT_EQ(tprompt_get_logical_line_length(handle, 0), 3); // "ABC"
	EXPECT_EQ(tprompt_get_logical_line_length(handle, 1), 5); // "DEFGH"
	EXPECT_EQ(tprompt_get_logical_line_length(handle, 2), 2); // "XY"

	tprompt_close(handle);
}

TEST(NewlineLogicalLine, GetLineLength_EmptyLine)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "A\n\nC", 4);

	EXPECT_EQ(tprompt_get_logical_line_length(handle, 0), 1); // "A"
	EXPECT_EQ(tprompt_get_logical_line_length(handle, 1), 0); // ""
	EXPECT_EQ(tprompt_get_logical_line_length(handle, 2), 1); // "C"

	tprompt_close(handle);
}

/* ========================================================================
 * Buffer Has Newlines Test
 * ======================================================================== */

TEST(NewlineBuffer, HasNewlines_EmptyBuffer)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	EXPECT_FALSE(tprompt_buffer_has_newlines(handle));

	tprompt_close(handle);
}

TEST(NewlineBuffer, HasNewlines_NoNewlines)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Hello World", 11);
	EXPECT_FALSE(tprompt_buffer_has_newlines(handle));

	tprompt_close(handle);
}

TEST(NewlineBuffer, HasNewlines_WithNewline)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Hello\nWorld", 11);
	EXPECT_TRUE(tprompt_buffer_has_newlines(handle));

	tprompt_close(handle);
}

/* ========================================================================
 * Physical Line Calculation With Newlines
 * ======================================================================== */

TEST(NewlinePhysicalLine, Layout_SingleLogicalLine)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Hello", 5);
	tprompt_display_calculate_layout(handle);

	EXPECT_EQ(handle->display.total_physical_lines, 1);
	EXPECT_EQ(handle->display.physical_line, 0);

	tprompt_close(handle);
}

TEST(NewlinePhysicalLine, Layout_TwoLogicalLines_NoWrap)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Two short lines that don't wrap
	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2", 11);
	tprompt_display_calculate_layout(handle);

	// Should be 2 physical lines (one per logical line)
	EXPECT_EQ(handle->display.total_physical_lines, 2);
	EXPECT_EQ(handle->display.physical_line, 1); // Cursor at end of Line2

	tprompt_close(handle);
}

TEST(NewlinePhysicalLine, Layout_LogicalLineWraps)
{
	tprompt_handle_t handle = create_test_handle(20, 24);
	ASSERT_NOT_NULL(handle);

	// First logical line wraps: "> " (2) + "12345678901234567890" (20) = 22
	// This should create 2 physical lines for the first logical line
	// Second logical line: "ABC" = 1 physical line
	tprompt_buffer_insert(&handle->buffer, "12345678901234567890\nABC", 24);
	tprompt_display_calculate_layout(handle);

	// Total: 2 (wrapped first line) + 1 (second line) = 3 physical lines
	EXPECT_EQ(handle->display.total_physical_lines, 3);

	tprompt_close(handle);
}

TEST(NewlinePhysicalLine, Layout_CursorAtNewline)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2", 11);

	// Move cursor to the newline character (byte 5)
	handle->buffer.cursor = 5;
	tprompt_display_calculate_layout(handle);

	// Cursor should be at end of physical line 0
	EXPECT_EQ(handle->display.physical_line, 0);

	tprompt_close(handle);
}

TEST(NewlinePhysicalLine, Layout_CursorAfterNewline)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2", 11);

	// Move cursor after the newline (byte 6 = start of Line2)
	handle->buffer.cursor = 6;
	tprompt_display_calculate_layout(handle);

	// Cursor should be at start of physical line 1, after continuation marker
	// Continuation marker width = prompt width = 2 ("> ")
	EXPECT_EQ(handle->display.physical_line, 1);
	EXPECT_EQ(handle->display.physical_column, 2);

	tprompt_close(handle);
}

TEST(NewlinePhysicalLine, Layout_EmptyLogicalLine)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Three lines, middle one is empty
	tprompt_buffer_insert(&handle->buffer, "A\n\nC", 4);
	tprompt_display_calculate_layout(handle);

	EXPECT_EQ(handle->display.total_physical_lines, 3);

	tprompt_close(handle);
}

/* ========================================================================
 * Navigation With Newlines Tests
 * ======================================================================== */

TEST(NewlineNavigation, UpDown_AcrossNewlines)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2\nLine3", 17);

	// Start at end (Line3)
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor), 2);

	// Move up to Line2
	tprompt_cursor_move_up(handle);
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor), 1);

	// Move up to Line1
	tprompt_cursor_move_up(handle);
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor), 0);

	// Move down to Line2
	tprompt_cursor_move_down(handle);
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor), 1);

	tprompt_close(handle);
}

TEST(NewlineNavigation, HomeEnd_LogicalLineBoundaries)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2\nLine3", 17);

	// Position cursor in middle of Line2
	handle->buffer.cursor = 8; // "Li[n]e2"
	EXPECT_EQ(tprompt_get_logical_line_at_offset(handle, handle->buffer.cursor), 1);

	// Home should move to start of Line2 (byte 6)
	tprompt_cursor_move_to_logical_line_start(handle);
	EXPECT_EQ(handle->buffer.cursor, 6);

	// End should move to end of Line2 (byte 11 = before '\n')
	tprompt_cursor_move_to_logical_line_end(handle);
	EXPECT_EQ(handle->buffer.cursor, 11);

	tprompt_close(handle);
}

TEST(NewlineNavigation, Home_AtStartOfLine)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2", 11);
	handle->buffer.cursor = 6; // Start of Line2

	// Home from start should move to logical line start (byte 0)
	tprompt_cursor_move_to_start(&handle->buffer);
	EXPECT_EQ(handle->buffer.cursor, 0);

	tprompt_close(handle);
}

TEST(NewlineNavigation, End_AtEndOfLine)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Line1\nLine2", 11);
	handle->buffer.cursor = 11; // End of Line2

	// End from end should stay at logical line end
	tprompt_cursor_move_to_end(&handle->buffer);
	EXPECT_EQ(handle->buffer.cursor, 11);

	tprompt_close(handle);
}

/* ========================================================================
 * Edge Cases
 * ======================================================================== */

TEST(NewlineEdgeCases, OnlyNewlines)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "\n\n\n", 3);

	EXPECT_EQ(tprompt_count_logical_lines(handle), 4); // 3 newlines = 4 lines
	EXPECT_TRUE(tprompt_buffer_has_newlines(handle));

	tprompt_display_calculate_layout(handle);
	EXPECT_EQ(handle->display.total_physical_lines, 4);

	tprompt_close(handle);
}

TEST(NewlineEdgeCases, NewlineAtStart)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "\nText", 5);

	EXPECT_EQ(tprompt_count_logical_lines(handle), 2);

	size_t start, end;
	tprompt_get_logical_line_bounds(handle, 0, &start, &end);
	EXPECT_EQ(start, 0);
	EXPECT_EQ(end, 0); // Empty first line

	tprompt_close(handle);
}

TEST(NewlineEdgeCases, NewlineAtEnd)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	tprompt_buffer_insert(&handle->buffer, "Text\n", 5);

	EXPECT_EQ(tprompt_count_logical_lines(handle), 2);

	size_t start, end;
	tprompt_get_logical_line_bounds(handle, 1, &start, &end);
	EXPECT_EQ(start, 5);
	EXPECT_EQ(end, 5); // Empty last line

	tprompt_close(handle);
}

TEST(NewlineEdgeCases, UTF8WithNewlines)
{
	tprompt_handle_t handle = create_test_handle(80, 24);
	ASSERT_NOT_NULL(handle);

	// Japanese text with newlines: "日本\n語"
	tprompt_buffer_insert(&handle->buffer, "日本\n語", 10);

	EXPECT_EQ(tprompt_count_logical_lines(handle), 2);
	EXPECT_TRUE(tprompt_buffer_has_newlines(handle));

	// Check line bounds (UTF-8 aware)
	size_t start, end;
	tprompt_get_logical_line_bounds(handle, 0, &start, &end);
	EXPECT_EQ(start, 0);
	EXPECT_EQ(end, 6); // "日本" is 6 bytes, newline at byte 6

	tprompt_close(handle);
}

TEST(NewlineEdgeCases, MixedNewlinesAndWrapping)
{
	tprompt_handle_t handle = create_test_handle(10, 24);
	ASSERT_NOT_NULL(handle);

	// First line wraps, then newline, then short line
	// "> " (2) + "123456789" (9) = 11 cols - wraps to 2 physical lines
	// Then newline creates logical line 2
	// "AB" = 1 physical line
	// Total: 3 physical lines
	tprompt_buffer_insert(&handle->buffer, "123456789\nAB", 12);

	tprompt_display_calculate_layout(handle);
	EXPECT_EQ(handle->display.total_physical_lines, 3);
	EXPECT_EQ(tprompt_count_logical_lines(handle), 2);

	tprompt_close(handle);
}

/* ========================================================================
 * Main Entry Point
 * ======================================================================== */

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
