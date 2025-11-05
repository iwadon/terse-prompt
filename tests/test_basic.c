/**
 * @file test_basic.c
 * @brief Basic functionality tests for terse-prompt Phase 1
 * @version 0.1
 * @date 2025-11-02
 *
 * Tests UTF-8 utilities, buffer operations, cursor movement,
 * and basic handle initialization.
 */

#include "tprompt_internal.h"
#include "test_helpers.h"
#include <attest/attest.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ========================================================================
 * UTF-8 Utility Tests
 * ======================================================================== */

TEST(UTF8Utils, CharLength_SingleByte)
{
    EXPECT_EQ(tprompt_utf8_char_length('A'), 1);
    EXPECT_EQ(tprompt_utf8_char_length('0'), 1);
    EXPECT_EQ(tprompt_utf8_char_length(' '), 1);
}

TEST(UTF8Utils, CharLength_MultiByte)
{
    EXPECT_EQ(tprompt_utf8_char_length(0xC2), 2);  // 2-byte
    EXPECT_EQ(tprompt_utf8_char_length(0xE0), 3);  // 3-byte
    EXPECT_EQ(tprompt_utf8_char_length(0xF0), 4);  // 4-byte
}

TEST(UTF8Utils, CharLength_Invalid)
{
    EXPECT_EQ(tprompt_utf8_char_length(0x80), 0);  // Continuation byte
    EXPECT_EQ(tprompt_utf8_char_length(0xFF), 0);  // Invalid
}

TEST(UTF8Utils, NextChar_ASCII)
{
    const char *text = "Hello";
    EXPECT_EQ(tprompt_utf8_next_char(text, 0, 5), 1);
    EXPECT_EQ(tprompt_utf8_next_char(text, 1, 5), 2);
    EXPECT_EQ(tprompt_utf8_next_char(text, 4, 5), 5);
}

TEST(UTF8Utils, NextChar_UTF8)
{
    const char *text = "日本語";  // 3 characters, 9 bytes
    EXPECT_EQ(tprompt_utf8_next_char(text, 0, 9), 3);  // '日' is 3 bytes
    EXPECT_EQ(tprompt_utf8_next_char(text, 3, 9), 6);  // '本' is 3 bytes
    EXPECT_EQ(tprompt_utf8_next_char(text, 6, 9), 9);  // '語' is 3 bytes
}

TEST(UTF8Utils, NextChar_AtEnd)
{
    const char *text = "Test";
    EXPECT_EQ(tprompt_utf8_next_char(text, 4, 4), 4);
}

TEST(UTF8Utils, PrevChar_ASCII)
{
    const char *text = "Hello";
    EXPECT_EQ(tprompt_utf8_prev_char(text, 5), 4);
    EXPECT_EQ(tprompt_utf8_prev_char(text, 1), 0);
    EXPECT_EQ(tprompt_utf8_prev_char(text, 0), 0);
}

TEST(UTF8Utils, PrevChar_UTF8)
{
    const char *text = "日本語";  // 3 characters, 9 bytes
    EXPECT_EQ(tprompt_utf8_prev_char(text, 9), 6);  // Before '語'
    EXPECT_EQ(tprompt_utf8_prev_char(text, 6), 3);  // Before '本'
    EXPECT_EQ(tprompt_utf8_prev_char(text, 3), 0);  // Before '日'
}

TEST(UTF8Utils, Validate_ValidASCII)
{
    EXPECT_TRUE(tprompt_utf8_validate("Hello, World!", 13));
    EXPECT_TRUE(tprompt_utf8_validate("", 0));
}

TEST(UTF8Utils, Validate_ValidUTF8)
{
    EXPECT_TRUE(tprompt_utf8_validate("日本語", 9));
    EXPECT_TRUE(tprompt_utf8_validate("Привет", 12));  // Russian
    EXPECT_TRUE(tprompt_utf8_validate("🎉", 4));  // Emoji (4-byte)
}

TEST(UTF8Utils, Validate_Invalid)
{
    // Invalid continuation byte
    const char invalid1[] = {0xC2, 0x00, 0x00};
    EXPECT_FALSE(tprompt_utf8_validate(invalid1, 3));

    // Incomplete sequence
    const char invalid2[] = {0xE0, 0xA0};
    EXPECT_FALSE(tprompt_utf8_validate(invalid2, 2));

    // Overlong encoding
    const char invalid3[] = {0xC0, 0x80};
    EXPECT_FALSE(tprompt_utf8_validate(invalid3, 2));
}

TEST(UTF8Utils, CharCount_ASCII)
{
    EXPECT_EQ(tprompt_utf8_char_count("Hello", 5), 5);
    EXPECT_EQ(tprompt_utf8_char_count("", 0), 0);
}

TEST(UTF8Utils, CharCount_UTF8)
{
    EXPECT_EQ(tprompt_utf8_char_count("日本語", 9), 3);
    EXPECT_EQ(tprompt_utf8_char_count("Hello世界", 11), 7);  // 5 + 2
}

/* ========================================================================
 * Buffer Management Tests
 * ======================================================================== */

TEST(BufferManagement, Init_Success)
{
    tprompt_buffer_t buffer;
    EXPECT_EQ(tprompt_buffer_init(&buffer, 0), 0);
    EXPECT_NE(buffer.data, NULL);
    EXPECT_GT(buffer.size, 0);
    EXPECT_EQ(buffer.length, 0);
    EXPECT_EQ(buffer.cursor, 0);
    EXPECT_EQ(buffer.data[0], '\0');
    tprompt_buffer_free(&buffer);
}

TEST(BufferManagement, Init_CustomSize)
{
    tprompt_buffer_t buffer;
    EXPECT_EQ(tprompt_buffer_init(&buffer, 512), 0);
    EXPECT_GE(buffer.size, 512);
    tprompt_buffer_free(&buffer);
}

TEST(BufferManagement, Clear)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 0);
    tprompt_buffer_insert(&buffer, "test", 4);

    tprompt_buffer_clear(&buffer);
    EXPECT_EQ(buffer.length, 0);
    EXPECT_EQ(buffer.cursor, 0);
    EXPECT_EQ(buffer.data[0], '\0');

    tprompt_buffer_free(&buffer);
}

TEST(BufferManagement, Insert_AtEnd)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 0);

    EXPECT_EQ(tprompt_buffer_insert(&buffer, "Hello", 5), 0);
    EXPECT_EQ(buffer.length, 5);
    EXPECT_EQ(buffer.cursor, 5);
    EXPECT_STREQ(buffer.data, "Hello");

    tprompt_buffer_free(&buffer);
}

TEST(BufferManagement, Insert_AtStart)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 0);

    tprompt_buffer_insert(&buffer, "World", 5);
    buffer.cursor = 0;
    EXPECT_EQ(tprompt_buffer_insert(&buffer, "Hello ", 6), 0);

    EXPECT_EQ(buffer.length, 11);
    EXPECT_EQ(buffer.cursor, 6);
    EXPECT_STREQ(buffer.data, "Hello World");

    tprompt_buffer_free(&buffer);
}

TEST(BufferManagement, Insert_Middle)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 0);

    tprompt_buffer_insert(&buffer, "Helo", 4);
    buffer.cursor = 2;
    EXPECT_EQ(tprompt_buffer_insert(&buffer, "l", 1), 0);

    EXPECT_EQ(buffer.length, 5);
    EXPECT_EQ(buffer.cursor, 3);
    EXPECT_STREQ(buffer.data, "Hello");

    tprompt_buffer_free(&buffer);
}

TEST(BufferManagement, Insert_UTF8)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 0);

    const char *japanese = "日本語";
    EXPECT_EQ(tprompt_buffer_insert(&buffer, japanese, 9), 0);
    EXPECT_EQ(buffer.length, 9);
    EXPECT_STREQ(buffer.data, japanese);

    tprompt_buffer_free(&buffer);
}

TEST(BufferManagement, EnsureCapacity_Grow)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 16);

    size_t original_size = buffer.size;
    EXPECT_EQ(tprompt_buffer_ensure_capacity(&buffer, 100), 0);
    EXPECT_GT(buffer.size, original_size);
    EXPECT_GE(buffer.size, 101);  // +1 for null terminator

    tprompt_buffer_free(&buffer);
}

TEST(BufferManagement, Set_ReplaceContent)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 0);

    tprompt_buffer_insert(&buffer, "Original", 8);
    EXPECT_EQ(tprompt_buffer_set(&buffer, "Replaced"), 0);

    EXPECT_EQ(buffer.length, 8);
    EXPECT_EQ(buffer.cursor, 8);
    EXPECT_STREQ(buffer.data, "Replaced");

    tprompt_buffer_free(&buffer);
}

/* ========================================================================
 * Cursor Movement Tests
 * ======================================================================== */

TEST(CursorMovement, MoveToStart)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 0);
    tprompt_buffer_insert(&buffer, "Hello", 5);

    tprompt_cursor_move_to_start(&buffer);
    EXPECT_EQ(buffer.cursor, 0);

    tprompt_buffer_free(&buffer);
}

TEST(CursorMovement, MoveToEnd)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 0);
    tprompt_buffer_insert(&buffer, "Hello", 5);
    buffer.cursor = 0;

    tprompt_cursor_move_to_end(&buffer);
    EXPECT_EQ(buffer.cursor, 5);

    tprompt_buffer_free(&buffer);
}

TEST(CursorMovement, MoveToOffset)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 0);
    tprompt_buffer_insert(&buffer, "Hello", 5);

    tprompt_cursor_move_to_offset(&buffer, 2);
    EXPECT_EQ(buffer.cursor, 2);

    // Clamp to buffer length
    tprompt_cursor_move_to_offset(&buffer, 100);
    EXPECT_EQ(buffer.cursor, 5);

    tprompt_buffer_free(&buffer);
}

/* ========================================================================
 * Handle Initialization Tests
 * ======================================================================== */

TEST(HandleManagement, Open_DefaultOptions)
{
    tprompt_handle_t handle = tprompt_open(NULL);
    ASSERT_NE(handle, NULL);

    // Verify default settings
    EXPECT_NE(handle->terse, NULL);
    EXPECT_TRUE(handle->owns_terse);
    EXPECT_NE(handle->buffer.data, NULL);
    EXPECT_NE(handle->prompt, NULL);

#ifdef TERSE_ENABLE_TEST_MODE
    // In test mode, verify mocked terminal size
    terse_size_t size = terse_get_size(handle->terse);
    EXPECT_EQ(size.rows, 24);
    EXPECT_EQ(size.cols, 80);
    EXPECT_TRUE(size.known);
#endif

    tprompt_close(handle);
}

TEST(HandleManagement, Open_CustomOptions)
{
    tprompt_options_t opts = {
        .prompt = "test> ",
        .history_file = NULL,
        .max_input_size = 2048,
        .max_history_size = 50,
        .completion_callback = NULL,
        .completion_user_data = NULL,
        .completion_prefixes = NULL,
        .terse_handle = NULL,
        .flags = TPROMPT_FLAG_MULTILINE
    };

    tprompt_handle_t handle = tprompt_open(&opts);
    ASSERT_NE(handle, NULL);
    EXPECT_STREQ(handle->prompt, "test> ");
    EXPECT_EQ(handle->options.max_input_size, 2048);
    EXPECT_EQ(handle->history.max_size, 50);

    tprompt_close(handle);
}

TEST(HandleManagement, Open_WithMockTerseHandle)
{
#ifdef TERSE_ENABLE_TEST_MODE
    // Create mock terse handle
    terse_handle_t terse_h = test_create_terse_handle();
    ASSERT_NE(terse_h, NULL);

    // Pass to tprompt
    tprompt_options_t opts = {
        .prompt = "mock> ",
        .terse_handle = terse_h
    };

    tprompt_handle_t handle = tprompt_open(&opts);
    ASSERT_NE(handle, NULL);
    EXPECT_EQ(handle->terse, terse_h);
    EXPECT_FALSE(handle->owns_terse);  // External handle

    // Verify mock settings are active
    terse_size_t size = terse_get_size(terse_h);
    EXPECT_EQ(size.rows, 24);
    EXPECT_EQ(size.cols, 80);

    tprompt_close(handle);
    terse_close(terse_h);
#endif
}

TEST(HandleManagement, Close_NullHandle)
{
    // Should not crash
    tprompt_close(NULL);
}

/* ========================================================================
 * Error Handling Tests
 * ======================================================================== */

TEST(ErrorHandling, SetAndClear)
{
    tprompt_error_info_t error;

    tprompt_set_error(&error, TPROMPT_ERROR_MEMORY, ENOMEM, "Out of memory");
    EXPECT_EQ(error.category, TPROMPT_ERROR_MEMORY);
    EXPECT_EQ(error.code, ENOMEM);
    EXPECT_STREQ(error.message, "Out of memory");

    tprompt_clear_error(&error);
    EXPECT_EQ(error.category, TPROMPT_ERROR_NONE);
    EXPECT_EQ(error.code, 0);
    EXPECT_EQ(error.message[0], '\0');
}

TEST(ErrorHandling, GetLastError_NullHandle)
{
    tprompt_error_info_t err = tprompt_get_last_error(NULL);
    // Should return global error (category may vary)
    EXPECT_GE(err.category, TPROMPT_ERROR_NONE);
}

/* ========================================================================
 * History Tests
 * ======================================================================== */

TEST(History, Init)
{
    tprompt_history_t history;
    tprompt_history_init(&history, 100);

    EXPECT_EQ(history.head, NULL);
    EXPECT_EQ(history.tail, NULL);
    EXPECT_EQ(history.count, 0);
    EXPECT_EQ(history.max_size, 100);
    EXPECT_EQ(history.current, NULL);

    tprompt_history_free(&history);
}

TEST(History, ResetPosition)
{
    tprompt_history_t history;
    tprompt_history_init(&history, 100);

    tprompt_history_reset_position(&history);
    EXPECT_EQ(history.current, NULL);

    tprompt_history_free(&history);
}

/* ========================================================================
 * Completion Tests
 * ======================================================================== */

TEST(Completion, Init)
{
    tprompt_completion_state_t state;
    tprompt_completion_init(&state);

    EXPECT_FALSE(state.active);
    EXPECT_EQ(state.candidates, NULL);
    EXPECT_EQ(state.candidate_count, 0);
    EXPECT_EQ(state.selected_index, 0);
    EXPECT_EQ(state.trigger_offset, 0);
    EXPECT_EQ(state.trigger_char, '\0');

    tprompt_completion_free(&state);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
