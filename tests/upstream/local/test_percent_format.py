"""Project-authored formatting vectors and conversion-protocol checks."""

import unittest


class PercentFormat(unittest.TestCase):
    def test_mapping_lookup_order(self):
        class Lookup(object):
            def __init__(self):
                self.keys = []

            def __getitem__(self, key):
                self.keys.append(key)
                return {"": "blank", "size": 29}[key]

        mapping = Lookup()
        self.assertEqual("<%()s>/%(size)04d/%()s" % mapping, "<blank>/0029/blank")
        self.assertEqual(mapping.keys, ["", "size", ""])

    def test_string_and_repr_conversion(self):
        class Label(object):
            def __str__(self):
                return "display-label"

            def __repr__(self):
                return "debug-label"

        value = Label()
        self.assertEqual("%s|%r" % (value, value), "display-label|debug-label")

    def test_float_conversion_protocol(self):
        class Measurement(object):
            def __float__(self):
                return 12.75

        self.assertEqual("%07.2f" % Measurement(), "0012.75")

    def test_conversion_exception_propagates(self):
        class BrokenLabel(object):
            def __str__(self):
                raise LookupError("unavailable label")

        self.assertRaises(LookupError, lambda: "%s" % BrokenLabel())


def vector_check(pattern, operand, expected):
    def check(self):
        result = pattern % operand
        self.assertEqual(result, expected)
        self.assertIs(type(result), type(expected))
    return check


def rejected_check(pattern, operand, exception):
    def check(self):
        with self.assertRaises(exception):
            pattern % operand
    return check


def character_check(code, unicode_pattern):
    def check(self):
        class CharacterCode(object):
            def __int__(self):
                return code

        pattern = u"[%c]" if unicode_pattern else "[%c]"
        expected = u"[" + unichr(code) + u"]" if unicode_pattern else "[" + chr(code) + "]"
        self.assertEqual(pattern % CharacterCode(), expected)
    return check


def integer_fallback_check(pattern, expected):
    def check(self):
        calls = []

        class IntegerSource(object):
            def __int__(self):
                calls.append("int")
                raise LookupError("use the wide conversion")

            def __long__(self):
                calls.append("long")
                return -12345678901L

        self.assertEqual(pattern % IntegerSource(), expected)
        self.assertEqual(calls, ["int", "long"])
    return check


VECTORS = (
    ("%d", 117, "117"), ("%i", -117, "-117"), ("%u", 117L, "117"),
    ("%+d", 117, "+117"), ("% d", 117, " 117"),
    ("%07d", -117, "-000117"), ("%-7d", 117, "117    "),
    ("%.6d", 117, "000117"), ("%+08d", 117, "+0000117"),
    ("%o", 117, "165"), ("%#o", 117, "0165"),
    ("%x", 117, "75"), ("%X", 117, "75"), ("%#x", 117, "0x75"),
    ("%#X", 117, "0X75"), ("%#08x", 117, "0x000075"),
    ("%d", 3.75, "3"), ("%d", -3.75, "-3"),
    ("%d", True, "1"), ("%d", False, "0"),
    ("%.2f", 6.25, "6.25"), ("%8.2f", 6.25, "    6.25"),
    ("%08.2f", 6.25, "00006.25"), ("%+.1f", 6.25, "+6.2"),
    ("%.2e", 6.25, "6.25e+00"), ("%.2E", 6.25, "6.25E+00"),
    ("%.3g", 62500.0, "6.25e+04"), ("%.3G", 62500.0, "6.25E+04"),
    ("%#g", 6.25, "6.25000"),
    ("%s", "parcel", "parcel"), ("%.3s", "parcel", "par"),
    ("%8.3s", "parcel", "     par"), ("%-8.3s", "parcel", "par     "),
    ("%r", "parcel", "'parcel'"), ("%s", ((2, 7),), "(2, 7)"),
    ("%c", 93, "]"), ("%c", "@", "@"),
    ("%%:%s:%%", "parcel", "%:parcel:%"),
    ("%*.*f", (9, 2, 6.25), "     6.25"),
    ("%*s", (-9, "parcel"), "parcel   "),
    ("%(name)s:%(count)03d", {"name": "parcel", "count": 4}, "parcel:004"),
    ("<%()s>", {"": "empty-name"}, "<empty-name>"),
    ("%()04d", {"": 29}, "0029"),
    (u"<%()s>", {"": u"\u03bb"}, u"<\u03bb>"),
    (u"%s", "parcel", u"parcel"), ("%s", u"\u03bb", u"\u03bb"),
    (u"%.2s", u"\u03bb\u03bc\u03bd", u"\u03bb\u03bc"),
    (u"%c", 0x3a9, u"\u03a9"), (u"%c", u"\u03bb", u"\u03bb"),
)

for index, (pattern, operand, expected) in enumerate(VECTORS):
    setattr(PercentFormat, "test_vector_%02d" % index,
            vector_check(pattern, operand, expected))

for label, pattern, operand, exception in (
    ("unfinished", "%", 9, ValueError), ("specifier", "%q", 9, ValueError),
    ("few_arguments", "%s/%s", (9,), TypeError),
    ("many_arguments", "%s", (9, 8), TypeError),
    ("mapping_key", "%(unknown)s", {}, KeyError),
    ("mapping_required", "%(known)s", (9,), TypeError),
    ("width_type", "%*s", (2.5, "parcel"), TypeError),
    ("character_length", "%c", "parcel", TypeError),
    ("character_negative", "%c", -7, OverflowError),
    ("character_large", "%c", 256, OverflowError),
    ("integer_required", "%d", "117", TypeError),
    ("float_required", "%f", "6.25", TypeError),
):
    setattr(PercentFormat, "test_reject_" + label, rejected_check(pattern, operand, exception))

for code in (0, 93, 255):
    for unicode_pattern in (False, True):
        label = "test_character_protocol_%d_%s" % (code, unicode_pattern)
        setattr(PercentFormat, label, character_check(code, unicode_pattern))

for label, pattern, expected in (
    ("decimal", "%d", "-12345678901"), ("integer", "%i", "-12345678901"),
    ("unsigned", "%u", "-12345678901"), ("hex", "%x", "-2dfdc1c35"),
    ("uppercase", "%X", "-2DFDC1C35"), ("octal", "%o", "-133767016065"),
):
    setattr(PercentFormat, "test_wide_fallback_" + label,
            integer_fallback_check(pattern, expected))
