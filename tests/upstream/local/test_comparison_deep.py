"""Project-authored Python 2 comparison, hash and truth protocol witnesses."""

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
