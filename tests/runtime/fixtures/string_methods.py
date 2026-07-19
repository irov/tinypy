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
assert "prefix-value".startswith(("other", "prefix"))
assert "prefix-value".endswith("value", 1)
assert "  value\t".strip() == "value"
assert "xyvalueyx".strip("xy") == "value"
assert "abcabc".replace("ab", "X", 1) == "Xcabc"
assert "ab".replace("", "-", 2) == "-a-b"
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
assert "abc".isalpha() and not "ab1".isalpha()
assert "123".isdigit() and not "".isdigit()
assert "abc123".isalnum() and not "abc-123".isalnum()
assert " \t\r\n".isspace() and not " x ".isspace()
assert "abc1".islower() and not "Abc".islower()
assert "ABC1".isupper() and not "ABc".isupper()
assert "Hello World".istitle() and not "Hello world".istitle()
assert "-42".zfill(5) == "-0042"
assert u"a\u20acb".find(u"\u20ac") == 1
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
assert u"%s" % u"\u20ac" == u"\u20ac"


class FormatField(object):
    def __init__(self):
        self._mapping = {"value": 42}


format_field = FormatField()
assert "{0.__class__.__name__}".format(format_field) == "FormatField"
assert "{0._mapping[value]}".format(format_field) == "42"
assert "a\r\nb\nc".splitlines() == ["a", "b", "c"]
assert "a\r\nb\n".splitlines(True) == ["a\r\n", "b\n"]
assert "a\tb".expandtabs(4) == "a   b"
assert "a:b:c".partition(":") == ("a", ":", "b:c")
assert "a:b:c".rpartition(":") == ("a:b", ":", "c")
assert "abc".partition("-") == ("abc", "", "")
assert "\xc3\xa9".decode("utf-8") == u"\xe9"
assert u"\xe9".encode("utf-8") == "\xc3\xa9"
assert "\xff".decode("ascii", "ignore") == u""
assert u"\xe9".encode("ascii", "replace") == "?"
assert "\xff".decode("latin-1") == u"\xff"
