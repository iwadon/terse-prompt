# Repository Guidelines

## Project Structure & Module Organization
- `include/` exposes the stable API in `tprompt.h`; treat it as public.
- `src/` contains the implementation and internal headers; keep platform specifics separated as seen in `tprompt_internal.h`.
- `tests/` hosts Attest-based regression suites grouped by feature (history, completion, multiline).
- `examples/` offers runnable demos; keep behavior synchronized with the API docs.
- `docs/` stores product requirements and design notes; update when behavior changes.
- `external/` holds the `terse` submodule; sync via `git submodule update --init --recursive` after cloning.

## Build, Test, and Development Commands
- Configure: `cmake -B build -G Ninja` (set `TPROMPT_BUILD_EXAMPLES` or `TPROMPT_BUILD_TESTS` as needed).
- Build library and samples: `cmake --build build`.
- Run all tests: `ctest --test-dir build --output-on-failure`.
- Run demos: `./build/examples/simple_demo` or `./build/examples/advanced_demo`.

## Coding Style & Naming Conventions
- Follow `.clang-format` (WebKit profile, tabs width 4) via `clang-format -i src/*.c include/*.h`.
- `.editorconfig` enforces tab indentation for C/C++ and LF endings; use spaces only in CMake/YAML/JSON.
- Prefer `tprompt_*` for API symbols and internal helpers; avoid exporting file-local helpers.
- Keep functions grouped by feature with separator comments mirroring the existing layout.

## Testing Guidelines
- Extend Attest suites in `tests/`; name new files `test_<feature>.c` and register them in `tests/CMakeLists.txt`.
- Cover both success and failure paths, including multi-byte input scenarios and terminal edge cases.
- Use terse's test mode to simulate terminals; avoid brittle timing-dependent checks.
- Run `ctest --rerun-failed --test-dir build` before pushing.

## Commit & Pull Request Guidelines
- Use Conventional Commit prefixes seen in history (`feat:`, `fix:`, `docs:`, `test:`) and keep scope narrow.
- Explain behavioral changes and test coverage in the body; mention follow-up work if deferred.
- PRs should describe the motivation, link issues/spec updates, and include output or recordings for interactive changes.
- Verify formatting, tests, and submodule pointers locally before requesting review.

## Submodules & Configuration Tips
- When updating `external/terse`, refresh the submodule pointer and note the change in the PR summary.
- Exclude `build/` artifacts from commits; use `cmake --build build --target clean` when needed.
- Document new configuration flags in `docs/requirements.md` to keep requirements aligned.
