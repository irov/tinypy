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

    def test_dict_order_follows_table_growth_and_presizing(self):
        def built(keys):
            result = {}
            for key in keys:
                result[key] = 1
            return result
        self.assertEqual(built((9, 1, 2, 3, 4, 5)).keys(), [1, 2, 3, 4, 5, 9])
        self.assertEqual(built((9, 1, 2, 3, 4)).keys(), [9, 2, 3, 4, 1])
        grown = built(range(16, 24))
        for key in range(16, 21):
            del grown[key]
        for key in (32, 64, 96, 128, 160):
            grown[key] = 1
        self.assertEqual(grown.keys(), [32, 64, 96, 128, 160, 21, 22, 23])
        source = built([33, 1] + range(100, 117))
        updated = {}
        updated.update(source)
        for copy in (source.copy(), dict(source), updated, dict(**dict((str(key), key) for key in source))):
            self.assertEqual(len(copy), 19)
        self.assertEqual(source.copy().keys()[:3], [1, 33, 100])
        self.assertEqual(dict(source).keys()[:3], [1, 33, 100])
        self.assertEqual(updated.keys()[:3], [1, 33, 100])
        self.assertEqual(dict.fromkeys(source).keys()[:3], [33, 100, 101])
        self.assertEqual(dict.fromkeys([33, 1] + range(100, 117)).keys()[:3], [33, 100, 101])
        self.assertEqual(dict([(33, 1), (1, 1)] + [(key, 1) for key in range(100, 117)]).keys()[:3], [33, 100, 101])
        names = ['k%d' % index for index in range(22)]
        self.assertEqual(dict.fromkeys(names[:11]).keys(), ['k10', 'k3', 'k2', 'k1', 'k0', 'k7', 'k6', 'k5', 'k4', 'k9', 'k8'])
        self.assertEqual(dict.fromkeys(dict.fromkeys(names[:11])).keys(), ['k3', 'k2', 'k1', 'k10', 'k7', 'k6', 'k5', 'k4', 'k9', 'k8', 'k0'])
        self.assertEqual(dict.fromkeys(set(names[:11])).keys(), ['k3', 'k2', 'k1', 'k10', 'k7', 'k6', 'k5', 'k4', 'k9', 'k8', 'k0'])
        keyword_source = built(names)
        self.assertEqual(dict(**keyword_source).keys(), dict(keyword_source).keys())
        display = {33: 0, 1: 0, 65: 0, 2: 0, 97: 0, 3: 0, 129: 0, 4: 0, 161: 0, 5: 0, 193: 0, 6: 0}
        self.assertEqual(display.keys(), [65, 2, 3, 4, 5, 6, 97, 33, 1, 129, 161, 193])

    def test_set_order_follows_setobject(self):
        names = ['k%d' % index for index in range(30)]
        self.assertEqual(list({9, 1}), [9, 1])
        self.assertEqual(list({1, 9}), [1, 9])
        self.assertEqual(list(set([9, 1, 2, 3, 4, 5])), [1, 2, 3, 4, 5, 9])
        self.assertEqual(list(set(names[:6]).symmetric_difference(['k1', 'zz'])), ['zz', 'k3', 'k2', 'k0', 'k5', 'k4'])
        toggled = set(names[:6])
        toggled.symmetric_difference_update(['k1', 'zz'])
        self.assertEqual(list(toggled), ['zz', 'k3', 'k2', 'k0', 'k5', 'k4'])
        self.assertEqual(list(set(names[:19]) | set(['zz'])), ['zz', 'k13', 'k12', 'k11', 'k10', 'k17', 'k16', 'k15', 'k14', 'k18', 'k3', 'k2', 'k1', 'k0', 'k7', 'k6', 'k5', 'k4', 'k9', 'k8'])
        self.assertEqual(list(set(names[:19]) - set(['k1'])), ['k13', 'k12', 'k11', 'k10', 'k17', 'k16', 'k15', 'k14', 'k18', 'k3', 'k2', 'k0', 'k7', 'k6', 'k5', 'k4', 'k9', 'k8'])
        self.assertEqual(list(set(names[:19]) & set(names[5:30])), ['k13', 'k12', 'k11', 'k10', 'k17', 'k16', 'k15', 'k14', 'k18', 'k7', 'k6', 'k5', 'k9', 'k8'])
        comparisons = []
        class Equal(object):
            def __init__(self, name):
                self.name = name
            def __hash__(self):
                return 0
            def __eq__(self, other):
                comparisons.append((self.name, other.name))
                return True
        display = {Equal('a'), Equal('b')}
        self.assertEqual([member.name for member in display], ['a'])
        self.assertEqual(comparisons, [('a', 'b')])

    def test_popitem_and_pop_resume_from_slot_zero(self):
        popping = dict.fromkeys([0, 8, 16, 3, 11, 19, 40, 41])
        popped = []
        while popping:
            popped.append(popping.popitem()[0])
            if len(popped) == 3:
                popping[24] = 1
        self.assertEqual(popped, [0, 3, 8, 41, 11, 16, 40, 19, 24])
        popping = set([0, 8, 16, 3, 11, 19, 40, 41])
        popped = []
        while popping:
            popped.append(popping.pop())
            if len(popped) == 3:
                popping.add(24)
        self.assertEqual(popped, [0, 3, 8, 41, 11, 16, 40, 19, 24])
        self.assertRaises(KeyError, {}.popitem)
        self.assertRaises(KeyError, set().pop)

    def test_insertion_uses_slot_filled_by_key_comparison(self):
        class Key(object):
            def __init__(self, name, action=None):
                self.name = name
                self.action = action
            def __hash__(self):
                return 0
            def __eq__(self, other):
                action = self.action
                self.action = None
                if action is not None:
                    action()
                return self is other
        first, second, late = Key('A'), Key('B'), Key('X')
        collided = {}
        collided[first] = 'a'
        collided[second] = 'b'
        del collided[first]
        second.action = lambda: collided.__setitem__(late, 'x')
        collided[Key('C')] = 'c'
        self.assertEqual(len(collided), 2)
        self.assertEqual(sorted((key.name, value) for key, value in collided.items()), [('B', 'b'), ('X', 'c')])

    def test_copies_and_set_operations_reuse_stored_hashes(self):
        calls = [0]
        class Counted(object):
            def __init__(self, value):
                self.value = value
            def __hash__(self):
                calls[0] += 1
                return self.value
            def __eq__(self, other):
                return isinstance(other, Counted) and other.value == self.value
        keys = [Counted(value) for value in range(10)]
        mapping = dict.fromkeys(keys)
        members = set(keys)
        others = set(keys[5:] + [Counted(20)])
        calls[0] = 0
        results = [mapping.copy(), dict(mapping), set(mapping), dict.fromkeys(mapping), dict.fromkeys(members), set(members),
                   members.copy(), members | others, members & others, members - others, members ^ others, frozenset(members),
                   mapping.viewkeys() & members]
        merged = set(members)
        merged.symmetric_difference_update(others)
        self.assertEqual([len(result) for result in results], [10, 10, 10, 10, 10, 10, 10, 11, 5, 5, 6, 10, 10])
        self.assertEqual(len(merged), 6)
        self.assertEqual(calls[0], 10)

    def test_subscript_errors_name_the_sequence(self):
        def message(error, function, *args):
            try:
                function(*args)
            except error as exception:
                return str(exception)
            raise AssertionError('no %s' % error.__name__)
        def store(target, key):
            target[key] = 0
        def remove(target, key):
            del target[key]
        self.assertEqual(message(IndexError, lambda: [1][5]), 'list index out of range')
        self.assertEqual(message(IndexError, lambda: (1,)[5]), 'tuple index out of range')
        self.assertEqual(message(IndexError, lambda: 'x'[5]), 'string index out of range')
        self.assertEqual(message(IndexError, lambda: u'x'[-5]), 'string index out of range')
        self.assertEqual(message(IndexError, lambda: xrange(3)[5]), 'xrange object index out of range')
        self.assertEqual(message(IndexError, store, [1], 5), 'list assignment index out of range')
        self.assertEqual(message(IndexError, remove, [1], -5), 'list assignment index out of range')
        self.assertEqual(message(IndexError, lambda: [1][2 ** 70]), "cannot fit 'long' into an index-sized integer")
        self.assertEqual(message(TypeError, lambda: [1]['a']), 'list indices must be integers, not str')
        self.assertEqual(message(TypeError, lambda: (1,)[1.5]), 'tuple indices must be integers, not float')
        self.assertEqual(message(TypeError, lambda: 'x'[None]), 'string indices must be integers, not NoneType')
        self.assertEqual(message(TypeError, lambda: u'x'['a']), 'string indices must be integers')
        self.assertEqual(message(TypeError, lambda: xrange(3)['a']), "sequence index must be integer, not 'str'")
        self.assertEqual(message(TypeError, lambda: None[0]), "'NoneType' object has no attribute '__getitem__'")
        self.assertEqual(message(TypeError, lambda: set()[0]), "'set' object does not support indexing")
        self.assertEqual(message(TypeError, store, (1,), 0), "'tuple' object does not support item assignment")
        self.assertEqual(message(TypeError, remove, (1,), 0), "'tuple' object doesn't support item deletion")
        self.assertEqual(message(TypeError, remove, (1,), slice(None)), "'tuple' object does not support item deletion")
        self.assertEqual(message(TypeError, lambda: [1, 2]['a':]), 'slice indices must be integers or None or have an __index__ method')
        self.assertEqual(message(TypeError, slice(None).indices, 'a'), "'str' object cannot be interpreted as an index")
        values = range(5)
        self.assertEqual(message(TypeError, values.__setitem__, slice(None, None, 2), 5), 'must assign iterable to extended slice')
        self.assertEqual(message(TypeError, values.__setitem__, slice(1, 2), 5), 'can only assign an iterable')
        self.assertEqual(message(ValueError, values.__setitem__, slice(None, None, 2), [1]), 'attempt to assign sequence of size 1 to extended slice of size 3')
        self.assertRaises(MemoryError, [1, 2].__imul__, 2 ** 62)

    def test_index_callbacks_may_shrink_the_list(self):
        class Shrinking(object):
            def __init__(self, target, contents, index):
                self.target = target
                self.contents = contents
                self.index = index
            def __index__(self):
                self.target.__init__(self.contents)
                return self.index
        items = range(50)
        with self.assertRaises(IndexError) as failure:
            items[Shrinking(items, [1], 40)]
        self.assertEqual(str(failure.exception), 'list index out of range')
        items = range(50)
        self.assertRaises(IndexError, items.__setitem__, Shrinking(items, [1], 40), 5)
        self.assertEqual(items, [1])
        items = range(50)
        self.assertRaises(IndexError, items.__delitem__, Shrinking(items, [1], 40))
        self.assertEqual(items, [1])
        items = range(50)
        self.assertEqual(items[10:Shrinking(items, [1], 40)], [])
        items = range(50)
        items[10:Shrinking(items, range(30), 40)] = []
        self.assertEqual(items, range(10))
        items = range(50)
        del items[10:Shrinking(items, [1], 40):2]
        self.assertEqual(items, [1])

    def test_classic_instance_slices_use_offsets_only_for_indices(self):
        class Recorder:
            def __getitem__(self, key):
                return key
            def __setitem__(self, key, value):
                self.stored = (key, value)
            def __delitem__(self, key):
                self.deleted = key
        class Sized(Recorder):
            def __len__(self):
                return 10
        class Index(object):
            def __index__(self):
                return 3
        recorder = Recorder()
        self.assertEqual(recorder['a':'b'], slice('a', 'b', None))
        self.assertEqual(recorder[1.5:2], slice(1.5, 2, None))
        self.assertEqual(recorder[1:None], slice(1, None, None))
        self.assertEqual(recorder[1:2], slice(1, 2, None))
        self.assertEqual(recorder[Index():], slice(3, 9223372036854775807, None))
        self.assertRaises(AttributeError, lambda: recorder[-1:])
        recorder['x':'y'] = 1
        self.assertEqual(recorder.stored, (slice('x', 'y', None), 1))
        del recorder[1.5:]
        self.assertEqual(recorder.deleted, slice(1.5, None, None))
        sized = Sized()
        self.assertEqual(sized[-1:], slice(9, 9223372036854775807, None))
        self.assertEqual(sized[:-2], slice(0, 8, None))
        self.assertEqual(sized[-10 ** 30:], slice(-9223372036854775798, 9223372036854775807, None))

    def test_sort_matches_listsort(self):
        compared = [0]
        limit = [0]
        def counting(left, right):
            compared[0] += 1
            if compared[0] == limit[0]:
                raise ValueError('stop')
            return cmp(left, right)
        seed = [1]
        def random(bound):
            seed[0] = (seed[0] * 1103515245 + 12345) & 0x7fffffff
            return seed[0] % bound
        shapes = [[random(100000) for index in range(2000)], range(2000) + [random(2000) for index in range(10)],
                  range(0, 4000, 2) + range(1, 4000, 2), range(1000, 2000) + range(1000), [random(3) for index in range(2000)]]
        counts = []
        for shape in shapes:
            compared[0] = 0
            values = list(shape)
            values.sort(cmp=counting)
            self.assertEqual(values, sorted(shape))
            counts.append(compared[0])
        self.assertEqual(counts, [19270, 2189, 7998, 2027, 10696])
        values = [5, 3, 9, 1, 7, 2, 8, 6, 4, 0]
        compared[0] = 0
        limit[0] = 12
        self.assertRaises(ValueError, values.sort, cmp=counting)
        self.assertEqual(values, [1, 2, 3, 5, 7, 9, 8, 6, 4, 0])
        def reinitialize(value):
            values.__init__([9, 8])
            return value
        values = [3, 1, 2]
        with self.assertRaises(ValueError) as failure:
            values.sort(key=reinitialize)
        self.assertEqual(str(failure.exception), 'list modified during sort')
        self.assertEqual(values, [1, 2, 3])
        self.assertRaises(TypeError, [1, 2].sort, cmp=lambda left, right: 1L)

    def test_deep_nesting_in_repr_and_hash(self):
        nested_list = []
        nested_dict = {}
        for index in range(5000):
            nested_list = [nested_list]
            nested_dict = {1: nested_dict}
        message = 'maximum recursion depth exceeded while getting the repr of an object'
        for function, value in ((repr, nested_list), (str, nested_list), (repr, nested_dict)):
            with self.assertRaises(RuntimeError) as failure:
                function(value)
            self.assertEqual(str(failure.exception), message)
        nested_tuple = ()
        for index in range(100000):
            nested_tuple = (nested_tuple,)
        self.assertIsInstance(hash(nested_tuple), int)
        self.assertEqual(hash(((((1, 'a'),),),)), hash(((((1, 'a'),),),)))

    def test_repr_reentry_through_user_repr(self):
        members = set()
        items = []
        class SetHolder(object):
            def __repr__(self):
                return 'H' + repr(members)
        class ListHolder(object):
            def __repr__(self):
                return 'H' + repr(items)
        class Invalid(object):
            def __repr__(self):
                return 42
        members.add(SetHolder())
        items.append(ListHolder())
        self.assertEqual(repr(members), 'set([Hset(...)])')
        self.assertEqual(repr(items), '[H[...]]')
        with self.assertRaises(TypeError) as failure:
            repr([Invalid()])
        self.assertEqual(str(failure.exception), '__repr__ returned non-string (type int)')

    def test_sequence_comparisons_and_containment(self):
        left = []
        right = []
        class Emptying(object):
            def __eq__(self, other):
                del left[:]
                del right[:]
                return False
        left[:] = [Emptying(), Emptying()]
        right[:] = [Emptying(), Emptying()]
        self.assertTrue(left == right)
        compared = []
        class Probe(object):
            def __eq__(self, other):
                compared.append('probe')
                return True
        class Stored(object):
            def __eq__(self, other):
                compared.append('stored')
                return False
        self.assertTrue(Probe() in [Stored()])
        self.assertTrue(Probe() in (Stored(),))
        self.assertTrue(Probe() in iter([Stored()]))
        self.assertTrue(Probe() in {1: Stored()}.viewvalues())
        self.assertEqual(compared, ['probe'] * 4)
        self.assertRaises(TypeError, lambda: [1] & set([1]))
        self.assertEqual(sorted({1: 2, 3: 4}.viewkeys() & [1]), [1])

    def test_list_clearing_releases_items_from_the_end(self):
        released = []
        class Tracked(object):
            def __init__(self, name):
                self.name = name
            def __del__(self):
                released.append(self.name)
        for clear in (lambda values: values.__imul__(0), lambda values: values.__delslice__(0, 3), lambda values: values.__setslice__(0, 3, [9])):
            values = [Tracked('a'), Tracked('b'), Tracked('c')]
            del released[:]
            clear(values)
            self.assertEqual(released, ['c', 'b', 'a'])

    def test_classic_iteration_errors(self):
        class Plain:
            pass
        class ReturnsPlain:
            def __iter__(self):
                return Plain()
        class ReturnsInteger:
            def __iter__(self):
                return 1
        def message(function):
            try:
                function()
            except TypeError as exception:
                return str(exception)
            raise AssertionError('no TypeError')
        self.assertEqual(message(lambda: iter(Plain())), 'iteration over non-sequence')
        self.assertEqual(message(lambda: iter(ReturnsInteger())), "__iter__ returned non-iterator of type 'int'")
        self.assertEqual(message(lambda: list(ReturnsPlain())), 'instance has no next() method')

    def test_sort_parses_arguments_like_pyarg(self):
        def message(function, *args, **kwargs):
            try:
                function(*args, **kwargs)
            except TypeError as exception:
                return str(exception)
            raise AssertionError('no TypeError')
        values = [2, 1]
        self.assertEqual(message(values.sort, foo=1), "'foo' is an invalid keyword argument for this function")
        self.assertEqual(message(values.sort, None, cmp=None), "Argument given by name ('cmp') and position (1)")
        self.assertEqual(message(values.sort, 1, 2, 3, 4), 'sort() takes at most 3 arguments (4 given)')
        self.assertEqual(message(values.sort, reverse=1.5), 'integer argument expected, got float')
        self.assertEqual(message(values.sort, iterable=[2]), "'iterable' is an invalid keyword argument for this function")
        values.sort(None, None, 1)
        self.assertEqual(values, [2, 1])

    def test_container_repr_encodes_unicode_results(self):
        class Accented(object):
            def __repr__(self):
                return u'\xe9'
        class Plain(object):
            def __repr__(self):
                return u'abc'
        self.assertEqual(repr([Plain()]), '[abc]')
        for container in ([Accented()], (Accented(),), {1: Accented()}):
            with self.assertRaises(UnicodeEncodeError) as failure:
                repr(container)
            self.assertEqual(failure.exception.args, ('ascii', u'\xe9', 0, 1, 'ordinal not in range(128)'))
