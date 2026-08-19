assert bool() is False
assert bool([1]) is True
assert int() == 0
assert int(True) == 1
assert int("255") == 255
assert int("ff", 16) == 255
assert int("0b101", 0) == 5
assert long("12345678901234567890") == 12345678901234567890L
assert float() == 0.0
assert float("1.25") == 1.25
assert repr(float("12345678901234567890")) == "1.2345678901234567e+19"
assert repr(float("394888.8664994673092485312057888752")) == "394888.8664994673"
assert repr(float("-701761475.458")) == "-701761475.458"
assert complex() == 0j
assert complex(2, 3) == 2 + 3j
assert complex("1.5") == 1.5 + 0j
assert repr(complex("12345678901234567890")) == "(1.2345678901234567e+19+0j)"
assert complex("1+2j") == 1 + 2j
assert complex("-j") == -1j
assert repr(complex(-2.0, -0.0)) == "(-2-0j)"
assert repr(complex(-0.0, -0.0)) == "(-0-0j)"
assert repr(complex(complex("-2-0j"), 0.0)) == "(-2+0j)"
try:
    complex("1 +2j")
except ValueError:
    pass
else:
    raise AssertionError("complex accepted internal whitespace")
assert int(u"\u0661") == 1
assert long(u"\u0661") == 1L
assert float(u"\u0661") == 1.0
assert complex(u"\u0661") == 1 + 0j
assert int(1e20) == 100000000000000000000L
assert long(-1e20) == -100000000000000000000L
assert str() == ""
assert str(42) == "42"
assert unicode() == u""
assert unicode("text") == u"text"
assert unicode("caf\xc3\xa9", "utf-8") == u"caf\xe9"
assert unicode("a\xff", "ascii", "ignore") == u"a"
for default_ascii_input in ("caf\xc3\xa9", "\xff"):
    try:
        unicode(default_ascii_input)
    except UnicodeDecodeError:
        pass
    else:
        raise AssertionError("unicode accepted non-ASCII bytes without an encoding")
try:
    str(u"\xe9")
except UnicodeEncodeError:
    pass
else:
    raise AssertionError("str accepted non-ASCII Unicode with the default encoding")


class UnicodeConversion(object):
    def __unicode__(self):
        return u"unicode conversion"

    def __str__(self):
        return "string conversion"


assert unicode(UnicodeConversion()) == u"unicode conversion"


class UnicodeStringFallback(object):
    def __str__(self):
        return u"\xe9"


assert unicode(UnicodeStringFallback()) == u"\xe9"


class InvalidUnicodeConversion(object):
    def __unicode__(self):
        return 42


try:
    unicode(InvalidUnicodeConversion())
except TypeError:
    pass
else:
    raise AssertionError("unicode accepted a non-string __unicode__ result")
assert list() == []
assert list((1, 2, 3)) == [1, 2, 3]
assert list("ab") == ["a", "b"]
assert tuple() == ()
assert tuple([1, 2]) == (1, 2)
assert dict() == {}
assert dict([("a", 1), ("b", 2)]) == {"a": 1, "b": 2}
assert dict(["ab"]) == {"a": "b"}
assert dict(answer=42) == {"answer": 42}


def dictionary_pair():
    yield "generated"
    yield 7


assert dict([dictionary_pair()]) == {"generated": 7}


class MappingInput(object):
    def keys(self):
        return ["left", "right"]

    def __getitem__(self, key):
        return {"left": 1, "right": 2}[key]

    def __iter__(self):
        raise AssertionError("mapping input was treated as a pair iterable")


assert dict(MappingInput()) == {"left": 1, "right": 2}


class ListSubclass(list):
    pass


class DictSubclass(dict):
    pass


class SetSubclass(set):
    pass


class BytearraySubclass(bytearray):
    pass


class IntegerSubclass(int):
    pass


class LongSubclass(long):
    pass


class FloatSubclass(float):
    pass


class ComplexSubclass(complex):
    pass


class StringSubclass(str):
    pass


class UnicodeSubclass(unicode):
    pass


class FormattedIntegerSubclass(int):
    def __format__(self, spec):
        return "custom:" + spec


class CustomListInitializer(list):
    def __init__(self, value):
        self.append(value)


class SlottedDictSubclass(dict):
    __slots__ = ("marker",)


list_subclass = ListSubclass((1, 2))
dict_subclass = DictSubclass(answer=42)
set_subclass = SetSubclass((1, 2))
bytearray_subclass = BytearraySubclass((65, 66))
slotted_dict_subclass = SlottedDictSubclass.fromkeys((1, 2), 3)
slotted_dict_subclass.marker = 4
assert type(list_subclass) is ListSubclass and list_subclass == [1, 2]
assert type(dict_subclass) is DictSubclass and dict_subclass == {"answer": 42}
assert type(set_subclass) is SetSubclass and set_subclass == set((1, 2))
assert type(bytearray_subclass) is BytearraySubclass and str(bytearray_subclass) == "AB"
for immutable_value, immutable_type, expected in (
    (IntegerSubclass(3), IntegerSubclass, 3),
    (LongSubclass(3), LongSubclass, 3L),
    (FloatSubclass(3), FloatSubclass, 3.0),
    (ComplexSubclass(3), ComplexSubclass, 3 + 0j),
    (StringSubclass("abc"), StringSubclass, "abc"),
    (UnicodeSubclass(u"abc"), UnicodeSubclass, u"abc"),
):
    assert type(immutable_value) is immutable_type
    assert immutable_value == expected
    immutable_value.marker = 7
    assert immutable_value.marker == 7
assert type(FloatSubclass.fromhex("0x1.8p+1")) is FloatSubclass
assert format(IntegerSubclass(15), "04x") == "000f"
assert format(FormattedIntegerSubclass(15), "04x") == "custom:04x"
assert CustomListInitializer(7) == [7]
assert type(slotted_dict_subclass) is SlottedDictSubclass
assert slotted_dict_subclass == {1: 3, 2: 3}
assert slotted_dict_subclass.marker == 4


class PlainMixin(object):
    pass


class MixedDict(PlainMixin, dict):
    pass


assert MixedDict(answer=42) == {"answer": 42}


class SlottedMixin(object):
    __slots__ = ("value",)


try:
    class ConflictingDict(dict, SlottedMixin):
        pass
except TypeError:
    pass
else:
    raise AssertionError("incompatible container layout was accepted")


class NumericConversions(object):
    def __int__(self):
        return 41

    def __long__(self):
        return 42L

    def __float__(self):
        return 4.25

    def __complex__(self):
        return 4 + 2j


numeric_conversions = NumericConversions()
assert int(numeric_conversions) == 41
assert long(numeric_conversions) == 42L
assert float(numeric_conversions) == 4.25
assert complex(numeric_conversions) == 4 + 2j
assert float((1L << 54) + 1L) == float(1L << 54)
assert float((1L << 54) + 3L) == float((1L << 54) + 4L)
assert bin(10) == "0b1010"
assert oct(10) == "012"
assert oct(10L) == "012L"
assert hex(255) == "0xff"
assert hex(255L) == "0xffL"
assert reduce(lambda left, right: left + right, [1, 2, 3]) == 6

dynamic_intern_left = "".join(("inter", "ned"))
dynamic_intern_right = "".join(("intern", "ed"))
assert dynamic_intern_left is not dynamic_intern_right
assert intern(dynamic_intern_left) is intern(dynamic_intern_right)


class VarsTarget(object):
    pass


vars_target = VarsTarget()
vars_target.answer = 42
assert vars(vars_target) == {"answer": 42}
assert vars() is locals()

slice_value = slice(-3, 20, 2)
assert slice_value.start == -3
assert slice_value.stop == 20
assert slice_value.step == 2
assert slice_value.indices(10) == (7, 10, 2)
assert slice(-3, 20, 2) == slice_value
assert slice(-3, 19, 2) < slice_value
assert slice.__cmp__(slice(-3, 19, 2), slice_value) == -1
assert slice.__repr__(slice_value) == "slice(-3, 20, 2)"
try:
    hash(slice_value)
except TypeError:
    pass
else:
    raise AssertionError("slice unexpectedly hashable")
try:
    slice.__hash__(slice_value)
except TypeError:
    pass
else:
    raise AssertionError("slice.__hash__ unexpectedly succeeded")


class SliceLength(object):
    def __index__(self):
        return 10


assert slice_value.indices(SliceLength()) == (7, 10, 2)
assert (1).bit_length() == 1
assert (-1).bit_length() == 1
assert (1L << 100).bit_length() == 101
assert (1.5).as_integer_ratio() == (3, 2)
assert not (1.5).is_integer() and (2.0).is_integer()
assert (1 + 2j).conjugate() == 1 - 2j
assert (3).real == 3 and (3).imag == 0
assert (3).numerator == 3 and (3).denominator == 1
assert (1.5).hex() == "0x1.8000000000000p+0"
assert float.fromhex("  -0x1.8p+1  ") == -3.0

for overflowing_float_conversion in (
    lambda: float(10L ** 10000),
    lambda: complex(10L ** 10000),
    lambda: (10L ** 10000) + 0.0,
    lambda: round(10L ** 10000),
    lambda: "%f" % (10L ** 10000),
):
    try:
        overflowing_float_conversion()
    except OverflowError:
        pass
    else:
        raise AssertionError("huge long converted to infinity instead of raising OverflowError")


class RealComplexConversion(object):
    def __complex__(self):
        return 4.5


assert complex(RealComplexConversion()) == 4.5 + 0j


class FloatOnlyConversion(object):
    def __float__(self):
        return 2.5


assert complex(1, FloatOnlyConversion()) == 1 + 2.5j


class IndexValue(object):
    def __index__(self):
        return 2


index_value = IndexValue()
assert [0, 1, 2, 3][index_value] == 2
assert [0, 1, 2, 3][:index_value] == [0, 1]
assert "a" * index_value == "aa"
assert "banana".find("na", index_value) == 2
assert "a" * -(1L << 100) == ""


class HugeNegativeIndex(object):
    def __index__(self):
        return -(1L << 100)


assert [] * HugeNegativeIndex() == []
try:
    [] * (1L << 100)
except OverflowError:
    pass
else:
    raise AssertionError("huge positive repeat did not overflow")

value = object()
assert type(value) is object
Dynamic = type("Dynamic", (object,), {"answer": 42})
assert Dynamic.answer == 42


class FalseByNonzero(object):
    def __nonzero__(self):
        return False


class FalseByLength(object):
    def __len__(self):
        return 0


assert bool(FalseByNonzero()) is False
assert bool(FalseByLength()) is False


class LongSubtype(long):
    pass


class UnicodeSubtype(unicode):
    pass


class StringSubtype(str):
    pass


assert type(long(LongSubtype(3))) is long
assert type(unicode(UnicodeSubtype(u"value"))) is unicode
assert type(StringSubtype("value")[:]) is str
assert type(UnicodeSubtype(u"value")[:]) is unicode
assert (3).__trunc__() == 3
assert (3L).__trunc__() == 3L
assert (1.75).__trunc__() == 1
assert (3).__hex__() == "0x3"
assert (3L).__hex__() == "0x3L"
assert (3).__oct__() == "03"
assert (3L).__oct__() == "03L"
assert float.__getformat__("double").startswith("IEEE, ")
assert float.__getformat__("float").startswith("IEEE, ")
assert float.__setformat__("double", "unknown") is None
try:
    float.__getformat__("invalid")
except ValueError:
    pass
else:
    raise AssertionError("float.__getformat__ accepted an invalid kind")


class ObjectProtocolTarget(object):
    def __getattribute__(self, name):
        if name == "masked":
            return "override"
        return object.__getattribute__(self, name)

    def __getattr__(self, name):
        return "fallback"

    def __setattr__(self, name, value):
        object.__setattr__(self, "set_" + name, value)

    def __delattr__(self, name):
        object.__setattr__(self, "deleted", name)


object_protocol_target = ObjectProtocolTarget()
object.__setattr__(object_protocol_target, "masked", "base")
assert object_protocol_target.masked == "override"
assert object.__getattribute__(object_protocol_target, "masked") == "base"
try:
    object.__getattribute__(object_protocol_target, "missing")
except AttributeError:
    pass
else:
    raise AssertionError("object.__getattribute__ called __getattr__")
object.__setattr__(object_protocol_target, "plain", 42)
assert object.__getattribute__(object_protocol_target, "plain") == 42
object.__delattr__(object_protocol_target, "plain")
object_protocol_target.normal = 7
assert object.__getattribute__(object_protocol_target, "set_normal") == 7
del object_protocol_target.normal
assert object.__getattribute__(object_protocol_target, "deleted") == "normal"
assert object.__hash__(object_protocol_target) == hash(object_protocol_target)
assert object.__format__(object_protocol_target, "s") == str(object_protocol_target)
assert object.__subclasshook__(int) is NotImplemented
