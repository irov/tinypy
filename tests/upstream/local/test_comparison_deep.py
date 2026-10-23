"""Project-authored Python 2 comparison, hash and truth protocol witnesses."""

import sys
import unittest
import _weakref as weakref


class ComparisonDeep(unittest.TestCase):
    def test_cmp_calls_new_style_method_once(self):
        events = []
        class Compared(object):
            def __cmp__(self, other):
                events.append('cmp')
                return -7
        self.assertEqual(cmp(Compared(), Compared()), -1)
        self.assertEqual(events, ['cmp'])

    def test_cmp_calls_classic_method_once(self):
        events = []
        class Compared:
            def __cmp__(self, other):
                events.append('cmp')
                return 7
        self.assertEqual(cmp(Compared(), Compared()), 1)
        self.assertEqual(events, ['cmp'])

    def test_cmp_same_type_precedes_rich_comparison(self):
        events = []
        class Compared(object):
            def __cmp__(self, other):
                events.append('cmp')
                return -1
            def __eq__(self, other):
                events.append('eq')
                return True
        left, right = Compared(), Compared()
        self.assertEqual(cmp(left, right), -1)
        self.assertEqual(events, ['cmp'])
        self.assertTrue(left == right)
        self.assertEqual(events, ['cmp', 'eq'])

    def test_cmp_integer_subtype_uses_stored_value(self):
        events = []
        class Number(int):
            def __eq__(self, other):
                events.append('eq')
                return True
        self.assertEqual(cmp(Number(1), Number(2)), -1)
        self.assertEqual(events, [])
        self.assertTrue(Number(1) == Number(2))
        self.assertEqual(events, ['eq'])

    def test_cmp_identity_avoids_callbacks(self):
        class Compared(object):
            def __cmp__(self, other):
                raise LookupError('unreachable')
        value = Compared()
        self.assertEqual(cmp(value, value), 0)

    def test_cmp_float_result_uses_integer_conversion(self):
        class New(object):
            def __cmp__(self, other):
                return -1.75
        class Old:
            def __cmp__(self, other):
                return -1.75
        for factory in (New, Old):
            self.assertEqual(cmp(factory(), factory()), -1)
            self.assertTrue(factory() < factory())

    def test_cmp_result_conversion_invoked_once(self):
        events = []
        class Number(object):
            def __int__(self):
                events.append('int')
                return 3
        class Compared(object):
            def __cmp__(self, other):
                events.append('cmp')
                return Number()
        self.assertEqual(cmp(Compared(), Compared()), 1)
        self.assertEqual(events, ['cmp', 'int'])

    def test_cmp_result_overflow_differs_for_classic_instances(self):
        class New(object):
            def __cmp__(self, other):
                return 1L << 100
        class Old:
            def __cmp__(self, other):
                return 1L << 100
        self.assertRaises(OverflowError, cmp, New(), New())
        self.assertRaises(TypeError, cmp, Old(), Old())

    def test_cmp_conversion_failure_preserves_new_style_exception(self):
        class Number(object):
            def __int__(self):
                raise LookupError('conversion failed')
        class New(object):
            def __cmp__(self, other):
                return Number()
        class Old:
            def __cmp__(self, other):
                return Number()
        self.assertRaises(LookupError, cmp, New(), New())
        self.assertRaises(TypeError, cmp, Old(), Old())

    def test_cmp_method_failure_is_preserved_for_both_styles(self):
        class New(object):
            def __cmp__(self, other):
                raise LookupError('method failed')
        class Old:
            def __cmp__(self, other):
                raise LookupError('method failed')
        for factory in (New, Old):
            self.assertRaises(LookupError, cmp, factory(), factory())

    def test_rich_not_implemented_retries_same_type_slot(self):
        events = []
        class Compared(object):
            def __eq__(self, other):
                events.append('eq')
                return NotImplemented
            def __ne__(self, other):
                events.append('ne')
                return NotImplemented
        self.assertFalse(Compared() == Compared())
        self.assertEqual(events, ['eq'] * 6)
        events[:] = []
        self.assertTrue(Compared() != Compared())
        self.assertEqual(events, ['ne'] * 6)

    def test_rich_not_implemented_retries_classic_slot(self):
        events = []
        class Compared:
            def __eq__(self, other):
                events.append('eq')
                return NotImplemented
        self.assertFalse(Compared() == Compared())
        self.assertEqual(events, ['eq'] * 4)

    def test_rich_reflection_retries_strict_subtype(self):
        events = []
        class Base(object):
            def __lt__(self, other):
                events.append('base.lt')
                return NotImplemented
        class Sub(Base):
            def __gt__(self, other):
                events.append('sub.gt')
                return NotImplemented
        Base() < Sub()
        self.assertEqual(events, ['sub.gt', 'base.lt', 'base.lt', 'sub.gt', 'sub.gt', 'base.lt'])

    def test_rich_subtype_result_precedes_base(self):
        result = object()
        events = []
        class Base(object):
            def __lt__(self, other):
                events.append('base')
                return False
        class Sub(Base):
            def __gt__(self, other):
                events.append('sub')
                return result
        self.assertIs(Base() < Sub(), result)
        self.assertEqual(events, ['sub'])

    def test_new_style_rich_lookup_error_becomes_not_implemented(self):
        events = []
        class Failed(object):
            def __get__(self, instance, owner):
                events.append('get')
                raise ValueError('lookup failed')
        class Compared(object):
            __eq__ = Failed()
        self.assertFalse(Compared() == Compared())
        self.assertEqual(events, ['get'] * 6)
        self.assertEqual(1 + 2, 3)

    def test_rich_method_exception_is_preserved(self):
        class Compared(object):
            def __eq__(self, other):
                raise ValueError('comparison failed')
        with self.assertRaises(ValueError):
            Compared() == Compared()

    def test_sequence_order_returns_child_rich_result(self):
        result = object()
        events = []
        class Compared(object):
            def __eq__(self, other):
                events.append('eq')
                return False
            def __lt__(self, other):
                events.append('lt')
                return result
        for factory in (list, tuple):
            events[:] = []
            self.assertIs(factory([Compared()]) < factory([Compared()]), result)
            self.assertEqual(events, ['eq', 'lt'])

    def test_sequence_direct_method_returns_child_rich_result(self):
        result = object()
        class Compared(object):
            def __eq__(self, other):
                return False
            def __ge__(self, other):
                return result
        for factory in (list, tuple):
            self.assertIs(factory([Compared()]).__ge__(factory([Compared()])), result)

    def test_list_order_uses_entries_after_equality_callback(self):
        events = []
        left, right = [], []
        class Compared(object):
            def __eq__(self, other):
                events.append('eq')
                left[0], right[0] = 2, 1
                return False
            def __lt__(self, other):
                events.append('lt')
                return True
        originals = [Compared(), Compared()]
        left[:] = originals[:1]
        right[:] = originals[1:]
        self.assertFalse(left < right)
        self.assertEqual(events, ['eq'])
        self.assertEqual((left, right), ([2], [1]))

    def test_list_order_uses_sizes_after_equality_callback(self):
        events = []
        left, right = [], []
        class Compared(object):
            def __eq__(self, other):
                events.append('eq')
                left[:] = []
                return False
            def __lt__(self, other):
                events.append('lt')
                return False
        originals = [Compared(), Compared()]
        left[:] = originals[:1]
        right[:] = originals[1:]
        self.assertTrue(left < right)
        self.assertEqual(events, ['eq'])

    def test_sequence_membership_identity_avoids_equality(self):
        class Compared(object):
            def __eq__(self, other):
                raise LookupError('unreachable')
        value = Compared()
        self.assertTrue(value in [value])
        self.assertTrue([value] == [value])

    def test_builtin_rich_methods_accept_subtypes(self):
        class Sequence(list):
            pass
        class Number(float):
            pass
        self.assertIs([1].__eq__(Sequence([1])), True)
        self.assertIs((1.0).__eq__(Number(1)), True)
        self.assertIs(Number(1).__lt__(2), True)

    def test_builtin_rich_methods_decline_unrelated_types(self):
        self.assertIs([].__eq__(()), NotImplemented)
        self.assertIs(().__eq__([]), NotImplemented)
        self.assertIs({}.__eq__([]), NotImplemented)
        self.assertIs('a'.__eq__(1), NotImplemented)
        self.assertIs((1.0).__eq__('1'), NotImplemented)

    def test_buffer_and_byte_string_use_type_ordering(self):
        value = buffer('a')
        self.assertFalse(value == 'a')
        self.assertFalse('a' == value)
        self.assertEqual(cmp(value, 'a'), -1)
        self.assertEqual(cmp('a', value), 1)
        self.assertTrue(value < 'a')
        self.assertIs('a'.__eq__(value), NotImplemented)

    def test_unicode_comparison_decodes_buffer(self):
        value = buffer('a')
        self.assertTrue(value == u'a')
        self.assertTrue(u'a' == value)
        self.assertEqual(cmp(value, u'a'), 0)
        self.assertTrue(value < u'b')
        self.assertIs(u'a'.__eq__(value), True)
        failed = buffer('\xe9')
        with self.assertRaises(UnicodeDecodeError):
            failed < u'a'

    def test_new_style_hash_accepts_float_and_integer_conversion(self):
        events = []
        class Number(object):
            def __int__(self):
                events.append('int')
                return 17
        class FloatHash(object):
            def __hash__(self):
                return 3.75
        class ConvertedHash(object):
            def __hash__(self):
                events.append('hash')
                return Number()
        self.assertEqual(hash(FloatHash()), 3)
        self.assertEqual(hash(ConvertedHash()), 17)
        self.assertEqual(events, ['hash', 'int'])

    def test_classic_hash_requires_integer_result(self):
        class Number(object):
            def __int__(self):
                return 17
        class FloatHash:
            def __hash__(self):
                return 3.75
        class ConvertedHash:
            def __hash__(self):
                return Number()
        self.assertRaises(TypeError, hash, FloatHash())
        self.assertRaises(TypeError, hash, ConvertedHash())

    def test_hash_result_subtype_override_depends_on_instance_style(self):
        events = []
        class Integer(int):
            def __hash__(self):
                events.append('integer.hash')
                return 23
        class Wide(long):
            def __hash__(self):
                events.append('long.hash')
                return 29
        for result, expected in ((Integer(7), 23), (Wide(11), 29)):
            class New(object):
                def __hash__(self):
                    return result
            class Old:
                def __hash__(self):
                    return result
            events[:] = []
            self.assertEqual(hash(New()), int(result))
            self.assertEqual(events, [])
            self.assertEqual(hash(Old()), expected)
            self.assertEqual(len(events), 1)

    def test_hash_result_minus_one_is_normalized(self):
        class New(object):
            def __hash__(self):
                return -1
        class Old:
            def __hash__(self):
                return -1
        self.assertEqual(hash(New()), -2)
        self.assertEqual(hash(Old()), -2)

    def test_hash_return_conversion_error_is_preserved(self):
        class Number(object):
            def __int__(self):
                raise LookupError('conversion failed')
        class Hashed(object):
            def __hash__(self):
                return Number()
        self.assertRaises(LookupError, hash, Hashed())

    def test_hash_return_temporary_is_released(self):
        references = []
        class Number(object):
            def __int__(self):
                return 5
        class Hashed(object):
            def __hash__(self):
                result = Number()
                references.append(weakref.ref(result))
                return result
        self.assertEqual(hash(Hashed()), 5)
        self.assertIs(references[0](), None)

    def test_classic_nonzero_rejects_negative_integer(self):
        class New(object):
            def __nonzero__(self):
                return -1
        class Old:
            def __nonzero__(self):
                return -1
        self.assertTrue(bool(New()))
        self.assertRaises(ValueError, bool, Old())

    def test_classic_length_rejects_long_result(self):
        class New(object):
            def __len__(self):
                return 1L
        class Old:
            def __len__(self):
                return 1L
        self.assertEqual(len(New()), 1)
        self.assertTrue(bool(New()))
        self.assertRaises(TypeError, len, Old())
        self.assertRaises(TypeError, bool, Old())

    def test_new_style_length_accepts_float_conversion(self):
        class Sized(object):
            def __len__(self):
                return 1.75
        self.assertEqual(len(Sized()), 1)
        self.assertTrue(bool(Sized()))

    def test_numeric_inherited_nonzero_precedes_added_length(self):
        events = []
        for base in (int, long, float, complex):
            class Number(base):
                def __len__(self):
                    events.append('len')
                    return 0
            self.assertTrue(bool(Number(1)))
            self.assertFalse(bool(Number(0)))
            self.assertEqual(events, [])

    def test_new_style_length_overflow_is_reported_for_truth(self):
        class Sized(object):
            def __len__(self):
                return 1L << 100
        self.assertRaises(OverflowError, len, Sized())
        self.assertRaises(OverflowError, bool, Sized())

    def test_set_cmp_identity_and_cross_kind_relationships(self):
        value = set([1])
        self.assertEqual(cmp(value, value), 0)
        self.assertEqual(cmp(set([1]), frozenset([1])), 0)
        self.assertEqual(cmp(set([1]), frozenset([1, 2])), -1)
        self.assertRaises(TypeError, cmp, set([1]), set([1]))
        self.assertRaises(TypeError, cmp, set([1]), frozenset([2]))

    def test_builtin_subclasses_compare_through_the_inherited_slot(self):
        # A subclass keeping the comparisons of its built-in base compares
        # through the slot it inherits, so nested values run out of the
        # recursion budget in cmp rather than in a call.
        class List(list):
            pass
        class Tuple(tuple):
            pass
        class Dict(dict):
            pass
        messages = []
        for wrap in (lambda item: List([item]), lambda item: Tuple((item,)), lambda item: Dict({1: item})):
            left = right = 0
            for index in range(sys.getrecursionlimit() + 100):
                left = wrap(left)
                right = wrap(right)
            for compare in (lambda: left == right, lambda: left < right):
                try:
                    compare()
                except RuntimeError as error:
                    messages.append(str(error))
        self.assertEqual(messages, ['maximum recursion depth exceeded in cmp'] * 6)

    def test_metaclass_cmp_decides_class_comparisons(self):
        class Ordered(type):
            def __cmp__(cls, other):
                return cmp(getattr(cls, 'rank', 0), getattr(other, 'rank', 0))
        first = Ordered('First', (object,), {'rank': 2})
        second = Ordered('Second', (object,), {'rank': 1})
        third = Ordered('Third', (object,), {'rank': 2})
        self.assertTrue(first == third)
        self.assertFalse(first != third)
        self.assertTrue(second < first)
        self.assertTrue(first >= third)
        self.assertTrue(third in [first])
        self.assertEqual([item.__name__ for item in sorted([first, second])], ['Second', 'First'])
        self.assertEqual(cmp(first, second), 1)
        self.assertIs(type.__eq__(first, third), NotImplemented)
        self.assertFalse(first == int)
        self.assertTrue(first > int)
        self.assertTrue(int < first)
        class Plain(type):
            pass
        alpha = Plain('Alpha', (object,), {})
        beta = Plain('Alpha', (object,), {})
        self.assertFalse(alpha == beta)
        self.assertTrue(alpha != beta)
        self.assertIs(type.__eq__(alpha, alpha), True)

    def test_foreign_rich_comparison_wrapper_keeps_the_inherited_comparison(self):
        class Items(list):
            __eq__ = str.__eq__
        left, right = Items([1, 2]), Items([1, 2])
        self.assertTrue(left == right)
        self.assertFalse(left != right)
        self.assertTrue(left in [right])
        self.assertEqual(cmp(left, right), 0)
        self.assertTrue(Items([1]) < Items([2]))
        class Pair(tuple):
            __eq__ = str.__eq__
        self.assertTrue(Pair((1,)) == Pair((1,)))
        class Values(set):
            __eq__ = float.__eq__
        values = Values([1])
        self.assertTrue(values == values)
        self.assertTrue(values == Values([1]))
        self.assertTrue(values == set([1]))
        class Frozen(frozenset):
            __ne__ = set.__ne__
        self.assertFalse(Frozen([1]) != Frozen([1]))
        self.assertRaises(TypeError, lambda: Items([1]).__eq__(Items([1])))

    def test_cell_and_code_order_compare_their_contents_three_way(self):
        calls = []
        class Ordered(object):
            def __init__(self, value):
                self.value = value
            def __eq__(self, other):
                return True
            def __cmp__(self, other):
                calls.append(self.value)
                return cmp(self.value, other.value)
        def cell(value):
            def read():
                return value
            return read.__closure__[0]
        first, second = cell(Ordered(1)), cell(Ordered(2))
        self.assertEqual((type(first).__cmp__(first, second), cmp(first, second), first < second, first == second, second > first), (-1, -1, True, False, True))
        self.assertEqual(calls, [1, 1, 1, 1, 2])
        def template():
            return 1
        source = template.func_code
        def code(constants):
            return type(source)(source.co_argcount, source.co_nlocals, source.co_stacksize, source.co_flags, source.co_code, constants, source.co_names, source.co_varnames, source.co_filename, source.co_name, source.co_firstlineno, source.co_lnotab)
        del calls[:]
        left, right = code((None, Ordered(1))), code((None, Ordered(2)))
        self.assertEqual((left == right, cmp(left, right), left < right), (False, 0, False))
        shared = Ordered(3)
        class Text(str):
            pass
        self.assertEqual((code((None, shared)) == code((None, shared)), code((None, Text('a'))) == code((None, Text('a'))), code((None, (1, 'a'))) == code((None, (1, 'a')))), (True, False, True))
        self.assertEqual((code((None, 0.0)) == code((None, -0.0)), code((None, 1)) == code((None, 1L)), code((None, frozenset([1, 2]))) == code((None, frozenset([2, 1])))), (False, False, True))

    def test_code_equality_compares_fields_in_the_order_of_code_richcompare(self):
        def template():
            return 1
        source = template.func_code
        class Name(str):
            __hash__ = str.__hash__
            def __eq__(self, other):
                raise ValueError('name')
        class Constant(object):
            def __eq__(self, other):
                raise ValueError('constant')
        def code(argcount, name, constants, names):
            return type(source)(argcount, argcount, source.co_stacksize, source.co_flags, source.co_code, constants, names, ('a',)[:argcount], 'code.py', name, 1, source.co_lnotab)
        cases = (
            (lambda: code(0, Name('f'), (None,), ()) == code(1, Name('f'), (None,), ()), 'name'),
            (lambda: code(0, 'f', (Constant(),), ('a',)) == code(0, 'f', (Constant(),), ('b',)), 'constant'),
        )
        for function, message in cases:
            with self.assertRaises(ValueError) as caught:
                function()
            self.assertEqual(str(caught.exception), message)
        self.assertFalse(code(0, 'f', (Constant(),), ()) == code(1, 'f', (Constant(),), ()))
        self.assertFalse(code(0, 'f', (Constant(),), ()) == code(0, 'g', (Constant(),), ()))

    def test_code_equality_compares_nested_constants_by_their_keys(self):
        def template():
            return 1
        source = template.func_code
        def code(constants):
            return type(source)(source.co_argcount, source.co_nlocals, source.co_stacksize, source.co_flags, source.co_code, constants, source.co_names, source.co_varnames, source.co_filename, source.co_name, source.co_firstlineno, source.co_lnotab)
        def nested(leaf):
            current = code((None, leaf))
            for level in range(8):
                current = code((None, (level, current)))
            return current
        def deep(container, leaf):
            for level in range(1200):
                leaf = container((leaf, level))
            return leaf
        self.assertEqual((nested(1) == nested(1), nested(1) == nested(1.0), nested(0.0) == nested(-0.0), nested((1, 'a')) == nested((1, 'a'))), (True, False, False, True))
        self.assertEqual((code((None, frozenset([(1, 2.0), 'a']))) == code((None, frozenset(['a', (1, 2.0)]))), code((None, frozenset([(1, 2)]))) == code((None, frozenset([(1, 2.0)])))), (True, False))
        self.assertRaises(RuntimeError, lambda: code((None, deep(tuple, 1))) == code((None, deep(tuple, 1))))
        self.assertFalse(code((None, deep(frozenset, 1))) == code((None, deep(frozenset, 2))))
