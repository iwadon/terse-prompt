# terse-prompt Examples

This directory contains example programs demonstrating how to use terse-prompt.

## Building Examples

Examples are built automatically when you configure the project:

```bash
cmake -B build -G Ninja
cmake --build build
```

To disable building examples, configure with:

```bash
cmake -B build -G Ninja -DTPROMPT_BUILD_EXAMPLES=OFF
```

## Running Examples

After building, the example executables will be in the `build/examples/` directory:

```bash
# Simple demo
./build/examples/simple_demo

# Advanced demo with history
./build/examples/advanced_demo
```

## Available Examples

### simple_demo

Demonstrates the simplest way to use terse-prompt with the `tprompt()` wrapper function.

**Features shown:**
- Single function call for input
- Basic error handling (EOF detection)
- Memory management (freeing returned strings)

**Usage:**
- Type text and press Enter to submit
- Type `quit` or `exit` to exit
- Press Ctrl+D for EOF

### advanced_demo

Demonstrates the framework API with custom configuration and history support.

**Features shown:**
- Using `tprompt_open()` and `tprompt_readline()`
- Custom configuration (buffer sizes, history settings)
- History management and persistence
- Detailed error handling with `tprompt_get_last_error()`

**Usage:**
- Type text and press Enter to submit
- Use Up/Down arrows to navigate history
- Type `help` for command list
- Type `quit` to exit
- History is saved to `demo_history.txt` in the current directory

### completion_demo

Demonstrates the completion system with multiple trigger prefixes.

**Features shown:**
- Completion callback implementation
- Multiple completion contexts (commands and mentions)
- Incremental filtering as user types
- Navigation and selection of completion candidates

**Usage:**
- Type `/` to trigger command completion (help, clear, quit, etc.)
- Type `@` to trigger mention completion (alice, bob, charlie, etc.)
- Use Up/Down arrows to navigate candidates
- Press Tab to confirm selection
- Press Esc to cancel completion
- Type `quit` to exit

**Example:**
```
> /h        (shows: help, history, hello)
> @al       (shows: alice)
> @a        (shows: alice, admin)
```

## Note on Current Implementation

These examples demonstrate the intended API usage. Some features (like history navigation with arrow keys) are not fully implemented yet in the library. As the library implementation progresses, these examples will become fully functional.
