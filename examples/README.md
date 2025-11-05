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

## Note on Current Implementation

These examples demonstrate the intended API usage. Some features (like history navigation with arrow keys) are not fully implemented yet in the library. As the library implementation progresses, these examples will become fully functional.
