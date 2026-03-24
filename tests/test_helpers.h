/**
 * @file test_helpers.h
 * @brief Test helper functions for terse-prompt tests
 *
 * Provides utilities for creating mock terse handles in test mode,
 * making tests TTY-independent.
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <terse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define test_unlink _unlink
#else
#include <unistd.h>
#define test_unlink unlink
#endif

#ifdef TERSE_ENABLE_TEST_MODE
#include <terse_test.h>
#endif

/**
 * @brief Create a terse handle for testing with custom size
 *
 * In test mode (TERSE_ENABLE_TEST_MODE), creates a mock terse handle with:
 * - Custom terminal size (rows x cols)
 * - P1 profile (basic colors, no mouse)
 * - Recording disabled initially
 *
 * In normal mode, creates a regular terse handle with P0 profile
 * (works in non-TTY environments).
 *
 * @param rows terminal height (rows), or 0 for default (24)
 * @param cols terminal width (columns), or 0 for default (80)
 * @return terse handle, or NULL on failure
 */
static inline terse_handle_t test_create_terse_handle_sized(int rows, int cols)
{
	terse_handle_t handle = terse_open(TERSE_PROFILE_AUTO, NULL);
	if (!handle) {
		return NULL;
	}

#ifdef TERSE_ENABLE_TEST_MODE
	// Use defaults if not specified
	if (rows == 0)
		rows = 24;
	if (cols == 0)
		cols = 80;

	// Mock custom terminal size
	terse_test_mock_size(handle, rows, cols);

	// Mock P1 capabilities (basic colors, no advanced features)
	terse_capabilities_t mock_caps = {
		.profile = TERSE_P1,
		.has_basic_output = 1,
		.has_cursor_visibility = 1,
		.has_move_absolute = 1,
		.has_move_relative = 1,
		.has_clear_line = 1,
		.has_clear_screen = 1,
		.has_size = 1,
		.has_sgr_basic = 1,
		.has_sgr_extended = 0,
		.has_truecolor = 0,
		.has_text_styles = 1,
		.mouse = TERSE_MOUSE_NONE,
		.has_bracketed_paste = 0,
		.has_title = 0,
		.has_hyperlinks = 0,
		.has_cursor_shape = 0,
		.colors = TERSE_COLOR_BASIC16,
		.effects = 0,
		.has_clipboard_write = 0,
		.images = TERSE_IMAGE_NONE,
		.notifications = 0,
		.keyboard_features = 0
	};
	terse_test_mock_capabilities(handle, &mock_caps);
#else
	(void)rows;
	(void)cols;
#endif

	return handle;
}

/**
 * @brief Create a terse handle for testing
 *
 * In test mode (TERSE_ENABLE_TEST_MODE), creates a mock terse handle with:
 * - 80x24 terminal size
 * - P1 profile (basic colors, no mouse)
 * - Recording disabled initially
 *
 * In normal mode, creates a regular terse handle with P0 profile
 * (works in non-TTY environments).
 *
 * @return terse handle, or NULL on failure
 */
static inline terse_handle_t test_create_terse_handle(void)
{
	return test_create_terse_handle_sized(24, 80);
}

/**
 * @brief Start recording terse API calls (test mode only)
 *
 * @param handle terse handle
 */
static inline void test_start_recording(terse_handle_t handle)
{
#ifdef TERSE_ENABLE_TEST_MODE
	terse_test_start_recording(handle);
#else
	(void)handle;
#endif
}

/**
 * @brief Stop recording terse API calls (test mode only)
 *
 * @param handle terse handle
 */
static inline void test_stop_recording(terse_handle_t handle)
{
#ifdef TERSE_ENABLE_TEST_MODE
	terse_test_stop_recording(handle);
#else
	(void)handle;
#endif
}

/**
 * @brief Get recorded API calls (test mode only)
 *
 * @param handle terse handle
 * @param out_count pointer to store call count
 * @return pointer to call records, or NULL if not in test mode
 */
#ifdef TERSE_ENABLE_TEST_MODE
static inline const terse_call_record_t *test_get_calls(terse_handle_t handle, int *out_count)
{
	return terse_test_get_calls(handle, out_count);
}
#else
static inline const void *test_get_calls(terse_handle_t handle, int *out_count)
{
	(void)handle;
	if (out_count)
		*out_count = 0;
	return NULL;
}
#endif

/**
 * @brief Clear recorded API calls (test mode only)
 *
 * @param handle terse handle
 */
static inline void test_clear_calls(terse_handle_t handle)
{
#ifdef TERSE_ENABLE_TEST_MODE
	terse_test_clear_calls(handle);
#else
	(void)handle;
#endif
}

/**
 * @brief Mock input events (test mode only)
 *
 * @param handle terse handle
 * @param events array of events to inject
 * @param count number of events
 */
static inline void test_mock_events(terse_handle_t handle,
	const terse_event_t *events,
	int count)
{
#ifdef TERSE_ENABLE_TEST_MODE
	terse_test_mock_events(handle, events, count);
#else
	(void)handle;
	(void)events;
	(void)count;
#endif
}

/**
 * @brief Reset all mocks (test mode only)
 *
 * @param handle terse handle
 */
static inline void test_reset_mocks(terse_handle_t handle)
{
#ifdef TERSE_ENABLE_TEST_MODE
	terse_test_reset_mocks(handle);
#else
	(void)handle;
#endif
}

/**
 * @brief Create a temporary file for testing (cross-platform)
 *
 * @param path_buf buffer to receive the temporary file path
 * @param buf_size size of path_buf
 * @return 0 on success, -1 on failure
 */
static inline int test_create_temp_file(char *path_buf, size_t buf_size)
{
#ifdef _WIN32
	char tmp_dir[260];
	if (!GetTempPathA(sizeof(tmp_dir), tmp_dir)) {
		return -1;
	}
	char tmp_path[260];
	if (!GetTempFileNameA(tmp_dir, "tp_", 0, tmp_path)) {
		return -1;
	}
	if (strlen(tmp_path) >= buf_size) {
		return -1;
	}
	strcpy(path_buf, tmp_path);
	return 0;
#else
	if (buf_size < 32) {
		return -1;
	}
	strcpy(path_buf, "/tmp/tprompt_test_XXXXXX");
	int fd = mkstemp(path_buf);
	if (fd < 0) {
		return -1;
	}
	close(fd);
	return 0;
#endif
}

#endif /* TEST_HELPERS_H */
