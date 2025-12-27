/**
 * @file test_history.c
 * @brief History management tests for terse-prompt Phase 2
 * @version 0.1
 * @date 2025-11-02
 *
 * Tests history add, navigation, duplicate skipping, LRU eviction,
 * and file persistence.
 */

#include "tprompt_internal.h"
#include <attest/attest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ========================================================================
 * History Basic Operations Tests
 * ======================================================================== */

TEST(History, AddAndNavigate)
{
	tprompt_history_t history;
	tprompt_history_init(&history, 100);

	// Add three entries
	EXPECT_EQ(tprompt_history_add_internal(&history, "first"), 0);
	EXPECT_EQ(tprompt_history_add_internal(&history, "second"), 0);
	EXPECT_EQ(tprompt_history_add_internal(&history, "third"), 0);
	EXPECT_EQ(history.count, 3);

	// Navigate backward (prev returns newer-to-older)
	const char *entry = tprompt_history_prev(&history);
	EXPECT_NOT_NULL(entry);
	EXPECT_STREQ(entry, "third");

	entry = tprompt_history_prev(&history);
	EXPECT_NOT_NULL(entry);
	EXPECT_STREQ(entry, "second");

	entry = tprompt_history_prev(&history);
	EXPECT_NOT_NULL(entry);
	EXPECT_STREQ(entry, "first");

	// At the end, should return NULL
	entry = tprompt_history_prev(&history);
	EXPECT_NULL(entry);

	// Navigate forward
	entry = tprompt_history_next(&history);
	EXPECT_NOT_NULL(entry);
	EXPECT_STREQ(entry, "second");

	entry = tprompt_history_next(&history);
	EXPECT_NOT_NULL(entry);
	EXPECT_STREQ(entry, "third");

	// At the newest, should return NULL
	entry = tprompt_history_next(&history);
	EXPECT_NULL(entry);

	tprompt_history_free(&history);
}

TEST(History, DuplicateSkip)
{
	tprompt_history_t history;
	tprompt_history_init(&history, 100);

	// Add entries with consecutive duplicates
	EXPECT_EQ(tprompt_history_add_internal(&history, "command1"), 0);
	EXPECT_EQ(tprompt_history_add_internal(&history, "command2"), 0);
	EXPECT_EQ(tprompt_history_add_internal(&history, "command2"), 0); // Consecutive duplicate - should be skipped
	EXPECT_EQ(tprompt_history_add_internal(&history, "command3"), 0);

	// Should only have 3 entries (consecutive duplicate was skipped)
	EXPECT_EQ(history.count, 3);

	// Verify order: command3 (newest), command2, command1 (oldest)
	const char *entry = tprompt_history_prev(&history);
	EXPECT_STREQ(entry, "command3");

	entry = tprompt_history_prev(&history);
	EXPECT_STREQ(entry, "command2");

	entry = tprompt_history_prev(&history);
	EXPECT_STREQ(entry, "command1");

	tprompt_history_free(&history);
}

TEST(History, LRUEviction)
{
	tprompt_history_t history;
	tprompt_history_init(&history, 3); // Max size = 3

	// Add 5 entries (should evict 2 oldest)
	EXPECT_EQ(tprompt_history_add_internal(&history, "entry1"), 0);
	EXPECT_EQ(tprompt_history_add_internal(&history, "entry2"), 0);
	EXPECT_EQ(tprompt_history_add_internal(&history, "entry3"), 0);
	EXPECT_EQ(history.count, 3);

	EXPECT_EQ(tprompt_history_add_internal(&history, "entry4"), 0);
	EXPECT_EQ(history.count, 3); // Still 3 entries

	EXPECT_EQ(tprompt_history_add_internal(&history, "entry5"), 0);
	EXPECT_EQ(history.count, 3);

	// Should only have entry3, entry4, entry5 (entry1 and entry2 evicted)
	const char *entry = tprompt_history_prev(&history);
	EXPECT_STREQ(entry, "entry5");

	entry = tprompt_history_prev(&history);
	EXPECT_STREQ(entry, "entry4");

	entry = tprompt_history_prev(&history);
	EXPECT_STREQ(entry, "entry3");

	entry = tprompt_history_prev(&history);
	EXPECT_NULL(entry); // No more entries

	tprompt_history_free(&history);
}

TEST(History, EmptyNavigation)
{
	tprompt_history_t history;
	tprompt_history_init(&history, 100);

	// Navigation on empty history should return NULL
	EXPECT_NULL(tprompt_history_prev(&history));
	EXPECT_NULL(tprompt_history_next(&history));

	tprompt_history_free(&history);
}

TEST(History, FilePersistence)
{
	// Create temporary file
	char temp_file[] = "/tmp/tprompt_test_XXXXXX";
	int fd = mkstemp(temp_file);
	ASSERT_GE(fd, 0);
	close(fd);

	// Create history and add entries
	tprompt_history_t history1;
	tprompt_history_init(&history1, 100);
	EXPECT_EQ(tprompt_history_add_internal(&history1, "cmd1"), 0);
	EXPECT_EQ(tprompt_history_add_internal(&history1, "cmd2"), 0);
	EXPECT_EQ(tprompt_history_add_internal(&history1, "cmd3"), 0);

	// Save to file
	EXPECT_EQ(tprompt_history_save_internal(&history1, temp_file), 0);
	tprompt_history_free(&history1);

	// Load into new history
	tprompt_history_t history2;
	tprompt_history_init(&history2, 100);
	EXPECT_EQ(tprompt_history_load_internal(&history2, temp_file, 0, 0), 0);
	EXPECT_EQ(history2.count, 3);

	// Verify loaded entries (should be in same order)
	const char *entry = tprompt_history_prev(&history2);
	EXPECT_STREQ(entry, "cmd3");

	entry = tprompt_history_prev(&history2);
	EXPECT_STREQ(entry, "cmd2");

	entry = tprompt_history_prev(&history2);
	EXPECT_STREQ(entry, "cmd1");

	tprompt_history_free(&history2);

	// Cleanup
	unlink(temp_file);
}

TEST(History, ResetPosition)
{
	tprompt_history_t history;
	tprompt_history_init(&history, 100);

	EXPECT_EQ(tprompt_history_add_internal(&history, "cmd1"), 0);
	EXPECT_EQ(tprompt_history_add_internal(&history, "cmd2"), 0);

	// Navigate
	tprompt_history_prev(&history);
	EXPECT_NOT_NULL(history.current);

	// Reset should set current to NULL
	tprompt_history_reset_position(&history);
	EXPECT_NULL(history.current);

	tprompt_history_free(&history);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(int argc, char **argv)
{
	return attest_main(argc, argv);
}
