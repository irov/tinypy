import sys

assert sys.version_info == (2, 7, 18, "final", 0)
assert sys.api_version == 1013
assert sys.maxsize == sys.maxint
assert sys.getdefaultencoding() == "ascii"
assert "_struct" in sys.builtin_module_names
assert sys.warnoptions == []
assert sys.meta_path == []
assert sys.path_hooks == []
assert sys.path_importer_cache == {}

for exit_code in (None, 7, "exit"):
    try:
        if exit_code is None:
            sys.exit()
        else:
            sys.exit(exit_code)
    except SystemExit as exit_error:
        assert exit_error.code is exit_code
    else:
        raise AssertionError("sys.exit did not raise SystemExit")

import __future__ as future

assert future.all_feature_names == [
    "nested_scopes", "generators", "division", "absolute_import",
    "with_statement", "print_function", "unicode_literals",
]
assert future.division.getOptionalRelease() == (2, 2, 0, "alpha", 2)
assert future.division.getMandatoryRelease() == (3, 0, 0, "alpha", 0)
assert future.division.compiler_flag == future.CO_FUTURE_DIVISION == 0x2000
assert future.CO_FUTURE_UNICODE_LITERALS == 0x20000
assert repr(future.division) == "_Feature((2, 2, 0, 'alpha', 2), (3, 0, 0, 'alpha', 0), 8192)"

import __builtin__
if sys.platform == "win32":
    assert hasattr(__builtin__, "WindowsError")
else:
    assert not hasattr(__builtin__, "WindowsError")

import _sre

assert _sre.getcodesize() == _sre.CODESIZE == 4
assert _sre.copyright == " SRE 2.2.2 Copyright (c) 1997-2002 by Secret Labs AB "

assert callable(sys.displayhook)
assert sys.displayhook is sys.__displayhook__


class DisplaySink(object):
    def __init__(self):
        self.text = ""

    def write(self, text):
        self.text += text


display_sink = DisplaySink()
original_stdout = sys.stdout
sys.stdout = display_sink
try:
    assert sys.displayhook(17) is None
    assert sys.displayhook(None) is None
finally:
    sys.stdout = original_stdout
assert display_sink.text == "17\n"
assert _ == 17

displayed = []
original_displayhook = sys.displayhook
sys.displayhook = lambda value: displayed.append(value)
assert eval(compile("6 * 7", "<single>", "single")) is None
assert displayed == [42]
sys.displayhook = original_displayhook

warning_sink = DisplaySink()
original_stderr = sys.stderr
sys.stderr = warning_sink
try:
    compile("assert (1, 2)", "warn.py", "exec")
    compile("def f():\n x=1\n global x", "warn.py", "exec")
    compile("def f():\n print x\n global x", "warn.py", "exec")
    compile("def f():\n from m import *", "warn.py", "exec")
finally:
    sys.stderr = original_stderr
assert warning_sink.text == (
    "warn.py:1: SyntaxWarning: assertion is always true, perhaps remove parentheses?\n"
    "warn.py:3: SyntaxWarning: name 'x' is assigned to before global declaration\n"
    "warn.py:3: SyntaxWarning: name 'x' is used prior to global declaration\n"
    "warn.py:1: SyntaxWarning: import * only allowed at module level\n"
)

try:
    compile("return 1", "sample.py", "exec")
except SyntaxError as syntax_error:
    assert syntax_error.args == ("'return' outside function", ("sample.py", 1, None, None))
    assert syntax_error.offset is None
    assert syntax_error.text is None
else:
    raise AssertionError("return outside function compiled")

dynamic_unicode = eval("u'\xd0\xb0'")
assert type(dynamic_unicode) is unicode
assert len(dynamic_unicode) == 2
assert [ord(character) for character in dynamic_unicode] == [0xd0, 0xb0]

utf8_unicode = eval("# coding: utf-8\nu'\xd0\xb0'")
assert utf8_unicode == u"\u0430"

current_frame = sys._getframe()
for frame_attribute in (
    "f_back", "f_code", "f_builtins", "f_globals", "f_locals", "f_lasti",
    "f_lineno", "f_trace", "f_exc_type", "f_exc_value", "f_exc_traceback",
    "f_restricted",
):
    assert frame_attribute in dir(current_frame)
assert current_frame.f_code.co_name == "<module>"
assert current_frame.f_restricted is False
current_frame.f_trace = "trace marker"
assert current_frame.f_trace == "trace marker"

try:
    raise ValueError("traceback")
except ValueError:
    current_traceback = sys.exc_info()[2]
    assert current_frame.f_exc_type is ValueError
    assert isinstance(current_frame.f_exc_value, ValueError)
    assert current_frame.f_exc_traceback is current_traceback
    for traceback_attribute in ("tb_next", "tb_frame", "tb_lasti", "tb_lineno"):
        assert traceback_attribute in dir(current_traceback)
    assert current_traceback.tb_frame is current_frame
    assert current_traceback.tb_next is None

original_limit = sys.getrecursionlimit()
assert original_limit == 1000
assert sys.maxunicode == 0x10ffff

try:
    sys.setrecursionlimit(0)
    assert False, "zero recursion limit"
except ValueError:
    pass

try:
    sys.setrecursionlimit("20")
    assert False, "non-integer recursion limit"
except TypeError:
    pass

sys.setrecursionlimit(20)
assert sys.getrecursionlimit() == 20


def recursive_call():
    recursive_call()


try:
    recursive_call()
    assert False, "recursion limit was not enforced"
except RuntimeError:
    pass

sys.setrecursionlimit(original_limit)
assert sys.getrecursionlimit() == original_limit


# The version and float tables are reachable by name as well as by index, and
# the exception being handled is mirrored for Python 2 compatibility.
assert sys.version_info.major == 2
assert sys.version_info.minor == 7
assert sys.version_info.micro == 18
assert sys.version_info.releaselevel == "final"
assert sys.version_info.serial == 0
assert sys.version_info[0] == 2
assert len(sys.version_info) == 5

assert sys.float_info.mant_dig == 53
assert sys.float_info.dig == 15
assert sys.float_info.radix == 2
assert sys.float_info.max > 1e308
assert 0.0 < sys.float_info.min < 1e-307
assert 0.0 < sys.float_info.epsilon < 1e-15
assert sys.float_info.max_exp == 1024
assert sys.float_info.min_exp == -1021

assert sys.getsizeof(1) > 0
assert sys.getsizeof("abc") > 0
assert sys.getsizeof(object(), 99) > 0

sys.exc_clear()
assert sys.exc_type is None
assert sys.exc_value is None
assert sys.exc_traceback is None
try:
    raise ValueError("mirrored")
except ValueError:
    assert sys.exc_type is ValueError
    assert isinstance(sys.exc_value, ValueError)
    assert sys.exc_traceback is not None
assert sys.exc_type is ValueError
sys.exc_clear()
assert sys.exc_type is None
assert sys.exc_value is None
assert sys.exc_traceback is None
assert sys.exc_info() == (None, None, None)
