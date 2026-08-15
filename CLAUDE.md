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
- `src/tprompt_*.c`: 機能別に分割された実装 (buffer, display, history, completion,
  keybinding, session, action, status, api, init, error, match, utf8, util)
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
- `.clang-format` (WebKit profile, tab width 4) で整形: `clang-format -i src/*.c include/*.h`
- `.editorconfig` により C/C++ はタブインデント・LF 改行を強制。スペースは CMake/YAML/JSON のみ
- Function documentation in header files
- Implementation comments for non-obvious logic

**Naming Conventions**:
- Functions: `tprompt_*`
- Types: `tprompt_*_t`
- Constants: `TPROMPT_*`
- Internal helpers: Declared in `tprompt_internal.h`

### POSIX 依存の方針

厳格な ISO C11 (`-std=c11` + `CMAKE_C_EXTENSIONS OFF`) でビルドするため、glibc は
POSIX 拡張をヘッダから隠す。**非 ISO C の関数を安易に使わないこと。**

- **`strdup()` は使わない。** 代わりに `tprompt_strdup()` (`src/tprompt_util.c`) を使う。
  NULL 引数を許容し NULL を返すので、呼び出し側での NULL ガードは不要
- **`_POSIX_C_SOURCE` を定義してよいのは 2 ファイルのみ**: `src/tprompt_history.c`
  (ファイル I/O) と `src/tprompt_session.c` (端末制御)。この 2 つは POSIX 機能そのものを
  使うため依存が正当だが、それ以外のファイルに広げないこと。マクロを増やす前に、
  ISO C11 だけで書けないかを検討する
- **`examples/` は内部ヘッダを使えない**ため、必要なら各ファイルに static な
  ローカルヘルパーを置く
- `-Werror=implicit-function-declaration` が有効なので、宣言のない関数呼び出しは
  ビルドエラーになる

**背景**: `strdup` は POSIX 拡張であり、macOS では既定で見えるが glibc の strict モードでは
隠れる。さらに MSVC では C4996 非推奨警告の対象で、非 POSIX 環境には存在しない。
`_POSIX_C_SOURCE` の一括定義は glibc の問題しか解決せず、macOS では `__DARWIN_C_LEVEL` が
下がって BSD 拡張 (`strlcpy`, `cfmakeraw` 等) が逆に隠れる副作用がある。

## Development Workflow

1. **Feature Implementation**:
   - Update API in `include/tprompt.h` if needed
   - Add internal helpers in `tprompt_internal.h`
   - 該当する機能の `src/tprompt_*.c` に実装する。新しい領域なら新規ファイルを追加し、
     ルート `CMakeLists.txt` の `add_library(tprompt STATIC ...)` にソースを登録する
   - Write tests in `tests/`

2. **Testing**:
   - Run full test suite: `ctest --test-dir build`
   - All tests must pass
   - Add tests for new features before implementation

3. **Cross-platform Verification** (ローカルCI):
   - `git push weir && weir ci wait` で macOS (host) と Ubuntu コンテナ (container) の
     両環境のビルド・テストを検証できる
   - **macOS でのテスト通過だけでは不十分。** glibc は strict ISO C11 で POSIX 拡張を
     隠すなど Apple libc と挙動が異なり、Linux 側だけが落ちることがある
     (実例: `strdup` の implicit declaration エラー。「POSIX 依存の方針」を参照)
   - 失敗時は `weir ci log -failed` で失敗したステップのログを取得する
   - `.weir.yml` の `paths-ignore` により、ドキュメントのみの変更はスキップされる

4. **Commit**:
   - Follow Conventional Commits specification
   - Ensure clean build and passing tests

## Documentation

**Key Documents**:
- `docs/requirements.md`: Functional and non-functional requirements
- `docs/api-design.md`: Detailed API specification with examples
- `docs/implementation-status.md`: Remaining work, roadmap, known limitations
- `include/tprompt.h`: API reference (authoritative source)

**Relationship**:
- Requirements define *what* to build
- API design defines *how* to use it
- Implementation status tracks *what remains* and *where we're going*
- This document (CLAUDE.md) defines *how* it works internally

**When to Consult**:
- Requirements: Understanding feature scope and priorities
- API design: Implementation details, usage patterns, examples
- Implementation status: Remaining tasks, future roadmap, known limitations
- tprompt.h: Current API surface, function signatures

### Documentation Update Rules

実装作業の中で以下のタイミングでドキュメントを更新する。

**実装完了時 (`/review-session` 実行時)**:
- `docs/implementation-status.md` の残作業・制限事項に変化がないか確認
- スコープ外にした項目があれば、ロードマップまたは制限事項に記録

**新機能追加時**:
- 既知の制限事項が解消された場合、該当項目を削除
- 新しい制限事項が生じた場合、追記

**更新しなくてよいもの**:
- コードから自明な情報（API一覧、テスト数、行数統計、コミット履歴）
- `include/tprompt.h` や `git log` で確認できる内容

## Important Notes

### Current Implementation Status

All core features are fully implemented and tested. See `docs/implementation-status.md` for remaining work, future roadmap, and known limitations.

### Platform Support

**Current**: POSIX-based systems (macOS, Linux), MSVC build support
**Testing**: macOS verified, Linux/Windows untested

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
