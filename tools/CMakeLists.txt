# terse-prompt development tools

# Debug REPL - internal debugging tool with status line showing internal state
add_executable(tprompt_debug tprompt_debug.c)
target_link_libraries(tprompt_debug PRIVATE tprompt)
