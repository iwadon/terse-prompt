/**
 * simple_demo.c - Simple demonstration of tprompt wrapper API
 *
 * This example shows the simplest way to use terse-prompt:
 * just call tprompt() to get a line of input.
 */

#include "example_console.h"
#include "tprompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	example_setup_console_utf8();

	terse_handle_t terse_h = terse_open(TERSE_PROFILE_AUTO, NULL);
	if (!terse_h) {
		fprintf(stderr, "Error: Failed to initialize terse\n");
		return 1;
	}

	tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
	opts.terse_handle = terse_h;

	tprompt_handle_t handle = tprompt_open(&opts);
	if (!handle) {
		tprompt_error_info_t err = tprompt_get_last_error(NULL);
		fprintf(stderr, "Error: Failed to initialize tprompt: %s\n", err.message);
		terse_close(terse_h);
		return 1;
	}

	terse_write_text(terse_h, "terse-prompt Simple Demo\n");
	terse_write_text(terse_h, "=========================\n");
	terse_write_text(terse_h, "This demo uses the simple tprompt() wrapper API.\n");
	terse_write_text(terse_h, "Type some text and press Enter to submit.\n");
	terse_write_text(terse_h, "Press Ctrl+D to exit.\n\n");

	while (1) {
		char *line = tprompt_readline(handle, ">>> ");

		if (line == NULL) {
			tprompt_error_info_t err = tprompt_get_last_error(handle);
			if (err.category != TPROMPT_ERROR_NONE) {
				terse_write_text(terse_h, "\nError: ");
				terse_write_text(terse_h, err.message);
				terse_write_text(terse_h, "\n");
				tprompt_close(handle);
				terse_close(terse_h);
				return 1;
			}
			terse_write_text(terse_h, "\nGoodbye!\n");
			break;
		}

		terse_write_text(terse_h, "You entered: \"");
		terse_write_text(terse_h, line);
		terse_write_text(terse_h, "\"\n");

		if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
			free(line);
			terse_write_text(terse_h, "Goodbye!\n");
			break;
		}

		free(line);
	}

	tprompt_close(handle);
	terse_close(terse_h);
	return 0;
}
