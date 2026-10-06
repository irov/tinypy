"""Project-authored Python 2.7 container and iterator protocol checks."""

import unittest


class ContainerSemantics(unittest.TestCase):
    def test_bytearray_constructor_keywords(self):
        self.assertEqual(list(bytearray(source='xy')), [120, 121])
        self.assertEqual(list(bytearray(source=u'xy', encoding='utf-8')), [120, 121])
        self.assertEqual(list(bytearray('xy', encoding='utf-8')), [120, 121])
        self.assertEqual(list(bytearray(source='xy', errors='strict')), [120, 121])
        self.assertRaises(TypeError, bytearray, encoding='utf-8')
        self.assertRaises(TypeError, bytearray, u'xy', errors='strict')
        self.assertRaises(TypeError, bytearray, [1, 2], encoding='utf-8')
        self.assertRaises(TypeError, bytearray, 'xy', source='xy')

    def test_bytearray_init_clears_first_and_keeps_partial_progress(self):
        target = bytearray('ab')
        def source():
            yield 4
            yield 7
            raise ValueError('source failed')
        self.assertRaises(ValueError, target.__init__, source())
        self.assertEqual(list(target), [4, 7])

        target.__init__(target)
        self.assertEqual(list(target), [])
        target.extend('ab')
        self.assertRaises(TypeError, target.__init__, bad='argument')
        self.assertEqual(list(target), [])

        target.extend('ab')
        exported = memoryview(target)
        self.assertRaises(BufferError, target.__init__, 'cd')
        self.assertEqual(list(target), [97, 98])
        del exported

    def test_sequence_iteration_stops_on_indexerror_or_stopiteration(self):
        for ending in (IndexError, StopIteration):
            calls = []
            class Sequence(object):
                def __getitem__(self, index):
                    calls.append(index)
                    if index == 2:
                        raise ending('finished')
                    return index + 4
            iterator = iter(Sequence())
            self.assertEqual(list(iterator), [4, 5])
            self.assertEqual(calls, [0, 1, 2])
            self.assertRaises(StopIteration, next, iterator)
            self.assertEqual(calls, [0, 1, 2])

    def test_sequence_iteration_propagates_other_errors(self):
        class Sequence(object):
            def __getitem__(self, index):
                if index == 1:
                    raise KeyError('item')
                return index
        iterator = iter(Sequence())
        self.assertEqual(next(iterator), 0)
        with self.assertRaises(KeyError) as failure:
            next(iterator)
        self.assertEqual(failure.exception.args, ('item',))

    def test_contains_precedes_iterator_protocol(self):
        calls = []
        class Container(object):
            def __contains__(self, item):
                calls.append(item)
                return [item] if item == 'present' else []
            def __iter__(self):
                raise AssertionError('iteration must not run')
        source = Container()
        self.assertTrue('present' in source)
        self.assertTrue('absent' not in source)
        self.assertEqual(calls, ['present', 'absent'])

    def test_membership_consumes_iterator_through_match(self):
        iterator = iter([2, 5, 8, 11])
        self.assertTrue(5 in iterator)
        self.assertEqual(next(iterator), 8)
        self.assertFalse(99 in iterator)
        self.assertRaises(StopIteration, next, iterator)

    def test_callable_iterator_retains_exhaustion(self):
        calls = []
        values = iter([4, 9, 12])
        def produce():
            value = next(values)
            calls.append(value)
            return value
        iterator = iter(produce, 9)
        self.assertEqual(list(iterator), [4])
        self.assertEqual(next(iterator, 'done'), 'done')
        self.assertEqual(calls, [4, 9])
        self.assertEqual(next(values), 12)

    def test_next_default_does_not_swallow_callback_error(self):
        class Iterator(object):
            def __iter__(self):
                return self
            def next(self):
                raise ValueError('iteration failed')
        self.assertRaises(ValueError, next, Iterator(), 'fallback')
        self.assertEqual(next(iter([]), 'fallback'), 'fallback')

    def test_list_iterator_observes_appended_items(self):
        source = [3, 6]
        iterator = iter(source)
        self.assertEqual(next(iterator), 3)
        source.append(9)
        self.assertEqual(list(iterator), [6, 9])
        source.append(12)
        self.assertRaises(StopIteration, next, iterator)

    def test_repeated_sequence_shares_elements(self):
        element = []
        for sequence in ([element], (element,)):
            repeated = sequence * 3
            self.assertEqual(len(repeated), 3)
            for item in repeated:
                self.assertIs(item, element)
            self.assertEqual(sequence * -7, type(sequence)())
        element.append('changed')
        self.assertEqual(repeated, (['changed'], ['changed'], ['changed']))

    def test_list_self_extension_and_subclass_iteration(self):
        class Stored(list):
            def __iter__(self):
                return iter([11, 13])
        for operation in (list.extend, list.__iadd__):
            source = [2, 7]
            operation(source, source)
            self.assertEqual(source, [2, 7, 2, 7])
            source = Stored([2, 7])
            operation(source, source)
            self.assertEqual(source, [2, 7, 11, 13])

    def test_list_extend_keeps_progress_on_iteration_failure(self):
        for operation in (list.extend, list.__iadd__):
            target = [1]
            def source():
                self.assertEqual(target, [1])
                yield 4
                self.assertEqual(target, [1, 4])
                yield 7
                raise ValueError('source failed')
            with self.assertRaises(ValueError):
                operation(target, source())
            self.assertEqual(target, [1, 4, 7])

    def test_slice_assignment_is_atomic_on_iteration_failure(self):
        target = [1, 4, 7]
        def source():
            yield 20
            yield 30
            raise ValueError('source failed')
        with self.assertRaises(ValueError):
            target[1:] = source()
        self.assertEqual(target, [1, 4, 7])

    def test_list_extend_length_callback_can_mutate_destination(self):
        for operation in (list.extend, list.__iadd__):
            target = [1, 2]
            events = []
            class Source(object):
                def __iter__(self):
                    events.append('iter')
                    return iter([7, 8])
                def __len__(self):
                    events.append('len')
                    target[:] = [4]
                    return 2
            operation(target, Source())
            self.assertEqual(target, [4, 7, 8])
            self.assertEqual(events, ['iter', 'len'])

    def test_list_extend_length_error_precedes_iteration(self):
        for operation in (list.extend, list.__iadd__):
            target = [1]
            events = []
            class Source(object):
                def __iter__(self):
                    events.append('iter')
                    return self
                def __len__(self):
                    events.append('len')
                    target.append(4)
                    raise ValueError('length failed')
                def next(self):
                    events.append('next')
                    return 9
            self.assertRaises(ValueError, operation, target, Source())
            self.assertEqual(target, [1, 4])
            self.assertEqual(events, ['iter', 'len'])

    def test_extended_slice_size_mismatch_keeps_list(self):
        target = [0, 1, 2, 3, 4, 5]
        for replacement in ([8], [8, 9, 10, 11]):
            with self.assertRaises(ValueError):
                target[::2] = replacement
            self.assertEqual(target, [0, 1, 2, 3, 4, 5])
        target[::-2] = [8, 9, 10]
        self.assertEqual(target, [0, 10, 2, 9, 4, 8])

    def test_slice_construction_defers_zero_step_validation(self):
        selection = slice(None, None, 0)
        self.assertEqual(selection.step, 0)
        self.assertRaises(ValueError, selection.indices, 4)
        with self.assertRaises(ValueError):
            [0, 1, 2, 3][selection]

    def test_slice_clips_large_index_objects(self):
        class Index(object):
            def __init__(self, value):
                self.value = value
            def __index__(self):
                return self.value
        huge = 1L << 100
        source = [0, 1, 2, 3, 4]
        self.assertEqual(source[Index(-huge):Index(huge):Index(2)], [0, 2, 4])
        self.assertEqual(source[Index(huge):Index(-huge):Index(-2)], [4, 2, 0])

    def test_list_index_bounds_use_index_protocol(self):
        calls = []
        class Index(object):
            def __init__(self, value):
                self.value = value
            def __index__(self):
                calls.append(self.value)
                return self.value
        source = [2, 5, 8]
        self.assertEqual(source.index(5, Index(1), Index(3)), 1)
        self.assertEqual(source, [2, 5, 8])
        self.assertEqual(calls, [1, 3])
        self.assertRaises(TypeError, source.insert, Index(1), 3)
        self.assertRaises(TypeError, source.pop, Index(-1))
        self.assertEqual(calls, [1, 3])

    def test_index_errors_do_not_mutate_list(self):
        class BadIndex(object):
            def __index__(self):
                return '1'
        class FailingIndex(object):
            def __index__(self):
                raise LookupError('index failed')
        source = [2, 5]
        for index, error in ((BadIndex(), TypeError), (FailingIndex(), LookupError)):
            self.assertRaises(error, source.index, 5, index)
            self.assertRaises(TypeError, source.insert, index, 9)
            self.assertRaises(TypeError, source.pop, index)
            self.assertEqual(source, [2, 5])

    def test_dict_missing_is_only_used_by_subscription(self):
        calls = []
        class Defaults(dict):
            def __missing__(self, key):
                calls.append(key)
                return ('missing', key)
        source = Defaults(a=3)
        self.assertEqual(source['absent'], ('missing', 'absent'))
        self.assertIs(source.get('absent'), None)
        self.assertFalse('absent' in source)
        self.assertEqual(source.setdefault('absent', 8), 8)
        self.assertEqual(source['absent'], 8)
        self.assertEqual(calls, ['absent'])

    def test_dict_update_pair_failure_retains_previous_pairs(self):
        source = {'original': 1}
        with self.assertRaises(ValueError):
            source.update(iter([('added', 4), ('bad', 5, 6), ('unused', 7)]))
        self.assertEqual(source, {'original': 1, 'added': 4})

    def test_dict_iterator_value_replacement_is_allowed(self):
        source = {'first': 1, 'second': 2}
        iterator = source.iteritems()
        key, value = next(iterator)
        for existing in source:
            source[existing] += 10
        remaining = list(iterator)
        self.assertEqual(len(remaining), 1)
        self.assertEqual(remaining[0][1], source[remaining[0][0]])

    def test_set_update_keeps_progress_on_iteration_failure(self):
        source = {2}
        def additions():
            yield 5
            yield 8
            raise LookupError('source failed')
        self.assertRaises(LookupError, source.update, additions())
        self.assertEqual(source, {2, 5, 8})

    def test_set_methods_accept_iterables_but_operators_require_sets(self):
        source = {2, 5}
        self.assertEqual(source.union([5, 8]), {2, 5, 8})
        self.assertEqual(source.intersection(iter([5, 8])), {5})
        self.assertEqual(source.difference([5, 8]), {2})
        self.assertEqual(source.symmetric_difference([5, 8]), {2, 8})
        for expression in ('source | [5]', 'source & [5]', 'source - [5]', 'source ^ [5]'):
            self.assertRaises(TypeError, eval, expression, {'source': source})

    def test_set_subclass_repr_uses_overridden_iterator(self):
        class StoredSet(set):
            def __iter__(self):
                return iter([9])
        class StoredFrozenSet(frozenset):
            def __iter__(self):
                return iter([9])
        self.assertEqual(repr(StoredSet([1, 2])), 'StoredSet([9])')
        self.assertEqual(repr(StoredFrozenSet([1, 2])), 'StoredFrozenSet([9])')

    def test_list_init_clears_first_and_keeps_partial_progress(self):
        target = [1]
        events = []
        def source():
            events.append(tuple(target))
            yield 4
            events.append(tuple(target))
            yield 7
            raise ValueError('source failed')
        self.assertRaises(ValueError, target.__init__, source())
        self.assertEqual(target, [4, 7])
        self.assertEqual(events, [(), (4,)])

        target = [1, 2]
        target.__init__(target)
        self.assertEqual(target, [])

        class Stored(list):
            def __iter__(self):
                return iter([9])
        target = Stored([1, 2])
        target.__init__(target)
        self.assertEqual(target, [9])

    def test_sequence_constructor_keywords(self):
        self.assertEqual(list(sequence=(2, 5)), [2, 5])
        self.assertEqual(tuple(sequence=[2, 5]), (2, 5))

        target = [1]
        target.__init__(sequence=(4, 7))
        self.assertEqual(target, [4, 7])
        self.assertRaises(TypeError, target.__init__, [2], sequence=[3])
        self.assertEqual(target, [4, 7])
        self.assertRaises(TypeError, target.__init__, iterable=[8])
        self.assertEqual(target, [4, 7])

    def test_set_difference_update_keeps_partial_progress(self):
        source = {0, 1, 2}
        def removals():
            yield 1
            raise ValueError('source failed')
        self.assertRaises(ValueError, source.difference_update, removals())
        self.assertEqual(source, {0, 2})

    def test_set_init_clears_first_and_keeps_partial_progress(self):
        target = {1}
        def source():
            yield 4
            yield 7
            raise ValueError('source failed')
        self.assertRaises(ValueError, target.__init__, source())
        self.assertEqual(target, {4, 7})

        target.__init__(target)
        self.assertEqual(target, set())
        target.add(2)
        self.assertRaises(TypeError, target.__init__, sequence=[3])
        self.assertEqual(target, {2})

    def test_property_accessors_return_independent_descriptors(self):
        def get(instance):
            return instance.value
        def put(instance, value):
            instance.value = value
        def remove(instance):
            del instance.value
        original = property(get, doc='value accessor')
        writable = original.setter(put)
        removable = writable.deleter(remove)
        self.assertIs(original.fset, None)
        self.assertIs(writable.fdel, None)
        self.assertIs(removable.fget, get)
        self.assertIs(removable.fset, put)
        self.assertIs(removable.fdel, remove)
        self.assertEqual(removable.__doc__, 'value accessor')
        self.assertIs(removable.__get__(None, object), removable)
