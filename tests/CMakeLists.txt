# Phase 1 basic functionality tests
add_executable(test_basic
    test_basic.c
)

target_include_directories(test_basic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(test_basic PRIVATE
    _POSIX_C_SOURCE=200809L
    $<$<BOOL:${TPROMPT_USE_TERSE_TEST_MODE}>:TERSE_ENABLE_TEST_MODE=1>
)

target_link_libraries(test_basic PRIVATE
    tprompt
    attest
)

add_test(NAME test_basic COMMAND test_basic)

# Phase 2 history tests
add_executable(test_history
    test_history.c
)

target_include_directories(test_history PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(test_history PRIVATE
    _POSIX_C_SOURCE=200809L
    $<$<BOOL:${TPROMPT_USE_TERSE_TEST_MODE}>:TERSE_ENABLE_TEST_MODE=1>
)

target_link_libraries(test_history PRIVATE
    tprompt
    attest
)

add_test(NAME test_history COMMAND test_history)

# Phase 3 completion tests
add_executable(test_completion
    test_completion.c
)

target_include_directories(test_completion PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(test_completion PRIVATE
    _POSIX_C_SOURCE=200809L
    $<$<BOOL:${TPROMPT_USE_TERSE_TEST_MODE}>:TERSE_ENABLE_TEST_MODE=1>
)

target_link_libraries(test_completion PRIVATE
    tprompt
    attest
)

add_test(NAME test_completion COMMAND test_completion)

# Phase 4 multi-line editing tests
add_executable(test_multiline
    test_multiline.c
)

target_include_directories(test_multiline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(test_multiline PRIVATE
    _POSIX_C_SOURCE=200809L
    $<$<BOOL:${TPROMPT_USE_TERSE_TEST_MODE}>:TERSE_ENABLE_TEST_MODE=1>
)

target_link_libraries(test_multiline PRIVATE
    tprompt
    attest
)

add_test(NAME test_multiline COMMAND test_multiline)

# Phase 5 newline character support tests
add_executable(test_newline
    test_newline.c
)

target_include_directories(test_newline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(test_newline PRIVATE
    _POSIX_C_SOURCE=200809L
    $<$<BOOL:${TPROMPT_USE_TERSE_TEST_MODE}>:TERSE_ENABLE_TEST_MODE=1>
)

target_link_libraries(test_newline PRIVATE
    tprompt
    attest
)

add_test(NAME test_newline COMMAND test_newline)

# Phase 6 keybindings tests (Ctrl+W, Ctrl+K, Ctrl+U, Ctrl+A, Ctrl+E)
add_executable(test_keybindings
    test_keybindings.c
)

target_include_directories(test_keybindings PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(test_keybindings PRIVATE
    _POSIX_C_SOURCE=200809L
    $<$<BOOL:${TPROMPT_USE_TERSE_TEST_MODE}>:TERSE_ENABLE_TEST_MODE=1>
)

target_link_libraries(test_keybindings PRIVATE
    tprompt
    attest
)

add_test(NAME test_keybindings COMMAND test_keybindings)

# Display and rendering regression tests
add_executable(test_display
    test_display.c
)

target_include_directories(test_display PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(test_display PRIVATE
    _POSIX_C_SOURCE=200809L
    $<$<BOOL:${TPROMPT_USE_TERSE_TEST_MODE}>:TERSE_ENABLE_TEST_MODE=1>
)

target_link_libraries(test_display PRIVATE
    tprompt
    attest
)

add_test(NAME test_display COMMAND test_display)

# Validation callback tests
add_executable(test_validation
    test_validation.c
)

target_include_directories(test_validation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(test_validation PRIVATE
    _POSIX_C_SOURCE=200809L
    $<$<BOOL:${TPROMPT_USE_TERSE_TEST_MODE}>:TERSE_ENABLE_TEST_MODE=1>
)

target_link_libraries(test_validation PRIVATE
    tprompt
    attest
)

add_test(NAME test_validation COMMAND test_validation)

# Integration tests (tprompt_readline with event streams)
add_executable(test_integration
    test_integration.c
)

target_include_directories(test_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(test_integration PRIVATE
    _POSIX_C_SOURCE=200809L
    $<$<BOOL:${TPROMPT_USE_TERSE_TEST_MODE}>:TERSE_ENABLE_TEST_MODE=1>
)

target_link_libraries(test_integration PRIVATE
    tprompt
    attest
)

add_test(NAME test_integration COMMAND test_integration)

# Status line and cursor position tests
add_executable(test_status
    test_status.c
)

target_include_directories(test_status PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(test_status PRIVATE
    _POSIX_C_SOURCE=200809L
    $<$<BOOL:${TPROMPT_USE_TERSE_TEST_MODE}>:TERSE_ENABLE_TEST_MODE=1>
)

target_link_libraries(test_status PRIVATE
    tprompt
    attest
)

add_test(NAME test_status COMMAND test_status)
