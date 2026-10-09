"""Check numeric slot dispatch with an explicit operand-type matrix.

These are project-authored tests. Expectations are checked on CPython 2.7.18.
"""

import unittest


def _error(operation, *args, **kwargs):
    try:
        operation(*args, **kwargs)
    except BaseException as error:
        return type(error).__name__, error.args
    raise AssertionError('operation must fail')


def _numeric_class(base, name, forward, reflected):
    methods = {}
    for operator in ('add', 'mod', 'lshift', 'pow'):
        if forward:
            methods['__%s__' % operator] = lambda self, other, *rest: name + '.forward'
        if reflected:
            methods['__r%s__' % operator] = lambda self, other, *rest: name + '.reflected'
    return type(name, (base,), methods)


class NumericDispatch(unittest.TestCase):
    def test_subtype_reflection_and_inherited_methods(self):
        Reflected = _numeric_class(int, 'Reflected', False, True)
        self.assertEqual(3 + Reflected(2), 'Reflected.reflected')
        self.assertEqual(3.5 + Reflected(2), 5.5)
        self.assertEqual(3.5 << _numeric_class(str, 'Text', False, True)('x'), 'Text.reflected')
        Forward = _numeric_class(int, 'Forward', True, False)
        Inheriting = type('Inheriting', (Forward,), {})
        for operation in (lambda a, b: a + b, lambda a, b: a % b, lambda a, b: a << b, lambda a, b: a ** b, lambda a, b: pow(a, b, 5)):
            self.assertEqual(operation(Forward(1), Inheriting(2)), 'Forward.forward')
        self.assertEqual(pow(Inheriting(2), 3, 5), 'Forward.forward')

    def test_builtin_slot_before_unrelated_reflection(self):
        Reflected = _numeric_class(int, 'Reflected', False, True)
        self.assertEqual(_error(lambda: 'ab' % Reflected(2)), ('TypeError', ('not all arguments converted during string formatting',)))
        self.assertEqual('ab' % _numeric_class(list, 'Items', True, True)([1]), 'ab')
        self.assertEqual('ab' % _numeric_class(str, 'Text', False, True)('x'), 'Text.reflected')

    def test_sequence_methods_behind_numeric_slots(self):
        Text = _numeric_class(str, 'Text', False, True)
        Reflected = _numeric_class(int, 'Reflected', True, True)
        self.assertEqual(_error(lambda: Text('x') + Reflected(1)), ('TypeError', ("cannot concatenate 'str' and 'Reflected' objects",)))
        self.assertEqual(Text('x') + Text('y'), 'xy')
        self.assertEqual(_error(lambda: 'a' + 1), ('TypeError', ("cannot concatenate 'str' and 'int' objects",)))
        self.assertEqual(_error(lambda: u'a' + 1), ('TypeError', ('coercing to Unicode: need string or buffer, int found',)))
        self.assertEqual(_error('a'.__add__, 5), ('TypeError', ("cannot concatenate 'str' and 'int' objects",)))
        self.assertIs('%d'.__rmod__(1), NotImplemented)
        Doubling = type('Doubling', (list,), {'__rmul__': lambda self, other: NotImplemented})
        self.assertEqual(Doubling([3]) * 2, [3, 3])
        Declining = type('Declining', (list,), {'__add__': lambda self, other: NotImplemented})
        self.assertEqual(_error(lambda: Declining([1]) + [2]), ('TypeError', ("unsupported operand type(s) for +: 'Declining' and 'list'",)))

    def test_inplace_protocol_order(self):
        Reflected = _numeric_class(object, 'Reflected', False, True)
        items = [1]
        items += Reflected()
        self.assertEqual(items, 'Reflected.reflected')
        Items = type('Items', (list,), {'__radd__': lambda self, other: 'radd'})
        extended = Items([1])
        extended += 'ab'
        self.assertEqual((type(extended), extended), (Items, [1, 'a', 'b']))
        self.assertEqual(_error(Items([1]).__iadd__, Reflected()), ('TypeError', ("'Reflected' object is not iterable",)))
        self.assertEqual(_error(_inplace_power, type('Power', (object,), {'__ipow__': lambda self, other: NotImplemented})(), 2),
                         ('TypeError', ("unsupported operand type(s) for ** or pow(): 'Power' and 'int'",)))
        self.assertEqual(_inplace_multiply(3, [1]), [1, 1, 1])
        self.assertEqual(_error(_inplace_multiply, {}, [1]), ('TypeError', ("unsupported operand type(s) for *=: 'dict' and 'list'",)))

    def test_classic_instances_look_methods_up_once(self):
        log = []

        class Lookup:
            def __getattr__(self, name):
                log.append(name)
                if name == '__index__':
                    return lambda: 2
                raise AttributeError(name)

        class Failing:
            def __getattr__(self, name):
                log.append(name)
                raise KeyError(name)
        self.assertEqual([1] * Lookup(), [1, 1])
        self.assertEqual(log, ['__coerce__', '__rmul__', '__coerce__', '__index__'])
        for operation, name in ((lambda: Failing() + 1, '__coerce__'), (lambda: 1 + Failing(), '__coerce__'), (lambda: -Failing(), '__neg__')):
            del log[:]
            self.assertEqual(_error(operation), ('KeyError', (name,)))
            self.assertEqual(log, [name])

    def test_classic_unary_and_conversions(self):
        class Plain:
            pass

        class Converting:
            def __int__(self):
                return 7
        for operation, name in ((lambda value: -value, '__neg__'), (lambda value: ~value, '__invert__'), (abs, '__abs__'),
                                (hex, '__hex__'), (float, '__float__'), (complex, '__float__'), (int, '__trunc__'), (long, '__trunc__')):
            self.assertEqual(_error(operation, Plain()), ('AttributeError', ("Plain instance has no attribute '%s'" % name,)))
        self.assertEqual((long(Converting()), int(Converting())), (7L, 7))
        self.assertEqual((bool(x=1), int(x='10', base=2), complex(imag=2)), (True, 2, 2j))
        self.assertEqual(_error(bool, y=1), ('TypeError', ("'y' is an invalid keyword argument for this function",)))

    def test_coercion_of_old_style_operands(self):
        class Coercing:
            def __coerce__(self, other):
                return (5, 6) if isinstance(other, list) else None
        self.assertEqual([1] + Coercing(), 11)
        self.assertEqual(Coercing() * [1], 30)
        self.assertEqual(_error(lambda: None + Coercing()), ('TypeError', ("unsupported operand type(s) for +: 'NoneType' and 'instance'",)))

    def test_three_argument_power(self):
        class Classic:
            def __pow__(self, *args):
                return args
        Plain = type('Plain', (object,), {'__rpow__': lambda self, *args: 'rpow'})
        self.assertEqual(_error(pow, Plain(), 2, 3), ('AttributeError', ('__pow__',)))
        self.assertEqual(_error(pow, type('Bare', (object,), {})(), Classic(), 5), ('AttributeError', ("'Bare' object has no attribute '__pow__'",)))
        self.assertEqual(pow(Classic(), 2, 5), (2, 5))
        self.assertEqual(_error(pow, 2, -1, 1.5), ('TypeError', ('pow() 2nd argument cannot be negative when 3rd argument specified',)))
        self.assertEqual(_error(pow, 2L, -1, 1.5), ('TypeError', ('pow() 3rd argument not allowed unless all arguments are integers',)))
        self.assertEqual(_error(float.__pow__, 2.0, 'x', 5), ('TypeError', ('pow() 3rd argument not allowed unless all arguments are integers',)))
        self.assertIs(complex.__pow__(1j, 'x', 5), NotImplemented)
        self.assertIs(int.__pow__(2, 3, 5L), NotImplemented)

    def test_numeric_messages(self):
        self.assertEqual(_error(lambda: 0 ** -1), ('ZeroDivisionError', ('0.0 cannot be raised to a negative power',)))
        self.assertEqual(_error(lambda: 0j ** -1), ('ZeroDivisionError', ('0.0 to a negative or complex power',)))
        overflow = _error(lambda: 10.0 ** 400)
        self.assertEqual((overflow[0], len(overflow[1]), overflow[1][0]), ('OverflowError', 2, 34))
        self.assertEqual(_error(lambda: 1 << 2 ** 100), ('OverflowError', ('long int too large to convert to int',)))
        self.assertEqual(_error(lambda: 1 >> -2 ** 100), ('OverflowError', ('long int too large to convert to int',)))
        self.assertEqual(_error(float('inf').as_integer_ratio), ('OverflowError', ('Cannot pass infinity to float.as_integer_ratio.',)))
        self.assertEqual(_error(float('nan').as_integer_ratio), ('ValueError', ('Cannot pass NaN to float.as_integer_ratio.',)))
        self.assertEqual(_error(round, 'a'), ('TypeError', ('a float is required',)))
        self.assertEqual(_error(round, 1.7976931348623157e308, -308), ('OverflowError', ('rounded value too large to represent',)))
        self.assertEqual(_error(lambda: 'ab' * 2 ** 62), ('OverflowError', ('repeated string is too long',)))

    def test_index_conversion_names_the_operand(self):
        Index = type('Index', (object,), {'__init__': lambda self, value: setattr(self, 'value', value), '__index__': lambda self: self.value})
        self.assertEqual(_error(lambda: 'x' * Index(2 ** 70)), ('OverflowError', ("cannot fit 'Index' into an index-sized integer",)))
        self.assertEqual(_error(lambda: 'x' * Index(1.5)), ('TypeError', ('__index__ returned non-(int,long) (type float)',)))
        self.assertEqual(_error(lambda: 'abc'[Index(2 ** 70)]), ('IndexError', ("cannot fit 'Index' into an index-sized integer",)))
        self.assertEqual(_error(lambda: [1][-2 ** 70]), ('IndexError', ("cannot fit 'long' into an index-sized integer",)))

    def test_long_subclass_remainder_identity(self):
        Long = type('Long', (long,), {})
        for value, divisor, kept in ((2, 3, True), (32768, 32769, True), (2 ** 29, 2 ** 29 + 1, True), (-4, -7, True),
                                     (0, 3, True), (5, 3, False), (-2, 3, False)):
            self.assertIs(type(Long(value) % divisor) is Long, kept)
            self.assertIs(type(divmod(Long(value), divisor)[1]) is Long, kept)
            self.assertEqual(divmod(Long(value), divisor), divmod(value, divisor))

    def test_fromhex_rounding_tie_at_the_leading_digit(self):
        for text, expected in (('8p-1078', 5e-324), ('0x8p-1078', 5e-324), ('.8p-1074', 5e-324), ('08p-1078', 0.0),
                               ('0.8p-1074', 0.0), ('1p-1075', 0.0), ('80p-1082', 5e-324), ('18p-1078', 1e-323)):
            self.assertEqual(float.fromhex(text), expected)
            self.assertEqual(float.fromhex(u'-' + text), -expected)

    def test_new_style_coercion_of_unchecked_operands(self):
        log = []

        class Pair(Exception):
            def __coerce__(self, other):
                log.append(type(other).__name__)
                return (1, 2.5)
        Declining = type('Declining', (object,), {'__coerce__': lambda self, other: log.append('declining')})
        self.assertEqual((divmod(IOError(1, 2), Pair()), IOError() - Pair()), ((2.0, 0.5), 1.5))
        self.assertEqual(_error(lambda: Declining() + 5), ('TypeError', ("unsupported operand type(s) for +: 'Declining' and 'int'",)))
        self.assertEqual(_error(lambda: [] + Declining()), ('TypeError', ("__coerce__ didn't return a 2-tuple",)))
        self.assertEqual(log, ['IOError', 'IOError', 'declining'])

    def test_coerce_builtin_uses_number_coercion(self):
        log = []

        class Lookup:
            def __getattr__(self, name):
                log.append(name)
                return 1

        class Swapping:
            def __coerce__(self, other):
                return (other, 'swapped')
        Malformed = type('Malformed', (object,), {'__coerce__': lambda self, other: 'x'})
        self.assertEqual(_error(coerce, Malformed(), 1), ('TypeError', ("__coerce__ didn't return a 2-tuple",)))
        self.assertEqual(_error(coerce, Lookup(), 1), ('TypeError', ("'int' object is not callable",)))
        self.assertEqual(log, ['__coerce__'])
        self.assertEqual((coerce(1, 2.5), coerce(True, 2L), coerce([], []), coerce(1, Swapping())), ((1.0, 2.5), (1L, 2L), ([], []), ('swapped', 1)))
        self.assertEqual(_error(coerce, 'a', 'b'), ('TypeError', ('number coercion failed',)))

    def test_ternary_slot_wrapper_arity(self):
        for method in ((2).__pow__, (2.0).__rpow__, (1j).__pow__, (2L).__rpow__):
            self.assertEqual(_error(method), ('TypeError', (' expected at least 1 arguments, got 0',)))
            self.assertEqual(_error(method, 1, 2, 3), ('TypeError', (' expected at most 2 arguments, got 3',)))

    def test_truth_result_validation(self):
        class Classic:
            def __init__(self, result):
                self.result = result

            def __nonzero__(self):
                return self.result

        class ClassicLength:
            def __init__(self, result):
                self.result = result

            def __len__(self):
                return self.result
        Integer = type('Integer', (int,), {})
        Length = type('Length', (object,), {'__len__': lambda self: -1})

        def truth(result):
            return type('Truth', (object,), {'__nonzero__': lambda self: result})()
        for result in ('x', Integer(1), 1L):
            self.assertEqual(_error(bool, truth(result)), ('TypeError', ('__nonzero__ should return bool or int, returned %s' % type(result).__name__,)))
        self.assertEqual((bool(truth(True)), bool(truth(0)), bool(Classic(Integer(1))), bool(ClassicLength(0))), (True, False, True, False))
        for value in (Classic(1L), ClassicLength('x')):
            self.assertEqual(_error(bool, value), ('TypeError', ('__nonzero__ should return an int',)))
        self.assertEqual(_error(bool, ClassicLength(-1)), ('ValueError', ('__nonzero__ should return >= 0',)))
        self.assertEqual(_error(bool, Length()), ('ValueError', ('__len__() should return >= 0',)))


def _inplace_power(left, right):
    left **= right
    return left


def _inplace_multiply(left, right):
    left *= right
    return left


class NumericOrdering(unittest.TestCase):
    def test_default_order_of_numbers(self):
        IntLike = type('IntLike', (object,), {'__int__': lambda self: 1})

        class Classic:
            pass
        self.assertEqual((cmp(Classic(), 5), cmp(Classic(), 5L), cmp(Classic(), 1.0), cmp(Classic(), True), cmp(Classic(), 1j)), (-1, -1, -1, 1, -1))
        self.assertEqual((cmp(IntLike(), 5), cmp(IntLike(), []), cmp(Classic(), IntLike())), (1, -1, -1))
        self.assertEqual((cmp(float('nan'), 1), cmp(1L, float('nan'))), (-1, 1))
        self.assertEqual(cmp(KeyError(), type('Zed', (object,), {})()), 1)

    def test_rich_comparison_falls_back_to_type_compare(self):
        log = []
        methods = {'__eq__': lambda self, other: log.append('eq') or NotImplemented}
        Integer = type('Integer', (int,), methods)
        self.assertTrue(Integer(5) == Integer(5))
        self.assertEqual(log, ['eq', 'eq'])
        Real = type('Real', (float,), methods)
        first = Real(1.5)
        self.assertFalse(first == Real(1.5))
        self.assertTrue(first == first)
        Mapping = type('Mapping', (dict,), methods)
        self.assertTrue(Mapping(a=1) == Mapping(a=1))
        Items = type('Items', (set,), methods)
        self.assertEqual(_error(lambda: Items([1]) == Items([1])), ('TypeError', ('cannot compare sets using cmp()',)))

    def test_comparison_coerces_new_style_operands(self):
        log = []
        Declining = type('Declining', (object,), {'__coerce__': lambda self, other: log.append(type(other).__name__)})
        Refusing = type('Refusing', (Exception,), {'__coerce__': lambda self, other: log.append(type(other).__name__) or NotImplemented})
        Items = type('Items', (list,), {'__coerce__': lambda self, other: log.append(type(other).__name__)})
        for operation in (lambda: Declining() == 1, lambda: cmp(Declining(), 2), lambda: 1j < Declining(), lambda: max(Items([1]), 2.5)):
            self.assertEqual(_error(operation), ('TypeError', ("__coerce__ didn't return a 2-tuple",)))
        self.assertEqual(('x' == Refusing(), Items([1]) == [1]), (False, True))
        self.assertEqual(log, ['int', 'int', 'complex', 'float', 'str'])
        Pairing = type('Pairing', (object,), {'__coerce__': lambda self, other: (3, other)})
        self.assertEqual((Pairing() < 5, Pairing() == 3, cmp(Pairing(), 3), Pairing() < 5L, Pairing() < 5.0), (True, True, 0, False, False))

        class CoerceTo(object):
            def __coerce__(self, other):
                return 42, other

        class Classic:
            def __cmp__(self, other):
                log.append(other)
                return 0
        del log[:]
        self.assertEqual(cmp(Classic(), CoerceTo()), 0)
        self.assertEqual(log, [42])

    def test_rich_comparison_slots_decline_foreign_operands(self):
        for method, operand in (((1.5).__eq__, 1j), ((1.5).__lt__, 1j), ((1.5).__ne__, complex(0, float('nan'))), ('a'.__eq__, u'a'), ('a'.__lt__, u'b'),
                                ('\xe9'.__ge__, u'x'), ({}.__lt__, {}), ({1: 2}.__gt__, {}), ({}.__eq__, [])):
            self.assertIs(method(operand), NotImplemented)
        self.assertEqual((u'a'.__eq__('a'), {}.__eq__({}), (1.5).__eq__(1L)), (True, True, False))
        log = []
        Mapping = type('Mapping', (dict,), {'__cmp__': lambda self, other: log.append('cmp') or 0})
        self.assertEqual((Mapping() >= Mapping(), Mapping() < {}), (True, False))
        self.assertEqual(log, ['cmp', 'cmp'])


class NumericConversions(unittest.TestCase):
    def test_integer_conversion_falls_back_to_trunc_attribute(self):
        log = []

        class Lookup(object):
            def __getattr__(self, name):
                log.append(name)
                return lambda: 7

        class Truncating:
            def __trunc__(self):
                return self
        Text = type('Text', (str,), {'__getattr__': lambda self, name: lambda: 5})
        self.assertEqual((int(Lookup()), long(Lookup()), int(Text('12')), long(u'12')), (7, 7L, 5, 12L))
        self.assertEqual(log, ['__trunc__', '__trunc__'])
        self.assertEqual(_error(long, int), ('TypeError', ("descriptor '__trunc__' of 'int' object needs an argument",)))
        self.assertEqual(_error(int, Truncating()), ('TypeError', ('__trunc__ returned non-Integral (type Truncating)',)))

    def test_conversions_keep_returned_subclasses(self):
        Integer = type('Integer', (int,), {})
        Long = type('Long', (long,), {})
        Real = type('Real', (float,), {})
        Converting = type('Converting', (object,), {'__int__': lambda self: Integer(5), '__long__': lambda self: Long(6), '__float__': lambda self: Real(1.5)})

        class Classic:
            def __int__(self):
                return Integer(5)

            def __long__(self):
                return Long(6)

            def __float__(self):
                return Real(1.5)
        for value in (Converting(), Classic()):
            self.assertEqual([type(convert(value)) for convert in (int, long, float)], [Integer, Long, Real])
        Truncating = type('Truncating', (object,), {'__trunc__': lambda self: True})
        self.assertEqual((type(int(Truncating())), type(Integer(Truncating())), Integer(Truncating())), (bool, Integer, 1))
        self.assertIs(type(long(type('Big', (object,), {'__trunc__': lambda self: Long(2)})())), Long)
        real, imaginary = 1.5, 2j
        self.assertEqual((float(real) is real, complex(imaginary) is imaginary), (True, True))

    def test_int_subclass_construction_takes_a_c_long(self):
        Integer = type('Integer', (int,), {})
        for argument in (2 ** 64, '99999999999999999999', -2 ** 63 - 1):
            self.assertEqual(_error(Integer, argument), ('OverflowError', ('Python int too large to convert to C long',)))
        self.assertEqual((Integer(2 ** 63 - 1), type(Integer(5L))), (2 ** 63 - 1, Integer))

    def test_conversion_result_messages(self):
        Real = type('Real', (object,), {'__init__': lambda self, value: setattr(self, 'value', value), '__float__': lambda self: self.value})
        Complex = type('Complex', (object,), {'__init__': lambda self, value: setattr(self, 'value', value), '__complex__': lambda self: self.value})

        class Classic:
            def __init__(self, value):
                self.value = value

            def __float__(self):
                return self.value
        for value in (True, None, 3, 2j):
            message = ('TypeError', ('__float__ returned non-float (type %s)' % type(value).__name__,))
            for convert in (float, complex):
                self.assertEqual(_error(convert, Real(value)), message)
                self.assertEqual(_error(convert, Classic(value)), message)
        self.assertEqual((complex(1, Real(True)), complex(1, Real(3L)), complex(Real(2.5), Real(0.5))), (1 + 1j, 1 + 3j, 2.5 + 0.5j))
        self.assertEqual((complex(Complex(True)), complex(Complex(3)), complex(Complex(Real(1.5)))), (1 + 0j, 3 + 0j, 1.5 + 0j))
        self.assertEqual(_error(complex, Complex(None)), ('TypeError', ('complex() argument must be a string or a number',)))
        self.assertEqual(_error(long, type('Long', (object,), {'__long__': lambda self: 1.5})()), ('TypeError', ('__long__ returned non-long (type float)',)))
        self.assertEqual((repr(complex(1, -0.0)), repr(complex(1j, -0.0)), repr(complex(-0.0, 1j))), ('(1-0j)', '1j', '(-1+0j)'))

    def test_unicode_literals_turn_spaces_into_blanks(self):
        self.assertEqual(_error(int, u'\r\n\x1c\x85'), ('ValueError', ("invalid literal for int() with base 10: ''",)))
        self.assertEqual(_error(float, u'a\tb'), ('ValueError', ('could not convert string to float: a b',)))
        self.assertEqual(_error(long, u'\r\n\x1c\x85', 36), ('ValueError', ("invalid literal for long() with base 36: '    '",)))
        self.assertEqual((int(u'\x1c12\u2003'), float(u'\t1.5\x1f')), (12, 1.5))

    def test_base_zero_literals_beyond_a_c_long(self):
        self.assertEqual((int('0xcb090AAe7bF5cb13fL', 0), int(' 0x8000000000000000L ', 0), int('0' + '7' * 25 + 'l', 0)), (0xcb090AAe7bF5cb13fL, 2 ** 63, int('7' * 25, 8)))
        self.assertEqual(_error(int, '0x123456789abcdef0123zz', 0), ('ValueError', ("invalid literal for long() with base 16: '0x123456789abcdef0123zz'",)))
        for text in ('0x7fffffffffffffffL', '-0x8000000000000001L', '99999999999999999999L'):
            self.assertEqual(_error(int, text, 0), ('ValueError', ('invalid literal for int() with base 0: %r' % text,)))


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
