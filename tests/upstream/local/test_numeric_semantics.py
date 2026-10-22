"""Project-authored Python 2 number formatting and ordering witnesses.

Expectations are checked on CPython 2.7.18.
"""

import unittest
import _struct


def _number(state, bits):
    value = 0
    while value.bit_length() < bits:
        state[0] = (state[0] * 1103515245 + 12345) & 0x7fffffff
        value = (value << 31) | state[0] | 1
    return value >> (value.bit_length() - bits)


def _digit_product(large, small):
    total = 0
    shift = 0
    while small:
        total += (large * (small & 0x7fff)) << shift
        small >>= 15
        shift += 15
    return total


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

    def test_division_by_two_digit_divisors(self):
        for divisor in (2 ** 15, 2 ** 15 + 1, 1000003, 10 ** 9 + 7, 2 ** 30 - 1, -(2 ** 15), -(10 ** 9 + 7)):
            for value in (3 ** 130, -(3 ** 130), 2 ** 64 - 1, -(2 ** 45), 10 ** 30 + 7, 2 ** 30 - 1, 2 ** 30, 1L, 0L):
                quotient, remainder = divmod(value, divisor)
                self.assertEqual((quotient * divisor + remainder, abs(remainder) < abs(divisor), remainder * divisor >= 0), (value, True, True))
                self.assertEqual((value // divisor, value % divisor), (quotient, remainder))

    def test_integral_float_hashes_as_the_integer(self):
        for value in (0, 1, -1, -2, 2 ** 52 + 1, 2 ** 53, 2 ** 62, -(2 ** 62), 2 ** 63 - 1024, -(2 ** 63), 2 ** 63,
                      -(2 ** 63) - 2048, 2 ** 64, 2 ** 70 + 2 ** 20, -(2 ** 100), 2 ** 1023):
            self.assertEqual((hash(float(value)), hash(-float(value))), (hash(value), hash(-value)))

    def test_float_str_keeps_the_zeros_of_a_tie_rounded_down(self):
        for value, text in ((1000000000005.0, '1.00000000000e+12'), (1039816169405.0, '1.03981616940e+12'),
                            (10000000000050.0, '1.00000000000e+13'), (100000000000500.0, '1.00000000000e+14'),
                            (1000000000015.0, '1.00000000002e+12'), (1000000000001.0, '1e+12'),
                            (1000000000000500.0, '1e+15'), (100000000000.5, '1e+11')):
            self.assertEqual((str(value), '%s' % value, format(value, ''), str(-value)), (text, text, text, '-' + text))
        self.assertEqual(repr(1000000000005.0), '1000000000005.0')
        self.assertEqual(str(complex(1, 1000000000005.0)), '(1+1.00000000000e+12j)')
        self.assertEqual(format(1000000000005.0, '.12'), '1.00000000000e+12')
        self.assertEqual(['%.2g' % 205.0, '%.3g' % 2005.0, '%.1g' % 25.0, '%.2G' % 12500.0, '%#.2g' % 215.0,
                          '%.2g' % 215.0, '%#.3g' % 1000000.0, '%#.0g' % 1000000.0],
                         ['2.0e+02', '2.00e+03', '2e+01', '1.2E+04', '2.2e+02', '2.2e+02', '1.00e+06', '1.e+06'])

    def test_integer_literal_errors_repeat_at_most_two_hundred_bytes(self):
        for convert, literal, base, reported, shown in ((int, '   ' + 'x' * 300, 10, 10, 'x' * 200),
                                                        (long, '   ' + 'x' * 300, 10, 10, '   ' + 'x' * 197),
                                                        (int, u'z' * 250, 35, 35, 'z' * 200), (long, '9' * 250 + '_', 0, 10, '9' * 200),
                                                        (int, '0x' + 'g' * 300, 0, 0, '0x' + 'g' * 198)):
            self.assertEqual(_error(convert, literal, base),
                             ('ValueError', 'invalid literal for %s() with base %d: %r' % (convert.__name__, reported, shown)))
        for convert, literal, base in ((int, 'x' * 300 + '\x00', 16), (long, ' ' * 3 + 'y' * 250 + '\x00', 10)):
            self.assertEqual(_error(convert, literal, base),
                             ('ValueError', 'invalid literal for %s() with base %d: %r' % (convert.__name__, base, literal)))

    def test_complex_power_and_division_follow_c_pow_and_c_quot(self):
        def bits(value):
            return _struct.pack('<dd', value.real, value.imag).encode('hex')
        nan = float('nan')
        infinity = float('inf')
        base = 1e309j
        exponent = -2
        self.assertEqual(bits(base ** exponent), '000000000000f87f000000000000f87f')
        self.assertEqual(bits(1e309j ** -2), '000000000000f87f000000000000f87f')
        self.assertEqual(bits(-(1e309j ** -1)), '000000000000f8ff000000000000f8ff')
        self.assertEqual(bits(complex(1, 2) / complex(nan, 1)), '000000000000f87f000000000000f87f')
        overflow = ('OverflowError', 'complex exponentiation')
        zero_division = ('ZeroDivisionError', '0.0 to a negative or complex power')
        for operation, outcome in ((lambda: complex(infinity, 0) ** 1, overflow), (lambda: complex(1e200, 0) ** 2, overflow),
                                   (lambda: complex(2, 0) ** 1025, overflow), (lambda: complex(1e300, 1e300) ** 1.5, overflow),
                                   (lambda: 0j ** -1, zero_division), (lambda: 0j ** 1j, zero_division),
                                   (lambda: 0j ** -2.5, zero_division)):
            self.assertEqual(_error(operation), outcome)
        self.assertEqual([0j ** 2.5, 0j ** 0, complex(nan, nan) ** 0], [0j, 1 + 0j, 1 + 0j])
        result = complex(infinity, infinity) ** 1
        self.assertTrue(result.real != result.real and result.imag != result.imag)

    def test_float_text_converts_with_correct_rounding(self):
        for text, expected in (('1.5', '0x1.8000000000000p+0'), ('0.1', '0x1.999999999999ap-4'),
                               ('3.14159265358979', '0x1.921fb54442d11p+1'), ('1e22', '0x1.0f0cf064dd592p+73'),
                               ('9e22', '0x1.30ee8e7179044p+76'), ('1e23', '0x1.52d02c7e14af6p+76'),
                               ('9007199254740993', '0x1.0000000000000p+53'), ('9007199254740995', '0x1.0000000000002p+53'),
                               ('2.2250738585072011e-308', '0x0.fffffffffffffp-1022'),
                               ('2.2250738585072014e-308', '0x1.0000000000000p-1022'),
                               ('4.9406564584124654e-324', '0x0.0000000000001p-1022'),
                               ('2.4703282292062328e-324', '0x0.0000000000001p-1022'), ('2.4703282292062327e-324', '0x0.0p+0'),
                               ('1.7976931348623157e308', '0x1.fffffffffffffp+1023'), ('1.7976931348623159e308', 'inf'),
                               ('1e400', 'inf'), ('-1e-400', '-0x0.0p+0'), ('0.30000000000000004', '0x1.3333333333334p-2'),
                               ('123456789012345678901234567890', '0x1.8ee90ff6c373ep+96'),
                               ('  -2.5e-3\n', '-0x1.47ae147ae147bp-9'), ('+.5', '0x1.0000000000000p-1'), ('5.', '0x1.4000000000000p+2'),
                               ('1E+05', '0x1.86a0000000000p+16'), ('000001.5000', '0x1.8000000000000p+0'),
                               ('1' + '0' * 30 + '.5e-30', '0x1.0000000000000p+0'), ('0.' + '0' * 330 + '1e330', '0x1.999999999999ap-4')):
            self.assertEqual((float(text).hex(), float(unicode(text)).hex()), (expected, expected))
            self.assertEqual(complex(text).real.hex(), expected)

    def test_float_text_rounds_long_and_boundary_significands(self):
        largest_half = str(2 ** 1024 - 2 ** 970)
        smallest_half_digits = str(5 ** 1075)
        smallest_half = '0.' + '0' * (1075 - len(smallest_half_digits)) + smallest_half_digits
        one_half_ulp = '1.00000000000000011102230246251565404236316680908203125'
        for text, expected in (('9007199254740993.0000000000000000001', '0x1.0000000000001p+53'),
                               ('9007199254740992.9999999999999999999', '0x1.0000000000000p+53'),
                               ('10000000000000000001e-5', '0x1.6bcc41e900000p+46'), ('18446744073709551615', '0x1.0000000000000p+64'),
                               ('18446744073709551616e-20', '0x1.79ca10c924223p-3'), ('9999999999999999999e-308', '0x1.f2f5c7a1a488ep-961'),
                               (largest_half, 'inf'), (largest_half[:-1] + '1', '0x1.fffffffffffffp+1023'),
                               (largest_half + '.000000000000000000000001', 'inf'), (smallest_half, '0x0.0p+0'),
                               (smallest_half + '00000000000001', '0x0.0000000000001p-1022'), ('1' + '0' * 900 + 'e-900', '0x1.0000000000000p+0'),
                               ('0.' + '9' * 1000, '0x1.0000000000000p+0'), ('4.9406564584124654417656879286822137236505980e-324', '0x0.0000000000001p-1022'),
                               ('7.2057594037927933e16', '0x1.0000000000000p+56'), (one_half_ulp, '0x1.0000000000000p+0'),
                               (one_half_ulp + '1', '0x1.0000000000001p+0'), ('-' + one_half_ulp[:-1] + '4', '-0x1.0000000000000p+0')):
            self.assertEqual((float(text).hex(), float(unicode(text)).hex()), (expected, expected))

    def test_float_literals_round_like_float_text(self):
        for text in ('0.30000000000000004', '1e300', '1.7976931348623157e+308', '1.7976931348623159e308', '5e-324',
                     '2.4703282292062327e-324', '2.2250738585072011e-308', '9007199254740993.0', '123456789012345678901234567890.5',
                     '3.1415926535897932384626433832795028841971', '.5e-10', '1E+05', '0.' + '0' * 330 + '1e330',
                     '18446744073709551616e-20', str(2 ** 1024 - 2 ** 970) + '.0'):
            self.assertEqual((eval(text).hex(), eval(text + 'j').imag.hex()), (float(text).hex(), float(text).hex()))

    def test_reflected_float_subtraction_keeps_the_nan_operand(self):
        def bits(value):
            return _struct.pack('>d', value).encode('hex')
        class Subclass(float):
            pass
        nan = float('nan')
        for value in (nan, -nan):
            expected = bits(value)
            augmented = 3L
            augmented -= value
            self.assertEqual([bits(3L - value), bits(True - value), bits(value.__rsub__(3)), bits(value.__rsub__(3L)),
                              bits(value.__rsub__(3.0)), bits(float.__sub__(3.0, value)), bits(augmented),
                              bits(Subclass(3) - value), bits(3L - Subclass(value)), bits(3 - value), bits(3.0 - value)],
                             [expected] * 11)
            for difference in (3L - complex(value, value), complex(value, value).__rsub__(3), complex(3, 3) - complex(value, value)):
                self.assertEqual((bits(difference.real), bits(difference.imag)), (expected, expected))

    def test_integer_power_goes_on_from_the_step_that_overflows(self):
        class Small(int):
            pass

        for base, exponent in ((3, 45), (3, 41), (-3, 45), (10, 25), (2, 64), (-2, 65), (7, 30), (3037000499, 3), (-3037000500, 3), (2 ** 31, 3), (Small(3), 45)):
            expected = 1L
            for _ in range(exponent):
                expected *= base
            for result in (base ** exponent, pow(base, exponent), base ** long(exponent), long(base) ** exponent):
                self.assertEqual((type(result), result), (long, expected))
        self.assertEqual((3 ** 2L, (-3) ** 3L, type(3 ** 2L)), (9L, -27L, long))

    def test_integer_power_keeps_int_until_a_step_overflows(self):
        def kinds(values):
            return [(type(value).__name__, value) for value in values]
        self.assertEqual(kinds([3 ** 39, 3 ** 40, (-2) ** 63, (-2) ** 64, 2 ** 63, 0 ** 0, (-1) ** 63, 2L ** 3, True ** 2, 2 ** True,
                                3037000500 ** 2]),
                         [('int', 4052555153018976267), ('long', 12157665459056928801L), ('int', -9223372036854775808),
                          ('long', 18446744073709551616L), ('long', 9223372036854775808L), ('int', 1), ('int', -1), ('long', 8L),
                          ('int', 1), ('int', 2), ('long', 9223372037000250000L)])
        self.assertEqual(kinds([pow(3, 1000, 1000003), pow(2 ** 32, 2, 7), pow(3037000500, 2, 10), pow(-2 ** 63, 1, -1), pow(-7, 3, 5),
                                pow(7, 3, -5), pow(5, 0, -3), pow(3L, 4, 5), pow(3, 4, 5L), pow(True, 5, 3)]),
                         [('int', 73216), ('long', 2L), ('long', 0L), ('long', 0L), ('int', 2), ('int', -2), ('int', -2),
                          ('long', 1L), ('long', 1L), ('int', 1)])
        self.assertEqual(kinds([2 ** -1, (-2) ** -3, 2L ** -2, (-2) ** -1075, 2 ** -1075]),
                         [('float', 0.5), ('float', -0.125), ('float', 0.25), ('float', -0.0), ('float', 0.0)])
        self.assertEqual([_error(pow, 0, -1), _error(pow, 2, -1, 5), _error(pow, 2, 3, 0), _error(pow, 10 ** 400, -1)],
                         [('ZeroDivisionError', '0.0 cannot be raised to a negative power'),
                          ('TypeError', 'pow() 2nd argument cannot be negative when 3rd argument specified'),
                          ('ValueError', 'pow() 3rd argument cannot be 0'),
                          ('OverflowError', 'long int too large to convert to float')])
        value = 5
        value **= 3
        self.assertEqual(kinds([value]), [('int', 125)])

        class Power(int):
            def __pow__(self, other, modulus=None):
                return 'pow', other, modulus

            def __rpow__(self, other):
                return 'rpow', other
        self.assertEqual([Power(3) ** 2, 3 ** Power(2), pow(Power(3), 2, 5), pow(3, Power(2), 5), pow(3, 2, Power(5))],
                         [('pow', 2, None), ('rpow', 3), ('pow', 2, 5), 4, 4])

    def test_huge_right_shift_counts_leave_the_sign(self):
        self.assertEqual([1 >> 2 ** 40, -1 >> 2 ** 40, 0 << 2 ** 40, 5L >> 2 ** 62, -5L >> 2 ** 62, (2 ** 100) >> (2 ** 63 - 1),
                          -(2 ** 100) >> (2 ** 63 - 1)],
                         [0, -1, 0, 0, -1, 0, -1])
        self.assertEqual([_error(lambda: 1 >> 2 ** 63), _error(lambda: 1 >> -(2 ** 63))],
                         [('OverflowError', 'long int too large to convert to int'), ('ValueError', 'negative shift count')])

    def test_lopsided_products_match_single_digit_sums(self):
        state = [7]
        for small_bits, large_bits in ((1920, 3840), (1935, 3870), (2400, 7300), (3000, 6001), (1950, 30000), (4500, 8999)):
            small = _number(state, small_bits)
            large = _number(state, large_bits)
            product = _digit_product(large, small)
            self.assertEqual([small * large, -large * small, small * -large], [product, -product, -product])
        value = _number(state, 6000)
        self.assertEqual(value ** 3, _digit_product(value * value, value))

    def test_products_and_squares_across_the_word_kernels(self):
        state = [11]
        for small_bits, large_bits in ((31, 47), (45, 60), (60, 61), (61, 900), (91, 1800), (1770, 1770), (1800, 1830), (1830, 4300),
                                       (2370, 2370), (2430, 2460), (1800, 9000)):
            for small, large in ((_number(state, small_bits), _number(state, large_bits)), ((1 << small_bits) - 1, (1 << large_bits) - 1)):
                product = _digit_product(large, small)
                self.assertEqual([small * large, -large * small, small * -large], [product, -product, -product])
        for bits in (61, 91, 1800, 2370, 2400, 2430, 4860):
            for value in (_number(state, bits), (1 << bits) - 1, -(1 << (bits - 1))):
                square = _digit_product(abs(value), abs(value))
                self.assertEqual([value * value, value ** 2, value * (value + 0)], [square, square, square])
        value = _number(state, 700)
        powers = [1]
        for exponent in range(1, 18):
            powers.append(powers[-1] * value)
        self.assertEqual([value ** exponent for exponent in range(18)], powers)
        self.assertEqual([(-value) ** 17, value ** 3L, type(2 ** 3L), (-3) ** 41], [-powers[17], powers[3], long, -36472996377170786403L])

    def test_divisions_keep_the_floor_identity(self):
        def check(dividend, divisor):
            quotient, remainder = divmod(dividend, divisor)
            self.assertEqual((quotient * divisor + remainder, abs(remainder) < abs(divisor), remainder * divisor >= 0), (dividend, True, True))
            self.assertEqual((dividend // divisor, dividend % divisor), (quotient, remainder))
        # Quotient estimates of BASE + 1 and the add-back step of 30-bit digits, as test_long checks them.
        for dividend, divisor in ((1231948412290879395966702881L, 1147341367131428698L), (815427756481275430342312021515587883L, 707270836069027745L),
                                  (627976073697012820849443363563599041L, 643588798496057020L),
                                  (1115141373653752303710932756325578065L, 1038556335171453937726882627L),
                                  (922498905405436751940989320930368494L, 949985870686786135626943396L),
                                  (768235853328091167204009652174031844L, 1091555541180371554426545266L)):
            for signs in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
                check(signs[0] * dividend, signs[1] * divisor)
        self.assertEqual(divmod(1115141373653752303710932756325578065L, 1038556335171453937726882627L), (1073741823L, 1038556335055541328303569044L))
        state = [13]
        for dividend_bits, divisor_bits in ((61, 31), (90, 60), (91, 61), (2100, 61), (4300, 2100), (9000, 4230), (600, 599)):
            for dividend in (_number(state, dividend_bits), (1 << dividend_bits) - 1):
                for divisor in (_number(state, divisor_bits), (1 << divisor_bits) - 1, 1 << (divisor_bits - 1)):
                    for signs in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
                        check(signs[0] * dividend, signs[1] * divisor)

    def test_modular_powers_match_right_to_left_squaring(self):
        def reference(base, exponent, modulus):
            result = 1 % modulus
            base %= modulus
            while exponent:
                if exponent & 1:
                    result = result * base % modulus
                base = base * base % modulus
                exponent >>= 1
            return result
        state = [17]
        for modulus_bits, exponent_sizes in ((2, (0, 1, 2, 600)), (31, (1, 240, 241)), (61, (2, 240, 241, 600)), (1100, (1, 241))):
            for modulus in (_number(state, modulus_bits), (1 << modulus_bits) - 1, -_number(state, modulus_bits)):
                for exponent_bits in exponent_sizes:
                    exponent = _number(state, exponent_bits) if exponent_bits else 0
                    for base in (_number(state, modulus_bits + 7), -_number(state, 45), modulus, 2 * modulus - 1):
                        self.assertEqual(pow(base, exponent, modulus), reference(base, exponent, modulus))
        modulus = -_number(state, 2430)
        for exponent in (3, _number(state, 241)):
            for base in (_number(state, 2437), -_number(state, 45)):
                self.assertEqual(pow(base, exponent, modulus), reference(base, exponent, modulus))
        self.assertEqual([pow(5L, 0, 1), pow(5L, 0, -1), pow(-5L, 3, -1), pow(5L, 0, -7), pow(-5L, 3, 7), pow(5L, 3, -7), pow(2 ** 64, 2 ** 300, 1 << 61)],
                         [0L, 0L, 0L, -6L, 1L, -1L, 0L])

    def test_recycled_floats_hold_their_own_values(self):
        held = [index + 0.5 for index in range(600)]
        del held
        held = [-(index + 0.25) for index in range(600)]
        self.assertEqual(held[::150], [-0.25, -150.25, -300.25, -450.25])
        x = 1.5
        signs = [_struct.pack('>d', value)[0] for value in (-(x - x), x * -0.0, -x + x, -(-x + x), 0.0 * -x)]
        self.assertEqual(signs, ['\x80', '\x80', '\x00', '\x80', '\x80'])
        total = 0.0
        for index in range(1000):
            total += index * 0.5 - (-index) * 0.25
        self.assertEqual(total, 374625.0)
