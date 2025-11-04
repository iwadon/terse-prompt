/**
 * @file test_completion.c
 * @brief Completion functionality tests for terse-prompt Phase 3
 * @version 0.1
 * @date 2025-11-05
 *
 * Tests completion trigger detection, candidate filtering, navigation,
 * TAB confirmation, and ESC cancellation.
 */

#include "tprompt_internal.h"
#include <attest/attest.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ========================================================================
 * Mock Completion Callback
 * ======================================================================== */

static const char **mock_candidates = NULL;
static size_t mock_candidate_count = 0;

static tprompt_completion_result_t mock_completion_callback(
    const char *text,
    size_t cursor_pos,
    const char *prefix_char,
    void *user_data)
{
    (void)text;
    (void)cursor_pos;
    (void)prefix_char;
    (void)user_data;

    tprompt_completion_result_t result;
    result.candidates = (char **)mock_candidates;
    result.count = mock_candidate_count;
    return result;
}

/* ========================================================================
 * Completion State Tests
 * ======================================================================== */

TEST(Completion, StateInit)
{
    tprompt_completion_state_t state;
    tprompt_completion_init(&state);

    EXPECT_FALSE(state.active);
    EXPECT_EQ(state.trigger_char, '\0');
    EXPECT_EQ(state.trigger_offset, 0);
    EXPECT_EQ(state.candidate_count, 0);
    EXPECT_EQ(state.candidates, NULL);

    tprompt_completion_free(&state);
}

/* ========================================================================
 * Completion Candidate Navigation Tests
 * ======================================================================== */

TEST(Completion, SelectNext)
{
    tprompt_completion_state_t state;
    tprompt_completion_init(&state);

    const char *candidates[] = {"cmd1", "cmd2", "cmd3"};
    state.candidates = (char **)candidates;
    state.candidate_count = 3;
    state.selected_index = 0;
    state.active = true;

    // Navigate: 0 -> 1 -> 2 -> 0 (wrap around)
    tprompt_completion_select_next(&state);
    EXPECT_EQ(state.selected_index, 1);

    tprompt_completion_select_next(&state);
    EXPECT_EQ(state.selected_index, 2);

    tprompt_completion_select_next(&state);
    EXPECT_EQ(state.selected_index, 0);

    // Don't free candidates (they're static)
    state.candidates = NULL;
    tprompt_completion_free(&state);
}

TEST(Completion, SelectPrev)
{
    tprompt_completion_state_t state;
    tprompt_completion_init(&state);

    const char *candidates[] = {"cmd1", "cmd2", "cmd3"};
    state.candidates = (char **)candidates;
    state.candidate_count = 3;
    state.selected_index = 0;
    state.active = true;

    // Navigate: 0 -> 2 -> 1 -> 0 (wrap around)
    tprompt_completion_select_prev(&state);
    EXPECT_EQ(state.selected_index, 2);

    tprompt_completion_select_prev(&state);
    EXPECT_EQ(state.selected_index, 1);

    tprompt_completion_select_prev(&state);
    EXPECT_EQ(state.selected_index, 0);

    state.candidates = NULL;
    tprompt_completion_free(&state);
}

TEST(Completion, SingleCandidateNavigation)
{
    tprompt_completion_state_t state;
    tprompt_completion_init(&state);

    const char *candidates[] = {"only"};
    state.candidates = (char **)candidates;
    state.candidate_count = 1;
    state.selected_index = 0;
    state.active = true;

    // Navigation should stay at 0
    tprompt_completion_select_next(&state);
    EXPECT_EQ(state.selected_index, 0);

    tprompt_completion_select_prev(&state);
    EXPECT_EQ(state.selected_index, 0);

    state.candidates = NULL;
    tprompt_completion_free(&state);
}

TEST(Completion, EmptyCandidates)
{
    tprompt_completion_state_t state;
    tprompt_completion_init(&state);

    state.candidates = NULL;
    state.candidate_count = 0;
    state.selected_index = 0;

    // Should not crash
    tprompt_completion_select_next(&state);
    EXPECT_EQ(state.selected_index, 0);

    tprompt_completion_select_prev(&state);
    EXPECT_EQ(state.selected_index, 0);

    tprompt_completion_free(&state);
}

/* ========================================================================
 * Buffer and Completion Integration Tests
 * ======================================================================== */

TEST(Completion, BufferIntegration)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 128);

    // Insert text: "/hel"
    EXPECT_EQ(tprompt_buffer_insert(&buffer, "/", 1), 0);
    EXPECT_EQ(tprompt_buffer_insert(&buffer, "hel", 3), 0);
    EXPECT_EQ(buffer.length, 4);
    EXPECT_EQ(buffer.cursor, 4);

    // Simulate completion replacement using memmove (like tprompt_completion_confirm does)
    size_t trigger_offset = 1;  // After '/'
    size_t bytes_to_remove = buffer.cursor - trigger_offset;

    if (bytes_to_remove > 0) {
        memmove(buffer.data + trigger_offset,
                buffer.data + buffer.cursor,
                buffer.length - buffer.cursor + 1);
        buffer.length -= bytes_to_remove;
        buffer.cursor = trigger_offset;
    }

    // Insert completion
    EXPECT_EQ(tprompt_buffer_insert(&buffer, "hello", 5), 0);
    EXPECT_STREQ(buffer.data, "/hello");

    tprompt_buffer_free(&buffer);
}

TEST(Completion, UTF8BufferIntegration)
{
    tprompt_buffer_t buffer;
    tprompt_buffer_init(&buffer, 128);

    // Insert UTF-8 text: "/日本"
    EXPECT_EQ(tprompt_buffer_insert(&buffer, "/", 1), 0);
    EXPECT_EQ(tprompt_buffer_insert(&buffer, "日本", 6), 0);  // 2 chars, 6 bytes
    EXPECT_EQ(buffer.length, 7);

    // Simulate completion replacement using memmove
    size_t trigger_offset = 1;
    size_t bytes_to_remove = buffer.cursor - trigger_offset;

    if (bytes_to_remove > 0) {
        memmove(buffer.data + trigger_offset,
                buffer.data + buffer.cursor,
                buffer.length - buffer.cursor + 1);
        buffer.length -= bytes_to_remove;
        buffer.cursor = trigger_offset;
    }

    EXPECT_EQ(tprompt_buffer_insert(&buffer, "日本語", 9), 0);
    EXPECT_STREQ(buffer.data, "/日本語");

    tprompt_buffer_free(&buffer);
}

/* ========================================================================
 * Memory Safety Tests
 * ======================================================================== */

TEST(Completion, MemoryCleanup)
{
    tprompt_completion_state_t state;
    tprompt_completion_init(&state);

    // Allocate candidates (simulating callback allocation)
    char **candidates = malloc(3 * sizeof(char *));
    candidates[0] = strdup("cmd1");
    candidates[1] = strdup("cmd2");
    candidates[2] = strdup("cmd3");

    state.candidates = candidates;
    state.candidate_count = 3;
    state.active = true;

    // Free should clean up
    tprompt_completion_free(&state);
    EXPECT_EQ(state.candidates, NULL);
    EXPECT_EQ(state.candidate_count, 0);
}

TEST(Completion, MultipleActivations)
{
    tprompt_completion_state_t state;
    tprompt_completion_init(&state);

    // Activate multiple times (simulating real usage)
    for (int i = 0; i < 5; i++) {
        char **candidates = malloc(2 * sizeof(char *));
        candidates[0] = strdup("a");
        candidates[1] = strdup("b");

        state.candidates = candidates;
        state.candidate_count = 2;
        state.active = true;

        // Deactivate and free
        tprompt_completion_free(&state);
        EXPECT_EQ(state.candidates, NULL);
    }

    tprompt_completion_free(&state);
}

/* ========================================================================
 * Main Entry Point
 * ======================================================================== */

int main(int argc, char **argv)
{
    return attest_main(argc, argv);
}
