/**
 * @file tprompt_completion.h
 * @brief Completion module for terse-prompt
 * @version 0.1
 * @date 2025-11-13
 *
 * Internal module for managing completion functionality including
 * trigger detection, candidate management, navigation, and confirmation.
 */

#ifndef TPROMPT_COMPLETION_H
#define TPROMPT_COMPLETION_H

#include "tprompt.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct tprompt_handle *tprompt_handle_t;
typedef struct tprompt_completion_state tprompt_completion_state_t;

/**
 * @brief Completion state structure
 */
struct tprompt_completion_state {
	bool active;			/**< Whether completion is active */
	char **candidates;		/**< Current candidate list (legacy, NULL-terminated) */
	tprompt_completion_candidate_t *candidates_ex; /**< Extended candidates with descriptions */
	size_t candidate_count; /**< Number of candidates */
	size_t selected_index;	/**< Currently selected candidate index */
	size_t trigger_offset;	/**< Byte offset of completion trigger character */
	char trigger_char;		/**< Character that triggered completion */
	bool use_extended;		/**< Whether using extended candidates */
};

/* ========================================================================
 * Completion Management Functions
 * ======================================================================== */

/**
 * @brief Initialize completion state
 * @param state Completion state to initialize
 */
void tprompt_completion_init(tprompt_completion_state_t *state);

/**
 * @brief Free completion state resources
 * @param state Completion state to free
 */
void tprompt_completion_free(tprompt_completion_state_t *state);

/**
 * @brief Activate completion and fetch candidates
 * @param handle Prompt handle
 * @param trigger_char Character that triggered completion
 * @param trigger_offset Byte offset of trigger character
 * @return 0 on success, -1 on failure
 */
int tprompt_completion_activate(tprompt_handle_t handle, char trigger_char, size_t trigger_offset);

/**
 * @brief Deactivate completion and clean up
 * @param handle Prompt handle
 */
void tprompt_completion_deactivate(tprompt_handle_t handle);

/**
 * @brief Update completion candidates based on current input
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_completion_update(tprompt_handle_t handle);

/**
 * @brief Select next completion candidate
 * @param state Completion state
 */
void tprompt_completion_select_next(tprompt_completion_state_t *state);

/**
 * @brief Select previous completion candidate
 * @param state Completion state
 */
void tprompt_completion_select_prev(tprompt_completion_state_t *state);

/**
 * @brief Confirm selected completion candidate
 * @param handle Prompt handle
 * @return 0 on success, -1 on failure
 */
int tprompt_completion_confirm(tprompt_handle_t handle);

/**
 * @brief Check if character is a completion trigger
 * @param handle Prompt handle
 * @param ch Character to check
 * @return true if trigger, false otherwise
 */
bool tprompt_is_completion_trigger(tprompt_handle_t handle, char ch);

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_COMPLETION_H */
