from __future__ import print_function

import sys


class Sink(object):
    def __init__(self):
        self.text = ""

    def write(self, text):
        self.text += text


sink = Sink()
print("alpha", 42, sep="/", end="!", file=sink)
print(file=sink)
print("tail", sep=None, end=None, file=sink)
assert sink.text == "alpha/42!\ntail\n"

original_stdout = sys.stdout
sys.stdout = sink
try:
    print("default", file=None)
finally:
    sys.stdout = original_stdout
assert sink.text.endswith("default\n")


class TypedSink(object):
    def __init__(self):
        self.parts = []

    def write(self, text):
        self.parts.append((type(text), text))


typed_sink = TypedSink()
print(u"\u20ac", "tail", file=typed_sink)
assert typed_sink.parts == [
    (unicode, u"\u20ac"),
    (unicode, u" "),
    (str, "tail"),
    (unicode, u"\n"),
]

typed_sink = TypedSink()
print("left", "right", sep=u"\u2022", file=typed_sink)
assert typed_sink.parts == [
    (str, "left"),
    (unicode, u"\u2022"),
    (str, "right"),
    (unicode, u"\n"),
]

typed_sink = TypedSink()
print(u"\ud800", end=u"", file=typed_sink)
assert typed_sink.parts == [(unicode, u"\ud800"), (unicode, u"")]

for keyword in ("sep", "end"):
    try:
        if keyword == "sep":
            print("bad", sep=1, file=sink)
        else:
            print("bad", end=1, file=sink)
    except TypeError:
        pass
    else:
        raise AssertionError("print accepted a non-string " + keyword)

try:
    print("bad", flush=True, file=sink)
except TypeError:
    pass
else:
    raise AssertionError("Python 2 print accepted flush")
