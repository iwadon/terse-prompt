# terse-prompt examples

# Simple demo - uses tprompt() wrapper API
add_executable(simple_demo simple_demo.c)
target_link_libraries(simple_demo PRIVATE tprompt)

# Advanced demo - uses framework API with history
add_executable(advanced_demo advanced_demo.c)
target_link_libraries(advanced_demo PRIVATE tprompt)
