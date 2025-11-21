/**
 * completion_demo.c - Completion system demonstration
 *
 * This example shows how to use tprompt's completion system with:
 * - Custom completion prefixes ('/' for commands, '@' for mentions)
 * - Incremental filtering based on user input
 * - Multiple completion contexts
 */

#include "tprompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Sample data for completion
static const char *commands[] = {
	"help",
	"clear",
	"quit",
	"exit",
	"history",
	"status",
	"config",
	"hello",
	NULL
};

static const char *mentions[] = {
	"alice",
	"bob",
	"charlie",
	"david",
	"eve",
	"admin",
	NULL
};

/**
 * Helper function to check if string starts with prefix (case-insensitive)
 */
static int starts_with_ignore_case(const char *str, const char *prefix)
{
	while (*prefix) {
		if (tolower((unsigned char)*str) != tolower((unsigned char)*prefix)) {
			return 0;
		}
		str++;
		prefix++;
	}
	return 1;
}

/**
 * Completion callback for commands (prefix: '/')
 */
static tprompt_completion_result_t complete_commands(const char *text, size_t cursor_pos, void *user_data)
{
	(void)cursor_pos; // Unused in this simple demo
	(void)user_data; // Unused

	tprompt_completion_result_t result = {0};

	// Extract the command part after '/'
	const char *command_start = strchr(text, '/');
	if (command_start == NULL) {
		return result; // No '/' found, no completion
	}
	command_start++; // Skip '/'

	// Count matching commands
	size_t match_count = 0;
	for (size_t i = 0; commands[i] != NULL; i++) {
		if (starts_with_ignore_case(commands[i], command_start)) {
			match_count++;
		}
	}

	if (match_count == 0) {
		return result; // No matches
	}

	// Allocate array for candidates
	result.candidates = malloc(match_count * sizeof(char *));
	if (result.candidates == NULL) {
		return result;
	}

	// Collect matching commands
	result.count = 0;
	for (size_t i = 0; commands[i] != NULL; i++) {
		if (starts_with_ignore_case(commands[i], command_start)) {
			result.candidates[result.count] = strdup(commands[i]);
			if (result.candidates[result.count] == NULL) {
				// Cleanup on error
				for (size_t j = 0; j < result.count; j++) {
					free((void *)result.candidates[j]);
				}
				free(result.candidates);
				result.candidates = NULL;
				result.count = 0;
				return result;
			}
			result.count++;
		}
	}

	return result;
}

/**
 * Completion callback for mentions (prefix: '@')
 */
static tprompt_completion_result_t complete_mentions(const char *text, size_t cursor_pos, void *user_data)
{
	(void)cursor_pos; // Unused in this simple demo
	(void)user_data; // Unused

	tprompt_completion_result_t result = {0};

	// Extract the mention part after '@'
	const char *mention_start = strrchr(text, '@');
	if (mention_start == NULL) {
		return result; // No '@' found, no completion
	}
	mention_start++; // Skip '@'

	// Count matching mentions
	size_t match_count = 0;
	for (size_t i = 0; mentions[i] != NULL; i++) {
		if (starts_with_ignore_case(mentions[i], mention_start)) {
			match_count++;
		}
	}

	if (match_count == 0) {
		return result; // No matches
	}

	// Allocate array for candidates
	result.candidates = malloc(match_count * sizeof(char *));
	if (result.candidates == NULL) {
		return result;
	}

	// Collect matching mentions
	result.count = 0;
	for (size_t i = 0; mentions[i] != NULL; i++) {
		if (starts_with_ignore_case(mentions[i], mention_start)) {
			result.candidates[result.count] = strdup(mentions[i]);
			if (result.candidates[result.count] == NULL) {
				// Cleanup on error
				for (size_t j = 0; j < result.count; j++) {
					free((void *)result.candidates[j]);
				}
				free(result.candidates);
				result.candidates = NULL;
				result.count = 0;
				return result;
			}
			result.count++;
		}
	}

	return result;
}

/**
 * Main completion callback that dispatches based on prefix
 */
static tprompt_completion_result_t completion_callback(
	const char *text,
	size_t cursor_pos,
	const char *prefix_char,
	void *user_data)
{
	// Dispatch based on which prefix triggered completion
	if (strcmp(prefix_char, "/") == 0) {
		return complete_commands(text, cursor_pos, user_data);
	} else if (strcmp(prefix_char, "@") == 0) {
		return complete_mentions(text, cursor_pos, user_data);
	}

	// Unknown prefix
	tprompt_completion_result_t empty = {0};
	return empty;
}

static void print_usage(void)
{
	printf("terse-prompt Completion Demo\n");
	printf("=============================\n");
	printf("This demo shows the completion system with two prefixes:\n\n");
	printf("Commands (prefix: '/'):\n");
	printf("  Type '/' to see available commands\n");
	printf("  Available: /help, /clear, /quit, /exit, /history, /status, /config, /hello\n\n");
	printf("Mentions (prefix: '@'):\n");
	printf("  Type '@' to see available users\n");
	printf("  Available: @alice, @bob, @charlie, @david, @eve, @admin\n\n");
	printf("Completion Navigation:\n");
	printf("  Up/Down   - Select candidate\n");
	printf("  Tab       - Confirm selection\n");
	printf("  Esc       - Cancel completion\n");
	printf("  * Completion filters as you type\n\n");
	printf("Other Commands:\n");
	printf("  quit      - Exit the demo\n");
	printf("  Ctrl+D    - Exit the demo\n\n");
}

int main(void)
{
	print_usage();

	// Configure tprompt with completion enabled
	// Note: completion_prefixes is a string containing all trigger characters
	tprompt_options_t options = {
		.prompt = "> ",
		.continuation_prompt = NULL,
		.history_file = NULL, // No history file for this demo
		.max_input_size = 4096,
		.max_history_size = 100,
		.completion_callback = completion_callback,
		.completion_user_data = NULL,
		.completion_prefixes = "/@", // Both '/' and '@' trigger completion
		.terse_handle = NULL,
		.flags = 0, // Single-line mode for simplicity
		.custom_keybindings = NULL,
		.keybinding_count = 0,
		.validation_callback = NULL,
		.validation_user_data = NULL,
		.status_line_callback = NULL,
		.status_line_user_data = NULL
	};

	// Open tprompt handle
	tprompt_handle_t handle = tprompt_open(&options);
	if (handle == NULL) {
		tprompt_error_info_t error = tprompt_get_last_error(NULL);
		fprintf(stderr, "Error: Failed to initialize tprompt: %s\n", error.message);
		return 1;
	}

	printf("Completion system ready! Try typing '/' or '@'.\n\n");

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

		// Process input
		if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
			free(line);
			break;
		} else if (strcmp(line, "help") == 0) {
			print_usage();
		} else {
			// Echo the input
			printf("You entered: \"%s\"\n", line);
		}

		free(line);
	}

	// Clean up
	printf("\nShutting down...\n");
	tprompt_close(handle);
	printf("Goodbye!\n");

	return 0;
}
