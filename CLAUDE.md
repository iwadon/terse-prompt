# terse-prompt Development Guide

## Project Overview

terse-prompt is a C library that provides multi-line text input capabilities for CLI REPL tools, similar to GNU readline or linenoise but optimized for modern CLI interfaces like Claude Code.

**Current Stage**: Feature-complete (v0.1)
- API design complete and implemented (`include/tprompt.h`)
- All core editing features implemented and tested
- Status line support implemented
- Buffer-based differential rendering implemented

See `docs/implementation-status.md` for current test status and details.

**Dependencies**:
- **terse**: Terminal control library (via CMake FetchContent)
- **attest**: Testing framework (via CMake FetchContent)

## Build System

### Unix-like Systems (macOS, Linux)

**CMake Configuration with Ninja**:
```bash
# Configure build
cmake -B build -G Ninja

# Build library and tests
cmake --build build

# Run tests
ctest --test-dir build

# Clean build
rm -rf build
```

### Windows (MSVC)

**CMake Configuration with Visual Studio**:
```powershell
# Configure build (Visual Studio 2019 or later)
cmake -B build -G "Visual Studio 16 2019"

# Or for Visual Studio 2022
cmake -B build -G "Visual Studio 17 2022"

# Build library and tests (Debug configuration)
cmake --build build --config Debug

# Build library and tests (Release configuration)
cmake --build build --config Release

# Run tests
ctest --test-dir build -C Debug

# Clean build
rmdir /s /q build
```

**Notes for Windows**:
- Requires Visual Studio 2019 or later with C++ development tools
- CMake will detect the installed Visual Studio version automatically
- Debug and Release configurations are separate in MSVC
- Use `-C Debug` or `-C Release` with ctest to specify configuration

**Project Structure**:
- `include/tprompt.h`: Public API
- `src/tprompt.c`: Implementation
- `src/tprompt_internal.h`: Internal structures and helpers
- `tests/`: Test suite (using attest framework)

## Testing

**Framework**: attest (Google Test-style C testing library)

**Organization**:
- Tests organized by feature area
- All tests must pass before committing
- Run full test suite with `ctest --test-dir build`

**Testing Philosophy**:
- Test public API behavior, not internal implementation
- Cover edge cases (empty input, UTF-8 boundaries, buffer limits)
- Test error handling paths

### Mock Usage Pattern (REQUIRED)

**IMPORTANT**: All tests that create `tprompt_handle_t` MUST use mock terse handles via `test_helpers.h`.

**Why Mock Handles Are Required**:
- Tests without mocks trigger actual terminal control (escape sequences, raw mode)
- This causes terminal state corruption during test execution
- Mock mode is significantly faster by eliminating terminal I/O overhead
- Enables testing in non-TTY environments (CI/CD pipelines)
- Prevents test flakiness and terminal I/O interference

**Correct Pattern** (use this for ALL new tests):

```c
#include "test_helpers.h"
#include "tprompt_internal.h"
#include <attest/attest.h>

static tprompt_handle_t create_test_handle(void)
{
    // Create mock terse handle for test mode
    terse_handle_t terse_h = test_create_terse_handle();
    if (!terse_h) {
        return NULL;
    }

    tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
    opts.terse_handle = terse_h;  // Use mock handle

    tprompt_handle_t handle = tprompt_open(&opts);
    if (!handle) {
        terse_close(terse_h);
        return NULL;
    }

    return handle;
}

TEST(Example, BasicTest)
{
    tprompt_handle_t handle = create_test_handle();
    ASSERT_NE(handle, NULL);

    // Your test code here

    tprompt_close(handle);
}
```

**Common Mistake** (DO NOT do this):

```c
// WRONG: Creates real terse handle, affects terminal state
tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.terse_handle = NULL;  // This creates a REAL handle internally!
tprompt_handle_t handle = tprompt_open(&opts);
```

**Reference Implementations**:
- `tests/test_validation.c` - Comprehensive mock usage example
- `tests/test_keybindings.c` - Keybinding tests with mocks
- `tests/test_helpers.h` - Mock helper function definitions

## Code Architecture

### Dual API Design

The library provides two complementary interfaces:

1. **Simple Wrapper API** (`tprompt()`): Single function call for basic use cases
2. **Framework API** (`tprompt_open()`, `tprompt_readline()`, etc.): Full control with customization

**Design Principle**: Make simple things simple, complex things possible.

### Handle-Based Design

All state is encapsulated in an opaque `tprompt_handle_t`:
- Created by `tprompt_open()`, destroyed by `tprompt_close()`
- Internal structure defined in `src/tprompt_internal.h`
- Allows multiple independent editing sessions

### terse Integration

terse-prompt delegates terminal control to the terse library:
- **Default behavior**: Creates and manages terse handle internally
- **Advanced usage**: Accepts externally-provided terse handle via `tprompt_options_t`
- **Ownership model**: Only destroys internally-created handles

**Rationale**: Separation of concerns - terse handles terminal I/O, terse-prompt handles editing logic.

### UTF-8 Text Handling

**Internal Representation**: UTF-8 byte sequences
- All text operations aware of multi-byte characters
- Character width obtained from terse events (`event.data.ch.width`)
- Cursor position tracked in bytes, converted for display

**Key Consideration**: Character vs. byte offsets
- Internal: byte offsets for buffer operations
- User-facing: character counts for movement
- Helper functions bridge the gap (see `src/tprompt_internal.h`)

### Buffer Management

**Dynamic Growth Strategy**:
- Start small (configurable initial size)
- Double capacity when needed
- Respect `max_input_size` limit
- Efficient insertion/deletion at cursor

**Memory Safety**: All buffers NUL-terminated, bounds-checked.

### History System

**Architecture**: Doubly-linked list
- Head = most recent entry
- Tail = oldest entry
- Navigation pointer for browsing

**Persistence**:
- Memory-only by default
- Optional file persistence (loaded on open, saved on close)
- Configurable maximum size with LRU eviction

**Design Choice**: Linked list allows efficient addition/removal at both ends.

### Completion System

**Trigger-Based Activation**:
- User defines prefix characters (e.g., `/`, `@`)
- Completion activates on prefix entry
- Incremental filtering as user types

**Callback Architecture**:
- Application provides `tprompt_completion_fn` callback
- Library calls on each keystroke in completion mode
- Application returns filtered candidates
- Library manages display and selection

**UI Flow**:
1. User types trigger character → activate completion
2. Display candidate list below input
3. Arrow keys navigate candidates
4. TAB confirms selection
5. ESC cancels completion

### Multi-line Editing

**Logical vs. Physical Lines**:
- **Logical line**: User's text (newline-delimited in future, currently single logical line)
- **Physical line**: Screen rows (wrapped at terminal width)

**Home/End Behavior** (Claude Code-style):
- First press: Move to physical line boundary
- Second press: Move to logical line boundary

**Display Strategy**: Calculate layout based on terminal width, render with wrapping.

### Custom Keybindings

**Architecture**: Flexible keybinding system for customizing input confirmation and newline insertion
- **Default behavior**: Enter confirms (with validation if set); Shift/Ctrl+Enter insert newline in multiline mode
- **Opt-in bypass**: Bind `TPROMPT_ACTION_CONFIRM_WITHOUT_VALIDATION` (e.g., Alt+Enter) if you need a validation-skipping submit key
- **Custom bindings**: Override with `tprompt_keybinding_t` array in `tprompt_options_t`
- **Runtime modification**: `tprompt_set_keybindings()` allows dynamic changes

**Key Features**:
- **Partial override**: Custom bindings only override specified keys, others use defaults
- **Event matching**: Supports special keys (Enter, Tab, etc.), character keys (with Ctrl/Alt), and function keys
- **Helper macros**: `TPROMPT_BIND_KEY()`, `TPROMPT_BIND_CHAR()`, `TPROMPT_BIND_FUNCTION()` for easy initialization
- **Validation**: Duplicate bindings and unknown actions trigger warnings but don't fail

**Actions**:
- `TPROMPT_ACTION_CONFIRM_INPUT`: Submit input and return to caller
- `TPROMPT_ACTION_INSERT_NEWLINE`: Insert newline at cursor
- `TPROMPT_ACTION_CONFIRM_WITHOUT_VALIDATION`: Submit immediately without running validation callback
- Future: Completion, cancellation, history navigation

**Example**:
```c
tprompt_keybinding_t bindings[] = {
    TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, 0, TPROMPT_ACTION_CONFIRM_INPUT),           // Enter = confirm
    TPROMPT_BIND_KEY(TERSE_EVENT_ENTER, TERSE_MOD_SHIFT, TPROMPT_ACTION_INSERT_NEWLINE),  // Shift+Enter = newline
    TPROMPT_BIND_CHAR('j', TERSE_MOD_CTRL, TPROMPT_ACTION_INSERT_NEWLINE),          // Ctrl+J = newline (lowercase!)
};

tprompt_options_t opts = TPROMPT_OPTIONS_DEFAULT;
opts.custom_keybindings = bindings;
opts.keybinding_count = 3;
```

**Important**: When binding Ctrl+letter combinations, use lowercase letters. The terse library transmits Ctrl+J as `TERSE_EVENT_CHAR` with `scalar='j'` (lowercase) and `TERSE_MOD_CTRL`.

**Implementation Details**:
- **Search order**: Custom bindings checked before default behavior in `tprompt_handle_key_event()`
- **Matching logic**: Key type + modifiers + (scalar for CHAR, function number for FUNCTION)
- **Memory management**: Array deep-copied in `tprompt_open()`, freed in `tprompt_close()`

### Error Handling

**Two-Level Error Reporting**:
1. **Return value**: NULL/-1 indicates failure
2. **Error details**: Retrieved via `tprompt_get_last_error()`

**Error Context**:
- Per-handle errors for active sessions
- Global error for `tprompt_open()` failures
- Error cleared on successful operations

**errno Integration**: I/O errors also set errno for compatibility.

## Code Style

**C Standard**: C11, strict conformance (`-std=c11`)
**File Extensions**: `.c` and `.h`

**Formatting**:
- Consistent indentation (tabs/spaces as per existing code)
- Function documentation in header files
- Implementation comments for non-obvious logic

**Naming Conventions**:
- Functions: `tprompt_*`
- Types: `tprompt_*_t`
- Constants: `TPROMPT_*`
- Internal helpers: Declared in `tprompt_internal.h`

## Development Workflow

1. **Feature Implementation**:
   - Update API in `include/tprompt.h` if needed
   - Add internal helpers in `tprompt_internal.h`
   - Implement in `src/tprompt.c`
   - Write tests in `tests/`

2. **Testing**:
   - Run full test suite: `ctest --test-dir build`
   - All tests must pass
   - Add tests for new features before implementation

3. **Commit**:
   - Follow Conventional Commits specification
   - Ensure clean build and passing tests

## Documentation

**Key Documents**:
- `docs/requirements.md`: Functional and non-functional requirements
- `docs/api-design.md`: Detailed API specification with examples
- `include/tprompt.h`: API reference (authoritative source)

**Relationship**:
- Requirements define *what* to build
- API design defines *how* to use it
- This document (CLAUDE.md) defines *how* it works internally

**When to Consult**:
- Requirements: Understanding feature scope and priorities
- API design: Implementation details, usage patterns, examples
- tprompt.h: Current API surface, function signatures

## Important Notes

### Current Implementation Status

All core features are fully implemented and tested. See `docs/implementation-status.md` for detailed feature status, test results, and platform support information.

### Platform Support

**Current**: POSIX-based systems (macOS, Linux)
**Future**: POSIX-specific code isolated for potential Windows support

### External Dependencies

**terse**: Core dependency for terminal control
- Fetched via CMake FetchContent (see `CMakeLists.txt` for pinned version)
- Source: https://github.com/iwadon/terse.git

**attest**: Test framework
- Fetched via CMake FetchContent (see `CMakeLists.txt` for pinned version)
- Source: https://github.com/iwadon/attest.git
- Only required when `TPROMPT_BUILD_TESTING` is enabled

### Design Principles

1. **Simplicity First**: The simple wrapper should handle 80% of use cases
2. **Progressive Disclosure**: Advanced features available but not required
3. **Delegation**: Use terse for terminal control, focus on editing
4. **Safety**: All operations bounds-checked, UTF-8 aware
5. **Testability**: Internal functions separated for unit testing

### Key Design Decisions

**Why Two APIs?**
- Many users want "just readline()"
- Power users need customization
- Both share the same implementation

**Why Handle-Based?**
- Multiple independent sessions
- Clean resource management
- Thread-safety friendly (future)

**Why UTF-8 Internally?**
- Native format for most modern terminals
- No conversion overhead
- Direct compatibility with terse

**Why Callback-Based Completion?**
- Application controls candidate generation
- Library remains domain-agnostic
- Supports complex filtering logic

**Why Linked-List History?**
- Efficient LRU eviction
- Simple navigation pointer
- Low memory overhead for typical sizes

### Status Line System

**Architecture**: Callback-based status line rendering
- **Custom callback**: Application provides `tprompt_status_line_fn` callback
- **Debug mode**: Built-in debug status with `TPROMPT_FLAG_SHOW_DEBUG_STATUS`
- **Helper functions**: `tprompt_get_cursor_line()`, `tprompt_get_cursor_column()`

**UI Flow**:
1. Application sets status line callback via `tprompt_set_status_line_callback()`
2. Callback called during each render cycle
3. Callback returns formatted text (with optional ANSI colors)
4. Status line displayed below editing area, above completion candidates

**Design Choice**: Callback architecture allows application-specific status information without library coupling.

### Buffer-Based Differential Rendering

**Architecture**: Virtual screen buffer with frame diffing
- **Current/Previous buffers**: Two screen buffers for frame comparison
- **Dirty cell tracking**: Per-cell dirty flags for minimal updates
- **Dynamic resizing**: Buffers grow dynamically as needed

**Performance Benefits**:
- Eliminates flicker on slow terminals (e.g., Human68k)
- Reduces escape sequence overhead
- Handles complex multi-line layouts efficiently

**Design Choice**: Screen buffer abstraction separates logical editing from physical rendering.

## Getting Started as a Claude Instance

1. **Read** `docs/requirements.md` to understand what the library should do
2. **Read** `include/tprompt.h` to understand the public API
3. **Examine** `src/tprompt_internal.h` for internal architecture
4. **Check** `docs/implementation-status.md` for current implementation status
5. **Refer back** to this document for architectural principles

When implementing new features:
- Maintain the established error handling patterns
- Keep UTF-8 awareness in all text operations
- Test thoroughly with the attest framework
- Follow the existing code style
- Update documentation to reflect changes

When in doubt about requirements or expected behavior, consult `docs/requirements.md` or `docs/api-design.md`.
