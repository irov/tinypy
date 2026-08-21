import sys

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
