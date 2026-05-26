/**
 * @file deft_integration_example.c
 * @brief Complete integration example for deft REPL
 *
 * Demonstrates:
 * - Validation callback for bracket/brace balancing
 * - Custom continuation prompt
 * - Multi-line editing with automatic continuation
 */

#include "example_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tprompt.h>

/* ========================================================================
 * Validation Callback - Bracket/Brace Balance Checker
 * ======================================================================== */

/**
 * @brief Check if input has balanced brackets/braces for deft
 *
 * Returns:
 * - TPROMPT_VALIDATION_ACCEPT: Input is complete and balanced
 * - TPROMPT_VALIDATION_CONTINUE: Has unclosed brackets, insert newline
 * - TPROMPT_VALIDATION_REJECT: Has over-closed brackets, reject
 */
static tprompt_validation_result_t deft_validate_input(
	const char *text,
	size_t length,
	void *user_data)
{
	(void)user_data; // Not used in this example

	int paren_count = 0;   // ( )
	int brace_count = 0;   // { }
	int bracket_count = 0; // [ ]

	// Count opening and closing delimiters
	for (size_t i = 0; i < length; i++) {
		switch (text[i]) {
		case '(':
			paren_count++;
			break;
		case ')':
			paren_count--;
			break;
		case '{':
			brace_count++;
			break;
		case '}':
			brace_count--;
			break;
		case '[':
			bracket_count++;
			break;
		case ']':
			bracket_count--;
			break;
		}
	}

	// If any delimiter is unclosed, request continuation (insert newline)
	if (paren_count > 0 || brace_count > 0 || bracket_count > 0) {
		return TPROMPT_VALIDATION_CONTINUE;
	}

	// If any delimiter is over-closed, reject the input
	if (paren_count < 0 || brace_count < 0 || bracket_count < 0) {
		return TPROMPT_VALIDATION_REJECT;
	}

	// All delimiters are balanced, accept the input
	return TPROMPT_VALIDATION_ACCEPT;
}

/* ========================================================================
 * Main REPL Loop
 * ======================================================================== */

int main(void)
{
	example_setup_console_utf8();

	printf("=== deft REPL Integration Example ===\n");
	printf("Multi-line editing with automatic bracket detection\n");
	printf("Press Ctrl+D to exit\n\n");

	// Configure tprompt options for deft
	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.prompt = "deft> ";							// Primary prompt
	opts.continuation_prompt = "...   ";			// Continuation prompt (same width as "deft> ")
	opts.history_file = ".deft_history";			// Persistent history
	opts.max_history_size = 1000;					// Keep last 1000 commands
	opts.flags = TPROMPT_FLAG_MULTILINE;			// Enable multi-line mode
	opts.validation_callback = deft_validate_input; // Bracket balancing
	opts.validation_user_data = NULL;				// No user data needed

	// Open tprompt session
	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		fprintf(stderr, "Failed to open tprompt\n");
		return 1;
	}

	// REPL loop
	int command_count = 0;
	while (1) {
		// Read input (with validation and continuation)
		char *input = tprompt_readline(handle, NULL);

		// Check for EOF (Ctrl+D)
		if (!input) {
			tprompt_error_info_t err = tprompt_get_last_error(handle);
			if (err.category == TPROMPT_ERROR_NONE) {
				printf("\nGoodbye!\n");
				break; // Clean EOF
			}
			// Error occurred
			fprintf(stderr, "Error: %s\n", err.message);
			break;
		}

		// Skip empty input
		if (input[0] == '\0') {
			free(input);
			continue;
		}

		// Execute the command (in real deft, this would parse and evaluate)
		command_count++;
		printf("\n--- Command #%d ---\n", command_count);
		printf("%s\n", input);
		printf("--- End ---\n\n");

		// In real deft, you would:
		// 1. Parse the input into AST
		// 2. Evaluate the AST
		// 3. Print the result

		// Example:
		// deft_value_t result = deft_eval(input);
		// deft_print(result);

		free(input);
	}

	// Clean up
	tprompt_close(handle);

	return 0;
}

/* ========================================================================
 * Example Session
 * ======================================================================== */

/*
Expected behavior:

$ ./deft_integration_example
=== deft REPL Integration Example ===
Multi-line editing with automatic bracket detection
Press Ctrl+D to exit

deft> let x = 42
--- Command #1 ---
let x = 42
--- End ---

deft> let add = fn(a, b) {
...   a + b
... }

--- Command #2 ---
let add = fn(a, b) {
  a + b
}
--- End ---

deft> let nested = {
...   inner: {
...     value: [1, 2, 3]
...   }
... }

--- Command #3 ---
let nested = {
  inner: {
	value: [1, 2, 3]
  }
}
--- End ---

deft> ^D
Goodbye!

Notes:
1. Single-line commands (balanced brackets) are accepted immediately
2. Multi-line commands (unclosed brackets) automatically insert newlines
3. Continuation lines show "...   " prompt (right-aligned with "deft> ")
4. Over-closed brackets (e.g., "test)") are rejected with a beep
5. History is saved to .deft_history
*/
