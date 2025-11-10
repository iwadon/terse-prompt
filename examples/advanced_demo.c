/**
 * advanced_demo.c - Advanced demonstration of tprompt framework API
 *
 * This example shows how to use the framework API with:
 * - Custom configuration (history, max input size)
 * - History persistence
 * - Error handling
 */

#include "tprompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void)
{
	printf("terse-prompt Advanced Demo\n");
	printf("===========================\n");
	printf("This demo uses the tprompt framework API with custom keybindings.\n\n");
	printf("Commands:\n");
	printf("  help   - Show this help\n");
	printf("  clear  - Clear screen (placeholder)\n");
	printf("  quit   - Exit the demo\n");
	printf("\nKeybindings:\n");
	printf("  Enter        - Confirm input and submit\n");
	printf("  Shift+Enter  - Insert newline\n");
	printf("  Ctrl+Enter   - Insert newline (Ctrl+J also works)\n");
	printf("\nHistory:\n");
	printf("  Use Up/Down arrows to navigate history\n");
	printf("  History is saved to 'demo_history.txt'\n\n");
}

int main(void)
{
	print_usage();

	// Define custom keybindings
	// Enter to confirm, Shift+Enter to insert newline, Ctrl+J also inserts newline
	// Note: terse transmits Ctrl+J as CHAR event with scalar='j' and mods=CTRL
	tprompt_keybinding_t custom_bindings[] = {
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
		TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, TERSE_MOD_SHIFT, TPROMPT_ACTION_INSERT_NEWLINE),
		TPROMPT_BIND_CHAR('j', TERSE_MOD_CTRL, TPROMPT_ACTION_INSERT_NEWLINE), // Ctrl+J
	};

	// Configure tprompt with custom options
	tprompt_options_t options = {
		.prompt = "tprompt> ",
		.max_input_size = 4096,
		.max_history_size = 100,
		.history_file = "demo_history.txt",
		.completion_callback = NULL,
		.completion_user_data = NULL,
		.completion_prefixes = NULL,
		.terse_handle = NULL,			 // Let tprompt create its own
		.flags = TPROMPT_FLAG_MULTILINE, // Multi-line mode
		.custom_keybindings = custom_bindings,
		.keybinding_count = 3
	};

	// Open tprompt handle
	tprompt_handle_t handle = tprompt_open(&options);
	if (handle == NULL) {
		tprompt_error_info_t error = tprompt_get_last_error(NULL);
		fprintf(stderr, "Error: Failed to initialize tprompt: %s\n", error.message);
		return 1;
	}

	printf("tprompt initialized successfully!\n\n");

	// Main loop
	while (1) {
		char *line = tprompt_readline(handle, NULL); // NULL uses default prompt from options

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

		// Add to history
		if (tprompt_history_add(handle, line) != 0) {
			tprompt_error_info_t error = tprompt_get_last_error(handle);
			fprintf(stderr, "Warning: Failed to add to history: %s\n", error.message);
		}

		// Process commands
		if (strcmp(line, "help") == 0) {
			print_usage();
		} else if (strcmp(line, "clear") == 0) {
			printf("(Clear screen not implemented in this demo)\n");
		} else if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
			free(line);
			break;
		} else {
			// Echo the input
			printf("You entered: \"%s\"\n", line);
		}

		free(line);
	}

	// Clean up
	printf("\nSaving history and shutting down...\n");
	tprompt_close(handle);
	printf("Goodbye!\n");

	return 0;
}
