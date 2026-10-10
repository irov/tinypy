if(NOT DEFINED TINYPY_EXECUTABLE)
    message(FATAL_ERROR "TINYPY_EXECUTABLE is required")
endif()

# Runs the CLI with ARGS and checks its exit status against CODES, the
# presence of every STDOUT and STDERR fragment, the absence of every ABSENT
# fragment from both streams, and the exact STDOUT_EQUAL and STDERR_EQUAL
# streams when given.
function(tinypy_expect name)
    cmake_parse_arguments(CASE "" "STDOUT_EQUAL;STDERR_EQUAL" "CODES;ARGS;STDOUT;STDERR;ABSENT" ${ARGN})
    execute_process(COMMAND "${TINYPY_EXECUTABLE}" ${CASE_ARGS} RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT result IN_LIST CASE_CODES)
        message(FATAL_ERROR "${name}: exit status ${result} is not one of ${CASE_CODES}:\n${stdout}${stderr}")
    endif()
    foreach(fragment IN LISTS CASE_STDOUT)
        string(FIND "${stdout}" "${fragment}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${name}: stdout is missing '${fragment}':\n${stdout}")
        endif()
    endforeach()
    foreach(fragment IN LISTS CASE_STDERR)
        string(FIND "${stderr}" "${fragment}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${name}: stderr is missing '${fragment}':\n${stderr}")
        endif()
    endforeach()
    foreach(fragment IN LISTS CASE_ABSENT)
        string(FIND "${stdout}${stderr}" "${fragment}" position)
        if(NOT position EQUAL -1)
            message(FATAL_ERROR "${name}: output contains '${fragment}':\n${stdout}${stderr}")
        endif()
    endforeach()
    if(DEFINED CASE_STDOUT_EQUAL AND NOT stdout STREQUAL CASE_STDOUT_EQUAL)
        message(FATAL_ERROR "${name}: stdout differs from '${CASE_STDOUT_EQUAL}':\n${stdout}")
    endif()
    if(DEFINED CASE_STDERR_EQUAL AND NOT stderr STREQUAL CASE_STDERR_EQUAL)
        message(FATAL_ERROR "${name}: stderr differs from '${CASE_STDERR_EQUAL}':\n${stderr}")
    endif()
endfunction()

set(runtime_source [=[def inner():
    return 1 / 0
def outer():
    return inner()
outer()]=])

tinypy_expect(runtime_error CODES 1 ARGS -c "${runtime_source}" STDERR
    "Traceback (most recent call last):"
    "  File \"<string>\", line 5, in <module>"
    "  File \"<string>\", line 4, in outer"
    "  File \"<string>\", line 2, in inner"
    "ZeroDivisionError: integer division or modulo by zero"
)

set(syntax_source [=[def broken(:
    pass]=])

tinypy_expect(syntax_error CODES 1 ARGS -c "${syntax_source}" STDERR
    "  File \"<string>\", line 1"
    "    def broken(:"
    "               ^"
    "SyntaxError: invalid syntax"
)

set(rendering_source [=[class Failure(Exception):
    def __str__(self):
        calls.append(1)
        return 'calls=%d' % len(calls)
calls = []
def fail():
    raise Failure()
try:
    fail()
finally:
    pass]=])

tinypy_expect(rendering_once CODES 1 ARGS -c "${rendering_source}" STDERR "Failure: calls=1")

# PyErr_Display: the traceback precedes the location of a SyntaxError, which
# comes from the attributes of the exception, and the message carries no
# "(file, line)" suffix.
tinypy_expect(compile_syntax_error CODES 1 ARGS -c "compile('a = = 1\\n', 'virtual.py', 'exec')"
    STDERR_EQUAL "Traceback (most recent call last):\n  File \"<string>\", line 1, in <module>\n  File \"virtual.py\", line 1\n    a = = 1\n        ^\nSyntaxError: invalid syntax\n"
    ABSENT "(virtual.py, line 1)")
tinypy_expect(semantic_syntax_error CODES 1 ARGS -c "compile('return 1\\n', 'virtual.py', 'exec')"
    STDERR_EQUAL "Traceback (most recent call last):\n  File \"<string>\", line 1, in <module>\n  File \"virtual.py\", line 1\nSyntaxError: 'return' outside function\n")
tinypy_expect(manual_syntax_error CODES 1 ARGS -c "raise SyntaxError('bad', ('f.py', 3, 6, 'x = 1\\n'))"
    STDERR_EQUAL "Traceback (most recent call last):\n  File \"<string>\", line 1, in <module>\n  File \"f.py\", line 3\n    x = 1\n        ^\nSyntaxError: bad\n")
tinypy_expect(syntax_error_without_location CODES 1 ARGS -c "raise SyntaxError('bad')"
    STDERR_EQUAL "Traceback (most recent call last):\n  File \"<string>\", line 1, in <module>\nSyntaxError: bad\n")
tinypy_expect(syntax_error_subclass CODES 1 ARGS -c "class MySyntax(SyntaxError):\n    pass\nraise MySyntax('sub', ('f.py', 1, 1, 'x\\n'))"
    STDERR_EQUAL "Traceback (most recent call last):\n  File \"<string>\", line 3, in <module>\n  File \"f.py\", line 1\n    x\n    ^\n__main__.MySyntax: sub\n")

# print_error_text: the caret line holds spaces only, an offset past the
# text selects its line, and leading blanks are dropped from both.
tinypy_expect(caret_with_tabs CODES 1 ARGS -c "compile('x\\t=\\t= 1', 'v', 'exec')"
    STDERR_EQUAL "Traceback (most recent call last):\n  File \"<string>\", line 1, in <module>\n  File \"v\", line 1\n    x\t=\t= 1\n        ^\nSyntaxError: invalid syntax\n")
tinypy_expect(caret_past_text CODES 1 ARGS -c "raise SyntaxError('bad', ('f.py', 3, 8, 'x = 1\\n'))"
    STDERR "  File \"f.py\", line 3\n    \n     ^\nSyntaxError: bad\n")
tinypy_expect(caret_indented CODES 1 ARGS -c "exec 'if 1:\\n  x = 1\\n y = 2'"
    STDERR "  File \"<string>\", line 3\n    y = 2\n        ^\nIndentationError: unindent does not match any outer indentation level\n")

# The message of an exception whose str() fails.
tinypy_expect(str_failed CODES 1 ARGS -c "class E(Exception):\n    def __str__(self):\n        return 42\nraise E()"
    STDERR "__main__.E: <exception str() failed>\n")

# handle_system_exit: ints and longs are the status, anything else is
# written to sys.stderr with status 1.
tinypy_expect(system_exit_negative CODES 255 -1 ARGS -c "import sys\nsys.exit(-1)" STDERR_EQUAL "" STDOUT_EQUAL "")
tinypy_expect(system_exit_long CODES 2 ARGS -c "import sys\nsys.exit(2L)" STDERR_EQUAL "")
tinypy_expect(system_exit_message CODES 1 ARGS -c "import sys\nsys.exit('msg')" STDERR_EQUAL "msg\n")
tinypy_expect(system_exit_code_property CODES 1 ARGS -c "class E(SystemExit):\n    @property\n    def code(self):\n        raise RuntimeError('no code')\nraise E(1)" STDERR_EQUAL "1\n")
tinypy_expect(system_exit_replaced_stderr CODES 1 ARGS -c "import sys\nclass W(object):\n    def write(self, s):\n        sys.stdout.write('captured:' + s)\nsys.stderr = W()\nsys.exit('msg')"
    STDOUT_EQUAL "captured:msgcaptured:\n" STDERR_EQUAL "")
tinypy_expect(system_exit_lost_stderr CODES 1 ARGS -c "import sys\nsys.stderr = None\nsys.exit('msg')" STDERR_EQUAL "msg\n")

# sys.exit raises SystemExit with its argument as the value, which an except
# clause normalizes and handle_system_exit reads as it is: a tuple is a
# message, the code attribute of an exception instance is the status.
tinypy_expect(system_exit_single_tuple CODES 1 ARGS -c "import sys\nsys.exit((3,))" STDERR_EQUAL "(3,)\n")
tinypy_expect(system_exit_empty_tuple CODES 1 ARGS -c "import sys\nsys.exit(())" STDERR_EQUAL "()\n")
tinypy_expect(system_exit_none_tuple CODES 1 ARGS -c "import sys\nsys.exit((None,))" STDERR_EQUAL "(None,)\n")
tinypy_expect(system_exit_instance CODES 5 ARGS -c "import sys\nsys.exit(SystemExit(5))" STDERR_EQUAL "")
tinypy_expect(system_exit_classic_code CODES 9 ARGS -c "import sys\nclass C:\n    code = 9\nsys.exit(C())" STDERR_EQUAL "")
tinypy_expect(system_exit_exception_code CODES 7 ARGS -c "import sys\ne = ValueError()\ne.code = 7\nsys.exit(e)" STDERR_EQUAL "")
tinypy_expect(system_exit_through_finally CODES 1 ARGS -c "import sys\ntry:\n    sys.exit((3,))\nfinally:\n    pass" STDERR_EQUAL "(3,)\n")
tinypy_expect(system_exit_reraised CODES 3 ARGS -c "import sys\ntry:\n    sys.exit((3,))\nexcept SystemExit:\n    raise" STDERR_EQUAL "")
tinypy_expect(system_exit_caught CODES 0 ARGS -c "import sys\ntry:\n    sys.exit((3,))\nexcept SystemExit as e:\n    print repr(e.code), repr(e.args)"
    STDOUT_EQUAL "3 (3,)\n" STDERR_EQUAL "")
tinypy_expect(system_exit_subclass_caught CODES 0 ARGS -c "import sys\nclass MyExit(SystemExit):\n    pass\ntry:\n    sys.exit(MyExit(5))\nexcept MyExit as e:\n    print type(e).__name__, sys.exc_info()[0].__name__, e.code"
    STDOUT_EQUAL "MyExit MyExit 5\n" STDERR_EQUAL "")

# PyErr_PrintEx: sys.excepthook reports uncaught exceptions, with its own
# failure and absence reported around the original exception.
tinypy_expect(excepthook_called CODES 1 ARGS -c "import sys\ndef hook(t, v, tb):\n    sys.stdout.write('hook %s %s\\n' % (t.__name__, v))\nsys.excepthook = hook\nraise ValueError('hooked')"
    STDOUT_EQUAL "hook ValueError hooked\n" STDERR_EQUAL "")
tinypy_expect(excepthook_raises CODES 1 ARGS -c "import sys\ndef hook(t, v, tb):\n    raise RuntimeError('hook failed')\nsys.excepthook = hook\nraise ValueError('hooked')"
    STDERR_EQUAL "Error in sys.excepthook:\nTraceback (most recent call last):\n  File \"<string>\", line 3, in hook\nRuntimeError: hook failed\n\nOriginal exception was:\nTraceback (most recent call last):\n  File \"<string>\", line 5, in <module>\nValueError: hooked\n")
tinypy_expect(excepthook_missing CODES 1 ARGS -c "import sys\ndel sys.excepthook\nraise ValueError('hook del')"
    STDERR_EQUAL "sys.excepthook is missing\nTraceback (most recent call last):\n  File \"<string>\", line 3, in <module>\nValueError: hook del\n")
tinypy_expect(excepthook_exits CODES 7 ARGS -c "import sys\ndef hook(t, v, tb):\n    sys.exit(7)\nsys.excepthook = hook\nraise ValueError('hooked')" STDERR_EQUAL "" STDOUT_EQUAL "")
tinypy_expect(default_excepthook CODES 0 ARGS -c "import sys\nprint sys.excepthook is sys.__excepthook__, type(sys.excepthook).__name__" STDOUT_EQUAL "True builtin_function_or_method\n")

# The default excepthook takes any objects: a type that is not an exception
# class is written with str(), anything but a traceback ends the report with
# its newline, which a failed write does not prevent.
tinypy_expect(excepthook_any_type CODES 0 ARGS -c "import sys\nsys.excepthook(1.5, 2, None)" STDERR_EQUAL "1.5: 2\n")
tinypy_expect(excepthook_plain_type CODES 0 ARGS -c "import sys\nsys.__excepthook__(int, 2, None)" STDERR_EQUAL "<type 'int'>: 2\n")
tinypy_expect(excepthook_not_traceback CODES 0 ARGS -c "import sys\nsys.excepthook(ValueError, ValueError('v'), 'not a traceback')" STDERR_EQUAL "\n")
tinypy_expect(excepthook_classic_dotted_name CODES 0 ARGS -c "import sys\nclass Old:\n    pass\nOld.__name__ = 'pkg.Dotted'\nsys.excepthook(Old, 'v', None)" STDERR_EQUAL "__main__.Dotted: v\n")
tinypy_expect(excepthook_failed_write CODES 0 ARGS -c "import sys\nclass W(object):\n    def write(self, s):\n        sys.stdout.write(repr(s) + '\\n')\n        raise IOError('no')\nsys.stderr = W()\nsys.excepthook(ValueError, ValueError('v'), None)"
    STDOUT_EQUAL "'ValueError'\n'\\n'\n" STDERR_EQUAL "")
tinypy_expect(excepthook_arity CODES 0 ARGS -c "import sys\ntry:\n    sys.excepthook(1, 2)\nexcept TypeError as e:\n    print e" STDOUT_EQUAL "excepthook expected 3 arguments, got 2\n")

# PyTraceBack_Print honours sys.tracebacklimit.
tinypy_expect(tracebacklimit CODES 1 ARGS -c "import sys\nsys.tracebacklimit = 1\ndef a():\n    b()\ndef b():\n    raise ValueError('limited')\na()"
    STDERR_EQUAL "Traceback (most recent call last):\n  File \"<string>\", line 6, in b\nValueError: limited\n")
tinypy_expect(tracebacklimit_zero CODES 1 ARGS -c "import sys\nsys.tracebacklimit = 0\nraise ValueError('limit 0')" STDERR_EQUAL "ValueError: limit 0\n")

# The report goes through the sys.stderr object, in the writes of PyErr_Display.
tinypy_expect(stderr_replaced CODES 1 ARGS -c "import sys\nclass W(object):\n    def write(self, s):\n        sys.stdout.write('captured:' + s)\nsys.stderr = W()\nraise ValueError('to captured stderr')"
    STDOUT_EQUAL "captured:Traceback (most recent call last):\ncaptured:  File \"<string>\", line 6, in <module>\ncaptured:ValueErrorcaptured:: captured:to captured stderrcaptured:\n" STDERR_EQUAL "")
tinypy_expect(stderr_lost CODES 1 ARGS -c "import sys\nsys.stderr = None\nraise ValueError('stderr none')" STDERR_EQUAL "lost sys.stderr\n")

# Py_Finalize: sys.exitfunc runs, then the module namespaces are released
# in the order of PyImport_Cleanup, so finalizers of their values run.
tinypy_expect(finalize_del CODES 0 ARGS -c "class C(object):\n    def __del__(self):\n        print 'del at exit'\nc = C()" STDOUT_EQUAL "del at exit\n")
tinypy_expect(finalize_del_raises CODES 0 ARGS -c "class C(object):\n    def __del__(self):\n        raise ValueError('in del')\nc = C()"
    STDERR "Exception ValueError: ValueError('in del',) in <bound method C.__del__ of <__main__.C object at " " ignored\n")
tinypy_expect(finalize_generator CODES 0 ARGS -c "def g():\n    try:\n        yield 1\n    finally:\n        print 'gen finally at exit'\nit = g()\nnext(it)" STDOUT_EQUAL "gen finally at exit\n")
tinypy_expect(finalize_globals CODES 0 ARGS -c "class C(object):\n    def __del__(self):\n        print 'global is', G\nG = 'alive'\nc = C()" STDOUT_EQUAL "global is None\n")
tinypy_expect(finalize_cycle CODES 0 ARGS -c "class A:\n    pass\na = A()\na.self = a\nexec 'def f(): pass' in A.__dict__\nprint 'cycles stay'" STDOUT_EQUAL "cycles stay\n" STDERR_EQUAL "")
tinypy_expect(exitfunc CODES 0 ARGS -c "import sys\ndef ef():\n    sys.stdout.write('exitfunc ran\\n')\nsys.exitfunc = ef\nprint 'body'" STDOUT_EQUAL "body\nexitfunc ran\n")
tinypy_expect(exitfunc_raises CODES 0 ARGS -c "import sys\ndef ef():\n    raise ValueError('exitfunc failed')\nsys.exitfunc = ef\nprint 'body'"
    STDOUT_EQUAL "body\n" STDERR_EQUAL "Error in sys.exitfunc:\nTraceback (most recent call last):\n  File \"<string>\", line 3, in ef\nValueError: exitfunc failed\n")
tinypy_expect(exitfunc_exits CODES 5 ARGS -c "import sys\ndef ef():\n    sys.exit(5)\nsys.exitfunc = ef" STDERR_EQUAL "")
tinypy_expect(exitfunc_exits_with_tuple CODES 1 ARGS -c "import sys\nsys.exitfunc = lambda: sys.exit((4,))" STDERR_EQUAL "(4,)\n")
tinypy_expect(exitfunc_lost_stderr CODES 0 ARGS -c "import sys\nsys.exitfunc = lambda: 1 / 0\nsys.stderr = None" STDERR_EQUAL "Error in sys.exitfunc:\nlost sys.stderr\n")

# A finalizer run by clearing a module namespace may remove the module from
# sys.modules.
tinypy_expect(finalize_module_removed CODES 0 ARGS -c "import sys\nm = type(sys)('ghost')\nclass K(object):\n    def __init__(self, modules):\n        self.modules = modules\n    def __del__(self):\n        del self.modules['ghost']\n        print 'ghost removed'\nm.k = K(sys.modules)\nsys.modules['ghost'] = m\ndel m"
    STDOUT_EQUAL "ghost removed\n" STDERR_EQUAL "")

# __main__ and sys.path of a command.
tinypy_expect(command_namespace CODES 0 ARGS -c "import sys\nprint repr(__package__), type(__builtins__).__name__, '__file__' in globals(), repr(sys.path[0]), repr(sys.argv)" x
    STDOUT_EQUAL "None module False '' ['-c', 'x']\n")

# Tabs against spaces are checked only with -t and -tt.
set(tabs_source "if 1:\n\tx = 1\n        y = 2\nprint 'mixed ok'")
tinypy_expect(tabs_unchecked CODES 0 ARGS -c "${tabs_source}" STDOUT_EQUAL "mixed ok\n" STDERR_EQUAL "")
tinypy_expect(tabs_warning CODES 0 ARGS -t -c "${tabs_source}" STDOUT_EQUAL "mixed ok\n" STDERR_EQUAL "<string>: inconsistent use of tabs and spaces in indentation\n")
tinypy_expect(tabs_error CODES 1 ARGS -tt -c "${tabs_source}" STDERR "TabError: inconsistent use of tabs and spaces in indentation\n")
