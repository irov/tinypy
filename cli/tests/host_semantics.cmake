if(NOT DEFINED TINYPY_EXECUTABLE OR NOT DEFINED TINYPY_WORK_DIR)
    message(FATAL_ERROR "TINYPY_EXECUTABLE and TINYPY_WORK_DIR are required")
endif()

# Scripts and the modules they import are files: the file tokenizer reads
# them, their directory resolves to sys.path[0], and the host finalizes the
# program like Py_Finalize. Each case runs a script from the work directory.

file(REMOVE_RECURSE "${TINYPY_WORK_DIR}")
file(MAKE_DIRECTORY "${TINYPY_WORK_DIR}/pkg")
get_filename_component(work_directory "${TINYPY_WORK_DIR}" REALPATH)
string(ASCII 233 e_acute)
string(ASCII 9 tab)

function(tinypy_expect_script name script)
    cmake_parse_arguments(CASE "" "STDOUT_EQUAL;STDERR_EQUAL;INPUT" "CODES;ARGS" ${ARGN})
    if(DEFINED CASE_INPUT)
        file(WRITE "${TINYPY_WORK_DIR}/${name}.input" "${CASE_INPUT}")
        execute_process(COMMAND "${TINYPY_EXECUTABLE}" ${CASE_ARGS} ${script} WORKING_DIRECTORY "${TINYPY_WORK_DIR}" INPUT_FILE "${TINYPY_WORK_DIR}/${name}.input" RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    else()
        execute_process(COMMAND "${TINYPY_EXECUTABLE}" ${CASE_ARGS} ${script} WORKING_DIRECTORY "${TINYPY_WORK_DIR}" RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    endif()
    if(NOT result IN_LIST CASE_CODES)
        message(FATAL_ERROR "${name}: exit status ${result} is not one of ${CASE_CODES}:\n${stdout}${stderr}")
    endif()
    if(DEFINED CASE_STDOUT_EQUAL AND NOT stdout STREQUAL CASE_STDOUT_EQUAL)
        message(FATAL_ERROR "${name}: stdout differs from '${CASE_STDOUT_EQUAL}':\n${stdout}")
    endif()
    if(DEFINED CASE_STDERR_EQUAL)
        # A semicolon would split the expectation into a list.
        string(REPLACE "<semicolon>" ";" expected_stderr "${CASE_STDERR_EQUAL}")
        if(NOT stderr STREQUAL expected_stderr)
            message(FATAL_ERROR "${name}: stderr differs from '${expected_stderr}':\n${stderr}")
        endif()
    endif()
endfunction()

# decoding_fgets: a file without a declared encoding is ASCII.
file(WRITE "${TINYPY_WORK_DIR}/non_ascii.py" "x = 1\ny = '${e_acute}'\n")
tinypy_expect_script(non_ascii non_ascii.py CODES 1
    STDERR_EQUAL "  File \"non_ascii.py\", line 2\nSyntaxError: Non-ASCII character '\\xe9' in file non_ascii.py on line 2, but no encoding declared<semicolon> see http://python.org/dev/peps/pep-0263/ for details\n")
file(WRITE "${TINYPY_WORK_DIR}/declared.py" "# coding: latin-1\nx = '${e_acute}'\nprint repr(x)\n")
tinypy_expect_script(declared declared.py CODES 0 STDOUT_EQUAL "'\\xe9'\n")

# The end of a file is one more line, with an empty text.
file(WRITE "${TINYPY_WORK_DIR}/eof.py" "x = (1,\n")
tinypy_expect_script(eof eof.py CODES 1 STDERR_EQUAL "  File \"eof.py\", line 2\n    \n           ^\nSyntaxError: invalid syntax\n")
tinypy_expect_script(eof_stdin - CODES 1 INPUT "x = (1,\n" STDERR_EQUAL "  File \"<stdin>\", line 2\n    \n           ^\nSyntaxError: invalid syntax\n")
file(WRITE "${TINYPY_WORK_DIR}/triple.py" "x = 1\ny = '''abc\ndef\n")
tinypy_expect_script(triple triple.py CODES 1 STDERR_EQUAL "  File \"triple.py\", line 5\n    \n    ^\nSyntaxError: EOF while scanning triple-quoted string literal\n")

# A cookie the host cannot honour fails on its line.
file(WRITE "${TINYPY_WORK_DIR}/cookie.py" "#!/x\n# coding: made_up\nprint 1\n")
tinypy_expect_script(cookie cookie.py CODES 1 STDERR_EQUAL "  File \"cookie.py\", line 2\nSyntaxError: encoding problem: made_up\n")

# Imported modules are files under the resolved script directory.
file(WRITE "${TINYPY_WORK_DIR}/bad_mod.py" "x = 1\ny = = 2\n")
file(WRITE "${TINYPY_WORK_DIR}/importer.py" "import bad_mod\n")
tinypy_expect_script(importer importer.py CODES 1
    STDERR_EQUAL "Traceback (most recent call last):\n  File \"importer.py\", line 1, in <module>\n    import bad_mod\n  File \"${work_directory}/bad_mod.py\", line 2\n    y = = 2\n        ^\nSyntaxError: invalid syntax\n")
file(WRITE "${TINYPY_WORK_DIR}/pkg/__init__.py" "PKG = 1\n")
file(WRITE "${TINYPY_WORK_DIR}/package.py" "import sys\nimport pkg\nprint repr(sys.path[0]), repr(__file__), repr(__package__)\nprint repr(pkg.__path__), repr(pkg.__file__), repr(pkg.__package__)\n")
tinypy_expect_script(package package.py CODES 0
    STDOUT_EQUAL "'${work_directory}' 'package.py' None\n['${work_directory}/pkg'] '${work_directory}/pkg/__init__.py' None\n")

# A SyntaxWarning of a file shows its line.
file(WRITE "${TINYPY_WORK_DIR}/warning.py" "def f():\n    x = 1\n    global x\nprint 'warned'\n")
tinypy_expect_script(warning warning.py CODES 0 STDOUT_EQUAL "warned\n"
    STDERR_EQUAL "warning.py:3: SyntaxWarning: name 'x' is assigned to before global declaration\n  global x\n")

# Tabs against spaces are checked only with -t and -tt.
file(WRITE "${TINYPY_WORK_DIR}/tabs.py" "if 1:\n${tab}x = 1\n        y = 2\n${tab}print 'mixed ok'\n")
tinypy_expect_script(tabs tabs.py CODES 0 STDOUT_EQUAL "mixed ok\n" STDERR_EQUAL "")
tinypy_expect_script(tabs_warning tabs.py CODES 0 ARGS -t STDOUT_EQUAL "mixed ok\n" STDERR_EQUAL "tabs.py: inconsistent use of tabs and spaces in indentation\n")
tinypy_expect_script(tabs_error tabs.py CODES 1 ARGS -tt STDOUT_EQUAL ""
    STDERR_EQUAL "  File \"tabs.py\", line 3\n    y = 2\n        ^\nTabError: inconsistent use of tabs and spaces in indentation\n")

# Finalization releases __main__ first, in its dictionary order, while the
# other modules are intact.
file(WRITE "${TINYPY_WORK_DIR}/helper_mod.py" "NAME_AT_IMPORT = __name__\n")
file(WRITE "${TINYPY_WORK_DIR}/finalize.py" "import helper_mod\nclass C(object):\n    def __del__(self):\n        print 'del sees module', helper_mod.NAME_AT_IMPORT\nc = C()\n")
tinypy_expect_script(finalize finalize.py CODES 0 STDOUT_EQUAL "del sees module helper_mod\n" STDERR_EQUAL "")
