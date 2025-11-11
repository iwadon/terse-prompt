set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR m68k)

set(CMAKE_SYSROOT /opt/homebrew/opt/elf2x68k)
set(triple m68k-xelf)

set(CMAKE_C_COMPILER ${CMAKE_SYSROOT}/bin/${triple}-gcc)
set(CMAKE_CXX_COMPILER ${CMAKE_SYSROOT}/bin/${triple}-g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Flag to enable Human68k-specific source files
set(DEFT_TARGET_HUMAN68K ON)

# Use terse's built-in mini iconv (Shift_JIS support) instead of system iconv
set(TERSE_ENABLE_ICONV OFF CACHE BOOL "Use built-in mini iconv for Human68k" FORCE)
