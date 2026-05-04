/**
 * simple_demo.c - Simple demonstration of tprompt wrapper API
 *
 * This example shows the simplest way to use terse-prompt:
 * just call tprompt() to get a line of input.
 */

#include "tprompt.h"
#include "example_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	example_setup_console_utf8();

	printf("terse-prompt Simple Demo\n");
	printf("=========================\n");
	printf("This demo uses the simple tprompt() wrapper API.\n");
	printf("Type some text and press Enter to submit.\n");
	printf("Press Ctrl+D to exit.\n\n");

	while (1) {
		char *line = tprompt(">>> ");

		if (line == NULL) {
			// Check if this was an error or EOF
			tprompt_error_info_t err = tprompt_get_last_error(NULL);
			if (err.category != TPROMPT_ERROR_NONE) {
				fprintf(stderr, "\nError: %s (code: %d)\n", err.message, err.code);
				return 1;
			}
			// EOF (Ctrl+D)
			printf("\nGoodbye!\n");
			break;
		}

		// Echo what was entered
		printf("You entered: \"%s\"\n", line);

		// Check for quit command
		if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
			free(line);
			printf("Goodbye!\n");
			break;
		}

		// Free the line (tprompt() allocates memory)
		free(line);
	}

	return 0;
}
