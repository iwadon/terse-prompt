/**
 * @file tprompt_util.c
 * @brief Miscellaneous internal utility functions for terse-prompt
 *
 * Provides small ISO C11-only helpers that replace non-standard (POSIX)
 * library functions, so that POSIX dependencies stay confined to the
 * files that genuinely need them (file I/O, terminal control).
 */

#include "tprompt_internal.h"
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * String Helpers
 * ======================================================================== */

char *tprompt_strdup(const char *s)
{
	if (!s) {
		return NULL;
	}

	size_t len = strlen(s) + 1;
	char *copy = malloc(len);
	if (!copy) {
		return NULL;
	}

	memcpy(copy, s, len);
	return copy;
}
