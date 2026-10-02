# -*- coding: utf-8 -*-
# Float formatting, str subclass results, join, translate, percent formatting,
# UTF-8 decoding diagnostics, in-place concatenation and the regular
# expression engine follow CPython 2.7.
import _sre


def compile_pattern(pattern, code, groups=0):
    return _sre.compile(pattern, 0, code, groups, {}, [None] * (groups + 1))


def error_text(callable_object, *args):
    try:
        callable_object(*args)
    except Exception as error:
        return type(error).__name__ + ": " + str(error)
    raise AssertionError("no exception raised")


assert (str(1e11), str(1e12), str(123456789012.0), str(0.0001), str(0.00001), repr(1e16), repr(1e15), str(1e10), str(99999999999.9)) == ("1e+11", "1e+12", "1.23456789012e+11", "0.0001", "1e-05", "1e+16", "1000000000000000.0", "10000000000.0", "99999999999.9")
assert (format(123456.0, ".6"), "{:.3}".format(100.0), format(12.0, ".3"), format(0.0, ".1"), format(1.0, ".1"), format(0.5, ".1"), format(123456.7, ".6"), format(1234.5, ".6"), format(-100.0, ".3"), format(1e11, ""), "{}".format(1e11), "{:+.3}".format(100.0), format(1e-5, ".3"), format(0.0001234, ".3")) == ("1.23456e+05", "1e+02", "12.0", "0e+00", "1e+00", "0.5", "1.23457e+05", "1234.5", "-1e+02", "1e+11", "1e+11", "+1e+02", "1e-05", "0.000123")


class StrSubclass(str):
    pass


class UnicodeSubclass(unicode):
    pass


assert [type(value).__name__ for value in (StrSubclass("abc").strip(), StrSubclass("abc").upper(), StrSubclass("abc").replace("x", "y"), StrSubclass("abc").center(1), StrSubclass("abc")[:], StrSubclass("abc").strip("x"), StrSubclass("abc").lstrip(), UnicodeSubclass(u"abc").strip(), StrSubclass("abc").zfill(1), StrSubclass("abc").ljust(2), StrSubclass("abc").ljust(10))] == ["str", "str", "str", "str", "str", "str", "str", "unicode", "str", "str", "str"]
assert "\xe9".join([u"a"]) == u"a"
assert u"\xe9".join(["a"]) == u"a"
assert type("-".join([StrSubclass("x")])).__name__ == "str"
assert ("-".join(["a", "b"]), u"-".join(["a", u"b"])) == ("a-b", u"a-b")


class IndexErrorTable(object):
    def __getitem__(self, key):
        raise IndexError(key)


class KeyErrorTable(object):
    def __getitem__(self, key):
        raise KeyError(key)


class ValueErrorTable(object):
    def __getitem__(self, key):
        raise ValueError(key)


assert u"ab".translate(IndexErrorTable()) == u"ab"
assert u"ab".translate(KeyErrorTable()) == u"ab"
assert error_text(u"ab".translate, ValueErrorTable()) == "ValueError: 97"
assert ("%.0d" % 0L, "%.0d" % 0, "%.0x" % 0L, "%d" % 0L, "%.2d" % 0L) == ("0", "", "0", "0", "00")
assert error_text("{0[0]x}".format, [1]) == "ValueError: Only '.' or '[' may follow ']' in format field specifier"

assert [value.decode("utf-8", "replace") for value in ("\xc3a", "\xe9\xe9a", "\xc3", "a\xf0\x90\x80b", "\x80abc", "ok")] == [u"�a", u"��a", u"�", u"a�b", u"�abc", u"ok"]
decode_errors = []
for sample in ("a\xe9b", "a\xc3", "a\xf0\x90b", "a\x80b", "\xc0\x80", "a\xe9\x80b"):
    try:
        sample.decode("utf-8")
    except UnicodeDecodeError as error:
        decode_errors.append(str(error))
assert decode_errors == [
    "'utf8' codec can't decode byte 0xe9 in position 1: unexpected end of data",
    "'utf8' codec can't decode byte 0xc3 in position 1: unexpected end of data",
    "'utf8' codec can't decode bytes in position 1-2: unexpected end of data",
    "'utf8' codec can't decode byte 0x80 in position 1: invalid start byte",
    "'utf8' codec can't decode byte 0xc0 in position 0: invalid start byte",
    "'utf8' codec can't decode bytes in position 1-2: invalid continuation byte",
]
assert ("abcabcab".count("ab"), "aaaa".count("aa"), "abc".count(""), u"\xe9a\xe9a".count(u"\xe9")) == (3, 2, 4, 2)


def concatenate():
    text = ""
    for index in xrange(20000):
        text += "xy"
    return len(text), text[:6], text[-4:]


def concatenate_shared():
    text = "ab"
    alias = text
    text += "cd"
    return text, alias


def concatenate_hashed():
    text = "ab"
    mapping = {text: 1}
    text += "c"
    return text in mapping, "ab" in mapping, hash(text) == hash("abc")


global_text = "p"
for index in range(3):
    global_text += "q"
assert concatenate() == (40000, "xyxyxy", "xyxy")
assert concatenate_shared() == ("abcd", "ab")
assert global_text == "pqqq"
assert concatenate_hashed() == (False, True, True)

literal_a = compile_pattern(u"a", [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1])
literal_abc = compile_pattern("abc", [17, 12, 3, 3, 3, 3, 3, 97, 98, 99, 0, 0, 0, 19, 97, 19, 98, 19, 99, 1])
digits = compile_pattern("[0-9]+", [17, 4, 0, 1, 0, 29, 10, 1, 4294967295L, 15, 5, 27, 48, 57, 0, 1, 1])
repeated_b = compile_pattern("ab+", [17, 8, 1, 2, 0, 1, 1, 97, 0, 19, 97, 29, 6, 1, 4294967295L, 19, 98, 1, 1])
branch_star = compile_pattern("(ab|a)*bc", [17, 4, 0, 2, 0, 28, 19, 0, 4294967295L, 21, 0, 19, 97, 7, 5, 19, 98, 18, 5, 3, 18, 2, 0, 21, 1, 22, 19, 98, 19, 99, 1], 1)
nested_star = compile_pattern("(a*)*", [28, 14, 0, 4294967295L, 21, 0, 29, 6, 0, 4294967295L, 19, 97, 1, 21, 1, 22, 1], 1)
counted_optional = compile_pattern("(a?){2}b", [17, 4, 0, 1, 3, 28, 14, 2, 2, 21, 0, 29, 6, 0, 1, 19, 97, 1, 21, 1, 22, 19, 98, 1], 1)
accented = compile_pattern(u"\xe9", [17, 8, 3, 1, 1, 1, 1, 233, 0, 19, 233, 1])
comma = compile_pattern(",", [17, 8, 3, 1, 1, 1, 1, 44, 0, 19, 44, 1])
many_optional = compile_pattern("a?" * 3000, [29, 6, 0, 1, 19, 97, 1] * 3000 + [1])

# Unicode subjects are decoded once per pattern, literal prefixes and leading
# character sets skip impossible positions, and repeats backtrack into their
# bodies without recursion.
assert len(literal_a.findall(u"\xe9a" * 20000)) == 20000
assert literal_abc.search("x" * 1000 + "abc").start() == 1000
assert digits.search("abc123").group() == "123"
assert repeated_b.findall("xabxabbbxab") == ["ab", "abbb", "ab"]
assert literal_abc.search("ab") is None
assert digits.search("abc") is None
assert accented.sub(u"e", u"caf\xe9 \xe9t\xe9") == u"cafe ete"
assert comma.split("a,b,,c") == ["a", "b", "", "c"]
assert [match.start() for match in literal_a.finditer("banana")] == [1, 3, 5]
assert branch_star.match("abc") is not None
assert nested_star.match("aa").group(1) == ""
assert counted_optional.match("b") is not None
assert many_optional.match("a" * 3000) is not None
