/**
 * @file tprompt_action.h
 * @brief Action execution system and logical line navigation
 * @version 0.1
 * @date 2025-11-20
 */

#ifndef TPROMPT_ACTION_H
#define TPROMPT_ACTION_H

#include "tprompt_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resolve keyboard event to action
 *
 * Checks custom keybindings first, then default keybindings, then falls back
 * to character insertion for regular character input.
 *
 * @param handle Prompt handle
 * @param event Keyboard event from terse
 * @return Action to execute (TPROMPT_ACTION_NONE if no action)
 */
tprompt_action_t tprompt_resolve_action(tprompt_handle_t handle, const terse_event_t *event);

/**
 * @brief Execute an action
 *
 * Dispatches to the appropriate action handler function based on the action type.
 * Context-dependent logic (e.g., multiline mode, validation) is handled within
 * the action handlers.
 *
 * @param handle Prompt handle
 * @param action Action to execute
 * @param event Original keyboard event (may be needed by some handlers)
 * @return 1 to confirm input, 0 to continue editing, -1 on error
 */
int tprompt_execute_action(tprompt_handle_t handle, tprompt_action_t action, const terse_event_t *event);

/* Individual Action Handlers */
int tprompt_action_move_left(tprompt_handle_t handle, const terse_event_t *event);
int tprompt_action_move_right(tprompt_handle_t handle, const terse_event_t *event);
int tprompt_action_delete_backward(tprompt_handle_t handle, const terse_event_t *event);
int tprompt_action_delete_forward(tprompt_handle_t handle, const terse_event_t *event);
int tprompt_action_move_home(tprompt_handle_t handle, const terse_event_t *event);
int tprompt_action_move_end(tprompt_handle_t handle, const terse_event_t *event);
int tprompt_action_history_prev(tprompt_handle_t handle, const terse_event_t *event);
int tprompt_action_history_next(tprompt_handle_t handle, const terse_event_t *event);
int tprompt_action_move_up(tprompt_handle_t handle, const terse_event_t *event);
int tprompt_action_move_down(tprompt_handle_t handle, const terse_event_t *event);

/* Logical Line Navigation */
size_t tprompt_count_logical_lines(tprompt_handle_t handle);
size_t tprompt_get_logical_line_at_offset(tprompt_handle_t handle, size_t byte_offset);
int tprompt_get_logical_line_bounds(tprompt_handle_t handle, size_t logical_line,
	size_t *out_start, size_t *out_end);
size_t tprompt_get_logical_line_length(tprompt_handle_t handle, size_t logical_line);

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_ACTION_H */
