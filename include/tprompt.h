/**
 * @file tprompt.h
 * @brief terse-prompt: Multi-line text input library for CLI REPL tools
 * @version 0.1
 * @date 2025-11-02
 *
 * terse-prompt provides two API styles:
 * 1. Simple wrapper API: Single function for quick usage
 * 2. Framework API: Customizable low-level interface
 */

#ifndef TPROMPT_H
#define TPROMPT_H

#include <stddef.h>
#include <terse.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Type Definitions
 * ======================================================================== */

/**
 * @brief Opaque handle for an editing session
 *
 * Created by tprompt_open() and destroyed by tprompt_close().
 * Manages internal terse handle and session state.
 */
typedef struct tprompt_handle *tprompt_handle_t;

/**
 * @brief Forward declaration for completion callback
 */
typedef struct tprompt_completion_result tprompt_completion_result_t;

/**
 * @brief Forward declaration for keybinding structure
 */
typedef struct tprompt_keybinding tprompt_keybinding_t;

/**
 * @brief Validation result enumeration
 *
 * Returned by validation callback to indicate whether input should be accepted,
 * rejected, or continued (insert newline).
 */
typedef enum tprompt_validation_result {
	TPROMPT_VALIDATION_ACCEPT = 0,   /**< Accept input and return to caller */
	TPROMPT_VALIDATION_CONTINUE = 1, /**< Insert newline and continue editing (multiline mode only) */
	TPROMPT_VALIDATION_REJECT = 2    /**< Reject input and continue editing */
} tprompt_validation_result_t;

/**
 * @brief Validation callback function type
 *
 * Called when user attempts to confirm input (e.g., pressing Enter).
 * Allows application to validate input completeness before accepting.
 *
 * @param text Current input text (NUL-terminated)
 * @param length Length of input text in bytes
 * @param user_data User-defined data passed to the callback
 * @return Validation result indicating action to take
 *
 * Example (Ruby REPL-style validation):
 * @code
 * tprompt_validation_result_t validate_ruby(const char *text, size_t length, void *user_data) {
 *     // Check if parentheses are balanced
 *     int paren_count = 0;
 *     for (size_t i = 0; i < length; i++) {
 *         if (text[i] == '(') paren_count++;
 *         if (text[i] == ')') paren_count--;
 *     }
 *
 *     if (paren_count > 0) {
 *         // Unbalanced parentheses - insert newline and continue
 *         return TPROMPT_VALIDATION_CONTINUE;
 *     }
 *
 *     return TPROMPT_VALIDATION_ACCEPT;
 * }
 * @endcode
 */
typedef tprompt_validation_result_t (*tprompt_validation_fn)(
	const char *text,
	size_t length,
	void *user_data);

/**
 * @brief Completion callback function type
 *
 * @param text Current input text (entire buffer)
 * @param cursor_pos Cursor position (byte offset)
 * @param prefix_char Trigger prefix character that initiated completion
 * @param user_data User-defined data passed to the callback
 * @return Completion result with candidate list
 *
 * Memory management:
 * - Callback allocates candidates array and each string with malloc
 * - terse-prompt frees them using tprompt_free_completion_result()
 */
typedef tprompt_completion_result_t (*tprompt_completion_fn)(
	const char *text,
	size_t cursor_pos,
	const char *prefix_char,
	void *user_data);

/**
 * @brief Keybinding structure for custom key actions
 *
 * Allows customization of key behavior such as input confirmation and newline insertion.
 * Use helper macros (TPROMPT_BIND_KEY, TPROMPT_BIND_CHAR, TPROMPT_BIND_FUNCTION) for
 * easier initialization.
 *
 * Note: When binding Ctrl+letter combinations, use lowercase letters. For example,
 * Ctrl+J is transmitted as TERSE_EVENT_CHAR with scalar='j' (lowercase) and TERSE_MOD_CTRL.
 *
 * Example:
 * @code
 * tprompt_keybinding_t bindings[] = {
 *     TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
 *     TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, TERSE_MOD_SHIFT, TPROMPT_ACTION_INSERT_NEWLINE),
 *     TPROMPT_BIND_CHAR('j', TERSE_MOD_CTRL, TPROMPT_ACTION_INSERT_NEWLINE),  // Ctrl+J
 * };
 * @endcode
 */
struct tprompt_keybinding {
	terse_event_type_t key; /**< Key type (TERSE_EVENT_ENTER, TERSE_EVENT_CHAR, etc.) */
	int modifiers;			/**< Modifier keys (TERSE_MOD_SHIFT, TERSE_MOD_CTRL, etc., 0 = none) */
	union {
		unsigned int scalar; /**< For TERSE_EVENT_CHAR: Unicode code point (e.g., 'J' = 0x4A) */
		int function_num;	 /**< For TERSE_EVENT_FUNCTION: Function key number (F1=1, F2=2, etc.) */
	} data;
	int action; /**< Action to perform (TPROMPT_ACTION_*) */
};

/**
 * @brief Options structure for tprompt_open()
 */
typedef struct tprompt_options {
	const char *prompt;								/**< Prompt string (NULL = "> ") */
	const char *history_file;						/**< History file path (NULL = memory only) */
	size_t max_input_size;							/**< Max input size in bytes (0 = unlimited) */
	size_t max_history_size;						/**< Max history entries (0 = unlimited) */
	tprompt_completion_fn completion_callback;		/**< Completion callback (NULL = disabled) */
	void *completion_user_data;						/**< User data for completion callback */
	const char *completion_prefixes;				/**< Completion trigger chars (e.g., "/@", NULL = disabled) */
	terse_handle_t terse_handle;					/**< Existing terse handle (NULL = auto-create) */
	int flags;										/**< Behavior flags (TPROMPT_FLAG_*) */
	const tprompt_keybinding_t *custom_keybindings; /**< Custom keybindings array (NULL = use defaults) */
	size_t keybinding_count;						/**< Number of custom keybindings */
	tprompt_validation_fn validation_callback;		/**< Validation callback (NULL = no validation) */
	void *validation_user_data;						/**< User data for validation callback */
} tprompt_options_t;

/**
 * @brief Completion result structure
 */
struct tprompt_completion_result {
	char **candidates; /**< NULL-terminated array of candidate strings */
	size_t count;	   /**< Number of candidates */
};

/**
 * @brief Error category enumeration
 */
typedef enum tprompt_error_category {
	TPROMPT_ERROR_NONE = 0,		/**< No error */
	TPROMPT_ERROR_INVALID_ARGS, /**< Invalid arguments */
	TPROMPT_ERROR_IO,			/**< I/O error (see errno) */
	TPROMPT_ERROR_MEMORY,		/**< Memory allocation failure */
	TPROMPT_ERROR_TERSE,		/**< terse API error */
	TPROMPT_ERROR_HISTORY_FILE, /**< History file read/write error */
	TPROMPT_ERROR_ENCODING,		/**< UTF-8 encoding error */
	TPROMPT_ERROR_SIZE_LIMIT	/**< Size limit exceeded */
} tprompt_error_category_t;

/**
 * @brief Error information structure
 */
typedef struct tprompt_error_info {
	tprompt_error_category_t category; /**< Error category */
	int code;						   /**< Detailed error code (errno equivalent) */
	char message[256];				   /**< Human-readable error message */
} tprompt_error_info_t;

/* ========================================================================
 * Constants and Flags
 * ======================================================================== */

/**
 * @brief Enable multi-line mode (default)
 */
#define TPROMPT_FLAG_MULTILINE (1 << 0)

/**
 * @brief Disable history feature
 */
#define TPROMPT_FLAG_DISABLE_HISTORY (1 << 1)

/**
 * @brief Disable automatic history saving on close
 */
#define TPROMPT_FLAG_NO_AUTO_SAVE (1 << 2)

/* ========================================================================
 * Keybinding Actions
 * ======================================================================== */

/**
 * @brief No action (use default behavior)
 */
#define TPROMPT_ACTION_NONE 0

/**
 * @brief Confirm input and return to caller
 */
#define TPROMPT_ACTION_CONFIRM_INPUT 1

/**
 * @brief Insert newline at cursor position
 */
#define TPROMPT_ACTION_INSERT_NEWLINE 2

/* Future expansion: TPROMPT_ACTION_CANCEL, TPROMPT_ACTION_COMPLETE, etc. */

/* ========================================================================
 * Keybinding Helper Macros
 * ======================================================================== */

/**
 * @brief Helper macro to bind a special key (Enter, Tab, arrows, etc.)
 * @param event Key event type (e.g., TERSE_EVENT_ENTER)
 * @param mods Modifier keys (e.g., TERSE_MOD_SHIFT, 0 for none)
 * @param act Action to perform (e.g., TPROMPT_ACTION_CONFIRM_INPUT)
 */
#define TPROMPT_BIND_KEY(event, mods, act) \
	{ (event), (mods), { .scalar = 0 }, (act) }

/**
 * @brief Helper macro to bind a character key
 * @param ch Character code point (e.g., 'J', 'a')
 * @param mods Modifier keys (e.g., TERSE_MOD_CTRL)
 * @param act Action to perform (e.g., TPROMPT_ACTION_INSERT_NEWLINE)
 */
#define TPROMPT_BIND_CHAR(ch, mods, act) \
	{ TERSE_EVENT_CHAR, (mods), { .scalar = (ch) }, (act) }

/**
 * @brief Helper macro to bind a function key (F1-F12)
 * @param num Function key number (1 for F1, 2 for F2, etc.)
 * @param mods Modifier keys (e.g., TERSE_MOD_CTRL)
 * @param act Action to perform
 */
#define TPROMPT_BIND_FUNCTION(num, mods, act) \
	{ TERSE_EVENT_FUNCTION, (mods), { .function_num = (num) }, (act) }

/**
 * @brief Default options initializer
 */
#define TPROMPT_OPTIONS_DEFAULT {    \
	.prompt = "> ",                  \
	.history_file = NULL,            \
	.max_input_size = 1024 * 1024,   \
	.max_history_size = 100,         \
	.completion_callback = NULL,     \
	.completion_user_data = NULL,    \
	.completion_prefixes = NULL,     \
	.terse_handle = NULL,            \
	.flags = TPROMPT_FLAG_MULTILINE, \
	.custom_keybindings = NULL,      \
	.keybinding_count = 0,           \
	.validation_callback = NULL,     \
	.validation_user_data = NULL     \
}

/* ========================================================================
 * Simple Wrapper API
 * ======================================================================== */

/**
 * @brief Simple one-line input function
 *
 * Reads a single line (or multi-line) of input with minimal setup.
 * Creates and destroys a temporary handle internally.
 * History is memory-only (no file persistence).
 * Emacs-style keybindings are enabled.
 *
 * @param prompt_text Prompt string (NULL uses "> ")
 * @return Allocated input string (caller must free()), or NULL on error/EOF
 *
 * On NULL return:
 * - errno == 0: EOF (Ctrl+D, etc.)
 * - errno != 0: Error occurred
 *
 * Example:
 * @code
 * char *input = tprompt("Enter command: ");
 * if (input) {
 *     printf("You entered: %s\n", input);
 *     free(input);
 * } else if (errno == 0) {
 *     printf("EOF\n");
 * } else {
 *     perror("tprompt");
 * }
 * @endcode
 */
char *tprompt(const char *prompt_text);

/* ========================================================================
 * Framework API - Initialization and Cleanup
 * ======================================================================== */

/**
 * @brief Open an editing session
 *
 * Creates a new editing session handle.
 * If history_file is specified, loads history from disk.
 * If terse_handle is NULL, creates a new terse handle internally.
 *
 * @param options Option structure (NULL uses default settings)
 * @return Session handle on success, NULL on failure
 *
 * On failure, use tprompt_get_last_error(NULL) to retrieve error details.
 *
 * Example:
 * @code
 * tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
 * opts.history_file = "~/.myapp_history";
 * opts.max_history_size = 1000;
 *
 * tprompt_handle_t handle = tprompt_open(&opts);
 * if (!handle) {
 *     tprompt_error_info_t err = tprompt_get_last_error(NULL);
 *     fprintf(stderr, "Error: %s\n", err.message);
 *     return 1;
 * }
 * @endcode
 */
tprompt_handle_t tprompt_open(const tprompt_options_t *options);

/**
 * @brief Close an editing session
 *
 * Frees all resources associated with the handle.
 * If history_file is set and TPROMPT_FLAG_NO_AUTO_SAVE is not set, saves history.
 * If terse_handle was created internally, destroys it.
 * If terse_handle was provided externally, does not destroy it.
 *
 * @param handle Session handle (NULL is safe and does nothing)
 */
void tprompt_close(tprompt_handle_t handle);

/* ========================================================================
 * Framework API - Editing Session
 * ======================================================================== */

/**
 * @brief Read a line of input
 *
 * Reads one line (multi-line capable) of input from the user.
 * Automatically adds the input to history.
 *
 * @param handle Session handle
 * @param prompt_override Prompt string (NULL uses handle's default prompt)
 * @return Allocated input string (caller must free()), or NULL on error/EOF
 *
 * On NULL return, use tprompt_get_last_error(handle) to determine:
 * - category == TPROMPT_ERROR_NONE: EOF
 * - category != TPROMPT_ERROR_NONE: Error occurred
 *
 * Example:
 * @code
 * while (running) {
 *     char *line = tprompt_readline(handle, get_dynamic_prompt());
 *     if (!line) {
 *         tprompt_error_info_t err = tprompt_get_last_error(handle);
 *         if (err.category == TPROMPT_ERROR_NONE) {
 *             break; // EOF
 *         }
 *         fprintf(stderr, "Error: %s\n", err.message);
 *         break;
 *     }
 *     process_command(line);
 *     free(line);
 * }
 * @endcode
 */
char *tprompt_readline(tprompt_handle_t handle, const char *prompt_override);

/* ========================================================================
 * Framework API - History Management
 * ======================================================================== */

/**
 * @brief Add an entry to history
 *
 * Manually adds a history entry. Usually not needed as tprompt_readline()
 * automatically adds entries.
 *
 * @param handle Session handle
 * @param entry History entry string
 * @return 0 on success, -1 on failure
 */
int tprompt_history_add(tprompt_handle_t handle, const char *entry);

/**
 * @brief Load history from file
 *
 * Loads history entries from the specified file.
 * Usually not needed as tprompt_open() automatically loads if history_file is set.
 *
 * @param handle Session handle
 * @param file_path History file path
 * @return 0 on success, -1 on failure
 */
int tprompt_history_load(tprompt_handle_t handle, const char *file_path);

/**
 * @brief Save history to file
 *
 * Saves history entries to the specified file.
 * Usually not needed as tprompt_close() automatically saves if history_file is set.
 *
 * @param handle Session handle
 * @param file_path History file path
 * @return 0 on success, -1 on failure
 */
int tprompt_history_save(tprompt_handle_t handle, const char *file_path);

/**
 * @brief Clear all history entries
 *
 * Clears all history entries from memory (does not delete the history file).
 *
 * @param handle Session handle
 */
void tprompt_history_clear(tprompt_handle_t handle);

/**
 * @brief Set maximum history size
 *
 * Sets the maximum number of history entries to keep.
 * When the limit is exceeded, oldest entries are removed.
 *
 * @param handle Session handle
 * @param max_size Maximum history size (0 = unlimited)
 */
void tprompt_history_set_max_size(tprompt_handle_t handle, size_t max_size);

/* ========================================================================
 * Framework API - Completion
 * ======================================================================== */

/**
 * @brief Set completion callback
 *
 * Registers a callback function for auto-completion.
 * Set to NULL to disable completion.
 *
 * @param handle Session handle
 * @param callback Completion callback function (NULL to disable)
 * @param user_data User data passed to callback
 */
void tprompt_set_completion_callback(
	tprompt_handle_t handle,
	tprompt_completion_fn callback,
	void *user_data);

/**
 * @brief Set completion trigger prefixes
 *
 * Sets the prefix characters that trigger completion.
 * For example, "/@" triggers completion on '/' or '@'.
 *
 * @param handle Session handle
 * @param prefixes Prefix string (NULL or empty to disable triggers)
 * @return 0 on success, -1 on failure
 */
int tprompt_set_completion_prefixes(tprompt_handle_t handle, const char *prefixes);

/**
 * @brief Free completion result
 *
 * Frees the memory allocated by a completion callback.
 * This function should be called by terse-prompt internally after using
 * the completion results.
 *
 * @param result Completion result to free
 */
void tprompt_free_completion_result(tprompt_completion_result_t *result);

/* ========================================================================
 * Framework API - Validation
 * ======================================================================== */

/**
 * @brief Set validation callback at runtime
 *
 * Updates the validation callback for the session. This allows dynamic
 * modification of input validation behavior during runtime.
 *
 * The validation callback is called when the user attempts to confirm input
 * (e.g., pressing Enter in single-line mode, or Ctrl+Enter in multiline mode).
 *
 * @param handle Session handle
 * @param callback Validation callback function (NULL to disable validation)
 * @param user_data User data passed to callback
 *
 * Example:
 * @code
 * tprompt_validation_result_t validate_complete(const char *text, size_t length, void *user_data) {
 *     // Check if input ends with semicolon
 *     if (length > 0 && text[length - 1] != ';') {
 *         return TPROMPT_VALIDATION_CONTINUE;  // Insert newline, wait for more input
 *     }
 *     return TPROMPT_VALIDATION_ACCEPT;
 * }
 *
 * tprompt_set_validation_callback(handle, validate_complete, NULL);
 * @endcode
 */
void tprompt_set_validation_callback(
	tprompt_handle_t handle,
	tprompt_validation_fn callback,
	void *user_data);

/* ========================================================================
 * Framework API - Keybindings
 * ======================================================================== */

/**
 * @brief Set custom keybindings at runtime
 *
 * Updates the custom keybindings for the session. This allows dynamic
 * modification of key behavior during runtime.
 *
 * The keybindings array is copied internally, so the caller may free
 * the original array after this call.
 *
 * If validation warnings occur (duplicate bindings, unknown actions),
 * they are recorded in the error info but the function still succeeds.
 * Invalid bindings are simply ignored.
 *
 * @param handle Session handle
 * @param bindings Array of keybindings (NULL to clear custom bindings)
 * @param count Number of keybindings (0 with NULL bindings clears)
 * @return 0 on success, -1 on failure (NULL handle or NULL bindings with count > 0)
 *
 * Example:
 * @code
 * tprompt_keybinding_t new_bindings[] = {
 *     TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
 *     TPROMPT_BIND_CHAR('j', TERSE_MOD_CTRL, TPROMPT_ACTION_INSERT_NEWLINE),  // Ctrl+J (lowercase)
 * };
 * tprompt_set_keybindings(handle, new_bindings, 2);
 * @endcode
 */
int tprompt_set_keybindings(tprompt_handle_t handle,
	const tprompt_keybinding_t *bindings,
	size_t count);

/* ========================================================================
 * Framework API - Error Handling
 * ======================================================================== */

/**
 * @brief Get last error information
 *
 * Retrieves the last error that occurred in the session.
 * If handle is NULL, returns global error information (e.g., from tprompt_open() failure).
 *
 * @param handle Session handle (NULL for global error)
 * @return Error information structure
 *
 * Example:
 * @code
 * tprompt_handle_t handle = tprompt_open(&opts);
 * if (!handle) {
 *     tprompt_error_info_t err = tprompt_get_last_error(NULL);
 *     fprintf(stderr, "Failed to open: %s\n", err.message);
 * }
 * @endcode
 */
tprompt_error_info_t tprompt_get_last_error(tprompt_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TPROMPT_H */
