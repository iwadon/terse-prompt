/**
 * @file tprompt_keybinding.h
 * @brief Keybinding management and key event handlers
 * @version 0.1
 * @date 2025-11-20
 */

#ifndef TPROMPT_KEYBINDING_H
#define TPROMPT_KEYBINDING_H

#include "tprompt_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get default keybindings array
 * @param out_count Output: number of default keybindings
 * @return Pointer to default keybindings array
 */
const tprompt_keybinding_t *tprompt_get_default_keybindings(size_t *out_count);

/**
 * @brief Handle character input event
 * @param handle Prompt handle
 * @param ch Character to insert (UTF-8 sequence)
 * @param width Display width of character
 * @return 0 on success, -1 on failure
 */
int tprompt_handle_char_input(tprompt_handle_t handle, const char *ch, int width);

/**
 * @brief Handle special key event
 * @param handle Prompt handle
 * @param event Terse key event
 * @return 0 to continue editing, 1 to confirm input, -1 on error
 */
int tprompt_handle_key_event(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Handle character input events (TERSE_EVENT_CHAR)
 *
 * Special handling for Ctrl+D (EOF/delete-char) and action system routing.
 *
 * @param handle Prompt handle
 * @param event Character event
 * @param should_break Output parameter: set to true if EOF signaled
 * @return 0 on success, -1 on error
 */
int tprompt_handle_char_event(tprompt_handle_t handle, const terse_event_t *event, bool *should_break);

/**
 * @brief Handle pending input confirmation (validation and mode-dependent behavior)
 * @param handle Prompt handle
 * @param should_break Output parameter: set to true if input should be confirmed
 * @return 0 on success, -1 on error
 */
int tprompt_handle_pending_confirmation(tprompt_handle_t handle, bool *should_break);

/**
 * @brief Check if buffer contains newline characters
 * @param handle Prompt handle
 * @return true if buffer contains newlines, false otherwise
 */
bool tprompt_buffer_has_newlines(tprompt_handle_t handle);

/**
 * @brief Find keybinding action for a given event
 *
 * Searches the custom keybindings array for a matching event.
 * Returns the action if found, or TPROMPT_ACTION_NONE if not found.
 *
 * @param handle Prompt handle
 * @param event Terse event to match
 * @return Action code (TPROMPT_ACTION_*) or TPROMPT_ACTION_NONE
 */
tprompt_action_t tprompt_find_keybinding_action(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Validate keybindings array
 *
 * Checks for:
 * - NULL bindings with count > 0 (error)
 * - Duplicate bindings (warning)
 * - Unknown action values (warning)
 *
 * Warnings are recorded in error info but do not cause failure.
 *
 * @param bindings Keybindings array to validate
 * @param count Number of keybindings
 * @param error Error info structure to populate
 * @return 0 on success, -1 on critical error (NULL bindings with count > 0)
 */
int tprompt_validate_keybindings(const tprompt_keybinding_t *bindings,
	size_t count,
	tprompt_error_info_t *error);

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_KEYBINDING_H */
