"""Project-authored Python 2 number formatting and ordering witnesses.

Expectations are checked on CPython 2.7.18.
"""

import unittest


def _error(operation, *args):
    try:
        operation(*args)
    except BaseException as error:
        return type(error).__name__, str(error)
    raise AssertionError('operation must fail')


class NumericSemantics(unittest.TestCase):
    def test_complex_str_keeps_twelve_integer_digits(self):
        value = complex(123456789012, 1)
        for text in (str(value), '%s' % value, '{}'.format(value), '{!s}'.format(value), unicode(value)):
            self.assertEqual(text, '(123456789012+1j)')
        self.assertEqual(str(complex(1, 123456789012)), '(1+123456789012j)')
        self.assertEqual(str(complex(0, 123456789012)), '123456789012j')
        self.assertEqual(str(complex(-123456789012, -123456789012.5)), '(-123456789012-123456789012j)')
        self.assertEqual(str(complex(1234567890123, 1)), '(1.23456789012e+12+1j)')
        self.assertEqual(str(complex(100000000000, 1e-5)), '(100000000000+1e-05j)')
        self.assertEqual(str(complex(1e12, 1e-4)), '(1e+12+0.0001j)')
        self.assertEqual(str(complex(0.5, float('inf'))), '(0.5+infj)')

    def test_complex_repr_keeps_sixteen_integer_digits(self):
        value = complex(1234567890123456, 1)
        for text in (repr(value), '%r' % value, '{!r}'.format(value), `value`):
            self.assertEqual(text, '(1234567890123456+1j)')
        self.assertEqual(repr(complex(12345678901234567, 1)), '(1.2345678901234568e+16+1j)')
        self.assertEqual(repr(complex(123456789012.5, 0)), '(123456789012.5+0j)')
        self.assertEqual(str(complex(123456789012.5, 0)), '(123456789012+0j)')

    def test_float_str_keeps_eleven_integer_digits(self):
        self.assertEqual(str(12345678901.0), '12345678901.0')
        self.assertEqual(str(123456789012.0), '1.23456789012e+11')
        self.assertEqual('%s' % 123456789012.5, '1.23456789012e+11')
        self.assertEqual(repr(123456789012.0), '123456789012.0')
        self.assertEqual(repr(1234567890123456.0), '1234567890123456.0')
        self.assertEqual(repr(12345678901234568.0), '1.2345678901234568e+16')

    def test_cmp_nan_against_long_beyond_double_overflows(self):
        nan = float('nan')
        big = 2 ** 2000
        overflow = ('OverflowError', 'long int too large to convert to float')
        class Long(long):
            pass
        class Float(float):
            pass
        for left, right in ((big, nan), (nan, big), (-big, nan), (Long(big), nan), (nan, Long(big)), (big, Float(nan)), (Float(nan), big), (Long(big), Float(nan))):
            self.assertEqual(_error(cmp, left, right), overflow)
            self.assertFalse(left < right)
            self.assertFalse(left > right)
            self.assertFalse(left == right)
            self.assertTrue(left != right)
            self.assertFalse(left <= right)
            self.assertFalse(left >= right)
        self.assertEqual(_error(cmp, {1: big}, {1: nan}), overflow)
        self.assertEqual(sorted([big, nan]), [big, nan])
        self.assertEqual(sorted([nan, big]), [nan, big])
        self.assertEqual(max(big, nan), big)
        self.assertFalse(nan in [big])
        self.assertFalse(big in [nan])

    def test_cmp_nan_against_representable_integers_orders_by_type(self):
        nan = float('nan')
        infinity = float('inf')
        big = 2 ** 2000
        class Integer(int):
            pass
        for other in (5, 0, 2 ** 70, 2 ** 1023, True, Integer(5), -(2 ** 1023)):
            self.assertEqual(abs(cmp(other, nan)), 1)
            self.assertEqual(cmp(nan, other), -cmp(other, nan))
        self.assertEqual(cmp(nan, nan), 0)
        self.assertEqual(cmp(big, 1e308), 1)
        self.assertEqual(cmp(1e308, big), -1)
        self.assertEqual(cmp(-big, -1e308), -1)
        self.assertEqual(cmp(big, infinity), -1)
        self.assertEqual(cmp(infinity, big), 1)
        self.assertEqual(cmp(-big, -infinity), 1)
        self.assertEqual(cmp(2 ** 1023, 2.0 ** 1023), 0)
        self.assertEqual(cmp(2 ** 1023 + 1, 2.0 ** 1023), 1)
        self.assertEqual(cmp(2 ** 1024, 1e308), 1)
        self.assertTrue(big < infinity)
        self.assertFalse(big == infinity)

    def test_cmp_coercion_to_long_beyond_double_overflows(self):
        nan = float('nan')
        big = 2 ** 2000
        overflow = ('OverflowError', 'long int too large to convert to float')
        class Huge:
            def __coerce__(self, other):
                return big, other
        class Undefined:
            def __coerce__(self, other):
                return nan, other
        operations = (cmp, lambda a, b: a < b, lambda a, b: a > b, lambda a, b: a == b, lambda a, b: a != b, lambda a, b: a <= b, lambda a, b: a >= b)
        for left, right in ((Huge(), nan), (nan, Huge()), (Undefined(), big), (big, Undefined())):
            for operation in operations:
                self.assertEqual(_error(operation, left, right), overflow)
        class Compared(int):
            def __cmp__(self, other):
                return 7
        self.assertEqual(cmp(Compared(5), nan), 1)
        self.assertEqual(cmp(nan, Compared(5)), -1)
        self.assertEqual(cmp(Compared(5), big), 1)
