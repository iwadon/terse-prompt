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
