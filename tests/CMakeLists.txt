# Phase 1 basic functionality tests
add_executable(test_basic
    test_basic.c
)

target_include_directories(test_basic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
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

target_link_libraries(test_newline PRIVATE
    tprompt
    attest
)

add_test(NAME test_newline COMMAND test_newline)
