/**
 * @file tprompt_error.c
 * @brief Error handling utilities for terse-prompt
 *
 * Provides functions for setting and clearing error information.
 */

#include "tprompt_internal.h"
#include <stdarg.h>
#include <stdio.h>

/* ========================================================================
 * Error Handling - Internal Helpers
 * ======================================================================== */

void tprompt_set_error(tprompt_error_info_t *error,
	tprompt_error_category_t category,
	int code,
	const char *message, ...)
{
	if (!error) {
		return;
	}

	error->category = category;
	error->code = code;

	if (message) {
		va_list args;
		va_start(args, message);
		vsnprintf(error->message, sizeof(error->message), message, args);
		va_end(args);
	} else {
		error->message[0] = '\0';
	}
}

void tprompt_clear_error(tprompt_error_info_t *error)
{
	if (!error) {
		return;
	}

	error->category = TPROMPT_ERROR_NONE;
	error->code = 0;
	error->message[0] = '\0';
}
