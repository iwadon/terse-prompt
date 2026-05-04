/**
 * @file example_console.h
 * @brief Console setup helpers shared by terse-prompt examples
 *
 * On Windows, the console output code page defaults to a locale-specific
 * encoding (e.g. CP932 in Japan), which mangles UTF-8 byte sequences when
 * the application prints them. terse-prompt itself returns UTF-8 strings
 * but does NOT change the console code page — that is the application's
 * responsibility, since the code page affects all I/O for the whole process.
 *
 * This header provides a tiny helper that example programs call from main()
 * to switch the console to UTF-8 (CP 65001) for the duration of the process,
 * restoring the original code page on exit. Real applications can either
 * adopt the same pattern or manage the code page in their own way.
 *
 * On POSIX systems the helper is a no-op.
 */

#ifndef EXAMPLE_CONSOLE_H
#define EXAMPLE_CONSOLE_H

#ifdef _WIN32
#include <stdlib.h>
#include <windows.h>

static UINT example_original_cp = 0;
static UINT example_original_output_cp = 0;

static void example_restore_console_cp(void)
{
	if (example_original_cp != 0) {
		SetConsoleCP(example_original_cp);
	}
	if (example_original_output_cp != 0) {
		SetConsoleOutputCP(example_original_output_cp);
	}
}

static void example_setup_console_utf8(void)
{
	example_original_cp = GetConsoleCP();
	example_original_output_cp = GetConsoleOutputCP();
	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);
	atexit(example_restore_console_cp);
}
#else
static inline void example_setup_console_utf8(void) {}
#endif

#endif /* EXAMPLE_CONSOLE_H */
