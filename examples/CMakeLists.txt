# terse-prompt examples

# Simple demo - uses tprompt() wrapper API
add_executable(simple_demo simple_demo.c)
target_link_libraries(simple_demo PRIVATE tprompt)

# Advanced demo - uses framework API with history
add_executable(advanced_demo advanced_demo.c)
target_link_libraries(advanced_demo PRIVATE tprompt)

# Status line demo - demonstrates status line functionality
add_executable(status_line_demo status_line_demo.c)
target_link_libraries(status_line_demo PRIVATE tprompt)

# Continuation prompt demo - demonstrates custom continuation prompts
add_executable(continuation_prompt_demo continuation_prompt_demo.c)
target_link_libraries(continuation_prompt_demo PRIVATE tprompt)

# Deft integration example - complete REPL example with validation
add_executable(deft_integration_example deft_integration_example.c)
target_link_libraries(deft_integration_example PRIVATE tprompt)

# Completion demo - demonstrates completion system with multiple prefixes
add_executable(completion_demo completion_demo.c)
target_link_libraries(completion_demo PRIVATE tprompt)

# Scrolling completion demo - demonstrates scrollable completion with 25+ candidates
add_executable(scrolling_completion_demo scrolling_completion_demo.c)
target_link_libraries(scrolling_completion_demo PRIVATE tprompt)
