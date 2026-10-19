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

    def test_shrinking_lists_give_storage_back(self):
        def pop_all(items):
            while len(items) > 1:
                items.pop()
        def pop_front(items):
            while len(items) > 1:
                items.pop(0)
        def delete_tail(items):
            del items[1:]
        def assign_tail(items):
            items[1:] = []
        def delete_items(items):
            while len(items) > 1:
                del items[-1]
        def remove_items(items):
            while len(items) > 1:
                items.remove(items[-1])
        def delete_stride(items):
            while len(items) > 1:
                del items[1::2]
        def delete_reversed(items):
            del items[:0:-1]
        empty_size = [].__sizeof__()
        for shrink in (pop_all, pop_front, delete_tail, assign_tail, delete_items, remove_items, delete_stride, delete_reversed):
            items = range(1000)
            shrink(items)
            self.assertTrue(items.__sizeof__() < empty_size + 64, shrink.__name__)
            self.assertEqual(len(items), 1)
            items.extend(range(5))
            self.assertEqual(items[1:], range(5))
        items = range(1000)
        del items[::-1]
        self.assertEqual((items, items.__sizeof__()), ([], empty_size))
        items = range(1000)
        items[:] = ()
        self.assertEqual((items, items.__sizeof__()), ([], empty_size))

    def test_shrinking_bytearrays_give_storage_back(self):
        def pop_all(data):
            while len(data) > 1:
                data.pop()
        def delete_tail(data):
            del data[1:]
        def assign_tail(data):
            data[1:] = ''
        def delete_items(data):
            while len(data) > 1:
                del data[-1]
        def remove_items(data):
            while len(data) > 1:
                data.remove(data[-1])
        def delete_stride(data):
            while len(data) > 1:
                del data[1::2]
        def delete_reversed(data):
            del data[:0:-1]
        for shrink in (pop_all, delete_tail, assign_tail, delete_items, remove_items, delete_stride, delete_reversed):
            data = bytearray(range(250) * 4)
            shrink(data)
            self.assertTrue(data.__alloc__() <= 4, shrink.__name__)
            self.assertEqual(len(data), 1)
            data.extend('abc')
            self.assertEqual(data[1:], bytearray('abc'))
        data = bytearray(1000)
        del data[1:]
        self.assertEqual(data.__alloc__(), 2)
        data = bytearray(1000)
        data.__init__()
        self.assertTrue(data.__alloc__() <= 1)
        data.__init__('xyz')
        self.assertEqual(data, bytearray('xyz'))

    def test_negative_length_hint_message_follows_the_call(self):
        class Hint(object):
            def __iter__(self):
                return iter([1])
            def __length_hint__(self):
                return -1
        class Derived(list):
            pass
        class Record(tuple):
            pass
        def augment(target):
            target += Hint()
        values = []
        bound_extend = values.extend
        evaluation = ('SystemError', 'error return without exception set')
        call = ('SystemError', 'NULL result without error in PyObject_Call')
        cases = [
            (lambda: values.extend(Hint()), evaluation),
            (lambda: bound_extend(Hint()), evaluation),
            (lambda: augment(values), evaluation),
            (lambda: augment(Derived()), evaluation),
            (lambda: values.__iadd__(Hint()), call),
            (lambda: list.__iadd__(values, Hint()), call),
            (lambda: Derived().__iadd__(Hint()), call),
            (lambda: Record(Hint()), call),
            (lambda: apply(tuple.__new__, (tuple, Hint())), call),
            (lambda: tuple(Hint()), call),
            (lambda: list(Hint()), call),
            (lambda: Derived(Hint()), call),
            (lambda: bytearray().extend(Hint()), evaluation),
            (lambda: bytearray.extend(bytearray(), Hint()), call),
        ]
        for operation, expected in cases:
            self.assertEqual(_error(operation), expected)
        self.assertEqual(values, [])

    def test_huge_length_hint_runs_out_of_memory(self):
        class Hint(object):
            def __iter__(self):
                return iter([1, 2])
            def __length_hint__(self):
                return 2 ** 62
        class Sized(object):
            def __iter__(self):
                return iter([1, 2])
            def __len__(self):
                return 2 ** 62 + 5
        class Record(tuple):
            pass
        def star(*arguments):
            return arguments
        for source in (Hint, Sized):
            for build in (tuple, list, Record, lambda iterable: star(*iterable), lambda iterable: [].extend(iterable)):
                self.assertEqual(_error(lambda: build(source()))[0], 'MemoryError')

    def test_comparison_refilling_the_free_slot_finds_that_entry(self):
        class Key(object):
            def __init__(self, name, target=None):
                self.name = name
                self.target = target
            def __hash__(self):
                return 1
            def __eq__(self, other):
                target = self.target
                self.target = None
                if target is not None:
                    if isinstance(target, dict):
                        target[1] = 'refilled'
                    else:
                        target.add(1)
                return self is other
            def __repr__(self):
                return self.name
        def make(container):
            if isinstance(container, dict):
                container[1] = 'first'
                colliding = Key('colliding')
                container[colliding] = 'colliding'
                del container[1]
            else:
                container.add(1)
                colliding = Key('colliding')
                container.add(colliding)
                container.discard(1)
            colliding.target = container
            return container
        probe = Key('probe')
        def run(operation, container):
            result = operation(make(container))
            if isinstance(container, dict):
                return result, sorted((repr(key), value) for key, value in container.items())
            return result, sorted(repr(key) for key in container)
        refilled = [('1', 'refilled'), ('colliding', 'colliding')]
        cases = [
            (lambda d: probe in d, {}, (True, refilled)),
            (lambda d: d.has_key(probe), {}, (True, refilled)),
            (lambda d: d.get(probe, 'default'), {}, ('refilled', refilled)),
            (lambda d: d[probe], {}, ('refilled', refilled)),
            (lambda d: d.setdefault(probe, 'default'), {}, ('refilled', refilled)),
            (lambda d: d.pop(probe, 'default'), {}, ('refilled', refilled[1:])),
            (lambda d: d.__delitem__(probe), {}, (None, refilled[1:])),
            (lambda d: d.__setitem__(probe, 'stored'), {}, (None, [('1', 'stored'), ('colliding', 'colliding')])),
            (lambda s: probe in s, set(), (True, ['1', 'colliding'])),
            (lambda s: s.discard(probe), set(), (None, ['colliding'])),
            (lambda s: s.remove(probe), set(), (None, ['colliding'])),
            (lambda s: s.add(probe), set(), (None, ['1', 'colliding'])),
        ]
        for operation, container, expected in cases:
            self.assertEqual(run(operation, container), expected)

    def test_lookup_finds_the_stored_key_whatever_its_hash(self):
        class Key(object):
            def __init__(self, value):
                self.value = value
            def __hash__(self):
                return self.value
            def __eq__(self, other):
                return False
            def __ne__(self, other):
                return True
        key = Key(1)
        mapping = {key: 'v'}
        key.value = 9
        self.assertEqual((key in mapping, mapping.get(key), len(mapping)), (True, 'v', 1))
        mapping[key] = 'w'
        self.assertEqual(mapping.values(), ['w'])
        members = set([key])
        key.value = 17
        self.assertTrue(key in members)
        members.add(key)
        self.assertEqual(len(members), 1)
        other = Key(3)
        mapping = {other: 1}
        other.value = 3 + 8 * 5
        self.assertEqual((mapping.pop(other, 'missing'), len(mapping)), (1, 0))

    def test_merge_does_not_resize_a_target_emptied_midway(self):
        class Key(object):
            def __init__(self, value, hash_value, action=None):
                self.value = value
                self.hash_value = hash_value
                self.action = action
            def __hash__(self):
                return self.hash_value
            def __eq__(self, other):
                action = self.action
                if action is not None:
                    self.action = None
                    action()
                return isinstance(other, Key) and other.value == self.value
            def __ne__(self, other):
                return not self.__eq__(other)
            def __repr__(self):
                return 'K%d' % self.value
        source = dict((Key(index, index % 4), index) for index in range(9))
        target = {}
        target[Key(11, 3, lambda: target.clear())] = 3
        target.update(source)
        self.assertEqual(repr(target.items()), '[(K4, 4), (K5, 5), (K6, 6), (K3, 3), (K7, 7), (K8, 8)]')
        members = set([Key(11, 3, lambda: members.clear())])
        members.update(set(source))
        self.assertEqual(repr(list(members)), '[K4, K5, K6, K3, K7, K8]')
        members = set([Key(11, 3, lambda: members.clear())])
        members |= set(source)
        self.assertEqual(repr(list(members)), '[K4, K5, K6, K3, K7, K8]')
        members = set([Key(11, 3, lambda: members.clear())])
        members.update(source)
        self.assertEqual(repr(list(members)), '[K4, K5, K6, K3, K8, K7]')

    def test_building_cuts_an_unfilled_reservation_back(self):
        class Few(object):
            def __init__(self, hint):
                self.hint = hint
            def __len__(self):
                return self.hint
            def __iter__(self):
                return iter([1, 2, 3])
        class Hinted(object):
            def __iter__(self):
                return self
            def next(self):
                raise StopIteration
            def __length_hint__(self):
                return 1000
        def extended(items):
            items.extend(Few(1000))
            return items
        def added(items):
            items += Few(1000)
            return items
        empty_size = [].__sizeof__()
        cases = [
            (lambda: list(Few(1000)), 48),
            (lambda: list(Few(5)), 48),
            (lambda: extended([1]), 56),
            (lambda: added([1]), 56),
            (lambda: sorted(Few(1000)), 48),
            (lambda: list(Hinted()), 0),
            (lambda: map(None, Few(1000)), 48),
            (lambda: map(abs, Few(1000)), 48),
            (lambda: map(None, Few(1000), Few(500)), 48),
            (lambda: zip(Few(1000), Few(1000)), 48),
            (lambda: filter(None, Few(1000)), 48),
            (lambda: filter(lambda item: False, Few(1000)), 0),
        ]
        for build, slots_size in cases:
            items = build()
            self.assertEqual(items.__sizeof__() - empty_size, slots_size)

    def test_slice_removal_releases_items_once_the_list_is_consistent(self):
        events = []
        class Item(object):
            def __init__(self, name, holder):
                self.name = name
                self.holder = holder
            def __del__(self):
                events.append((self.name, len(self.holder[0])))
        def delete_range(items):
            del items[1:-1]
        def assign_range(items):
            items[1:-1] = ['x']
        def delete_stride(items):
            del items[::2]
        def assign_stride(items):
            items[::2] = ['y'] * len(items[::2])
        def assign_empty(items):
            items[:] = []
        def delete_reversed(items):
            del items[::-1]
        for count in (3, 20):
            for operation in (delete_range, assign_range, delete_stride, assign_stride, assign_empty, delete_reversed):
                holder = [None]
                items = []
                for index in range(count):
                    items.append(Item(index, holder))
                holder[0] = items
                del events[:]
                operation(items)
                released = events[:]
                kept = len(items)
                if operation in (delete_range, assign_range):
                    expected = [(index, kept) for index in range(count - 2, 0, -1)]
                elif operation in (delete_stride, assign_stride):
                    expected = [(index, kept) for index in range(0, count, 2)]
                else:
                    expected = [(index, 0) for index in (range(count - 1, -1, -1) if operation is assign_empty else range(count))]
                self.assertEqual(released, expected, (count, operation.__name__))
                del items[:]
                holder[0] = None

    def test_bytearray_in_place_results_that_fit_ignore_exports(self):
        data = bytearray('abcdef')
        view = memoryview(data)
        data.__imul__(0)
        self.assertEqual(data, bytearray())
        data.__imul__(-1)
        del view
        data = bytearray('x' * 100)
        data *= 0
        data += 'abc'
        view = memoryview(data)
        self.assertEqual(data.__iadd__('def'), bytearray('abcdef'))
        self.assertEqual(data.__imul__(2), bytearray('abcdefabcdef'))
        self.assertEqual(_error(lambda: data.__imul__(20)), ('BufferError', 'Existing exports of data: object cannot be re-sized'))
        self.assertEqual(_error(lambda: data.__iadd__('z' * 100)), ('BufferError', 'Existing exports of data: object cannot be re-sized'))
        del view
        data = bytearray('x' * 100)
        data *= 0
        data += 'abc'
        self.assertEqual(data.__iadd__(data), bytearray('abcabc'))
        data = bytearray('abc')
        self.assertEqual(_error(lambda: data.__iadd__(data)), ('BufferError', 'Existing exports of data: object cannot be re-sized'))

    def test_bytearray_decoding_holds_an_export(self):
        import _codecs
        data = bytearray()
        outcomes = []
        def handler(error):
            try:
                del data[:]
                outcomes.append('resized')
            except BufferError:
                outcomes.append('BufferError')
            return (u'?', error.end)
        _codecs.register_error('test_container_deep_resize', handler)
        decoders = [
            lambda: data.decode('utf-8', 'test_container_deep_resize'),
            lambda: data.decode('ascii', 'test_container_deep_resize'),
            lambda: _codecs.decode(data, 'utf-8', 'test_container_deep_resize'),
            lambda: _codecs.utf_8_decode(data, 'test_container_deep_resize'),
        ]
        results = []
        for decode in decoders:
            data[:] = 'a\xffb'
            results.append((decode(), len(data)))
        self.assertEqual(outcomes, ['BufferError'] * 4)
        self.assertEqual(results, [(u'a?b', 3), (u'a?b', 3), (u'a?b', 3), ((u'a?b', 3), 3)])
        data[:] = 'abc'
        self.assertEqual(data.decode('utf-8'), u'abc')
        data.append(100)
        self.assertEqual(data, bytearray('abcd'))

    def test_unicode_concatenation_reads_the_buffer_characters(self):
        self.assertEqual(u'x' + buffer(u'abc', 1), u'xbc')
        self.assertEqual(u'x' + buffer(u'abcd', 1, 2), u'xbc')
        self.assertEqual(u'x'.__add__(buffer(u'ab')), u'xab')
        self.assertEqual(u'x' + buffer('abc', 1), u'xbc')
        text = u'x'
        text += buffer(u'ab')
        self.assertEqual(text, u'xab')
        self.assertEqual(_error(lambda: u'x' + buffer(u'a\xe9'))[0], 'UnicodeEncodeError')
        self.assertEqual(_error(lambda: u'x' + buffer('a\xe9'))[0], 'UnicodeDecodeError')

    def test_bytearray_count_searches_the_bounded_range(self):
        data = bytearray('abcabcab')
        cases = [
            (lambda: data.count('ab'), 3),
            (lambda: data.count('ab', 1), 2),
            (lambda: data.count('ab', -5, -1), 1),
            (lambda: data.count('', 3), 6),
            (lambda: data.count('', 9), 0),
            (lambda: data.count('', 5, 2), 0),
            (lambda: data.count(bytearray('b'), None, None), 3),
            (lambda: data.count(memoryview('c')), 2),
            (lambda: data.count(buffer('abc', 1)), 2),
            (lambda: data.count('a', 2 ** 70), 0),
            (lambda: data.count('a', -2 ** 70), 3),
            (lambda: bytearray('aaaa').count('aa'), 2),
            (lambda: bytearray().count(''), 1),
            (lambda: bytearray().count('a'), 0),
        ]
        for count, expected in cases:
            self.assertEqual(count(), expected)
        self.assertEqual(_error(lambda: data.count()), ('TypeError', 'count() takes at least 1 argument (0 given)'))
        self.assertEqual(_error(lambda: data.count('a', 1, 2, 3)), ('TypeError', 'count() takes at most 3 arguments (4 given)'))
        self.assertEqual(_error(lambda: data.count(5)), ('TypeError', "Type int doesn't support the buffer API"))
        class Index(object):
            def __index__(self):
                data[:] = 'zz'
                return 1
        self.assertEqual(data.count('a', Index()), 0)

    def test_keyed_sort_moves_items_with_their_keys(self):
        class Stop(Exception):
            pass
        budget = [0]
        class Key(object):
            def __init__(self, value):
                self.value = value
            def __lt__(self, other):
                budget[0] -= 1
                if budget[0] == 0:
                    raise Stop()
                return self.value < other.value
        shapes = [range(1000, 2000) + range(1000), range(0, 4000, 2) + range(1, 400, 2),
                  range(1, 400, 2) + range(0, 4000, 2), [(index * 7919) % 501 for index in range(1500)]]
        results = []
        for shape in shapes:
            for reverse in (False, True):
                for limit in (-1, 50, 1500, 4000):
                    items = [(value, index) for index, value in enumerate(shape)]
                    budget[0] = limit
                    try:
                        items.sort(key=lambda item: Key(item[0] // 3), reverse=reverse)
                    except Stop:
                        pass
                    results.append(sum(position * item[1] for position, item in enumerate(items)) % 1000003)
        self.assertEqual(results, [666005, 659008, 659008, 666005, 329669, 658348, 978090, 652453,
                                   705422, 903062, 903062, 705422, 974323, 902387, 477355, 134553,
                                   242968, 903062, 903062, 242968, 956445, 902507, 237055, 972989,
                                   812048, 871561, 319764, 882635, 541149, 871449, 316885, 945216])

    def test_slice_removal_finalizers_may_extend_the_list(self):
        class Item(object):
            def __init__(self, name, holder):
                self.name = name
                self.holder = holder
            def __del__(self):
                if self.holder[0] is not None:
                    self.holder[0].append(self.name)
        results = []
        for count in (4, 30):
            for operation in ('delete', 'assign', 'stride'):
                holder = [None]
                items = []
                for index in range(count):
                    items.append(Item(index, holder))
                holder[0] = items
                if operation == 'delete':
                    del items[1:-1]
                elif operation == 'assign':
                    items[1:-1] = ['x', 'y']
                else:
                    del items[1::2]
                results.append([item if isinstance(item, (int, str)) else 'item' for item in items])
                holder[0] = None
                del items[:]
        self.assertEqual(results[0], ['item', 'item', 2, 1])
        self.assertEqual(results[1], ['item', 'x', 'y', 'item', 2, 1])
        self.assertEqual(results[2], ['item', 'item', 1, 3])
        self.assertEqual(results[3], ['item', 'item'] + range(28, 0, -1))
        self.assertEqual(results[4], ['item', 'x', 'y', 'item'] + range(28, 0, -1))
        self.assertEqual(results[5], ['item'] * 15 + range(1, 30, 2))
