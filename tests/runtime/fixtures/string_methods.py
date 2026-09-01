# Portable Python 2.7 behavior checks adapted from CPython 2.7.18
# Lib/test/string_tests.py. Copyright (c) 2001-2020 Python Software
# Foundation; All Rights Reserved. Distributed under PSF License Version 2;
# see dependencies/python/LICENSE in the Mengine source tree.

assert "{} {}".format("a", 2) == "a 2"
assert "{name!r}".format(name="x") == "'x'"
assert "{0:04x}".format(15) == "000f"
assert "{{{}}}".format(1) == "{1}"
assert "x".center(5, "-") == "--x--"
assert "x".ljust(3, "-") == "x--"
assert "x".rjust(3, "-") == "--x"
assert ",".join(["a", "b"]) == "a,b"
assert "banana".find("na") == 2
assert "banana".rfind("na") == 4
assert "banana".index("na", 3) == 4
assert "banana".rindex("na", 0, 5) == 2
assert "banana".count("na") == 2
assert "abc".translate(None, "b") == "ac"
assert "prefix-value".startswith(("other", "prefix"))
assert "prefix-value".endswith("value", 1)
assert "  value\t".strip() == "value"
assert "xyvalueyx".strip("xy") == "value"
assert "  value  ".lstrip() == "value  "
assert "  value  ".rstrip() == "  value"
assert "xyvalueyx".lstrip("xy") == "valueyx"
assert "xyvalueyx".rstrip("xy") == "xyvalue"

# CPython 2.7.18 Lib/test/string_tests.py:
# CommonTest.test_strip_whitespace and CommonTest.test_strip.
assert "   hello   ".strip() == "hello"
assert "   hello   ".lstrip() == "hello   "
assert "   hello   ".rstrip() == "   hello"
assert "hello".strip() == "hello"

strip_whitespace = " \t\n\r\f\vabc \t\n\r\f\v"
assert strip_whitespace.strip() == "abc"
assert strip_whitespace.lstrip() == "abc \t\n\r\f\v"
assert strip_whitespace.rstrip() == " \t\n\r\f\vabc"

assert "   hello   ".strip(None) == "hello"
assert "   hello   ".lstrip(None) == "hello   "
assert "   hello   ".rstrip(None) == "   hello"
assert "hello".strip(None) == "hello"

assert "xyzzyhelloxyzzy".strip("xyz") == "hello"
assert "xyzzyhelloxyzzy".lstrip("xyz") == "helloxyzzy"
assert "xyzzyhelloxyzzy".rstrip("xyz") == "xyzzyhello"
assert "hello".strip("xyz") == "hello"
assert "mississippi".strip("mississippi") == ""
assert "mississippi".strip("i") == "mississipp"

identity_text = "unchanged value"
assert identity_text.strip() is identity_text
assert identity_text.lstrip("xyz") is identity_text
assert identity_text.rstrip("xyz") is identity_text
assert identity_text.ljust(2) is identity_text
assert identity_text.rjust(2) is identity_text
assert identity_text.center(2) is identity_text
assert identity_text.replace("missing", "value") is identity_text

assert "abcabc".replace("ab", "X", 1) == "Xcabc"
assert "ab".replace("", "-", 2) == "-a-b"
assert "".replace("", "x") == "x"
assert "".replace("", "x", 1) == ""
assert "".replace("", "x", 0) == ""
assert u"\u20ac".replace(u"", u"-") == u"-\u20ac-"
assert "a,b,,c".split(",") == ["a", "b", "", "c"]
assert "a,b,c".split(",", 1) == ["a", "b,c"]
assert "a,b,c".rsplit(",", 1) == ["a,b", "c"]
assert "  a  b \t c  ".split() == ["a", "b", "c"]
assert "  a  b \t c  ".rsplit(None, 1) == ["  a  b", "c"]
assert "AbC".lower() == "abc"
assert "AbC".upper() == "ABC"
assert "AbC".swapcase() == "aBc"
assert "hELLO".capitalize() == "Hello"
assert "hello WORLD".title() == "Hello World"
assert u"\u01c5".swapcase() == u"\u01c5"
assert u"\u01c5abc".capitalize() == u"\u01c4abc"
assert "abc".isalpha() and not "ab1".isalpha()
assert "123".isdigit() and not "".isdigit()
assert u"1".isdecimal() and u"1".isnumeric()
assert not u"\u00b2".isdecimal() and u"\u00b2".isdigit() and u"\u00b2".isnumeric()
assert not u"\u2155".isdigit() and u"\u2155".isnumeric()
assert not u"\u56db".isdigit() and u"\u56db".isnumeric()
assert "abc123".isalnum() and not "abc-123".isalnum()
assert " \t\r\n".isspace() and not " x ".isspace()
assert "abc1".islower() and not "Abc".islower()
assert "ABC1".isupper() and not "ABc".isupper()
assert "Hello World".istitle() and not "Hello world".istitle()
assert "-42".zfill(5) == "-0042"
assert u"a\u20acb".find(u"\u20ac") == 1
assert u"\N{EURO SIGN}" == u"\u20ac"
assert "%s:%r:%d" % ("x", "y", 3) == "x:'y':3"
assert "%#x:%#X:%o" % (31, 31, 9) == "0x1f:0X1F:11"
assert "%08d" % -42 == "-0000042"
assert "%-5s" % "x" == "x    "
assert "%.3s" % "abcdef" == "abc"
assert "%.3f" % 1.25 == "1.250"
assert "%*.*f" % (8, 2, 1.5) == "    1.50"
assert "%(name)s:%(value)04d" % {"name": "x", "value": 7} == "x:0007"
assert "%c:%c" % (65, "z") == "A:z"
assert "%e" % 12.5 == "1.250000e+01"
assert "%.3g" % 12.5 == "12.5"
assert "%x" % 0x123456789abcdef123456789L == "123456789abcdef123456789"
assert len("%.97d" % int("1")) == 97
assert len("%.116d" % int("1")) == 116
assert len("%.1000d" % (1L << 100)) == 1000
try:
    "%.2147483648d" % 1
except ValueError:
    pass
else:
    raise AssertionError("literal percent precision exceeded INT_MAX")
try:
    "%.*d" % (2147483648, 1)
except OverflowError:
    pass
else:
    raise AssertionError("dynamic percent precision exceeded INT_MAX")
assert "%#.0d" % 0 == ""
assert "%05.0d" % 42 == "00042"
assert "%05.0d" % 0 == "00000"
assert "%#.0x" % 0 == "0x"
assert "%#.6o" % True == "000001"
assert "%01s" % "" == " "
assert "%#.0e" % 0.0 == "0.e+00"
assert "%#.0g" % 0.0 == "0."
assert "%d" % 1e20 == "100000000000000000000"
assert "%d" % -0.5 == "0"
assert "%.0d" % 0.5 == ""
assert "%+.0d" % -0.5 == "+"
assert "%#.0o" % -0.5 == "0"
assert u"%s" % u"\u20ac" == u"\u20ac"
assert u"%10s" % u"\u20acx" == u"        \u20acx"

# Empty substrings still honor an unclamped start beyond the end.
assert "abc".find("", 3) == 3
assert "abc".find("", 4) == -1
assert "abc".rfind("", 4) == -1
assert "abc".count("", 4) == 0
assert not "abc".startswith("", 4)
assert not "abc".endswith("", 4)
assert "abc".startswith(u"", 100)
assert "abc".endswith(u"", 100)
assert u"abc".startswith("", 100)
assert u"abc".endswith("", 100)

# Python 2 advanced-format numeric flags and Unicode character widths.
assert "{:5}".format(3) == "    3"
assert "{:+}".format(3) == "+3"
assert "{:#010b}".format(10) == "0b00001010"
assert "{:#o}".format(42) == "0o52"
assert "{:>8}".format(1e20) == "   1e+20"
assert "{:#08x}".format(31) == "0x00001f"
assert "{:*^7}".format("x") == "***x***"
assert format("abc", "^020.2") == "000000000ab000000000"
assert format(u"caf\xe9", "<05s") == u"caf\xe90"
assert "{:>8.2f}".format(1.25) == "    1.25"
assert "{:c}".format(42) == "*"
assert "{:#c}".format(42) == "*"
assert format(65, u"c") == u"A"
try:
    format(0x80, u"c")
except UnicodeDecodeError:
    pass
else:
    raise AssertionError("Unicode character format skipped the default ASCII decode")
assert "{:.1}".format(1.25) == "1e+00"
assert "{:x<05}".format(42) == "42xxx"
assert "{:x>05}".format(-42) == "xx-42"
assert "{:x<-05.1f}".format(1 + 2j) == "1.0+2.0j"
assert "{:n}".format(42) == "42"
assert "{:%}".format(12345.6789) == "1234567.890000%"
assert "{:,.2f}".format(12345.6789) == "12,345.68"
assert "{:010,d}".format(1234) == "00,001,234"
assert "{:+010,d}".format(1234) == "+0,001,234"
assert "{:015,.2f}".format(12345.67) == "0,000,012,345.67"
assert "{:%}".format(float("inf")) == "inf%"
assert "{:f}".format(1e100) == "10000000000000000159028911097599180468360808563945281389781327557747838772170381060813469985856815104.000000"
assert "{0:{1}}".format(42, "05") == "00042"
assert "{0:.{1}f}".format(1.25, 3) == "1.250"
assert "{:{}}".format(42, "05") == "00042"
try:
    "{} {0}".format(1)
except ValueError:
    pass
else:
    raise AssertionError("automatic and manual format fields were mixed")
try:
    "{0} {}".format(1, 2)
except ValueError:
    pass
else:
    raise AssertionError("manual and automatic format fields were mixed")
assert type("{}".format(u"ascii")) is str
try:
    "{}".format(u"\u20ac")
except UnicodeEncodeError:
    pass
else:
    raise AssertionError("byte format did not ASCII-encode a Unicode field")
assert type(u"{}".format(u"\u20ac")) is unicode
try:
    u"{}".format("\xff")
except UnicodeDecodeError:
    pass
else:
    raise AssertionError("Unicode format accepted a non-ASCII byte field")
assert u"{:.2}".format(u"\u20acx") == u"\u20acx"
assert u"{:5}".format(u"\u20ac") == u"\u20ac    "
assert repr(u"\xe9") == "u'\\xe9'"
assert repr(u"\u20ac") == "u'\\u20ac'"
assert repr(u"\U0001f600") == "u'\\U0001f600'"

# Python 2 uses shortest round-tripping repr and a 12-digit str display.
assert repr(1.23456789012345) == "1.23456789012345"
assert str(1.23456789012345) == "1.23456789012"
assert repr(0.1 + 0.2) == "0.30000000000000004"
assert repr(1e20) == "1e+20"
assert repr(1e-5) == "1e-05"
assert repr(-0.0) == "-0.0"
assert str(12345678901234567890L) == "12345678901234567890"
assert repr(complex(1, 2)) == "(1+2j)"
assert str(complex(1, 2)) == "(1+2j)"
assert repr(complex(0, 2)) == "2j"
negative_nan = -float("nan")
assert repr(complex(1, negative_nan)) == "(1+nanj)"
assert repr(complex(negative_nan, negative_nan)) == "(nan+nanj)"
assert "{:g}".format(complex(1, negative_nan)) == "1+nanj"
assert "{:+g}".format(negative_nan) == "+nan"
assert "{:+}".format(1.23456789012345) == "+1.23456789012"
assert "{:.2}".format(1.0) == "1.0"
assert "{0:}".format(False) == "False"
assert "{0:05}".format(False) == "00000"
assert "{0:+}".format(True) == "+1"
assert "{0:s}".format(None) == "None"
assert "{0:s}".format([]) == "[]"
for integer_format_type in ("i", "u"):
    try:
        ("{0:" + integer_format_type + "}").format(1)
    except ValueError:
        pass
    else:
        raise AssertionError("unsupported integer format type was accepted")
try:
    "{0:c}".format(-42)
except OverflowError:
    pass
else:
    raise AssertionError("negative character format did not overflow")
try:
    "{:0>20.2e}".format(1 + 2j)
except ValueError:
    pass
else:
    raise AssertionError("complex format accepted zero fill")
try:
    "{:,n}".format(42)
except ValueError:
    pass
else:
    raise AssertionError("'n' format accepted digit grouping")

assert repr("'") == '"\'"'
assert repr('"') == "'\"'"
assert repr("'\"") == "'\\\'\"'"

unicode_index_text = u"a\u20ac\U0001f600z"
assert unicode_index_text[0] == u"a"
assert unicode_index_text[-1] == u"z"
assert unicode_index_text[1:3] == u"\u20ac\U0001f600"
assert unicode_index_text[::-1] == u"z\U0001f600\u20aca"
assert unicode_index_text[::2] == u"a\U0001f600"
assert unicode_index_text[:] is unicode_index_text


class FormatField(object):
    def __init__(self):
        self._mapping = {"value": 42}


format_field = FormatField()
assert "{0.__class__.__name__}".format(format_field) == "FormatField"
assert "{0._mapping[value]}".format(format_field) == "42"


class CustomFormat(object):
    def __format__(self, spec):
        return "custom:" + spec


class CustomUnicodeFormat(object):
    def __unicode__(self):
        return u"unicode"

    def __str__(self):
        return "string"


assert "{:tag}".format(CustomFormat()) == "custom:tag"
assert format(15, "04x") == "000f"
assert format(CustomFormat(), "tag") == "custom:tag"
assert type(format("abc", u"")) is unicode
assert type(format(15, u"d")) is unicode
assert type(format(None, u"")) is unicode
assert type(format(CustomFormat(), u"tag")) is unicode
assert u"{}".format(CustomUnicodeFormat()) == u"unicode"
assert u"{!s}".format(CustomUnicodeFormat()) == u"unicode"
assert format(u"\u20ac", u">3") == u"  \u20ac"
unicode_sparse_text = u"\u20ac" * 257
assert unicode_sparse_text[64] == u"\u20ac"
assert unicode_sparse_text[128:193] == u"\u20ac" * 65
unicode_edge_text = u"\u20ac" * 10000 + u"needle"
assert unicode_edge_text[0] == u"\u20ac"
assert unicode_edge_text[-1] == u"e"
assert unicode_edge_text.find(u"needle") == 10000
assert "a\r\nb\nc".splitlines() == ["a", "b", "c"]
assert "a\r\nb\n".splitlines(True) == ["a\r\n", "b\n"]
assert "a\vb\fc\x1cd\x1de\x1ef\x85g".splitlines() == ["a\vb\fc\x1cd\x1de\x1ef\x85g"]
assert u"a\vb\fc\x1cd\x1de\x1ef\x85g".splitlines() == [u"a", u"b", u"c", u"d", u"e", u"f", u"g"]
assert "a\tb".expandtabs(4) == "a   b"
assert "\x80\t".expandtabs(4) == "\x80   "
assert u"\x80\t".expandtabs(4) == u"\x80   "
assert "a:b:c".partition(":") == ("a", ":", "b:c")
assert "a:b:c".rpartition(":") == ("a:b", ":", "c")
assert "abc".partition("-") == ("abc", "", "")
assert type("a,b".split(u",")[0]) is unicode
assert type("abc".strip(u"a")) is unicode
assert type("a".replace(u"a", "b")) is unicode
assert type("a:b".partition(u":")[0]) is unicode
long_search_text = "a" * 200 + "needle" + "a" * 200 + "needle"
assert long_search_text.find("needle") == 200
assert long_search_text.rfind("needle") == 406
assert "\xc3\xa9".decode("utf-8") == u"\xe9"
assert u"\xe9".encode("utf-8") == "\xc3\xa9"
assert "\xff".decode("ascii", "ignore") == u""
assert u"\xe9".encode("ascii", "replace") == "?"
assert "\xff".decode("latin-1") == u"\xff"

# Keep representative literal forms in the byte-identical marshal-v2 corpus.
literal_plain = "abc"
literal_escaped = "\x61bc"
literal_punctuated = "a-b"
literal_nested = ("identifier",)
literal_empty = ""
literal_raw_identifier = r"raw_name"
literal_raw_punctuated = r"raw-name"
literal_implicit_concat = "joined" "_name"
literal_folded_concat = "folded" + "_name"
literal_triple_quoted = """triple_name"""
literal_unicode_identifier = u"unicode_name"
assert literal_plain == literal_escaped
assert literal_nested == ("identifier",)
assert literal_implicit_concat == "joined_name"
assert literal_folded_concat == "folded_name"
