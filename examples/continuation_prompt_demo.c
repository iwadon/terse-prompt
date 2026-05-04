/**
 * @file continuation_prompt_demo.c
 * @brief Demonstration of custom continuation prompts
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tprompt.h>
#include "example_console.h"

int main(void)
{
	example_setup_console_utf8();

	printf("=== Continuation Prompt Demo ===\n");
	printf("This demo shows custom continuation prompts.\n");
	printf("Press Shift+Enter or Ctrl+Alt+Enter to insert newlines.\n");
	printf("Press Enter to submit (in multiline mode).\n");
	printf("Press Ctrl+D to exit.\n\n");

	// Test 1: Default continuation prompt
	printf("Test 1: Default continuation prompt (\"| \")\n");
	{
		tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
		opts.prompt = "prompt> ";
		// opts.continuation_prompt is "| " by default

		tprompt_handle_t handle = tprompt_open(&opts);
		if (!handle) {
			fprintf(stderr, "Failed to open tprompt\n");
			return 1;
		}

		char *input = tprompt_readline(handle, NULL);
		if (input) {
			printf("You entered:\n%s\n\n", input);
			free(input);
		}

		tprompt_close(handle);
	}

	// Test 2: Custom continuation prompt (same length)
	printf("Test 2: Custom continuation prompt (same length as initial)\n");
	{
		tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
		opts.prompt = ">>> ";
		opts.continuation_prompt = "... ";

		tprompt_handle_t handle = tprompt_open(&opts);
		if (!handle) {
			fprintf(stderr, "Failed to open tprompt\n");
			return 1;
		}

		char *input = tprompt_readline(handle, NULL);
		if (input) {
			printf("You entered:\n%s\n\n", input);
			free(input);
		}

		tprompt_close(handle);
	}

	// Test 3: Short continuation prompt (will be right-padded)
	printf("Test 3: Short continuation prompt (will be right-padded with spaces)\n");
	{
		tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
		opts.prompt = "tprompt> ";  // 9 characters
		opts.continuation_prompt = ">>";  // 2 characters (will be padded to 9)

		tprompt_handle_t handle = tprompt_open(&opts);
		if (!handle) {
			fprintf(stderr, "Failed to open tprompt\n");
			return 1;
		}

		char *input = tprompt_readline(handle, NULL);
		if (input) {
			printf("You entered:\n%s\n\n", input);
			free(input);
		}

		tprompt_close(handle);
	}

	// Test 4: Long continuation prompt (will be truncated with warning)
	printf("Test 4: Long continuation prompt (will be truncated with warning)\n");
	{
		tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
		opts.prompt = "cmd> ";  // 5 characters
		opts.continuation_prompt = "continuation> ";  // 14 characters (will be truncated to 5)

		tprompt_handle_t handle = tprompt_open(&opts);
		if (!handle) {
			fprintf(stderr, "Failed to open tprompt\n");
			return 1;
		}

		char *input = tprompt_readline(handle, NULL);
		if (input) {
			printf("You entered:\n%s\n\n", input);
			free(input);
		}

		tprompt_close(handle);
	}

	printf("Demo completed!\n");
	return 0;
}
