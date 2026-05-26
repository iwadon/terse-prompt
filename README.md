# terse-prompt

A lightweight, modern C library for multi-line text input in CLI applications. Think GNU readline or linenoise, but designed for modern terminal interfaces.

## Features

- **Multi-line editing**: Full support for multi-line text input with word wrapping
- **Customizable continuation prompts**: Configure how continuation lines are displayed
- **UTF-8 support**: Native UTF-8 handling for international text
- **Command history**: Persistent history with file storage and LRU eviction
- **Tab completion**: Trigger-based completion system with incremental filtering
- **Custom keybindings**: Flexible keybinding system for customizing input behavior
- **Status line**: Customizable status line with application-specific information
- **Efficient rendering**: Buffer-based differential rendering for flicker-free display
- **Two API styles**: Simple wrapper for basic use, framework API for advanced customization
- **Zero external dependencies**: Only requires the bundled `terse` terminal control library

## Quick Start

### Building

```bash
# Clone the repository
git clone https://github.com/iwadon/terse-prompt.git
cd terse-prompt

# Configure and build (dependencies are fetched automatically via CMake FetchContent)
cmake -B build -G Ninja
cmake --build build

# Run tests
ctest --test-dir build
```

### Basic Usage

```c
#include <tprompt.h>
#include <stdio.h>

int main(void) {
    // Simple one-liner: just like readline()
    char *input = tprompt(">>> ", NULL);
    if (input) {
        printf("You entered: %s\n", input);
        free(input);
    }
    return 0;
}
```

### Multi-line with History

```c
#include <tprompt.h>
#include <stdio.h>

int main(void) {
    // Configure options
    tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
    opts.history_file = ".my_history";
    opts.max_history_size = 1000;
    opts.flags = TPROMPT_FLAG_MULTILINE;

    // Open a session
    tprompt_handle_t *tp = tprompt_open(&opts);
    if (!tp) {
        fprintf(stderr, "Failed to open tprompt\n");
        return 1;
    }

    // Read input in a loop
    while (1) {
        char *input = tprompt_readline(tp, ">>> ");
        if (!input) break;  // EOF or error

        if (*input) {
            tprompt_add_history(tp, input);
            printf("You entered:\n%s\n", input);
        }

        free(input);
    }

    tprompt_close(tp);
    return 0;
}
```

## API Overview

### Simple API

For basic use cases, just one function:

```c
char *tprompt(const char *prompt, tprompt_options_t *options);
```

Returns a malloc'd string containing the user's input. Free it when done.

### Framework API

For advanced features (completion, status line, validation):

```c
// Session management
tprompt_handle_t *tprompt_open(tprompt_options_t *options);
void tprompt_close(tprompt_handle_t *tp);

// Input reading
char *tprompt_readline(tprompt_handle_t *tp, const char *prompt);

// History management
void tprompt_add_history(tprompt_handle_t *tp, const char *line);
void tprompt_clear_history(tprompt_handle_t *tp);

// Completion (set callback in options)
typedef char **(*tprompt_completion_fn)(
    void *userdata,
    const char *input,
    size_t cursor_position,
    size_t *count_out
);

// Status line (set via tprompt_set_status_line_callback)
typedef char *(*tprompt_status_line_fn)(
    void *userdata,
    const char *input,
    size_t cursor_position
);

// Custom keybindings
void tprompt_set_keybindings(
    tprompt_handle_t *tp,
    tprompt_keybinding_t *bindings,
    size_t count
);
```

## Advanced Features

### Custom Continuation Prompts

Customize how continuation lines are displayed in multi-line mode:

```c
tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.prompt = ">>> ";
opts.continuation_prompt = "... ";  // Python-style continuation

// Or use default "| " if not specified
```

The continuation prompt is automatically padded or truncated to match the initial prompt width:
- **Shorter**: Right-padded with spaces to align text
- **Longer**: Truncated with a warning to prevent display issues

### Tab Completion

```c
char **my_completion_fn(void *userdata, const char *input,
                        size_t cursor_pos, size_t *count_out) {
    // Generate completions based on input
    static char *completions[] = { "foo", "bar", "baz" };
    *count_out = 3;
    return completions;
}

tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.completion_fn = my_completion_fn;
opts.completion_triggers = "/";  // Trigger on '/'
```

### Custom Keybindings

```c
// Customize Enter behavior
tprompt_keybinding_t bindings[] = {
    // Enter confirms, Shift+Enter inserts newline
    TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),
    TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, TERSE_MOD_SHIFT, TPROMPT_ACTION_INSERT_NEWLINE),
    // Ctrl+J also inserts newline
    TPROMPT_BIND_CHAR('j', TERSE_MOD_CTRL, TPROMPT_ACTION_INSERT_NEWLINE),
};

tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.custom_keybindings = bindings;
opts.keybinding_count = 3;
```

Need a manual "force submit" key? Bind `TPROMPT_ACTION_CONFIRM_WITHOUT_VALIDATION` (for example, to
`Alt+Enter`) to skip the validation callback only when explicitly requested by the user.

### Status Line

```c
char *my_status_fn(void *userdata, const char *input, size_t cursor_pos) {
    static char buf[128];
    snprintf(buf, sizeof(buf), "Position: %zu | Length: %zu",
             cursor_pos, strlen(input));
    return buf;
}

tprompt_handle_t *tp = tprompt_open(&opts);
tprompt_set_status_line_callback(tp, my_status_fn, NULL);
```

### Input Validation

```c
typedef enum {
    TPROMPT_VALIDATION_VALID,
    TPROMPT_VALIDATION_INCOMPLETE,
    TPROMPT_VALIDATION_INVALID
} tprompt_validation_result_t;

tprompt_validation_result_t my_validator(void *userdata, const char *input) {
    // Check if input is valid
    if (needs_more_input(input))
        return TPROMPT_VALIDATION_INCOMPLETE;
    if (is_invalid(input))
        return TPROMPT_VALIDATION_INVALID;
    return TPROMPT_VALIDATION_VALID;
}

tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.validation_fn = my_validator;
```

## Key Bindings

Default keybindings:

- `Enter`: Submit input
- `Shift+Enter` or `Ctrl+Enter`: Insert newline without submitting

To bypass validation, add a custom binding (not enabled by default), e.g. map `Alt+Enter` to
`TPROMPT_ACTION_CONFIRM_WITHOUT_VALIDATION` via `tprompt_keybinding_t`.

- **Navigation**:
  - Arrow keys: Move cursor
  - `Home`/`End`: Move to line start/end (press twice for logical line)
  - `Ctrl+A`/`Ctrl+E`: Move to start/end of line

- **Editing**:
  - `Backspace`/`Delete`: Delete characters
  - `Ctrl+U`: Delete to start of line
  - `Ctrl+K`: Delete to end of line

- **History**:
  - `Up`/`Down`: Navigate history

- **Completion**:
  - Type trigger character (e.g., `/`)
  - Arrow keys: Navigate candidates
  - `Tab`: Select candidate
  - `Esc`: Cancel completion

## Building for Different Platforms

### Unix-like (Linux, macOS)

```bash
cmake -B build -G Ninja
cmake --build build
```

### Windows (MSVC)

```powershell
# Visual Studio 2019 or later required
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

## Documentation

- `docs/requirements.md`: Feature requirements and specifications
- `docs/api-design.md`: Detailed API design with examples
- `include/tprompt.h`: Complete API reference
- `CLAUDE.md`: Development guide for contributors

## Examples

See the `examples/` directory:
- `simple_demo.c`: Basic usage with the simple API
- `advanced_demo.c`: Completion, status line, and custom keybindings
- `deft_integration_example.c`: Complete REPL example with validation and continuation prompts

Build and run:
```bash
cmake --build build
./build/examples/simple_demo
./build/examples/advanced_demo
./build/examples/deft_integration_example
```

## Integration Guides

- **[deft Integration Guide](docs/deft-integration-guide.md)**: Complete guide for integrating terse-prompt into REPL applications with validation callbacks and continuation prompts

## License

MIT-0 (MIT No Attribution) - see LICENSE file for details

## Contributing

Contributions welcome! Please:
1. Read `CLAUDE.md` for development guidelines
2. Ensure all tests pass: `ctest --test-dir build`
3. Follow Conventional Commits for commit messages
4. Update documentation for API changes

## Credits

Built on top of [terse](https://github.com/iwadon/terse), a minimal terminal control library.
