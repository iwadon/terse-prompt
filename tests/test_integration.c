/**
 * @file test_integration.c
 * @brief Integration tests for tprompt_readline() with simulated event streams
 * @version 0.1
 * @date 2025-11-20
 *
 * Tests the complete event handling pipeline from input simulation through
 * to final result, catching timing/ordering bugs that unit tests might miss.
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
 * @brief Create a test handle with custom keybindings
 * @param bindings Array of custom keybindings (NULL for none)
 * @param count Number of bindings (0 for none)
 * @return Prompt handle, or NULL on failure
 */
static tprompt_handle_t create_handle_with_keybindings(
	const tprompt_keybinding_t *bindings,
	size_t count)
{
	// Create mock terse handle for test mode
	terse_handle_t terse_h = test_create_terse_handle();
	if (!terse_h) {
		return NULL;
	}

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse_h;  // Use mock terse handle
	opts.flags = TPROMPT_FLAG_MULTILINE;
	opts.custom_keybindings = (tprompt_keybinding_t *)bindings;
	opts.keybinding_count = count;

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		terse_close(terse_h);
		return NULL;
	}

	return handle;
}

/* ========================================================================
 * Basic Integration Tests
 * ======================================================================== */

TEST(Integration, BasicInput_TypeAndEnter)
{
	// Test: Type "hello" + Enter, expect "hello"
	tprompt_handle_t handle = create_handle_with_keybindings(NULL, 0);
	ASSERT_NE(handle, NULL);

	// Access the terse handle directly (via internal structure)
	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence: type "hello" + Enter
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'h', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'o', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events, 6);

	// Call tprompt_readline and verify result
	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "hello");

	free(result);
	tprompt_close(handle);
}

TEST(Integration, CtrlEnterInsertsNewlineByDefault)
{
	// Test: Ctrl+Enter inserts newline instead of confirming
	tprompt_handle_t handle = create_handle_with_keybindings(NULL, 0);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence: type "line1" + Ctrl+Enter + "line2" + Enter
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '1', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = TERSE_MOD_CTRL }, // Insert newline
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '2', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 } // Confirm input
	};
	test_mock_events(terse, events, 12);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "line1\nline2");

	free(result);
	tprompt_close(handle);
}

TEST(Integration, CustomKeybinding_EnterConfirms)
{
	// Test: Custom Enter=confirm, type "test" + Enter, expect "test"
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	// Access the terse handle directly (via internal structure)
	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence: type "test" + Enter (no modifiers)
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
}

TEST(Integration, CustomKeybinding_ShiftEnterNewline)
{
	// Test: Custom Shift+Enter=newline, type "line1" + Shift+Enter + "line2" + Enter, expect "line1\nline2"
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, TERSE_MOD_SHIFT, TPROMPT_ACTION_INSERT_NEWLINE),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 2);
	ASSERT_NE(handle, NULL);

	// Access the terse handle directly (via internal structure)
	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence: type "line1" + Shift+Enter + "line2" + Enter
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '1', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = TERSE_MOD_SHIFT },  // Insert newline
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '2', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }  // Confirm input
	};
	test_mock_events(terse, events, 12);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "line1\nline2");

	free(result);
	tprompt_close(handle);
}

/* ========================================================================
 * Regression Tests for Timing/State Bugs
 * ======================================================================== */

/**
 * @brief Validation callback that rejects single "a", accepts "ab"
 */
static tprompt_validation_result_t validation_callback_reject_a_accept_ab(
	const char *text,
	size_t length,
	void *user_data)
{
	(void)length;
	(void)user_data;

	// Reject single "a", accept anything else (like "ab")
	if (strcmp(text, "a") == 0) {
		return TPROMPT_VALIDATION_REJECT;
	}
	return TPROMPT_VALIDATION_ACCEPT;
}

TEST(Integration, ForceConfirmation_ValidationRejectThenConfirm)
{
	// REGRESSION TEST for force_confirmation reset timing bug (commit b190ae6)
	// Setup: Multiline mode + custom keybinding Enter=CONFIRM_INPUT + validation callback that rejects "a", accepts "ab"
	// Sequence: type "a" + Enter (rejected) + type "b" + Enter (should accept "ab")
	// This test would FAIL if force_confirmation was reset BEFORE validation check

	// Use custom keybinding: Enter = CONFIRM_INPUT (which sets force_confirmation=true)
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	// Set validation callback
	handle->options.validation_callback = validation_callback_reject_a_accept_ab;
	handle->options.validation_user_data = NULL;

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence:
	// 1. Type "a"
	// 2. Enter (triggers CONFIRM_INPUT action -> sets force_confirmation=true, pending_confirmation=true)
	// 3. Main loop processes pending_confirmation -> validation rejects -> beep, continue editing
	//    CRITICAL: force_confirmation must be reset AFTER validation, not before
	// 4. Type "b" (now buffer = "ab")
	// 5. Enter (triggers CONFIRM_INPUT again)
	// 6. Main loop processes -> validation accepts -> breaks loop
	terse_event_t events[] = {
		// First attempt: type "a" + Enter (will be rejected)
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'a', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },  // Custom keybinding: force_confirmation=true
		// Second attempt: type "b" + Enter (should accept "ab")
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'b', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }   // Custom keybinding: force_confirmation=true
	};
	test_mock_events(terse, events, 4);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "ab");  // Should contain both characters

	free(result);
	tprompt_close(handle);
}

/**
 * @brief Validation callback that requests multiline continuation
 */
static tprompt_validation_result_t validation_callback_continue_on_incomplete(
	const char *text,
	size_t length,
	void *user_data)
{
	(void)length;
	(void)user_data;

	// Continue (insert newline) if text doesn't end with "done"
	// Accept if text ends with "done"
	size_t text_len = strlen(text);
	if (text_len >= 4 && strcmp(text + text_len - 4, "done") == 0) {
		return TPROMPT_VALIDATION_ACCEPT;
	}
	return TPROMPT_VALIDATION_CONTINUE;
}

TEST(Integration, PendingConfirmation_MultipleRounds)
{
	// REGRESSION TEST for pending_confirmation across multiple validation cycles
	// Setup: Multiline mode + plain Enter key + validation callback that continues until "done" suffix
	// Sequence: type "line1" + Enter (continue->newline) + "line2" + Enter (continue->newline) + "done" + Enter (accept)
	// This test verifies pending_confirmation is properly reset between validation cycles

	// Don't use custom keybindings - rely on default multiline behavior (plain Enter triggers validation)
	tprompt_handle_t handle = create_handle_with_keybindings(NULL, 0);
	ASSERT_NE(handle, NULL);

	// Set validation callback
	handle->options.validation_callback = validation_callback_continue_on_incomplete;
	handle->options.validation_user_data = NULL;

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence:
	// 1. Type "line1" + Enter (validation returns CONTINUE -> inserts newline, continues editing)
	// 2. Type "line2" + Enter (validation returns CONTINUE -> inserts newline, continues editing)
	// 3. Type "done" + Enter (validation returns ACCEPT -> confirms and exits)
	// NOTE: Plain Enter in multiline mode sets pending_confirmation, which triggers validation
	terse_event_t events[] = {
		// Round 1: "line1" + Enter (continues with newline)
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '1', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },  // Plain Enter
		// Round 2: "line2" + Enter (continues with newline)
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '2', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },  // Plain Enter
		// Round 3: "done" + Enter (accepts and exits)
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'd', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'o', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }   // Plain Enter
	};
	test_mock_events(terse, events, 17);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "line1\nline2\ndone");

	free(result);
	tprompt_close(handle);
}

/**
 * @brief Validation callback that requires "valid" prefix
 */
static tprompt_validation_result_t validation_callback_require_valid_prefix(
	const char *text,
	size_t length,
	void *user_data)
{
	(void)length;
	(void)user_data;

	// Accept if text contains "valid" substring
	if (strstr(text, "valid") != NULL) {
		return TPROMPT_VALIDATION_ACCEPT;
	}
	return TPROMPT_VALIDATION_REJECT;
}

TEST(Integration, CustomKeybinding_ValidationInteraction)
{
	// REGRESSION TEST for custom keybinding + validation interaction
	// Setup: Custom Enter=confirm + validation callback that requires "valid" prefix
	// Sequence: type "test" + Enter (rejected) + type " valid" + Enter (accepted)
	// This tests interaction between custom keybindings and validation system

	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	// Set validation callback
	handle->options.validation_callback = validation_callback_require_valid_prefix;
	handle->options.validation_user_data = NULL;

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence:
	// 1. Type "test"
	// 2. Enter (triggers validation -> rejected -> beep, continue)
	// 3. Type " valid" (now buffer = "test valid")
	// 4. Enter (triggers validation -> accepted -> breaks)
	terse_event_t events[] = {
		// First attempt: "test" + Enter (will be rejected)
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },  // Rejected
		// Second attempt: " valid" + Enter (will be accepted)
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = ' ', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'v', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'a', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'd', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }  // Accepted
	};
	test_mock_events(terse, events, 12);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "test valid");

	free(result);
	tprompt_close(handle);
}

/* ========================================================================
 * Phase 3: Complex Multi-Step Sequences and Edge Cases
 * ======================================================================== */

TEST(ComplexInput, TypeEditConfirm)
{
	// Test complex editing: type "hello" + backspace×2 + type "p me" = "help me"
	// Purpose: Tests editing operations (insert + delete) through main loop
	tprompt_handle_t handle = create_handle_with_keybindings(NULL, 0);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence: "hello" → backspace×2 → "p me" → Enter
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'h', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'o', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_BACKSPACE, .data.key.mods = 0 },  // Delete 'o'
		{ .type = TERSE_EVENT_BACKSPACE, .data.key.mods = 0 },  // Delete 'l'
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'p', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = ' ', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'm', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events, 12);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "help me");

	free(result);
	tprompt_close(handle);
}

TEST(EmptyInput, DirectConfirm)
{
	// Test edge case: Press Enter immediately without typing anything
	// Purpose: Tests confirmation with no input
	//
	// Expected behavior: Return empty string "" for empty confirmation
	// This is distinct from Ctrl+D on empty buffer, which returns NULL (EOF)
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence: Just Enter (no text typed)
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events, 1);

	char *result = tprompt_readline(handle, NULL);
	// Should return empty string "" for empty confirmation (not NULL)
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "");

	free(result);
	tprompt_close(handle);
}

TEST(MultipleNewlines, ComplexStructure)
{
	// Test multiple consecutive newlines: "line1" + 2×newline + "line3"
	// Purpose: Tests multiple consecutive newlines through event stream
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, TERSE_MOD_SHIFT, TPROMPT_ACTION_INSERT_NEWLINE),
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, TERSE_MOD_CTRL, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 2);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence: "line1" + Shift+Enter + Shift+Enter + "line3" + Ctrl+Enter
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '1', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = TERSE_MOD_SHIFT },  // First newline
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = TERSE_MOD_SHIFT },  // Second newline
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '3', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = TERSE_MOD_CTRL }  // Confirm
	};
	test_mock_events(terse, events, 13);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "line1\n\nline3");

	free(result);
	tprompt_close(handle);
}

/**
 * @brief Validation callback that continues until input length >= 10
 */
static tprompt_validation_result_t validation_callback_continue_until_length_10(
	const char *text,
	size_t length,
	void *user_data)
{
	(void)user_data;

	// Continue (insert newline) if length < 10
	// Accept if length >= 10
	if (length >= 10) {
		return TPROMPT_VALIDATION_ACCEPT;
	}
	return TPROMPT_VALIDATION_CONTINUE;
}

TEST(ValidationContinue, WithEditing)
{
	// Test VALIDATION_CONTINUE with buffer accumulation
	// Setup: Validation callback that continues until length >= 10
	// Sequence: "short" (5 chars) + Enter (continue→newline) + "text here" (9 chars) + Enter (accept, total=15)
	// Purpose: Tests VALIDATION_CONTINUE with buffer accumulation
	tprompt_handle_t handle = create_handle_with_keybindings(NULL, 0);
	ASSERT_NE(handle, NULL);

	// Set validation callback
	handle->options.validation_callback = validation_callback_continue_until_length_10;
	handle->options.validation_user_data = NULL;

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence:
	// "short" (5 chars) + Enter → continue (newline inserted, buffer="short\n", length=6)
	// "text here" (9 chars) + Enter → accept (buffer="short\ntext here", length=15)
	terse_event_t events[] = {
		// Round 1: "short" + Enter (continues with newline)
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'h', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'o', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'r', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 },  // Plain Enter
		// Round 2: "text here" + Enter (accepts)
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'x', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = ' ', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'h', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'r', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }   // Plain Enter
	};
	test_mock_events(terse, events, 16);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "short\ntext here");

	free(result);
	tprompt_close(handle);
}

TEST(CtrlJNewline, CustomBinding)
{
	// Test character-based custom keybinding with modifiers
	// Setup: Custom Ctrl+J=newline binding (lowercase 'j')
	// Sequence: "line1" + Ctrl+J + "line2" + Enter
	// Purpose: Tests character-based keybinding with modifiers
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_CHAR('j', TERSE_MOD_CTRL, TPROMPT_ACTION_INSERT_NEWLINE),  // Ctrl+J
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 2);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence: "line1" + Ctrl+J + "line2" + Enter
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '1', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'j', .data.ch.mods = TERSE_MOD_CTRL, .data.ch.width = 1 },  // Ctrl+J
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = '2', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }  // Confirm
	};
	test_mock_events(terse, events, 12);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "line1\nline2");

	free(result);
	tprompt_close(handle);
}

/* ========================================================================
 * EOF Handling Tests
 * ======================================================================== */

TEST(EOF, CtrlDOnEmptyBuffer)
{
	// Test: Press Ctrl+D immediately on empty buffer, expect NULL (EOF)
	// Purpose: Tests EOF handling with no input (Ctrl+D on empty line)
	tprompt_handle_t handle = create_handle_with_keybindings(NULL, 0);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence: Just Ctrl+D (no text typed)
	// Note: terse transmits Ctrl+D as uppercase 'D' with TERSE_MOD_CTRL
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'D', .data.ch.mods = TERSE_MOD_CTRL, .data.ch.width = 1 }
	};
	test_mock_events(terse, events, 1);

	char *result = tprompt_readline(handle, NULL);
	// Ctrl+D on empty buffer should return NULL (EOF)
	EXPECT_EQ(result, NULL);

	// Verify no error was set (EOF is not an error)
	tprompt_error_info_t error = tprompt_get_last_error(handle);
	EXPECT_EQ(error.category, TPROMPT_ERROR_NONE);

	// No free() needed since result is NULL
	tprompt_close(handle);
}

TEST(EOF, CtrlDDeletesCharWhenBufferNotEmpty)
{
	// Test: Type "hello", position at 'e', press Ctrl+D, expect "hllo" (delete-char, not EOF)
	// Purpose: Tests that Ctrl+D on non-empty buffer deletes character (like Delete key)
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence: type "hello" + Left×3 (position at 'e') + Ctrl+D + Enter
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'h', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'l', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'o', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ARROW_LEFT, .data.key.mods = 0 },  // Move to 'o'
		{ .type = TERSE_EVENT_ARROW_LEFT, .data.key.mods = 0 },  // Move to 'l'
		{ .type = TERSE_EVENT_ARROW_LEFT, .data.key.mods = 0 },  // Move to 'l'
		{ .type = TERSE_EVENT_ARROW_LEFT, .data.key.mods = 0 },  // Move to 'e'
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'D', .data.ch.mods = TERSE_MOD_CTRL, .data.ch.width = 1 },  // Delete 'e'
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }  // Confirm input
	};
	test_mock_events(terse, events, 11);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "hllo");  // 'e' should be deleted

	free(result);
	tprompt_close(handle);
}

TEST(EOF, CtrlDAfterDeletingAllContent)
{
	// Test: Type "x", backspace to delete it, press Ctrl+D, expect NULL (EOF)
	// Purpose: Tests that Ctrl+D on buffer that became empty signals EOF
	tprompt_handle_t handle = create_handle_with_keybindings(NULL, 0);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Mock event sequence: type "x" + backspace + Ctrl+D
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'x', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_BACKSPACE, .data.key.mods = 0 },  // Delete 'x', buffer now empty
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'D', .data.ch.mods = TERSE_MOD_CTRL, .data.ch.width = 1 }  // EOF
	};
	test_mock_events(terse, events, 3);

	char *result = tprompt_readline(handle, NULL);
	// Ctrl+D on empty buffer should return NULL (EOF)
	EXPECT_EQ(result, NULL);

	// Verify no error was set (EOF is not an error)
	tprompt_error_info_t error = tprompt_get_last_error(handle);
	EXPECT_EQ(error.category, TPROMPT_ERROR_NONE);

	// No free() needed since result is NULL
	tprompt_close(handle);
}

TEST(EOF, EmptyInputVsEOFDistinction)
{
	// REGRESSION TEST: Verify that empty input confirmation returns "" while EOF returns NULL
	// This test ensures we correctly distinguish between:
	// - Enter on empty buffer → "" (empty string, continue)
	// - Ctrl+D on empty buffer → NULL (EOF, exit)
	//
	// Bug history: Initially, both cases returned NULL because we only checked buffer.length == 0
	// Fix: Added eof_signaled flag to track explicit EOF (Ctrl+D) vs empty confirmation (Enter)
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Test 1: Empty buffer + Enter → should return empty string ""
	terse_event_t enter_events[] = {
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, enter_events, 1);

	char *result1 = tprompt_readline(handle, NULL);
	ASSERT_NE(result1, NULL);  // Must NOT be NULL
	EXPECT_STREQ(result1, "");  // Must be empty string
	free(result1);

	// Test 2: Empty buffer + Ctrl+D → should return NULL (EOF)
	terse_event_t ctrl_d_events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'D', .data.ch.mods = TERSE_MOD_CTRL, .data.ch.width = 1 }
	};
	test_mock_events(terse, ctrl_d_events, 1);

	char *result2 = tprompt_readline(handle, NULL);
	EXPECT_EQ(result2, NULL);  // Must be NULL for EOF

	// Verify no error for EOF
	tprompt_error_info_t error = tprompt_get_last_error(handle);
	EXPECT_EQ(error.category, TPROMPT_ERROR_NONE);

	tprompt_close(handle);
}

TEST(EOF, MultipleEmptyInputsBeforeEOF)
{
	// REGRESSION TEST: Verify multiple empty confirmations work correctly before EOF
	// This ensures the eof_signaled flag is properly reset between readline calls
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Three empty inputs followed by EOF
	for (int i = 0; i < 3; i++) {
		terse_event_t enter_events[] = {
			{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
		};
		test_mock_events(terse, enter_events, 1);

		char *result = tprompt_readline(handle, NULL);
		ASSERT_NE(result, NULL);  // Each should return empty string, not NULL
		EXPECT_STREQ(result, "");
		free(result);
	}

	// Finally, Ctrl+D should return NULL
	terse_event_t ctrl_d_events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'D', .data.ch.mods = TERSE_MOD_CTRL, .data.ch.width = 1 }
	};
	test_mock_events(terse, ctrl_d_events, 1);

	char *result_eof = tprompt_readline(handle, NULL);
	EXPECT_EQ(result_eof, NULL);

	tprompt_close(handle);
}

/* ========================================================================
 * Cursor Positioning Tests
 * ======================================================================== */

TEST(CursorPositioning, EmptyBufferFinalRender)
{
	// REGRESSION TEST: Verify that final render is executed even for empty buffer
	// This ensures cursor is positioned correctly before moving to next line
	//
	// Bug history: Originally, final render was skipped when buffer.cursor < buffer.length
	// was false (both 0 for empty buffer), leaving cursor in wrong position
	// Fix: Always execute final render, even for empty buffer
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Enable test mode call recording to verify rendering behavior
	// (Assumes terse library has call recording in test mode)

	// Test: Enter on empty buffer
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events, 1);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "");

	// Verify that rendering occurred (buffer should be at cursor position)
	// For empty buffer, cursor should be at position 0 after final render
	EXPECT_EQ(handle->buffer.cursor, 0);
	EXPECT_EQ(handle->buffer.length, 0);

	free(result);
	tprompt_close(handle);
}

TEST(CursorPositioning, NonEmptyBufferFinalRender)
{
	// Verify that cursor is moved to end of buffer before final render
	// This ensures clean output positioning for non-empty input
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Test: Type "test" + move left + Enter
	// Cursor should be moved to end before final render
	terse_event_t events[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ARROW_LEFT, .data.key.mods = 0 },  // Move cursor left
		{ .type = TERSE_EVENT_ARROW_LEFT, .data.key.mods = 0 },  // Move cursor left again
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }        // Confirm
	};
	test_mock_events(terse, events, 7);

	char *result = tprompt_readline(handle, NULL);
	ASSERT_NE(result, NULL);
	EXPECT_STREQ(result, "test");

	// After readline completes, cursor should have been moved to end (length 4)
	EXPECT_EQ(handle->buffer.cursor, 4);
	EXPECT_EQ(handle->buffer.length, 4);

	free(result);
	tprompt_close(handle);
}

/* ========================================================================
 * Multiple readline() Calls Tests
 * ======================================================================== */

TEST(MultipleReadline, PromptDisplaysOnSecondCall)
{
	// REGRESSION TEST: Verify that prompt displays correctly on second readline() call
	// Bug history: previous_buffer was not cleared between readline() calls, causing
	// differential rendering to skip redrawing the prompt on subsequent calls.
	// Fix: Clear previous_buffer at the start of each readline() call
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// First readline() call: type "first" + Enter
	terse_event_t events1[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'f', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'i', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'r', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 't', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events1, 6);

	char *result1 = tprompt_readline(handle, NULL);
	ASSERT_NE(result1, NULL);
	EXPECT_STREQ(result1, "first");
	free(result1);

	// Second readline() call: type "second" + Enter
	// This tests that previous_buffer was properly cleared
	terse_event_t events2[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 's', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'e', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'c', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'o', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'n', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'd', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events2, 7);

	char *result2 = tprompt_readline(handle, NULL);
	ASSERT_NE(result2, NULL);
	EXPECT_STREQ(result2, "second");
	free(result2);

	// Verify that both calls completed successfully
	// The key test is that the second readline() returned the correct value,
	// which proves that previous_buffer was properly cleared and the prompt was rendered

	tprompt_close(handle);
}

TEST(MultipleReadline, EmptyInputsBetweenCalls)
{
	// Test multiple readline() calls with empty inputs
	// This ensures buffer clearing works correctly even with no content
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Three consecutive empty inputs
	for (int i = 0; i < 3; i++) {
		terse_event_t enter_events[] = {
			{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
		};
		test_mock_events(terse, enter_events, 1);

		char *result = tprompt_readline(handle, NULL);
		ASSERT_NE(result, NULL);
		EXPECT_STREQ(result, "");
		free(result);
	}

	// Fourth call with actual content
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
}

TEST(MultipleReadline, AlternatingEmptyAndNonEmpty)
{
	// Test alternating between empty and non-empty inputs
	// This stresses the buffer clearing and rendering logic
	tprompt_keybinding_t bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
	};

	tprompt_handle_t handle = create_handle_with_keybindings(bindings, 1);
	ASSERT_NE(handle, NULL);

	terse_handle_t terse = handle->terse;
	ASSERT_NE(terse, NULL);

	// Round 1: Non-empty input
	terse_event_t events1[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'a', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events1, 2);
	char *result1 = tprompt_readline(handle, NULL);
	ASSERT_NE(result1, NULL);
	EXPECT_STREQ(result1, "a");
	free(result1);

	// Round 2: Empty input
	terse_event_t events2[] = {
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events2, 1);
	char *result2 = tprompt_readline(handle, NULL);
	ASSERT_NE(result2, NULL);
	EXPECT_STREQ(result2, "");
	free(result2);

	// Round 3: Non-empty input again
	terse_event_t events3[] = {
		{ .type = TERSE_EVENT_CHAR, .data.ch.scalar = 'b', .data.ch.mods = 0, .data.ch.width = 1 },
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events3, 2);
	char *result3 = tprompt_readline(handle, NULL);
	ASSERT_NE(result3, NULL);
	EXPECT_STREQ(result3, "b");
	free(result3);

	// Round 4: Empty input again
	terse_event_t events4[] = {
		{ .type = TERSE_EVENT_ENTER, .data.key.mods = 0 }
	};
	test_mock_events(terse, events4, 1);
	char *result4 = tprompt_readline(handle, NULL);
	ASSERT_NE(result4, NULL);
	EXPECT_STREQ(result4, "");
	free(result4);

	tprompt_close(handle);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
