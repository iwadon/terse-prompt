/**
 * tprompt_debug.c - Internal debugging tool for terse-prompt development
 *
 * This tool is for terse-prompt library developers to debug and verify
 * internal behavior. It displays real-time debug information including:
 * - Cursor position (physical x/y coordinates)
 * - Goal column (for vertical navigation)
 * - Buffer state
 *
 * NOT intended as an example for library users - see examples/ directory
 * for user-facing demonstration code.
 */

#include "tprompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void)
{
	printf("terse-prompt Internal Debug Tool\n");
	printf("==================================\n");
	printf("This tool is for library development and debugging.\n");
	printf("Status line shows internal state (cursor position, goal column, etc.)\n\n");
	printf("Commands:\n");
	printf("  help   - Show this help\n");
	printf("  quit   - Exit the tool\n");
	printf("\nKeybindings:\n");
	printf("  Enter        - Confirm input and submit\n");
	printf("  Shift+Enter  - Insert newline\n");
	printf("  Ctrl+J       - Insert newline\n");
	printf("\nNavigation:\n");
	printf("  Arrows       - Move cursor / Navigate history\n");
	printf("  Home/End     - Move to line start/end (staged: physical then logical)\n");
	printf("  Ctrl+A/E     - Move to logical line start/end\n");
	printf("  Ctrl+P/N     - Previous/Next history (Emacs-style)\n\n");
}

int main(void)
{
	print_usage();

	// Custom keybindings for debugging
	tprompt_keybinding_t custom_bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, TERSE_MOD_SHIFT, TPROMPT_ACTION_INSERT_NEWLINE),
		TPROMPT_BIND_CHAR('j', TERSE_MOD_CTRL, TPROMPT_ACTION_INSERT_NEWLINE), // Ctrl+J
	};

	// Configure with debug status line enabled
	tprompt_options_t options = {
		.prompt = "debug> ",
		.max_input_size = 4096,
		.max_history_size = 100,
		.history_file = ".tprompt_debug_history",
		.completion_callback = NULL,
		.completion_user_data = NULL,
		.completion_prefixes = NULL,
		.terse_handle = NULL,
		.flags = TPROMPT_FLAG_MULTILINE | TPROMPT_FLAG_SHOW_DEBUG_STATUS, // Enable debug status
		.custom_keybindings = custom_bindings,
		.keybinding_count = 3,
		.validation_callback = NULL,
		.validation_user_data = NULL,
		.status_line_callback = NULL, // Use built-in debug callback
		.status_line_user_data = NULL
	};

	// Open tprompt handle
	tprompt_handle_t handle = tprompt_open(&options);
	if (handle == NULL) {
		tprompt_error_info_t error = tprompt_get_last_error(NULL);
		fprintf(stderr, "Error: Failed to initialize tprompt: %s\n", error.message);
		return 1;
	}

	printf("Debug tool initialized. Status line shows internal state.\n\n");

	// Main loop
	while (1) {
		char *line = tprompt_readline(handle, NULL);

		if (line == NULL) {
			// Check if it's EOF or an error
			tprompt_error_info_t error = tprompt_get_last_error(handle);
			if (error.category != TPROMPT_ERROR_NONE) {
				fprintf(stderr, "\nError: %s\n", error.message);
			} else {
				printf("\nEOF detected (Ctrl+D)\n");
			}
			break;
		}

		// Handle empty input
		if (strlen(line) == 0) {
			free(line);
			continue;
		}

		// Process commands
		if (strcmp(line, "help") == 0) {
			print_usage();
		} else if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
			free(line);
			break;
		} else {
			// Echo the input with length info
			printf("Input: \"%s\" (length: %zu bytes)\n", line, strlen(line));
		}

		free(line);
	}

	// Clean up
	printf("\nShutting down debug tool...\n");
	tprompt_close(handle);
	printf("Goodbye!\n");

	return 0;
}
