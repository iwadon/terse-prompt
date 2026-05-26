/**
 * scrolling_completion_demo.c - Test scrolling completion with large candidate lists
 *
 * This example demonstrates the scrolling completion feature with:
 * - 25+ candidates to test the 10-row visible window
 * - Scroll indicators (↑ N more above, ↓ N more below)
 * - Up/Down navigation through scrollable list
 */

#include "example_console.h"
#include "tprompt.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Large list of commands to test scrolling (25 items)
typedef struct {
	const char *text;
	const char *description;
} command_info_t;

static const command_info_t commands[] = {
	{ "help", "ヘルプを表示" },
	{ "clear", "画面をクリア" },
	{ "quit", "プログラムを終了" },
	{ "exit", "プログラムを終了" },
	{ "history", "コマンド履歴を表示" },
	{ "status", "ステータスを表示" },
	{ "config", "設定を表示" },
	{ "hello", "挨拶メッセージ" },
	{ "version", "バージョン情報を表示" },
	{ "about", "アプリケーション情報" },
	{ "settings", "設定画面を開く" },
	{ "preferences", "環境設定" },
	{ "export", "データをエクスポート" },
	{ "import", "データをインポート" },
	{ "backup", "バックアップを作成" },
	{ "restore", "バックアップから復元" },
	{ "search", "検索機能" },
	{ "find", "ファイルを検索" },
	{ "replace", "置換機能" },
	{ "copy", "コピーする" },
	{ "paste", "貼り付ける" },
	{ "cut", "切り取る" },
	{ "undo", "元に戻す" },
	{ "redo", "やり直す" },
	{ "save", "保存する" },
	{ NULL, NULL }
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
 * Extended completion callback for commands
 */
static int complete_commands_ex(const char *text, size_t cursor_pos, const char *prefix_char, void *user_data,
	tprompt_completion_candidate_t **candidates, size_t *count)
{
	(void)cursor_pos;
	(void)prefix_char;
	(void)user_data;

	*candidates = NULL;
	*count = 0;

	// Extract the command part after '/'
	const char *command_start = strchr(text, '/');
	if (command_start == NULL) {
		return 0;
	}
	command_start++;

	// Count matching commands
	size_t match_count = 0;
	for (size_t i = 0; commands[i].text != NULL; i++) {
		if (starts_with_ignore_case(commands[i].text, command_start)) {
			match_count++;
		}
	}

	if (match_count == 0) {
		return 0;
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
			cands[idx].text = strdup(commands[i].text);
			cands[idx].description = commands[i].description ? strdup(commands[i].description) : NULL;

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

static void print_usage(void)
{
	printf("terse-prompt Scrolling Completion Demo\n");
	printf("=======================================\n");
	printf("This demo shows scrolling completion with 25 candidates.\n\n");
	printf("Commands (prefix: '/'):\n");
	printf("  Type '/' to see all 25 available commands\n");
	printf("  * Only 8 items visible at once\n");
	printf("  * Use Up/Down arrows to scroll through the list\n");
	printf("  * Scroll indicators show how many items are hidden\n\n");
	printf("Completion Navigation:\n");
	printf("  Up/Down   - Navigate and scroll candidate list\n");
	printf("  Tab       - Confirm selection\n");
	printf("  Esc       - Cancel completion\n\n");
	printf("Other Commands:\n");
	printf("  quit      - Exit the demo\n");
	printf("  Ctrl+D    - Exit the demo\n\n");
}

int main(void)
{
	example_setup_console_utf8();

	print_usage();

	// Configure tprompt with completion enabled
	tprompt_options_t options = {
		.prompt = "> ",
		.continuation_prompt = NULL,
		.history_file = NULL,
		.max_input_size = 4096,
		.max_history_size = 100,
		.completion_callback = NULL,
		.completion_ex_callback = complete_commands_ex,
		.completion_user_data = NULL,
		.completion_prefixes = "/",
		.terse_handle = NULL,
		.flags = 0,
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

	printf("Ready! Try typing '/' to see the scrollable completion list.\n\n");

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
