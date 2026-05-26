/**
 * @file status_line_demo.c
 * @brief Demonstration of status line functionality in terse-prompt
 *
 * This example shows three different use cases for status lines:
 * 1. Debug status (built-in)
 * 2. Simple single-line status
 * 3. Multi-line status with ANSI colors
 */

#include "example_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tprompt.h>

/* ========================================================================
 * Example 1: Debug Status (Built-in)
 * ======================================================================== */

void demo_debug_status(void)
{
	printf("=== Demo 1: Built-in Debug Status ===\n");
	printf("Shows cursor position (x, y) and goal column.\n");
	printf("Try moving around with arrow keys!\n");
	printf("Press Ctrl+D to exit.\n\n");

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.prompt = "debug> ";
	opts.flags = TPROMPT_FLAG_MULTILINE | TPROMPT_FLAG_SHOW_DEBUG_STATUS;

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		fprintf(stderr, "Failed to open tprompt\n");
		return;
	}

	char *input = tprompt_readline(handle, NULL);
	if (input) {
		printf("\nYou entered: %s\n", input);
		free(input);
	}

	tprompt_close(handle);
}

/* ========================================================================
 * Example 2: Simple Single-Line Status
 * ======================================================================== */

int simple_status_callback(tprompt_handle_t handle, char *buffer, size_t size, void *user_data)
{
	(void)user_data;

	size_t line = tprompt_get_cursor_line(handle);
	size_t col = tprompt_get_cursor_column(handle);

	snprintf(buffer, size, "Line %zu, Col %zu", line + 1, col + 1);
	return 1; // 1 line
}

void demo_simple_status(void)
{
	printf("\n=== Demo 2: Simple Single-Line Status ===\n");
	printf("Shows line and column numbers.\n");
	printf("Press Ctrl+D to exit.\n\n");

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.prompt = "edit> ";
	opts.flags = TPROMPT_FLAG_MULTILINE;
	opts.status_line_callback = simple_status_callback;
	opts.status_line_user_data = NULL;

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		fprintf(stderr, "Failed to open tprompt\n");
		return;
	}

	char *input = tprompt_readline(handle, NULL);
	if (input) {
		printf("\nYou entered: %s\n", input);
		free(input);
	}

	tprompt_close(handle);
}

/* ========================================================================
 * Example 3: Multi-Line Status with ANSI Colors
 * ======================================================================== */

typedef struct {
	const char *mode;
	const char *filename;
	int unsaved_changes;
} app_state_t;

int multiline_status_callback(tprompt_handle_t handle, char *buffer, size_t size, void *user_data)
{
	app_state_t *state = (app_state_t *)user_data;

	size_t line = tprompt_get_cursor_line(handle);
	size_t col = tprompt_get_cursor_column(handle);

	snprintf(buffer, size,
		"\x1b[1mMode:\x1b[0m %s | \x1b[1mFile:\x1b[0m %s %s\n"
		"\x1b[1mPos:\x1b[0m Ln %zu, Col %zu",
		state->mode,
		state->filename,
		state->unsaved_changes ? "\x1b[33m[Modified]\x1b[0m" : "",
		line + 1, col + 1);

	return 2; // 2 lines
}

void demo_multiline_status(void)
{
	printf("\n=== Demo 3: Multi-Line Status with Colors ===\n");
	printf("Shows application state and cursor position.\n");
	printf("Press Ctrl+D to exit.\n\n");

	app_state_t state = {
		.mode = "INSERT",
		.filename = "example.txt",
		.unsaved_changes = 1
	};

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.prompt = "vim> ";
	opts.flags = TPROMPT_FLAG_MULTILINE;
	opts.status_line_callback = multiline_status_callback;
	opts.status_line_user_data = &state;

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		fprintf(stderr, "Failed to open tprompt\n");
		return;
	}

	char *input = tprompt_readline(handle, NULL);
	if (input) {
		printf("\nYou entered: %s\n", input);
		free(input);
	}

	tprompt_close(handle);
}

/* ========================================================================
 * Example 4: Runtime Status Line Changes
 * ======================================================================== */

void demo_runtime_changes(void)
{
	printf("\n=== Demo 4: Runtime Status Line Changes ===\n");
	printf("Demonstrates changing status line callback at runtime.\n");
	printf("Type 'simple' for simple status, 'multi' for multi-line, 'off' to disable.\n");
	printf("Press Ctrl+D to exit.\n\n");

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.prompt = "cmd> ";
	opts.flags = TPROMPT_FLAG_MULTILINE;

	app_state_t state = {
		.mode = "COMMAND",
		.filename = "runtime_demo.txt",
		.unsaved_changes = 0
	};

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		fprintf(stderr, "Failed to open tprompt\n");
		return;
	}

	while (1) {
		char *input = tprompt_readline(handle, NULL);
		if (!input) {
			break; // EOF
		}

		// Process command
		if (strcmp(input, "simple") == 0) {
			printf("Switching to simple status...\n");
			tprompt_set_status_line_callback(handle, simple_status_callback, NULL);
		} else if (strcmp(input, "multi") == 0) {
			printf("Switching to multi-line status...\n");
			tprompt_set_status_line_callback(handle, multiline_status_callback, &state);
		} else if (strcmp(input, "off") == 0) {
			printf("Disabling status line...\n");
			tprompt_set_status_line_callback(handle, NULL, NULL);
		} else {
			printf("You entered: %s\n", input);
		}

		free(input);
	}

	tprompt_close(handle);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(int argc, char **argv)
{
	example_setup_console_utf8();

	printf("terse-prompt Status Line Demo\n");
	printf("==============================\n\n");

	if (argc > 1) {
		// Run specific demo
		int demo_num = atoi(argv[1]);
		switch (demo_num) {
		case 1:
			demo_debug_status();
			break;
		case 2:
			demo_simple_status();
			break;
		case 3:
			demo_multiline_status();
			break;
		case 4:
			demo_runtime_changes();
			break;
		default:
			printf("Usage: %s [1|2|3|4]\n", argv[0]);
			printf("  1: Debug status\n");
			printf("  2: Simple status\n");
			printf("  3: Multi-line status\n");
			printf("  4: Runtime changes\n");
			return 1;
		}
	} else {
		// Run all demos
		demo_debug_status();
		demo_simple_status();
		demo_multiline_status();
		demo_runtime_changes();
	}

	printf("\nDemo completed!\n");
	return 0;
}
