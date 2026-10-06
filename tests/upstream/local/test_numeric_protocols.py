"""Check numeric slot dispatch with an explicit operand-type matrix.

These are project-authored tests. Expectations are checked on CPython 2.7.18.
"""

import unittest


class NumericSlots(unittest.TestCase):
    def test_negative_quotient_and_remainder(self):
        for constructor in (int, long):
            for numerator, denominator, quotient, remainder in (
                (-17, 5, -4, 3), (17, -5, -4, -3), (-17, -5, 3, -2)
            ):
                left, right = constructor(numerator), constructor(denominator)
                self.assertEqual(divmod(left, right), (quotient, remainder))
                self.assertEqual(left, (left // right) * right + left % right)
                self.assertIs(type(left // right), constructor)
                self.assertIs(type(left % right), constructor)

    def test_operator_widening(self):
        for left_type in (int, long, float):
            for right_type in (int, long, float):
                left, right = left_type(11), right_type(3)
                expected_type = float if float in (left_type, right_type) else (
                    long if long in (left_type, right_type) else int)
                for result, expected in ((left + right, 14), (left - right, 8),
                                         (left * right, 33), (left ** right, 1331)):
                    self.assertEqual(result, expected)
                    self.assertIs(type(result), expected_type)

    def test_zero_divisors(self):
        for constructor in (int, long, float):
            for slot in ("__div__", "__floordiv__", "__mod__"):
                self.assertRaises(ZeroDivisionError, getattr(constructor, slot),
                                  constructor(11), constructor(0))


def slot_check(owner, operand_type, slot, expected):
    def check(self):
        result = getattr(owner, slot)(owner(11), operand_type(3))
        if expected is NotImplemented:
            self.assertIs(result, NotImplemented)
        else:
            self.assertEqual(result, expected)
            self.assertIs(type(result), type(expected))
    return check


def modular_check(owner, exponent_type, modulus_type):
    def check(self):
        arguments = (owner(11), exponent_type(3), modulus_type(7))
        if owner is float:
            self.assertRaises(TypeError, owner.__pow__, *arguments)
        elif exponent_type is float or modulus_type is float or (
                owner is int and long in (exponent_type, modulus_type)):
            self.assertIs(owner.__pow__(*arguments), NotImplemented)
        else:
            result = owner.__pow__(*arguments)
            self.assertEqual(result, 1)
            self.assertIs(type(result), owner)
    return check


for owner in (int, long, float):
    values = {
        "__add__": owner(14), "__sub__": owner(8), "__mul__": owner(33),
        "__div__": 11.0 / 3.0 if owner is float else owner(3),
        "__floordiv__": owner(3), "__mod__": owner(2),
        "__pow__": owner(1331), "__rsub__": owner(-8),
        "__rdiv__": 3.0 / 11.0 if owner is float else owner(0),
    }
    for operand_type in (int, long, float):
        rejects = (owner is int and operand_type is not int) or (
            owner is long and operand_type is float)
        for slot in sorted(values):
            label = "test_slot_%s_%s_%s" % (owner.__name__, operand_type.__name__, slot[2:-2])
            setattr(NumericSlots, label, slot_check(
                owner, operand_type, slot, NotImplemented if rejects else values[slot]))
        for modulus_type in (int, long, float):
            label = "test_modular_%s_%s_%s" % (
                owner.__name__, operand_type.__name__, modulus_type.__name__)
            setattr(NumericSlots, label, modular_check(owner, operand_type, modulus_type))
