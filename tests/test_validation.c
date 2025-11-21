/**
 * @file test_validation.c
 * @brief Validation callback tests for terse-prompt
 * @version 0.1
 * @date 2025-11-11
 *
 * Tests the validation callback feature that allows applications
 * to validate input before confirmation (IRB-style behavior).
 */

#include "test_helpers.h"
#include "tprompt_internal.h"
#include <attest/attest.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Validation Callback Helpers
 * ======================================================================== */

// Simple callback that always accepts
static tprompt_validation_result_t callback_always_accept(
	const char *text,
	size_t length,
	void *user_data)
{
	(void)text;
	(void)length;
	(void)user_data;
	return TPROMPT_VALIDATION_ACCEPT;
}

// Simple callback that always rejects
static tprompt_validation_result_t callback_always_reject(
	const char *text,
	size_t length,
	void *user_data)
{
	(void)text;
	(void)length;
	(void)user_data;
	return TPROMPT_VALIDATION_REJECT;
}

// Simple callback that always continues (requests newline)
static tprompt_validation_result_t callback_always_continue(
	const char *text,
	size_t length,
	void *user_data)
{
	(void)text;
	(void)length;
	(void)user_data;
	return TPROMPT_VALIDATION_CONTINUE;
}

// Counter to track callback invocations
static int validation_call_count = 0;

// Callback that counts invocations
static tprompt_validation_result_t callback_counting(
	const char *text,
	size_t length,
	void *user_data)
{
	(void)text;
	(void)length;
	validation_call_count++;
	tprompt_validation_result_t *result = (tprompt_validation_result_t *)user_data;
	return *result;
}

// Parenthesis balance checker (IRB-style)
static tprompt_validation_result_t callback_check_parens(
	const char *text,
	size_t length,
	void *user_data)
{
	(void)user_data;

	int paren_count = 0;
	int brace_count = 0;

	for (size_t i = 0; i < length; i++) {
		if (text[i] == '(') paren_count++;
		if (text[i] == ')') paren_count--;
		if (text[i] == '{') brace_count++;
		if (text[i] == '}') brace_count--;
	}

	// If unbalanced, request continuation (newline)
	if (paren_count > 0 || brace_count > 0) {
		return TPROMPT_VALIDATION_CONTINUE;
	}

	// If over-closed, reject
	if (paren_count < 0 || brace_count < 0) {
		return TPROMPT_VALIDATION_REJECT;
	}

	return TPROMPT_VALIDATION_ACCEPT;
}

/* ========================================================================
 * Basic Validation Tests
 * ======================================================================== */

TEST(Validation, NoCallback_AcceptsImmediately)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.validation_callback = NULL; // No validation

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Type "hello" and press Enter (multiline mode default)
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'h', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'o', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events, 6);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "hello");

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

TEST(Validation, AlwaysAccept_AcceptsImmediately)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.validation_callback = callback_always_accept;

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Type "test" and press Enter (confirmation validates through callback)
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events, 5);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "test");

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

TEST(Validation, AlwaysReject_BypassWithAltEnter)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.validation_callback = callback_always_reject;

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Type "test", press Enter (rejected), then Alt+Enter (accepted without validation)
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },        // Should be rejected (beep)
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = TERSE_MOD_ALT } // Alt+Enter accepts
	};
	test_mock_events(terse, events, 6);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "test");

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

/* ========================================================================
 * CONTINUE Behavior Tests
 * ======================================================================== */

TEST(Validation, Continue_InsertsNewlineInMultilineMode)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.flags = TPROMPT_FLAG_MULTILINE; // Multiline mode
	opts.validation_callback = callback_always_continue;

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Type "line1", press Enter (inserts newline), type "line2", press Alt+Enter
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '1', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },        // Should insert newline
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '2', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = TERSE_MOD_ALT } // Alt+Enter accepts
	};
	test_mock_events(terse, events, 12);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "line1\nline2");

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

TEST(Validation, Continue_BeepsInSingleLineMode)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.flags = 0; // Single-line mode (no TPROMPT_FLAG_MULTILINE)
	opts.validation_callback = callback_always_continue;

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Type "test", press Enter (should beep, not insert newline), press Alt+Enter
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },        // Should beep (treated as REJECT)
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = TERSE_MOD_ALT } // Alt+Enter accepts
	};
	test_mock_events(terse, events, 6);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "test"); // No newline inserted

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

/* ========================================================================
 * Callback Invocation Tests
 * ======================================================================== */

TEST(Validation, CallbackInvokedOnConfirmation)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_validation_result_t result_value = TPROMPT_VALIDATION_ACCEPT;
	validation_call_count = 0;

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.validation_callback = callback_counting;
	opts.validation_user_data = &result_value;

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Type "test" and press Enter
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events, 5);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "test");
	EXPECT_EQ(validation_call_count, 1); // Callback should have been invoked once

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

TEST(Validation, CallbackInvokedMultipleTimesOnReject)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_validation_result_t result_value = TPROMPT_VALIDATION_REJECT;
	validation_call_count = 0;

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.validation_callback = callback_counting;
	opts.validation_user_data = &result_value;

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Type "test", press Enter twice (rejected), then Alt+Enter (bypasses validation)
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },        // Rejected
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },        // Rejected
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = TERSE_MOD_ALT } // Accepted (validation called)
	};
	test_mock_events(terse, events, 7);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "test");
	EXPECT_EQ(validation_call_count, 3); // Called 3 times (2x Enter + 1x Alt+Enter)

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

/* ========================================================================
 * Practical Example: Parenthesis Balance Checker
 * ======================================================================== */

TEST(Validation, ParenthesisBalance_Balanced)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.flags = TPROMPT_FLAG_MULTILINE;
	opts.validation_callback = callback_check_parens;

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Type "func(arg)" and press Enter - should accept immediately
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'f', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'u', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'c', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '(', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'a', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'r', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'g', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = ')', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events, 10);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "func(arg)");

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

TEST(Validation, ParenthesisBalance_UnbalancedContinues)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.flags = TPROMPT_FLAG_MULTILINE;
	opts.validation_callback = callback_check_parens;

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Type "func(", press Enter (inserts newline), type "arg)", press Enter (accepts)
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'f', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'u', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'c', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '(', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },        // Unbalanced, insert newline
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'a', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'r', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'g', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = ')', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }         // Now balanced, accept
	};
	test_mock_events(terse, events, 11);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "func(\narg)");

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

TEST(Validation, ParenthesisBalance_OverClosedRejects)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.flags = TPROMPT_FLAG_MULTILINE;
	opts.validation_callback = callback_check_parens;

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Type "test)", press Enter (rejected), backspace, press Enter (accepts)
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = ')', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },        // Over-closed, reject
		{ .type = TERSE_EVENT_BACKSPACE },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }         // Now balanced, accept
	};
	test_mock_events(terse, events, 8);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "test");

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

/* ========================================================================
 * Runtime Callback Update Tests
 * ======================================================================== */

TEST(Validation, SetValidationCallback_UpdatesAtRuntime)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.validation_callback = NULL; // Start with no validation

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Update validation callback at runtime
	tprompt_set_validation_callback(handle, callback_always_accept, NULL);

	// Type "test" and press Enter
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events, 5);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "test");

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

TEST(Validation, SetValidationCallback_DisableAtRuntime)
{
	terse_handle_t terse = test_create_terse_handle();
	ASSERT_NE(terse, NULL);

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse;
	opts.validation_callback = callback_always_reject;

	tprompt_handle_t handle = tprompt_open(&opts);
	ASSERT_NE(handle, NULL);

	// Disable validation at runtime
	tprompt_set_validation_callback(handle, NULL, NULL);

	// Type "test" and press Enter - should accept immediately (no validation)
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events, 5);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "test");

	free(result);
	tprompt_close(handle);
	terse_close(terse);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
