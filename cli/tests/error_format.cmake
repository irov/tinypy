if(NOT DEFINED TINYPY_EXECUTABLE)
    message(FATAL_ERROR "TINYPY_EXECUTABLE is required")
endif()

set(runtime_source [=[def inner():
    return 1 / 0
def outer():
    return inner()
outer()]=])

execute_process(COMMAND "${TINYPY_EXECUTABLE}" -c "${runtime_source}" RESULT_VARIABLE runtime_result OUTPUT_VARIABLE runtime_stdout ERROR_VARIABLE runtime_stderr)

if(runtime_result EQUAL 0)
    message(FATAL_ERROR "runtime error command unexpectedly succeeded")
endif()

set(runtime_fragments
    "Traceback (most recent call last):"
    "  File \"<string>\", line 5, in <module>"
    "  File \"<string>\", line 4, in outer"
    "  File \"<string>\", line 2, in inner"
    "ZeroDivisionError: integer division by zero"
)

foreach(fragment IN LISTS runtime_fragments)
    string(FIND "${runtime_stderr}" "${fragment}" fragment_position)
    if(fragment_position EQUAL -1)
        message(FATAL_ERROR "runtime error output is missing '${fragment}':\n${runtime_stderr}")
    endif()
endforeach()

set(syntax_source [=[def broken(:
    pass]=])

execute_process(COMMAND "${TINYPY_EXECUTABLE}" -c "${syntax_source}" RESULT_VARIABLE syntax_result OUTPUT_VARIABLE syntax_stdout ERROR_VARIABLE syntax_stderr)

if(syntax_result EQUAL 0)
    message(FATAL_ERROR "syntax error command unexpectedly succeeded")
endif()

set(syntax_fragments
    "  File \"<string>\", line 1"
    "    def broken(:"
    "               ^"
    "SyntaxError: invalid syntax"
)

foreach(fragment IN LISTS syntax_fragments)
    string(FIND "${syntax_stderr}" "${fragment}" fragment_position)
    if(fragment_position EQUAL -1)
        message(FATAL_ERROR "syntax error output is missing '${fragment}':\n${syntax_stderr}")
    endif()
endforeach()
