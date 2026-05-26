/**
 * @file tprompt_history.c
 * @brief History management implementation for terse-prompt
 * @version 0.1
 * @date 2025-11-13
 */

#define _POSIX_C_SOURCE 200809L

#include "tprompt_history.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define TPROMPT_HISTORY_MAX_ENTRY_FALLBACK (1024 * 1024)
#define TPROMPT_HISTORY_MAX_TOTAL_FALLBACK (10 * 1024 * 1024)

static FILE *tprompt_history_open_read(const char *file_path)
{
#if defined(__unix__) || defined(__APPLE__)
	int flags = O_RDONLY;
#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	int fd = open(file_path, flags);
	if (fd < 0) {
		return NULL;
	}

	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		errno = EINVAL;
		return NULL;
	}

	FILE *fp = fdopen(fd, "r");
	if (!fp) {
		close(fd);
		return NULL;
	}
	return fp;
#else
	return fopen(file_path, "r");
#endif
}

static FILE *tprompt_history_open_write(const char *file_path)
{
#if defined(__unix__) || defined(__APPLE__)
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	int fd = open(file_path, flags, 0600);
	if (fd < 0) {
		return NULL;
	}

	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		errno = EINVAL;
		return NULL;
	}

	FILE *fp = fdopen(fd, "w");
	if (!fp) {
		close(fd);
		return NULL;
	}
	return fp;
#else
	return fopen(file_path, "w");
#endif
}

/* ========================================================================
 * History Management - Core Functions
 * ======================================================================== */

void tprompt_history_init(tprompt_history_t *history, size_t max_size)
{
	if (!history) {
		return;
	}

	history->head = NULL;
	history->tail = NULL;
	history->count = 0;
	history->max_size = max_size;
	history->current = NULL;
	history->saved_input = NULL;
}

void tprompt_history_free(tprompt_history_t *history)
{
	if (!history) {
		return;
	}

	tprompt_history_entry_t *entry = history->head;
	while (entry) {
		tprompt_history_entry_t *next = entry->next;
		free(entry->text);
		free(entry);
		entry = next;
	}

	history->head = NULL;
	history->tail = NULL;
	history->count = 0;
	history->current = NULL;

	// Free saved input if any
	if (history->saved_input) {
		free(history->saved_input);
		history->saved_input = NULL;
	}
}

int tprompt_history_add_internal(tprompt_history_t *history, const char *text)
{
	if (!history || !text) {
		return -1;
	}

	// Skip empty entries
	if (text[0] == '\0') {
		return 0;
	}

	// Duplicate check: don't add if it's the same as the most recent entry
	if (history->head && history->head->text) {
		if (strcmp(history->head->text, text) == 0) {
			return 0; // Skip duplicate
		}
	}

	// Allocate new entry
	tprompt_history_entry_t *entry = malloc(sizeof(tprompt_history_entry_t));
	if (!entry) {
		return -1;
	}

	// Duplicate text
	entry->text = strdup(text);
	if (!entry->text) {
		free(entry);
		return -1;
	}

	entry->next = NULL;
	entry->prev = NULL;

	// Insert at head (most recent position)
	if (!history->head) {
		// Empty history
		history->head = entry;
		history->tail = entry;
	} else {
		// Add to head
		entry->next = history->head;
		history->head->prev = entry;
		history->head = entry;
	}

	history->count++;

	// LRU eviction: remove from tail if we exceed max_size
	while (history->max_size > 0 && history->count > history->max_size) {
		if (!history->tail) {
			break; // Safety check
		}

		tprompt_history_entry_t *old_tail = history->tail;
		history->tail = old_tail->prev;

		if (history->tail) {
			history->tail->next = NULL;
		} else {
			// List became empty
			history->head = NULL;
		}

		free(old_tail->text);
		free(old_tail);
		history->count--;
	}

	// Reset navigation position to head (most recent)
	history->current = NULL;

	return 0;
}

/* ========================================================================
 * History Navigation
 * ======================================================================== */

const char *tprompt_history_prev(tprompt_history_t *history)
{
	if (!history) {
		return NULL;
	}

	// If current is NULL, start from head (most recent)
	if (!history->current) {
		history->current = history->head;
		return history->current ? history->current->text : NULL;
	}

	// Try to move to next (older) entry
	// Note: 'next' points toward older entries, 'prev' points toward newer entries
	if (history->current->next) {
		history->current = history->current->next;
		return history->current->text;
	}

	// Already at the oldest entry, return NULL but keep current position
	return NULL;
}

const char *tprompt_history_next(tprompt_history_t *history)
{
	if (!history || !history->current) {
		return NULL;
	}

	// Move to prev (newer) entry
	// Note: 'next' points toward older entries, 'prev' points toward newer entries
	history->current = history->current->prev;

	// Return current entry's text (or NULL if at end - returning to current input)
	return history->current ? history->current->text : NULL;
}

void tprompt_history_reset_position(tprompt_history_t *history)
{
	if (history) {
		history->current = NULL;
		// Free saved input when exiting history navigation
		if (history->saved_input) {
			free(history->saved_input);
			history->saved_input = NULL;
		}
	}
}

/* ========================================================================
 * File Persistence - Escape/Unescape Helpers
 * ======================================================================== */

/**
 * @brief Escape special characters in history entry for file storage
 *
 * Escapes: newline (\n), carriage return (\r), backslash (\\)
 * Caller must free the returned string.
 */
static char *tprompt_history_escape(const char *text)
{
	if (!text) {
		return NULL;
	}

	// Count characters that need escaping
	size_t escape_count = 0;
	for (const char *p = text; *p; p++) {
		if (*p == '\n' || *p == '\r' || *p == '\\') {
			escape_count++;
		}
	}

	// Allocate buffer (original length + escape characters + null terminator)
	size_t len = strlen(text);
	if (len > SIZE_MAX - escape_count - 1) {
		return NULL;
	}
	char *escaped = malloc(len + escape_count + 1);
	if (!escaped) {
		return NULL;
	}

	// Escape characters
	char *dst = escaped;
	for (const char *src = text; *src; src++) {
		if (*src == '\n') {
			*dst++ = '\\';
			*dst++ = 'n';
		} else if (*src == '\r') {
			*dst++ = '\\';
			*dst++ = 'r';
		} else if (*src == '\\') {
			*dst++ = '\\';
			*dst++ = '\\';
		} else {
			*dst++ = *src;
		}
	}
	*dst = '\0';

	return escaped;
}

/**
 * @brief Unescape special characters from history file
 *
 * Unescapes: \n, \r, \\
 * Modifies the string in-place.
 */
static void tprompt_history_unescape(char *text)
{
	if (!text) {
		return;
	}

	char *src = text;
	char *dst = text;

	while (*src) {
		if (*src == '\\' && *(src + 1)) {
			src++; // Skip backslash
			if (*src == 'n') {
				*dst++ = '\n';
			} else if (*src == 'r') {
				*dst++ = '\r';
			} else if (*src == '\\') {
				*dst++ = '\\';
			} else {
				// Unknown escape sequence, keep both characters
				*dst++ = '\\';
				*dst++ = *src;
			}
			src++;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';
}

static void tprompt_history_sanitize(char *text)
{
	if (!text) {
		return;
	}

	char *src = text;
	char *dst = text;

	while (*src) {
		unsigned char ch = (unsigned char)*src;
		if ((ch < 0x20 && ch != '\n' && ch != '\t') || ch == 0x7F) {
			src++;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = '\0';
}

/* ========================================================================
 * File Persistence - Load/Save
 * ======================================================================== */

int tprompt_history_load_internal(tprompt_history_t *history, const char *file_path,
	size_t max_entry_size, size_t max_total_size)
{
	if (!history || !file_path) {
		return -1;
	}

	// Open file for reading
	FILE *fp = tprompt_history_open_read(file_path);
	if (!fp) {
		// File not found is not an error (first time use)
		if (errno == ENOENT) {
			return 0;
		}
		return -1;
	}

	// Read line by line using fixed-size buffer
	// Note: 4096 bytes should be sufficient for typical history entries
	char buffer[4096];
	char *line = NULL;
	size_t line_len = 0;
	size_t line_cap = 0;
	size_t total_read = 0;
	bool skipping_long_line = false;
	size_t max_entry = max_entry_size > 0 ? max_entry_size : TPROMPT_HISTORY_MAX_ENTRY_FALLBACK;
	size_t max_total = max_total_size > 0 ? max_total_size : TPROMPT_HISTORY_MAX_TOTAL_FALLBACK;

	while (fgets(buffer, sizeof(buffer), fp)) {
		size_t buffer_len = strlen(buffer);
		total_read += buffer_len;
		if (total_read > max_total) {
			free(line);
			fclose(fp);
			return -1;
		}

		// Check if this is a continuation of a previous line
		// (no newline at end means buffer was full)
		bool has_newline = (buffer_len > 0 && buffer[buffer_len - 1] == '\n');

		if (skipping_long_line) {
			if (has_newline) {
				skipping_long_line = false;
			}
			continue;
		}

		if (line_len + buffer_len > max_entry) {
			skipping_long_line = !has_newline;
			line_len = 0;
			if (line) {
				line[0] = '\0';
			}
			continue;
		}

		// Append buffer to line
		if (line_len + buffer_len >= line_cap) {
			// Expand line buffer
			size_t new_cap = line_cap == 0 ? 4096 : line_cap * 2;
			while (new_cap < line_len + buffer_len + 1) {
				new_cap *= 2;
			}
			if (new_cap > max_entry + 1) {
				new_cap = max_entry + 1;
			}
			char *new_line = realloc(line, new_cap);
			if (!new_line) {
				free(line);
				fclose(fp);
				return -1;
			}
			line = new_line;
			line_cap = new_cap;
		}

		memcpy(line + line_len, buffer, buffer_len + 1); // +1 for NUL
		line_len += buffer_len;

		// If we found a newline, process this line
		if (has_newline) {
			// Remove trailing newline
			if (line_len > 0 && line[line_len - 1] == '\n') {
				line[line_len - 1] = '\0';
				line_len--;
			}

			// Remove trailing carriage return if present (CRLF handling)
			if (line_len > 0 && line[line_len - 1] == '\r') {
				line[line_len - 1] = '\0';
				line_len--;
			}

			// Unescape special characters
			tprompt_history_unescape(line);
			tprompt_history_sanitize(line);

			// Add to history (skip empty lines handled by add_internal)
			tprompt_history_add_internal(history, line);

			// Reset for next line
			line_len = 0;
			if (line) {
				line[0] = '\0';
			}
		}
	}

	// Handle case where file doesn't end with newline
	if (line_len > 0) {
		// Remove trailing carriage return if present
		if (line_len > 0 && line[line_len - 1] == '\r') {
			line[line_len - 1] = '\0';
			line_len--;
		}

		tprompt_history_unescape(line);
		tprompt_history_sanitize(line);
		tprompt_history_add_internal(history, line);
	}

	free(line);
	fclose(fp);

	return 0;
}

int tprompt_history_save_internal(tprompt_history_t *history, const char *file_path)
{
	if (!history || !file_path) {
		return -1;
	}

	// Open file for writing (truncate)
	FILE *fp = tprompt_history_open_write(file_path);
	if (!fp) {
		return -1;
	}

	// Write entries from tail to head (oldest to most recent)
	// This ensures that when we load and insert at head, the order is preserved
	tprompt_history_entry_t *entry = history->tail;
	while (entry) {
		// Escape special characters before writing
		char *escaped = tprompt_history_escape(entry->text);
		if (!escaped) {
			fclose(fp);
			return -1;
		}

		// Write escaped entry with newline
		int result = fprintf(fp, "%s\n", escaped);
		free(escaped);

		if (result < 0) {
			fclose(fp);
			return -1;
		}

		entry = entry->prev;
	}

	if (fclose(fp) != 0) {
		return -1;
	}

	return 0;
}
