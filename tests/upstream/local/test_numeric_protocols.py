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
