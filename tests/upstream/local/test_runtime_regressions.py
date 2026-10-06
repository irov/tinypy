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
