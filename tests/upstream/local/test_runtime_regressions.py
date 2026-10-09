"""Regression checks for callback dispatch, recursion and ownership cleanup."""

import sys
import unittest


class Regressions(unittest.TestCase):
    def test_recursive_classic_callable_with_cleanup(self):
        class Relay:
            pass

        receiver = Relay()
        Relay.__call__ = receiver
        try:
            self.assertRaises(RuntimeError, receiver)
        finally:
            del Relay.__call__
        self.assertEqual(6 * 7, 42)

    def test_recursive_modern_callable(self):
        class Relay(object):
            def __call__(self):
                return self()

        self.assertRaises(RuntimeError, Relay())
        self.assertEqual(6 * 7, 42)

    def test_recursive_function(self):
        def repeat(callback):
            return callback(callback)

        self.assertRaises(RuntimeError, repeat, repeat)
        self.assertEqual(repeat(lambda unused: 29), 29)

    def test_recursive_property(self):
        class Relay(object):
            @property
            def payload(self):
                return self.payload

        self.assertRaises(RuntimeError, getattr, Relay(), "payload")
        self.assertEqual(6 * 7, 42)

    def test_recursive_numeric_protocol(self):
        class Relay(object):
            def __add__(self, other):
                return self + other

        self.assertRaises(RuntimeError, lambda: Relay() + 29)
        self.assertEqual(6 * 7, 42)

    def test_recursion_limit_error_is_constructible(self):
        previous = sys.getrecursionlimit()
        try:
            sys.setrecursionlimit(40)
            nested = (str,)
            for unused in range(50):
                nested = (nested,)
            self.assertRaises(RuntimeError, isinstance, "parcel", nested)
            self.assertRaises(RuntimeError, issubclass, str, nested)
        finally:
            sys.setrecursionlimit(previous)
        self.assertTrue(isinstance("parcel", str))

    def test_augmented_assignment_rebinding(self):
        def copy_add(receiver, amount):
            return receiver.__class__(receiver.count + amount)

        class Parcel:
            def __init__(self, count):
                self.count = count

            __add__ = copy_add

        class Mutating(Parcel):
            def __iadd__(self, amount):
                self.count += amount
                return self

        class Replacing(Parcel):
            __iadd__ = copy_add

        for constructor, same in ((Parcel, False), (Mutating, True), (Replacing, False)):
            value = constructor(17)
            original = value
            value += 9
            self.assertIs(value is original, same)
            self.assertEqual(value.count, 26)
            self.assertIs(value.__class__, constructor)

    def test_classic_comparison_coercion_order(self):
        class NumericParcel:
            def __init__(self):
                self.calls = []

            def __coerce__(self, other):
                self.calls.append("coerce")
                return self, other

            def __cmp__(self, other):
                self.calls.append("compare")
                return 0

        value = NumericParcel()
        self.assertTrue(19 == value)
        self.assertEqual(value.calls, ["coerce", "compare"])

    def test_classic_coercion_replaces_operand_types(self):
        class NumericParcel:
            def __coerce__(self, other):
                return 29, other

        self.assertEqual(NumericParcel(), 29)
        self.assertEqual(29, NumericParcel())
        self.assertTrue(NumericParcel() > 19)
        self.assertTrue(19 < NumericParcel())

    def test_classic_bad_coercion_result(self):
        class NumericParcel:
            def __coerce__(self, other):
                return [29, other]

        self.assertRaises(TypeError, lambda: NumericParcel() == 29)

    def test_missing_classic_rich_descriptor(self):
        class Parcel:
            @property
            def __eq__(self):
                raise AttributeError("unavailable equality hook")

        self.assertFalse(Parcel() == Parcel())

    def test_classic_rich_callback_exception_propagates(self):
        class Parcel:
            def __eq__(self, other):
                raise AttributeError("callback failed")

        self.assertRaises(AttributeError, lambda: Parcel() == Parcel())

    def test_classic_initializer_descriptor_failure(self):
        class Parcel:
            @property
            def __init__(self):
                raise AttributeError("initializer unavailable")

        self.assertRaises(AttributeError, Parcel)

    def test_class_object_is_not_method_receiver(self):
        class Classic:
            def deliver(self):
                return 29

        class Modern(object):
            def deliver(self):
                return 29

        self.assertRaises(TypeError, Classic.deliver, Classic)
        self.assertRaises(TypeError, Modern.deliver, Modern)

    def test_numeric_slots_reject_foreign_receivers(self):
        self.assertRaises(TypeError, int.__add__, 9.0, 7)
        self.assertRaises(TypeError, long.__add__, 9, 7L)
        self.assertEqual(int.__add__(True, 7), 8)

    def test_new_function_becomes_static_method(self):
        class Packet(tuple):
            def __new__(cls, payload):
                return tuple.__new__(cls, (payload,))

        self.assertIsInstance(Packet.__dict__["__new__"], staticmethod)
        self.assertEqual(Packet.__new__(Packet, 23), (23,))
        self.assertEqual(Packet(13).__new__(Packet, 29), (29,))

    def test_format_exception_cleanup(self):
        class Measurement(object):
            def __float__(self):
                raise LookupError("measurement unavailable")

        self.assertRaises(TypeError, lambda: "%f" % Measurement())
        self.assertEqual("%f" % 6.25, "6.250000")

    def test_format_integer_conversion_failure(self):
        class Measurement(object):
            def __int__(self):
                raise LookupError("measurement unavailable")

        self.assertRaises(TypeError, lambda: "%d" % Measurement())

    def test_format_rejects_invalid_long_fallback(self):
        class Measurement(object):
            def __int__(self):
                raise LookupError("integer unavailable")

            def __long__(self):
                return 93.75

        self.assertRaises(TypeError, lambda: "%d" % Measurement())

    def test_format_unicode_float_character(self):
        self.assertEqual(u"[%c]" % 93.75, u"[]]")
        self.assertRaises(TypeError, lambda: "[%c]" % 93.75)

    def test_format_character_exception_rules(self):
        class Measurement(object):
            def __int__(self):
                raise LookupError("measurement unavailable")

        self.assertRaises(LookupError, lambda: "%c" % Measurement())
        self.assertRaises(TypeError, lambda: u"%c" % Measurement())

    def test_format_does_not_substitute_integer_for_float(self):
        class Measurement(object):
            def __int__(self):
                return 93

        self.assertRaises(TypeError, lambda: "%f" % Measurement())

    def test_format_requires_int_before_long_fallback(self):
        class Measurement(object):
            def __long__(self):
                return 93L

        self.assertRaises(TypeError, lambda: "%d" % Measurement())

    def test_classic_integer_argument_truncates(self):
        class Parcel:
            pass

        class Truncating:
            def __trunc__(self):
                return 1.5

        for convert in (int, range, xrange, chr, [1, 2, 3].pop):
            self.assertRaises(AttributeError, convert, Parcel())
        self.assertEqual([4, 5, 6].pop(Truncating()), 5)
        self.assertEqual(range(Truncating()), [0])
        self.assertEqual(chr(Truncating()), "\x01")

    def test_classic_repeat_count_from_getattr(self):
        class Parcel:
            def __getattr__(self, name):
                if name == "__index__":
                    return lambda: 2
                raise AttributeError(name)

        self.assertEqual("abc" * Parcel(), "abcabc")
        self.assertEqual(Parcel() * [7], [7, 7])

    def test_builtin_class_method_descriptor(self):
        class Mapping(dict):
            pass

        descriptor = dict.__dict__["fromkeys"]
        self.assertEqual(type(descriptor).__name__, "classmethod_descriptor")
        self.assertEqual(repr(descriptor), "<method 'fromkeys' of 'dict' objects>")
        self.assertEqual((descriptor.__name__, descriptor.__objclass__), ("fromkeys", dict))
        self.assertEqual(descriptor(dict, "a"), {"a": None})
        self.assertIs(type(descriptor(Mapping, "a")), Mapping)
        self.assertIs(type(descriptor.__get__(None, Mapping)("a")), Mapping)
        self.assertEqual(descriptor.__get__({})("b", 1), {"b": 1})
        self.assertRaises(TypeError, descriptor)
        self.assertRaises(TypeError, descriptor, 1)
        self.assertRaises(TypeError, descriptor, int, "a")
        self.assertRaises(TypeError, descriptor.__get__, None, int)
        self.assertRaises(TypeError, type(descriptor))
        self.assertIs(type(float.__dict__["fromhex"]), type(descriptor))
        self.assertIs(object.__subclasshook__(), NotImplemented)

    def test_property_members_are_readonly(self):
        def getter(owner):
            "getter documentation"
            return 1

        class Subproperty(property):
            pass

        value = property(getter)
        self.assertEqual(value.__doc__, "getter documentation")
        self.assertIs(value.fget, getter)
        for name in ("fget", "fset", "fdel", "__doc__"):
            self.assertRaises(TypeError, setattr, value, name, None)
            self.assertRaises(TypeError, delattr, value, name)
        derived = Subproperty(getter)
        derived.__doc__ = "replaced"
        self.assertEqual(derived.__doc__, "replaced")

    def test_instance_method_doc(self):
        class Modern(object):
            def method(self):
                "method documentation"

        class Classic:
            def method(self):
                "classic documentation"

        self.assertEqual(Modern.method.__doc__, "method documentation")
        self.assertEqual(Modern().method.__doc__, "method documentation")
        self.assertEqual(Classic().method.__doc__, "classic documentation")
        self.assertRaises(AttributeError, setattr, Modern.method, "__doc__", "x")
        self.assertRaises(AttributeError, delattr, Modern().method, "__doc__")

    def test_unready_iterator_attribute_assignment(self):
        iterator = iter(bytearray("ab"))
        self.assertRaises(TypeError, setattr, iterator, "label", 1)
        self.assertRaises(TypeError, delattr, iterator, "label")
        self.assertEqual(iterator.__length_hint__(), 2)
        self.assertRaises(AttributeError, setattr, iterator, "label", 1)

    def test_classic_getattr_supplies_legacy_protocols(self):
        class Legacy:
            def __getattr__(self, name):
                if name == "__getslice__":
                    return lambda low, high: ("slice", low, high)
                if name == "__format__":
                    return lambda spec: "format:" + spec
                if name == "__reversed__":
                    return lambda: iter("ab")
                raise AttributeError(name)

        self.assertEqual(Legacy()[1:2], ("slice", 1, 2))
        self.assertEqual("{0:x}".format(Legacy()), "format:x")
        self.assertEqual(list(reversed(Legacy())), ["a", "b"])

    def test_cmp_wrapper_and_str_fallback_messages(self):
        class Long(long):
            pass

        class Repr(object):
            def __repr__(self):
                return 1

        try:
            Long(1).__cmp__("a")
        except TypeError as error:
            self.assertEqual(str(error), "Long.__cmp__(x,y) requires y to be a 'Long', not a 'str'")
        else:
            self.fail("__cmp__ accepted a str")
        try:
            str(Repr())
        except TypeError as error:
            self.assertEqual(str(error), "__str__ returned non-string (type int)")
        else:
            self.fail("str accepted a non-string __repr__ result")
        try:
            repr(Repr())
        except TypeError as error:
            self.assertEqual(str(error), "__repr__ returned non-string (type int)")
        else:
            self.fail("repr accepted a non-string __repr__ result")
