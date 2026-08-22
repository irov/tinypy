import sys

print "alpha", 42
print "tail",
print "continued"
print >>sys.stderr, "error", 7


class Sink(object):
    def __init__(self):
        self.text = ""

    def write(self, text):
        self.text += text


sink = Sink()
print >>sink, "local", 9
assert sink.text == "local 9\n"


class TypedSink(object):
    def __init__(self):
        self.parts = []

    def write(self, text):
        self.parts.append((type(text), text))


typed_sink = TypedSink()
print >>typed_sink, u"\u20ac", "tail"
assert typed_sink.parts == [
    (unicode, u"\u20ac"),
    (str, " "),
    (str, "tail"),
    (str, "\n"),
]

assert repr([1, "x"]) == "[1, 'x']"
assert str([1, "x"]) == "[1, 'x']"
assert repr({"answer": 42}) == "{'answer': 42}"
assert repr(12345678901234567890L) == "12345678901234567890L"
assert repr(1.5) == "1.5"


class CustomRepresentation(object):
    def __repr__(self):
        return "custom-representation"


assert repr(CustomRepresentation()) == "custom-representation"
assert `CustomRepresentation()` == "custom-representation"
