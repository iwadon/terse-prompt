# terse-prompt Development Guide

## Project Overview

terse-prompt is a C library that provides multi-line text input capabilities for CLI REPL tools, similar to GNU readline or linenoise but optimized for modern CLI interfaces like Claude Code.

**Current Stage**: Framework implementation
- API design complete (`include/tprompt.h`)
- Basic structure implemented
- Core editing features in progress

**Dependencies**:
- **terse**: Terminal control library (in `external/terse/`)
- **attest**: Testing framework (in `external/attest/`)

## Build System

**CMake Configuration**:
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

This is an early-stage project. Many features are stubbed with TODO comments:
- Core editing loop (`tprompt_readline()`)
- UTF-8 navigation helpers
- History file I/O
- Completion logic
- Display rendering

Refer to TODO comments in source for implementation priorities.

### Platform Support

**Current**: POSIX-based systems (macOS, Linux)
**Future**: POSIX-specific code isolated for potential Windows support

### External Dependencies

**terse**: Core dependency for terminal control
- Located in `external/terse/` as Git submodule
- See `external/terse/CLAUDE.md` for terse-specific guidance

**attest**: Test framework
- Located in `external/attest/` as Git submodule
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

## Getting Started as a Claude Instance

1. **Read** `docs/requirements.md` to understand what the library should do
2. **Read** `include/tprompt.h` to understand the public API
3. **Examine** `src/tprompt_internal.h` for internal architecture
4. **Check** TODO comments in `src/tprompt.c` for implementation priorities
5. **Refer back** to this document for architectural principles

When implementing features:
- Maintain the established error handling patterns
- Keep UTF-8 awareness in all text operations
- Test thoroughly with the attest framework
- Follow the existing code style

When in doubt about requirements or expected behavior, consult `docs/requirements.md` or `docs/api-design.md`.
