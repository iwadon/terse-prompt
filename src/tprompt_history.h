/**
 * @file tprompt_history.h
 * @brief History management module for terse-prompt
 * @version 0.1
 * @date 2025-11-13
 *
 * Internal module for managing command history with LRU eviction,
 * navigation, and file persistence support.
 */

#ifndef TPROMPT_HISTORY_H
#define TPROMPT_HISTORY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of history structures */
typedef struct tprompt_history_entry tprompt_history_entry_t;
typedef struct tprompt_history tprompt_history_t;

/**
 * @brief History entry node (linked list)
 */
struct tprompt_history_entry {
	char *text;							/**< History entry text */
	struct tprompt_history_entry *next; /**< Next entry (older) */
	struct tprompt_history_entry *prev; /**< Previous entry (newer) */
};

/**
 * @brief History management structure
 */
struct tprompt_history {
	tprompt_history_entry_t *head;	  /**< Most recent entry */
	tprompt_history_entry_t *tail;	  /**< Oldest entry */
	size_t count;					  /**< Number of entries */
	size_t max_size;				  /**< Maximum number of entries (0 = unlimited) */
	tprompt_history_entry_t *current; /**< Current position during navigation */
	char *saved_input;				  /**< Saved input buffer when entering history navigation */
};

/* ========================================================================
 * History Management Functions
 * ======================================================================== */

/**
 * @brief Initialize history structure
 * @param history History structure to initialize
 * @param max_size Maximum number of entries (0 = unlimited)
 */
void tprompt_history_init(tprompt_history_t *history, size_t max_size);

/**
 * @brief Free all history resources
 * @param history History structure to free
 */
void tprompt_history_free(tprompt_history_t *history);

/**
 * @brief Add entry to history (internal implementation)
 * @param history History structure
 * @param text Entry text
 * @return 0 on success, -1 on failure
 */
int tprompt_history_add_internal(tprompt_history_t *history, const char *text);

/**
 * @brief Get previous history entry (navigate backward)
 * @param history History structure
 * @return Previous entry text, or NULL if at beginning
 */
const char *tprompt_history_prev(tprompt_history_t *history);

/**
 * @brief Get next history entry (navigate forward)
 * @param history History structure
 * @return Next entry text, or NULL if at end
 */
const char *tprompt_history_next(tprompt_history_t *history);

/**
 * @brief Reset history navigation position
 * @param history History structure
 */
void tprompt_history_reset_position(tprompt_history_t *history);

/**
 * @brief Load history from file (internal implementation)
 * @param history History structure
 * @param file_path File path
 * @return 0 on success, -1 on failure
 */
int tprompt_history_load_internal(tprompt_history_t *history, const char *file_path);

/**
 * @brief Save history to file (internal implementation)
 * @param history History structure
 * @param file_path File path
 * @return 0 on success, -1 on failure
 */
int tprompt_history_save_internal(tprompt_history_t *history, const char *file_path);

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_HISTORY_H */
