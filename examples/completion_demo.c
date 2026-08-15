/**
 * completion_demo.c - Completion system demonstration
 *
 * This example shows how to use tprompt's completion system with:
 * - Custom completion prefixes ('/' for commands, '@' for mentions)
 * - Incremental filtering based on user input
 * - Multiple completion contexts
 */

#include "example_console.h"
#include "tprompt.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Sample data for completion with descriptions
typedef struct {
	const char *text;
	const char *description;
} command_info_t;

static const command_info_t commands[] = {
	{ "help", "ヘルプを表示する" },
	{ "clear", "クリアする" },
	{ "quit", "終了する" },
	{ "exit", "終了する" },
	{ "history", "入力履歴を表示する" },
	{ "status", "ステータスを表示する" },
	{ "config", "設定を表示する" },
	{ "hello", "挨拶する" },
	{ "version", "バージョン情報を表示する" },
	{ "about", "アプリケーション情報を表示する" },
	{ "save", "現在の状態を保存する" },
	{ "load", "保存した状態を読み込む" },
	{ "export", "データをエクスポートする" },
	{ "import", "データをインポートする" },
	{ "settings", "設定画面を開く" },
	{ "refresh", "表示を更新する" },
	{ NULL, NULL }
};

// Mentions without descriptions (simple text list)
static const char *mentions[] = {
	"alice",
	"bob",
	"charlie",
	"david",
	"eve",
	"frank",
	"grace",
	"henry",
	"iris",
	"jack",
	"kate",
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
 * ISO C11-only strdup() replacement (POSIX strdup() is unavailable under
 * strict ISO C11). Unlike POSIX strdup(), a NULL argument returns NULL
 * instead of invoking undefined behavior.
 */
static char *demo_strdup(const char *s)
{
	if (s == NULL) {
		return NULL;
	}

	size_t len = strlen(s) + 1;
	char *copy = malloc(len);
	if (copy == NULL) {
		return NULL;
	}

	memcpy(copy, s, len);
	return copy;
}

/**
 * Extended completion callback for commands (prefix: '/')
 */
static int complete_commands_ex(const char *text, size_t cursor_pos, void *user_data,
	tprompt_completion_candidate_t **candidates, size_t *count)
{
	(void)cursor_pos; // Unused in this simple demo
	(void)user_data;  // Unused

	*candidates = NULL;
	*count = 0;

	// Extract the command part after '/'
	const char *command_start = strchr(text, '/');
	if (command_start == NULL) {
		return 0; // No '/' found, no completion
	}
	command_start++; // Skip '/'

	// Count matching commands
	size_t match_count = 0;
	for (size_t i = 0; commands[i].text != NULL; i++) {
		if (starts_with_ignore_case(commands[i].text, command_start)) {
			match_count++;
		}
	}

	if (match_count == 0) {
		return 0; // No matches
	}

	// Allocate array for candidates
	tprompt_completion_candidate_t *cands = malloc(match_count * sizeof(tprompt_completion_candidate_t));
	if (cands == NULL) {
		return -1;
	}

	// Collect matching commands
	size_t idx = 0;
	for (size_t i = 0; commands[i].text != NULL; i++) {
		if (starts_with_ignore_case(commands[i].text, command_start)) {
			cands[idx].text = demo_strdup(commands[i].text);
			cands[idx].description = demo_strdup(commands[i].description);

			if (cands[idx].text == NULL) {
				// Cleanup on error
				for (size_t j = 0; j < idx; j++) {
					free(cands[j].text);
					free(cands[j].description);
				}
				free(cands);
				return -1;
			}
			idx++;
		}
	}

	*candidates = cands;
	*count = match_count;
	return 0;
}

/**
 * Extended completion callback for mentions (prefix: '@')
 * This example shows candidates WITHOUT descriptions
 */
static int complete_mentions_ex(const char *text, size_t cursor_pos, void *user_data,
	tprompt_completion_candidate_t **candidates, size_t *count)
{
	(void)cursor_pos; // Unused in this simple demo
	(void)user_data;  // Unused

	*candidates = NULL;
	*count = 0;

	// Extract the mention part after '@'
	const char *mention_start = strrchr(text, '@');
	if (mention_start == NULL) {
		return 0; // No '@' found, no completion
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
		return 0; // No matches
	}

	// Allocate array for candidates
	tprompt_completion_candidate_t *cands = malloc(match_count * sizeof(tprompt_completion_candidate_t));
	if (cands == NULL) {
		return -1;
	}

	// Collect matching mentions (without descriptions)
	size_t idx = 0;
	for (size_t i = 0; mentions[i] != NULL; i++) {
		if (starts_with_ignore_case(mentions[i], mention_start)) {
			cands[idx].text = demo_strdup(mentions[i]);
			cands[idx].description = NULL; // No description for mentions

			if (cands[idx].text == NULL) {
				// Cleanup on error
				for (size_t j = 0; j < idx; j++) {
					free(cands[j].text);
					free(cands[j].description);
				}
				free(cands);
				return -1;
			}
			idx++;
		}
	}

	*candidates = cands;
	*count = match_count;
	return 0;
}

/**
 * Main extended completion callback that dispatches based on prefix
 */
static int completion_ex_callback(
	const char *text,
	size_t cursor_pos,
	const char *prefix_char,
	void *user_data,
	tprompt_completion_candidate_t **candidates,
	size_t *count)
{
	// Dispatch based on which prefix triggered completion
	if (strcmp(prefix_char, "/") == 0) {
		return complete_commands_ex(text, cursor_pos, user_data, candidates, count);
	} else if (strcmp(prefix_char, "@") == 0) {
		return complete_mentions_ex(text, cursor_pos, user_data, candidates, count);
	}

	// Unknown prefix
	*candidates = NULL;
	*count = 0;
	return 0;
}

static void print_usage(void)
{
	printf("terse-prompt Completion Demo\n");
	printf("=============================\n");
	printf("This demo shows the completion system with two prefixes:\n\n");
	printf("Commands (prefix: '/') - WITH descriptions:\n");
	printf("  Type '/' to see available commands (16 total)\n");
	printf("  * Each command shows a description on the right\n");
	printf("  * Only 8 items visible at once - use Up/Down to scroll\n\n");
	printf("Mentions (prefix: '@') - WITHOUT descriptions:\n");
	printf("  Type '@' to see available users (12 total)\n");
	printf("  * Simple candidate list without descriptions\n");
	printf("  * Only 8 items visible at once - use Up/Down to scroll\n\n");
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
	example_setup_console_utf8();

	print_usage();

	// Configure tprompt with completion enabled
	// Note: completion_prefixes is a string containing all trigger characters
	tprompt_options_t options = {
		.prompt = "> ",
		.continuation_prompt = NULL,
		.history_file = NULL, // No history file for this demo
		.max_input_size = 4096,
		.max_history_size = 100,
		.completion_callback = NULL,
		.completion_ex_callback = completion_ex_callback, // Use extended callback with descriptions
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
