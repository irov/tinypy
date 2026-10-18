"""Project-authored Python 2 container callback and boundary matrices."""

import unittest


def _error(operation):
    try:
        operation()
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError('operation must raise')


class ContainerDeep(unittest.TestCase):
    def test_dict_setdefault_hashes_missing_key_once(self):
        events = []
        class Key(object):
            def __hash__(self):
                events.append('hash')
                if len(events) != 1:
                    raise ValueError('repeated hash')
                return 1
        target = {}
        self.assertEqual(target.setdefault(Key(), 7), 7)
        self.assertEqual(events, ['hash'])
        self.assertEqual(target.values(), [7])

    def test_dict_setdefault_compares_collision_once(self):
        events = []
        class Key(object):
            def __hash__(self):
                return 1
            def __eq__(self, other):
                events.append('equal')
                return False
        target = {Key(): 2}
        self.assertEqual(target.setdefault(Key(), 7), 7)
        self.assertEqual(events, ['equal'])
        self.assertEqual(sorted(target.values()), [2, 7])

    def test_dict_setdefault_hash_callback_can_mutate_dictionary(self):
        target = {}
        class Key(object):
            def __hash__(self):
                target['side'] = 9
                return 1
        value = []
        self.assertIs(target.setdefault(Key(), value), value)
        self.assertEqual(target['side'], 9)
        self.assertEqual(len(target), 2)

    def test_dict_setdefault_collision_callback_can_clear_dictionary(self):
        target = {}
        class Key(object):
            def __hash__(self):
                return 1
            def __eq__(self, other):
                target.clear()
                return False
        target[Key()] = 2
        self.assertEqual(target.setdefault(Key(), 7), 7)
        self.assertEqual(target.values(), [7])

    def test_mutation_methods_accept_int_not_index(self):
        class Integer(object):
            def __int__(self):
                return 1
        class Index(object):
            def __index__(self):
                return 1
        for factory in (list, bytearray):
            target = factory([4, 7])
            target.insert(Integer(), 9)
            self.assertEqual(list(target), [4, 9, 7])
            self.assertEqual(target.pop(Integer()), 9)
            self.assertRaises(TypeError, target.insert, Index(), 9)
            self.assertRaises(TypeError, target.pop, Index())
            self.assertRaises(TypeError, factory().pop, Index())

    def test_mutation_methods_integer_callback_errors_precede_empty_check(self):
        class Failed(object):
            def __int__(self):
                raise LookupError('integer failed')
        class Invalid(object):
            def __int__(self):
                return '1'
        for factory in (list, bytearray):
            for items in ([], [4, 7]):
                target = factory(items)
                self.assertRaises(LookupError, target.pop, Failed())
                self.assertRaises(TypeError, target.pop, Invalid())
                self.assertRaises(TypeError, target.pop, '1')
                self.assertEqual(list(target), items)

    def test_mutation_methods_integer_overflow(self):
        for factory in (list, bytearray):
            for items in ([], [4, 7]):
                for index in (1L << 100, -(1L << 100)):
                    target = factory(items)
                    self.assertRaises(OverflowError, target.pop, index)
                    self.assertRaises(OverflowError, target.insert, index, 9)
                    self.assertEqual(list(target), items)

    def test_mutation_methods_integer_subclass_conversion(self):
        class Int(int):
            def __int__(self):
                raise AssertionError('subclass override')
        class Long(long):
            def __int__(self):
                return 0
        for factory in (list, bytearray):
            target = factory([4, 7])
            target.insert(Int(1), 9)
            self.assertEqual(target.pop(Int(1)), 9)
            self.assertEqual(list(target), [4, 7])
            target.insert(Long(1), 9)
            self.assertEqual(list(target), [9, 4, 7])
            self.assertEqual(target.pop(Long(1)), 9)

    def test_list_insert_observes_integer_callback_mutation(self):
        target = [1, 2]
        class Index(object):
            def __int__(self):
                target[:] = [4, 7, 8]
                return -1
        target.insert(Index(), 9)
        self.assertEqual(target, [4, 7, 9, 8])

    def test_list_pop_observes_integer_callback_mutation(self):
        target = []
        class Index(object):
            def __int__(self):
                target.extend([4, 7])
                return -1
        self.assertEqual(target.pop(Index()), 7)
        self.assertEqual(target, [4])

    def test_list_index_reloads_length_after_start_callback(self):
        target = [1]
        class Start(object):
            def __index__(self):
                target.extend([3, 5])
                return -1
        self.assertEqual(target.index(5, Start(), 100), 2)

    def test_list_index_explicit_stop_allows_growth(self):
        target = [1]
        class Start(object):
            def __index__(self):
                target.extend([3, 5])
                return 0
        self.assertEqual(target.index(5, Start(), 100), 2)

    def test_list_index_omitted_stop_retains_original_bound(self):
        target = [1]
        class Start(object):
            def __index__(self):
                target.extend([3, 5])
                return 0
        self.assertRaises(ValueError, target.index, 5, Start())
        self.assertEqual(target, [1, 3, 5])

    def test_list_index_negative_stop_uses_current_length(self):
        target = [1]
        class Stop(object):
            def __index__(self):
                target.extend([3, 5])
                return -1
        self.assertEqual(target.index(3, 0, Stop()), 1)

    def test_sort_reverse_uses_integer_conversion(self):
        events = []
        class Reverse(object):
            def __int__(self):
                events.append('int')
                return -2
            def __nonzero__(self):
                raise AssertionError('truth must not run')
        target = [1, 3, 2]
        target.sort(reverse=Reverse())
        self.assertEqual(target, [3, 2, 1])
        self.assertEqual(events, ['int'])

    def test_sort_reverse_rejects_invalid_protocols(self):
        class Index(object):
            def __index__(self):
                return 1
        for reverse in (None, [], 'yes', 0.5, Index()):
            target = [1, 3, 2]
            self.assertRaises(TypeError, target.sort, reverse=reverse)
            self.assertEqual(target, [1, 3, 2])

    def test_sort_reverse_c_int_overflow(self):
        for reverse in (1L << 40, -(1L << 40), 1L << 100):
            target = [1, 3, 2]
            self.assertRaises(OverflowError, target.sort, reverse=reverse)
            self.assertEqual(target, [1, 3, 2])

    def test_bytearray_item_range_and_exact_string_errors(self):
        class Text(str):
            pass
        for value in (-1, 256, 1L << 100, -(1L << 100), '', 'ab'):
            target = bytearray([4, 7])
            self.assertRaises(ValueError, target.append, value)
            self.assertRaises(ValueError, target.insert, 0, value)
            self.assertRaises(ValueError, target.__setitem__, 0, value)
            self.assertEqual(list(target), [4, 7])
        target = bytearray([4, 7])
        self.assertRaises(TypeError, target.append, Text('a'))
        target.append('a')
        self.assertEqual(list(target), [4, 7, 97])

    def test_bytearray_item_index_callback_error_is_preserved(self):
        class Failed(object):
            def __index__(self):
                raise OverflowError('callback failed')
        class Huge(object):
            def __index__(self):
                return 1L << 100
        target = bytearray([4])
        with self.assertRaises(OverflowError) as failure:
            target.append(Failed())
        self.assertEqual(failure.exception.args, ('callback failed',))
        self.assertRaises(ValueError, target.append, Huge())
        self.assertEqual(list(target), [4])

    def test_set_isdisjoint_stops_on_first_match(self):
        for factory in (set, frozenset):
            events = []
            def source():
                events.append(1)
                yield 1
                events.append(9)
                raise ValueError('unreachable tail')
            self.assertFalse(factory([1]).isdisjoint(source()))
            self.assertEqual(events, [1])

    def test_set_isdisjoint_uses_subclass_iterator(self):
        for factory in (set, frozenset):
            class Sub(factory):
                def __iter__(self):
                    return iter([9])
            self.assertTrue(factory([1]).isdisjoint(Sub([1])))
            self.assertFalse(factory([9]).isdisjoint(Sub([1])))

    def test_set_isdisjoint_same_subclass_ignores_iterator(self):
        for factory in (set, frozenset):
            class Sub(factory):
                def __iter__(self):
                    raise AssertionError('self iterator')
            value = Sub([1])
            self.assertFalse(value.isdisjoint(value))
            value = Sub()
            self.assertTrue(value.isdisjoint(value))

    def test_set_isdisjoint_does_not_hash_unreachable_tail(self):
        for factory in (set, frozenset):
            self.assertFalse(factory([1]).isdisjoint([1, []]))
            self.assertRaises(TypeError, factory([1]).isdisjoint, [2, []])

    def test_set_isdisjoint_hashes_each_visited_item(self):
        events = []
        class Key(object):
            def __hash__(self):
                events.append('hash')
                return 3
        key = Key()
        self.assertTrue(set().isdisjoint([key, key]))
        self.assertEqual(events, ['hash', 'hash'])

    def test_set_isdisjoint_compares_keys_in_larger_set(self):
        events = []
        class Key(object):
            def __init__(self, name):
                self.name = name
            def __hash__(self):
                return 1
            def __eq__(self, other):
                events.append((self.name, other.name))
                return False
        larger = set([Key('large'), 8])
        smaller = set([Key('small')])
        self.assertTrue(larger.isdisjoint(smaller))
        self.assertEqual(events, [('large', 'small')])

    def test_set_difference_update_does_not_convert_unhashable_set_items(self):
        target = set([frozenset([1]), 2])
        self.assertRaises(TypeError, target.difference_update, [set([1])])
        self.assertEqual(target, set([frozenset([1]), 2]))
        target.discard(set([1]))
        self.assertEqual(target, set([2]))

    def test_set_intersection_update_is_atomic_across_arguments(self):
        target = set([1, 2, 3])
        def failed():
            yield 2
            raise ValueError('second iterable failed')
        self.assertRaises(ValueError, target.intersection_update, [1, 2], failed())
        self.assertEqual(target, set([1, 2, 3]))

    def test_set_intersection_update_callback_sees_original(self):
        target = set([1, 2, 3])
        seen = []
        def source():
            seen.append(sorted(target))
            yield 2
        target.intersection_update([1, 2], source())
        self.assertEqual(seen, [[1, 2, 3]])
        self.assertEqual(target, set([2]))

    def test_builtin_iterator_exhaustion_is_sticky(self):
        for factory in (list, bytearray):
            target = factory([1])
            iterator = iter(target)
            self.assertEqual(next(iterator), 1)
            self.assertRaises(StopIteration, next, iterator)
            target.append(2)
            self.assertRaises(StopIteration, next, iterator)

    def test_list_self_subclass_extension_snapshots_before_appending(self):
        class Sub(list):
            pass
        for operation in (list.extend, list.__iadd__):
            target = Sub([1, 4])
            operation(target, target)
            self.assertEqual(target, [1, 4, 1, 4])

    def test_list_self_subclass_extension_is_atomic_on_iterator_failure(self):
        class Sub(list):
            def __iter__(self):
                yield 4
                raise ValueError('source failed')
        for operation in (list.extend, list.__iadd__):
            target = Sub([1])
            self.assertRaises(ValueError, operation, target, target)
            self.assertEqual(target, [1])

    def test_sequence_direct_multiply_accepts_index_protocol(self):
        class Index(object):
            def __index__(self):
                return 2
        class Reflected(object):
            def __rmul__(self, other):
                return 'reflected'
            def __index__(self):
                raise ValueError('direct index')
        for value in ('a', u'a', [4], (4,), bytearray('a')):
            self.assertEqual(value.__mul__(Index()), value * 2)
            self.assertEqual(value.__rmul__(Index()), value * 2)
        for value in ('a', u'a', [4], (4,)):
            self.assertEqual(value * Reflected(), 'reflected')
            self.assertRaises(ValueError, value.__mul__, Reflected())

    def test_sequence_direct_multiply_rejects_int_only_protocol(self):
        class Integer(object):
            def __int__(self):
                return 2
        for value in ('a', u'a', [4], (4,), bytearray('a')):
            self.assertRaises(TypeError, value.__mul__, Integer())
            self.assertRaises(TypeError, value.__rmul__, Integer())
        for value in ('a', u'a', [4], (4,)):
            for method in (value.__mul__, value.__rmul__):
                for multiplier in ('x', []):
                    try:
                        method(multiplier)
                    except TypeError as error:
                        self.assertEqual(str(error), "'%s' object cannot be interpreted as an index" % type(multiplier).__name__)
                    else:
                        self.fail('invalid direct multiplier accepted')
            try:
                value * 'x'
            except TypeError as error:
                self.assertEqual(str(error), "can't multiply sequence by non-int of type 'str'")
            else:
                self.fail('invalid operator multiplier accepted')

    def test_reversed_list_subclass_uses_stored_elements_and_size(self):
        class Sub(list):
            def __len__(self):
                raise AssertionError('length override')
            def __getitem__(self, index):
                raise AssertionError('getitem override')
        self.assertEqual(list(reversed(Sub([1, 4, 7]))), [7, 4, 1])

    def test_reversed_tuple_subclass_uses_sequence_overrides(self):
        events = []
        class Sub(tuple):
            def __len__(self):
                events.append('len')
                return 2
            def __getitem__(self, index):
                events.append(index)
                return index + 4
        iterator = reversed(Sub([1, 2, 3, 4]))
        self.assertEqual(next(iterator), 5)
        self.assertEqual(next(iterator), 4)
        self.assertRaises(StopIteration, next, iterator)
        self.assertEqual(events, ['len', 1, 0])

    def test_reversed_iterator_exhausts_after_arbitrary_item_error(self):
        events = []
        class Sequence(object):
            def __len__(self):
                return 2
            def __getitem__(self, index):
                events.append(index)
                raise ValueError('failed')
        iterator = reversed(Sequence())
        self.assertRaises(ValueError, next, iterator)
        self.assertRaises(StopIteration, next, iterator)
        self.assertEqual(events, [1])

    def test_reversed_iterator_type_and_subclassability(self):
        iterator = reversed([1])
        self.assertEqual(type(iterator).__name__, 'listreverseiterator')
        self.assertRaises(TypeError, type, 'Sub', (type(iterator),), {})
        for sequence in ((1,), 'a', u'a', bytearray('a')):
            self.assertEqual(type(reversed(sequence)).__name__, 'reversed')
        class Sub(reversed):
            pass
        self.assertIs(type(Sub((1, 2))), Sub)
        self.assertEqual(list(Sub((1, 2))), [2, 1])

    def test_reversed_list_iterator_length_hint_and_sticky_exhaustion(self):
        target = [1, 4, 7]
        iterator = reversed(target)
        self.assertEqual(iterator.__length_hint__(), 3)
        self.assertEqual(next(iterator), 7)
        self.assertEqual(iterator.__length_hint__(), 2)
        target[:] = [1]
        self.assertEqual(iterator.__length_hint__(), 0)
        self.assertRaises(StopIteration, next, iterator)
        target.extend([4, 7])
        self.assertRaises(StopIteration, next, iterator)

    def test_reversed_xrange_iterator_type_and_length_hint(self):
        iterator = reversed(xrange(1, 8, 2))
        self.assertEqual(type(iterator).__name__, 'rangeiterator')
        self.assertEqual(iterator.__length_hint__(), 4)
        self.assertEqual(next(iterator), 7)
        self.assertEqual(iterator.__length_hint__(), 3)
        self.assertEqual(list(iterator), [5, 3, 1])
        self.assertEqual(iterator.__length_hint__(), 0)
        self.assertRaises(StopIteration, next, iterator)

    def test_reversed_xrange_extreme_step(self):
        import sys
        maximum = sys.maxsize
        for sequence in (xrange(0), xrange(3), xrange(7, -2, -3), xrange(maximum, -maximum - 1, -maximum - 1)):
            self.assertEqual(list(reversed(sequence)), list(sequence)[::-1])

    def test_set_methods_preserve_python2_subtype(self):
        for factory in (set, frozenset):
            class Sub(factory):
                pass
            target = Sub([1, 2])
            self.assertIs(type(target.copy()), Sub)
            for method in ('union', 'intersection', 'difference', 'symmetric_difference'):
                self.assertIs(type(getattr(target, method)([2, 3])), Sub)

    def test_object_init_only_checks_its_arguments(self):
        class Mixin(object):
            def __init__(self):
                super(Mixin, self).__init__()
                self.ready = True
        class Items(list, Mixin):
            def __init__(self, data):
                list.__init__(self, data)
                Mixin.__init__(self)
        class Bag(set, Mixin):
            def __init__(self, data):
                set.__init__(self, data)
                Mixin.__init__(self)
        items = Items([1, 2, 3])
        self.assertEqual(items, [1, 2, 3])
        self.assertTrue(items.ready)
        self.assertEqual(sorted(Bag('ab')), ['a', 'b'])
        target = [1, 2]
        self.assertIs(object.__init__(target), None)
        self.assertEqual(target, [1, 2])
        mapping = {1: 2}
        object.__init__(mapping, x=1)
        self.assertEqual(mapping, {1: 2})
        array = bytearray('ab')
        object.__init__(array, 3)
        self.assertEqual(array, bytearray('ab'))
        self.assertIs(tuple.__init__, object.__init__)
        self.assertIs(tuple.__init__([], 5), None)
        self.assertEqual(_error(lambda: object.__init__(object(), 1)), ('TypeError', 'object.__init__() takes no parameters'))
        class Plain(list):
            __init__ = object.__init__
        self.assertEqual(Plain([1]), [])
        list.__init__(target, 'ab')
        self.assertEqual(target, ['a', 'b'])
        dict.__init__(mapping, {3: 4}, y=5)
        self.assertEqual(mapping, {1: 2, 3: 4, 'y': 5})
        bytearray.__init__(array, 'cd')
        self.assertEqual(array, bytearray('cd'))
        self.assertEqual(_error(lambda: list.__init__({})), ('TypeError', "descriptor '__init__' requires a 'list' object but received a 'dict'"))
        class Crossed(list):
            __init__ = dict.__init__
        self.assertEqual(_error(Crossed), ('TypeError', "descriptor '__init__' for 'dict' objects doesn't apply to 'Crossed' object"))

    def test_tuple_equality_compares_common_items_before_sizes(self):
        class Raising(object):
            def __eq__(self, other):
                raise ValueError('item compared')
        events = []
        class Recording(object):
            def __eq__(self, other):
                events.append('eq')
                return True
        self.assertEqual(_error(lambda: (Raising(), 1) == (1,)), ('ValueError', 'item compared'))
        self.assertEqual(_error(lambda: (Raising(),) != (1, 2)), ('ValueError', 'item compared'))
        self.assertFalse((Recording(), 1) == (1,))
        self.assertTrue((Recording(),) != (1, 2))
        self.assertEqual(events, ['eq', 'eq'])
        self.assertFalse([Raising(), 1] == [1])
        self.assertTrue([Raising()] != [1, 2])
        self.assertFalse((1, 2) == (1,))
        self.assertTrue(() != (1,))

    def test_dict_ordering_compares_the_entries_once(self):
        events = []
        class Key(object):
            def __init__(self, value):
                self.value = value
            def __hash__(self):
                events.append('hash')
                return hash(self.value)
            def __eq__(self, other):
                events.append('eq')
                return self.value == other
        left = {Key(1): 1, 2: 2}
        cases = (
            (lambda: left < {0: 2, 3: 4}, False, ['hash']),
            (lambda: left >= {0: 2, 3: 4}, True, ['hash']),
            (lambda: left <= {1: 2, 3: 5}, False, ['hash', 'eq', 'eq']),
            (lambda: left > {1: 2, 3: 5}, True, ['hash', 'eq', 'eq']),
            (lambda: cmp(left, {1: 2, 3: 5}), 1, ['hash', 'eq', 'eq']),
            (lambda: left == {1: 2, 3: 5}, False, ['hash', 'eq']),
        )
        for operation, result, expected in cases:
            del events[:]
            self.assertEqual(operation(), result)
            self.assertEqual(events, expected)
        class Clearing(object):
            def __hash__(self):
                return 1
            def __eq__(self, other):
                target.clear()
                return True
        target = {Clearing(): 'v'}
        self.assertFalse(target < {1: 2})
        target = {Clearing(): 'v'}
        self.assertTrue(target > {1: 2})

    def test_dict_view_comparisons_use_the_set_subclass_protocol(self):
        events = []
        class Overriding(set):
            def __len__(self):
                events.append('len')
                return 1
            def __contains__(self, item):
                events.append(('contains', item))
                return True
            def __iter__(self):
                events.append('iter')
                return iter([1])
        keys = {1: 0}.viewkeys()
        items = {1: 0}.viewitems()
        other = Overriding([5, 6])
        cases = (
            (lambda: keys == other, True, ['len', ('contains', 1)]),
            (lambda: other == keys, True, ['len', ('contains', 1)]),
            (lambda: items == other, True, ['len', ('contains', (1, 0))]),
            (lambda: keys != other, False, ['len', ('contains', 1)]),
            (lambda: keys < other, False, ['len']),
            (lambda: keys <= other, True, ['len', ('contains', 1)]),
            (lambda: other <= keys, True, ['len', 'iter']),
            (lambda: keys >= other, True, ['len', 'iter']),
            (lambda: other >= keys, True, ['len', ('contains', 1)]),
            (lambda: items >= other, False, ['len', 'iter']),
            (lambda: keys.__eq__(other), True, ['len', ('contains', 1)]),
        )
        for operation, result, expected in cases:
            del events[:]
            self.assertIs(operation(), result)
            self.assertEqual(events, expected)
        class Negative(set):
            def __len__(self):
                return -1
        self.assertEqual(_error(lambda: keys == Negative()), ('ValueError', '__len__() should return >= 0'))
        self.assertTrue(keys == set([1]) and set([1]) == keys and frozenset() < keys)

    def test_base_repr_represents_nested_self_through_the_subclass(self):
        class Listing(list):
            def __repr__(self):
                return 'R' + list.__repr__(self)
        class Mapping(dict):
            def __repr__(self):
                return 'D' + dict.__repr__(self)
        class Pair(tuple):
            def __repr__(self):
                return 'T' + tuple.__repr__(self)
        listing = Listing()
        listing.append(listing)
        mapping = Mapping()
        mapping[1] = mapping
        inner = []
        pair = Pair((inner,))
        inner.append(pair)
        try:
            self.assertEqual(repr(listing), 'R[R[...]]')
            self.assertEqual(repr(mapping), 'D{1: D{...}}')
            self.assertEqual(repr(pair), 'T([T(...)],)')
            self.assertEqual(list.__repr__(listing), '[R[...]]')
        finally:
            del listing[:], inner[:]
            mapping.clear()

    def test_xrange_getitem_method_converts_like_sq_item(self):
        target = xrange(5)
        class Integral(object):
            def __int__(self):
                return 1
        self.assertEqual(_error(lambda: target.__getitem__(2 ** 64)), ('OverflowError', "cannot fit 'long' into an index-sized integer"))
        self.assertEqual(_error(lambda: target.__getitem__(-2 ** 64)), ('OverflowError', "cannot fit 'long' into an index-sized integer"))
        self.assertEqual(_error(lambda: target.__getitem__(1.5)), ('TypeError', "'float' object cannot be interpreted as an index"))
        self.assertEqual(_error(lambda: target.__getitem__(Integral())), ('TypeError', "'Integral' object cannot be interpreted as an index"))
        self.assertEqual(_error(lambda: target.__getitem__(5)), ('IndexError', 'xrange object index out of range'))
        self.assertEqual(target.__getitem__(-1), 4)
        self.assertEqual(target.__getitem__(True), 1)
        self.assertEqual(_error(lambda: target[1.5]), ('TypeError', "sequence index must be integer, not 'float'"))
        # Only the method converts a slice; a subscript refuses it as sq_item.
        self.assertEqual(_error(lambda: target.__getitem__(slice(1, 2))), ('TypeError', "'slice' object cannot be interpreted as an index"))
        for subscript in (lambda: target[1:3], lambda: target[::2], lambda: target[slice(1, 2)]):
            self.assertEqual(_error(subscript), ('TypeError', "sequence index must be integer, not 'slice'"))

    def test_container_constructor_and_wrapper_messages(self):
        class HintMinusOne(object):
            def __iter__(self):
                return self
            def next(self):
                raise StopIteration
            def __length_hint__(self):
                return -1
        values = type({}.viewvalues())
        cases = (
            (lambda: frozenset.__new__(set), ('TypeError', 'frozenset.__new__(set): set is not a subtype of frozenset')),
            (lambda: set.__new__(frozenset), ('TypeError', 'set.__new__(frozenset): frozenset is not a subtype of set')),
            (lambda: set.__new__(1), ('TypeError', 'set.__new__(X): X is not a type object (int)')),
            (lambda: frozenset.__new__(), ('TypeError', 'frozenset.__new__(): not enough arguments')),
            (lambda: slice(stop=1), ('TypeError', 'slice() does not take keyword arguments')),
            (lambda: map(None, HintMinusOne()), ('SystemError', 'error return without exception set')),
            (lambda: values.__eq__({}.viewvalues(), 1, 2), ('TypeError', 'expected 1 arguments, got 3')),
            (lambda: type.__eq__(values), ('TypeError', 'expected 1 arguments, got 0')),
            (lambda: type.__lt__(values, 1, 2), ('TypeError', 'expected 1 arguments, got 2')),
        )
        for operation, expected in cases:
            self.assertEqual(_error(operation), expected)
        self.assertEqual(map(None, 'ab'), ['a', 'b'])
        self.assertEqual(_error(lambda: map(None, 5)), ('TypeError', "'int' object is not iterable"))
