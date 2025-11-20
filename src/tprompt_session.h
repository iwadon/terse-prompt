/**
 * @file tprompt_session.h
 * @brief Editing session management
 * @version 0.1
 * @date 2025-11-20
 */

#ifndef TPROMPT_SESSION_H
#define TPROMPT_SESSION_H

#include "tprompt_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get current logical line number at cursor
 * @param handle Prompt handle
 * @return Logical line number (0-based), or 0 on error
 */
size_t tprompt_get_cursor_line(tprompt_handle_t handle);

/**
 * @brief Get current column position within logical line
 * @param handle Prompt handle
 * @return Column position (0-based), or 0 on error
 */
size_t tprompt_get_cursor_column(tprompt_handle_t handle);

/**
 * @brief Read a line of input with editing capabilities
 * @param handle Prompt handle
 * @param prompt Prompt string to display
 * @return Pointer to input text (NUL-terminated), or NULL on error/EOF
 */
char *tprompt_readline(tprompt_handle_t handle, const char *prompt);

/**
 * @brief Check if validation callback is set
 * @param handle Prompt handle
 * @return true if validation callback is set, false otherwise
 */
bool tprompt_has_validation_callback(tprompt_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_SESSION_H */
