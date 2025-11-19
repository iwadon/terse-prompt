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
