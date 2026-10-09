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

    def test_dict_merge_probes_keys_like_hasattr(self):
        lookups = []
        class Source(object):
            def __init__(self, keys_attribute):
                self.keys_attribute = keys_attribute
            def __getattr__(self, name):
                lookups.append(name)
                if isinstance(self.keys_attribute, BaseException):
                    raise self.keys_attribute
                return self.keys_attribute
            def __getitem__(self, key):
                return ('value', key)
            def __iter__(self):
                return iter([('x', 1), ('y', 2)])
        class Recursive(object):
            def __getattr__(self, name):
                return getattr(self, name + '_')
            def __iter__(self):
                return iter([('z', 3)])
        self.assertEqual(dict(Source(lambda: ['a', 'b'])), {'a': ('value', 'a'), 'b': ('value', 'b')})
        self.assertEqual(lookups, ['keys', 'keys'])
        del lookups[:]
        merged = {}
        merged.update(Source(KeyError('keys')))
        self.assertEqual(merged, {'x': 1, 'y': 2})
        self.assertEqual(lookups, ['keys'])
        self.assertEqual(dict(Source(RuntimeError('probe'))), {'x': 1, 'y': 2})
        self.assertEqual(dict(Recursive()), {'z': 3})
        with self.assertRaises(TypeError) as failure:
            dict(Source('text'))
        self.assertEqual(str(failure.exception), "attribute of type 'str' is not callable")

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

    def test_sort_callbacks_may_leave_the_list_empty(self):
        def sort_through(mutate, use_cmp):
            values = [3, 1, 2]
            def key(value):
                mutate(values)
                return value
            def compare(left, right):
                mutate(values)
                return cmp(left, right)
            try:
                if use_cmp:
                    values.sort(cmp=compare)
                else:
                    values.sort(key=key)
            except ValueError as failure:
                return str(failure), values
            return None, values
        def pop_empty(values):
            self.assertRaises(IndexError, values.pop)
        accepted = [lambda values: values.__delslice__(0, 3), lambda values: values.__setslice__(0, 3, []),
                    lambda values: values.__setslice__(1, 3, ()), lambda values: values.__delitem__(slice(None, None, 2)),
                    lambda values: values.__setitem__(slice(None, None, 2), []), lambda values: values.extend([]),
                    lambda values: values.extend(values), lambda values: values.__iadd__(()), lambda values: values.__imul__(0),
                    lambda values: values.__imul__(2), lambda values: values.reverse(), lambda values: values.sort(),
                    lambda values: values.__init__(), lambda values: values.__init__([]), pop_empty]
        for mutate in accepted:
            for use_cmp in (False, True):
                self.assertEqual(sort_through(mutate, use_cmp), (None, [1, 2, 3]))
        def append_and_pop(values):
            values.append(1)
            values.pop()
        def append_and_clear(values):
            values.append(1)
            del values[:]
        rejected = [lambda values: values.append(1), lambda values: values.insert(0, 1), lambda values: values.__setslice__(0, 3, [1]),
                    lambda values: values.extend([1]), lambda values: values.extend(value for value in []),
                    lambda values: values.__init__([1]), append_and_pop, append_and_clear]
        for mutate in rejected:
            for use_cmp in (False, True):
                self.assertEqual(sort_through(mutate, use_cmp), ('list modified during sort', [1, 2, 3]))

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

    def test_dispatch_overrides_follow_class_changes(self):
        class Base(object):
            pass
        class Value(Base):
            pass
        class Other(object):
            def __add__(self, other):
                return 'other-add'
            def __eq__(self, other):
                return 'other-eq'
        left = Value()
        right = Value()
        def outcomes():
            operations = (
                lambda: left + right,
                lambda: 1 + left,
                lambda: left == right,
                lambda: type(left < right).__name__,
                lambda: bool(left),
                lambda: 1 in left,
                lambda: left[3],
                lambda: hash(left) == 7,
            )
            results = []
            for operation in operations:
                try:
                    results.append(operation())
                except TypeError as exception:
                    results.append(type(exception).__name__)
            return results
        plain = outcomes()
        self.assertEqual(plain, ['TypeError', 'TypeError', False, 'bool', True, 'TypeError', 'TypeError', False])
        Base.__add__ = lambda self, other: 'add'
        Base.__radd__ = lambda self, other: 'radd'
        Base.__eq__ = lambda self, other: 'eq'
        Base.__lt__ = lambda self, other: 'lt'
        Base.__nonzero__ = lambda self: False
        Base.__contains__ = lambda self, item: True
        Base.__getitem__ = lambda self, index: index * 2
        Base.__hash__ = lambda self: 7
        self.assertEqual(outcomes(), ['add', 'radd', 'eq', 'str', False, True, 6, True])
        Value.__add__ = lambda self, other: 'value-add'
        self.assertEqual(left + right, 'value-add')
        del Value.__add__
        self.assertEqual(left + right, 'add')
        for name in ('__add__', '__radd__', '__eq__', '__lt__', '__nonzero__', '__contains__', '__getitem__', '__hash__'):
            delattr(Base, name)
        self.assertEqual(outcomes(), plain)
        Value.__bases__ = (Other,)
        self.assertEqual(left + right, 'other-add')
        self.assertEqual(left == right, 'other-eq')
        Value.__bases__ = (Base,)
        self.assertEqual(outcomes(), plain)
        left.__class__ = Other
        self.assertEqual(left + right, 'other-add')
        left.__class__ = Value
        Base.__hash__ = None
        self.assertRaises(TypeError, hash, left)

    def test_inplace_and_reflected_dispatch_after_warming(self):
        class Number(object):
            def __init__(self, value):
                self.value = value
        number = Number(1)
        for attempt in range(3):
            self.assertRaises(TypeError, lambda: 2 * number)
        def inplace():
            target = number
            target += 5
            return target
        self.assertRaises(TypeError, inplace)
        Number.__rmul__ = lambda self, other: Number(self.value * other)
        Number.__add__ = lambda self, other: Number(self.value + other)
        self.assertEqual((2 * number).value, 2)
        self.assertEqual(inplace().value, 6)
        self.assertIsNot(inplace(), number)
        Number.__iadd__ = lambda self, other: self
        self.assertIs(inplace(), number)
        del Number.__iadd__
        self.assertIsNot(inplace(), number)

    def test_special_methods_survive_removing_themselves(self):
        class Fragile(object):
            def __add__(self, other):
                del Fragile.__add__
                return 'add'
            def __eq__(self, other):
                del Fragile.__eq__
                return 'eq'
            def __getitem__(self, index):
                del Fragile.__getitem__
                return index
            def __len__(self):
                del Fragile.__len__
                return 0
            def __hash__(self):
                del Fragile.__hash__
                return 11
            def next(self):
                del Fragile.next
                return 'next'
        value = Fragile()
        self.assertEqual(value + 1, 'add')
        self.assertRaises(TypeError, lambda: value + 1)
        self.assertEqual(value == 1, 'eq')
        self.assertEqual(value == 1, False)
        self.assertEqual(value[4], 4)
        self.assertRaises(TypeError, lambda: value[4])
        self.assertEqual(bool(value), False)
        self.assertEqual(bool(value), True)
        self.assertEqual(hash(value), 11)
        self.assertEqual(hash(value) == 11, False)
        self.assertEqual(next(value), 'next')
        self.assertRaises(TypeError, next, value)

    def test_dict_item_pairs_stay_independent(self):
        values = dict((index, str(index)) for index in range(40))
        pairs = list(values.iteritems())
        self.assertEqual(sorted(pairs), sorted(values.items()))
        self.assertEqual(len(set(pairs)), 40)
        iterator = values.iteritems()
        held = next(iterator)
        snapshot = tuple(held)
        later = [pair for pair in iterator]
        self.assertEqual(held, snapshot)
        self.assertEqual(sorted(later + [held]), sorted(values.items()))
        total = 0
        for key, value in values.iteritems():
            total += key + len(value)
            values[key] = value + '!'
        self.assertEqual(total, sum(range(40)) + sum(len(str(index)) for index in range(40)))
        self.assertEqual(sorted(values.values())[:2], ['0!', '1!'])
        seen = []
        for pair in values.viewitems():
            seen.append(pair)
        self.assertEqual(sorted(seen), sorted(values.items()))
        iterator = values.iteritems()
        next(iterator)
        values[100] = 'x'
        self.assertRaises(RuntimeError, next, iterator)

    def test_exact_operand_arithmetic_matches_slots(self):
        import sys
        smallest = -sys.maxint - 1
        cases = []
        for left in (7, -7, 0, smallest):
            for right in (3, -3, 1, -1):
                cases.append((left % right, left // right, left / right, divmod(left, right)))
        self.assertEqual(repr(smallest // -1), repr(sys.maxint + 1))
        self.assertEqual(repr(smallest % -1), '0L')
        self.assertEqual(repr(smallest / -1), repr(sys.maxint + 1))
        self.assertEqual(cases[:12], [(1, 2, 2, (2, 1)), (-2, -3, -3, (-3, -2)), (0, 7, 7, (7, 0)), (0, -7, -7, (-7, 0)), (2, -3, -3, (-3, 2)), (-1, 2, 2, (2, -1)), (0, -7, -7, (-7, 0)), (0, 7, 7, (7, 0))] + [(0, 0, 0, (0, 0))] * 4)
        self.assertEqual(repr(cases[15]), repr((smallest % -1, smallest // -1, smallest / -1, divmod(smallest, -1))))
        for operation, message in ((lambda: 5 % 0, 'integer division or modulo by zero'), (lambda: 5 // 0, 'integer division or modulo by zero'), (lambda: 5.0 % 0.0, 'float modulo'), (lambda: 5.0 // 0.0, 'float divmod()')):
            try:
                operation()
            except ZeroDivisionError as exception:
                self.assertEqual(str(exception), message)
            else:
                self.fail('no ZeroDivisionError')
        floats = []
        for left in (5.5, -5.5, 0.0, -0.0, 1e300):
            for right in (2.0, -2.0, 1e-300, float('inf'), float('-inf')):
                floats.append(repr((left % right, left // right)))
        self.assertEqual(floats, [
            '(1.5, 2.0)', '(-0.5, -3.0)', '(9.297525904218413e-301, 5.5e+300)', '(5.5, 0.0)', '(-inf, -1.0)',
            '(0.5, -3.0)', '(-1.5, 2.0)', '(7.02474095781587e-302, -5.5e+300)', '(inf, -1.0)', '(-5.5, 0.0)',
            '(0.0, 0.0)', '(-0.0, -0.0)', '(0.0, 0.0)', '(0.0, 0.0)', '(-0.0, -0.0)',
            '(0.0, -0.0)', '(-0.0, 0.0)', '(0.0, -0.0)', '(0.0, -0.0)', '(-0.0, 0.0)',
            '(0.0, 5e+299)', '(-0.0, -5e+299)', '(4.891554850853602e-301, inf)', '(1e+300, 0.0)', '(-inf, -1.0)'])
        self.assertEqual(repr(float('nan') % 2.0), 'nan')
        self.assertEqual(repr([True + True, 2L % 3, (1 + 2j) * 2, True << 3, 5L // 2, 3 + 2L, 7 % True, 2 ** 10, 2.0 ** 0.5]), repr([2, 2L, (2 + 4j), 8, 2L, 5L, 0, 1024, 1.4142135623730951]))
        self.assertRaises(TypeError, lambda: 1.5 << 2)
        self.assertRaises(TypeError, lambda: 1j << 2)
        class Modulo(int):
            def __mod__(self, other):
                return 'mod'
            def __rfloordiv__(self, other):
                return 'rfloordiv'
        self.assertEqual(Modulo(5) % 3, 'mod')
        self.assertEqual(5 // Modulo(3), 'rfloordiv')
        self.assertEqual(5 % Modulo(3), 2)

    def test_exact_operand_concatenation_and_formatting(self):
        left = [1, 2]
        right = [3]
        joined = left + right
        self.assertEqual((joined, left, right), ([1, 2, 3], [1, 2], [3]))
        self.assertEqual(left + left, [1, 2, 1, 2])
        self.assertEqual([] + [], [])
        alias = left
        alias += right
        self.assertIs(alias, left)
        self.assertEqual(left, [1, 2, 3])
        pair = (1, 2)
        grown = pair
        grown += (3,)
        self.assertEqual((pair, grown), ((1, 2), (1, 2, 3)))
        self.assertEqual('ab' + u'cd', u'abcd')
        self.assertEqual(type('ab' + 'cd'), str)
        self.assertEqual(u'\xe9' + 'x', u'\xe9x')
        self.assertRaises(UnicodeDecodeError, lambda: u'x' + '\xe9')
        text = 'a'
        text += 'b'
        self.assertEqual(text, 'ab')
        self.assertEqual('%s-%d' % ('x', 3), 'x-3')
        self.assertEqual('%s' % u'\xe9', u'\xe9')
        self.assertEqual(u'%s' % 'x', u'x')
        self.assertEqual('%(a)s' % {'a': 1}, '1')
        self.assertEqual('%s' % [1], '[1]')
        self.assertRaises(TypeError, lambda: '%d' % 'x')
        values = range(10)
        self.assertEqual((values[2:5], values[5:2], values[:0], values[::3], [][0:0], values[-3:]), ([2, 3, 4], [], [], [0, 3, 6, 9], [], [7, 8, 9]))
        class Text(str):
            def __add__(self, other):
                return 'text-add'
            def __mod__(self, other):
                return 'text-mod'
        self.assertEqual(Text('a') + 'b', 'text-add')
        self.assertEqual(Text('a') % 'b', 'text-mod')

    def test_exact_scalar_comparisons_match_rich_compare(self):
        import sys
        nan = float('nan')
        self.assertEqual([nan < 1.0, nan > 1.0, nan <= nan, nan >= nan, nan == nan, nan != nan], [False, False, False, False, False, True])
        self.assertEqual([nan == nan, [nan] == [nan], nan in [nan], (nan,) < (nan, 1)], [False, True, True, True])
        self.assertEqual(sorted([0.0, -0.0, -1.5, 2.5]), [-1.5, 0.0, -0.0, 2.5])
        self.assertEqual(sorted([-0.0, 0.0]), [-0.0, 0.0])
        self.assertEqual(sorted(['b', 'a\x00', 'a', '\xff', 'ab', '']), ['', 'a', 'a\x00', 'ab', 'b', '\xff'])
        self.assertEqual(['abc' < 'abd', 'abc' < 'ab', 'ab' <= 'ab', 'b' > 'abc', 'x' != 'x', 'x' == 'x'], [True, False, True, True, False, True])
        smallest = -sys.maxint - 1
        self.assertEqual([smallest < sys.maxint, smallest == smallest, sys.maxint >= sys.maxint, cmp(smallest, sys.maxint), cmp(3, 3), cmp(4, 3)], [True, True, True, -1, 0, 1])
        self.assertEqual(sorted([3, -2, sys.maxint, smallest, 0]), [smallest, -2, 0, 3, sys.maxint])
        self.assertEqual(sorted([(2, 'b'), (1, 'z'), (2, 'a')]), [(1, 'z'), (2, 'a'), (2, 'b')])
        self.assertEqual(sorted(range(10), key=lambda value: value % 3), [0, 3, 6, 9, 1, 4, 7, 2, 5, 8])
        self.assertEqual(sorted(['b', 'A', 'a'], key=str.lower), ['A', 'a', 'b'])
        mixed = [1 < 1.5, 2 == 2.0, 2 != 2.0, -0.0 == 0, 3 > nan, 3 != nan, 2 ** 53 + 1 == float(2 ** 53 + 1), sys.maxint < float(sys.maxint), float('inf') > sys.maxint, -float('inf') < smallest, 0.5 <= 0, 7 >= 6.999]
        self.assertEqual(mixed, [True, True, False, True, False, True, False, True, True, True, False, True])
        self.assertEqual(repr(sorted([3, 1.5, -2, 2.0, 0, -0.0, 2])), '[-2, 0, -0.0, 1.5, 2.0, 2, 3]')

    def test_exhausted_iterators_release_their_sequence_once(self):
        state = {'iterator': None, 'deleted': 0}
        def make(base):
            class Reentrant(base):
                def __del__(self):
                    state['deleted'] += 1
                    try:
                        next(state['iterator'])
                    except StopIteration:
                        pass
            return Reentrant
        kinds = ((list, reversed), (tuple, reversed), (str, reversed), (bytearray, reversed), (set, iter), (frozenset, iter), (list, enumerate))
        for count, (base, factory) in enumerate(kinds):
            state['iterator'] = factory(make(base)())
            self.assertEqual(state['deleted'], count)
            self.assertRaises(StopIteration, next, state['iterator'])
            self.assertEqual(state['deleted'], count + 1)
            self.assertRaises(StopIteration, next, state['iterator'])
            state['iterator'] = None

    def test_base_repr_called_from_an_overriding_subclass(self):
        class Set(set):
            def __repr__(self):
                return 'S<' + set.__repr__(self) + '>'
        class Frozen(frozenset):
            def __repr__(self):
                return 'Z<' + frozenset.__repr__(self) + '>'
        class Float(float):
            def __repr__(self):
                return 'F<' + float.__repr__(self) + '>'
            def __str__(self):
                return 'f<' + float.__str__(self) + '>'
        self.assertEqual((repr(Set([1])), repr(Frozen([2])), repr(Float(1.5)), str(Float(0.1))), ('S<Set([1])>', 'Z<Frozen([2])>', 'F<1.5>', 'f<0.1>'))
        def message(function, *args, **keywords):
            try:
                function(*args, **keywords)
            except TypeError as exception:
                return str(exception)
            raise AssertionError('no TypeError')
        self.assertEqual(message((1.5).__str__, 1), 'expected 0 arguments, got 1')
        self.assertEqual(message((1.5).__repr__, x=1), "wrapper __repr__ doesn't take keyword arguments")

    def test_dict_view_repr_marks_recursion(self):
        values = {}
        values[42] = values.viewvalues()
        items = {}
        items[1] = items.viewitems()
        keys = {}
        keys[1] = keys.viewkeys()
        self.assertEqual((repr(values), repr(items), str(keys)), ('{42: dict_values([...])}', '{1: dict_items([(1, ...)])}', '{1: dict_keys([1])}'))
        del values[42], items[1], keys[1]

    def test_hashable_set_subclass_is_found_by_its_own_hash(self):
        for base in (set, frozenset):
            class Hashed(base):
                def __hash__(self):
                    return id(self) & 0x7fffffff
            element = Hashed()
            container = set([element])
            self.assertIn(element, container)
            container.remove(element)
            container.add(element)
            container.discard(element)
            self.assertEqual(len(container), 0)
        container = set([frozenset([1]), 2])
        self.assertIn(set([1]), container)
        container.remove(set([1]))
        container.discard(set([2]))
        self.assertEqual(container, set([2]))
        class Unhashable(set):
            def __hash__(self):
                raise ValueError('no hash')
        self.assertRaises(ValueError, set().__contains__, Unhashable())
        with self.assertRaises(KeyError) as failure:
            set().remove(set([5]))
        self.assertEqual(failure.exception.args, (set([5]),))

    def test_builtin_reduce_keeps_instance_state_and_iteration(self):
        class Set(set):
            pass
        class Bytes(bytearray):
            pass
        class Iterated(frozenset):
            def __iter__(self):
                return iter([9])
        values = Set([1])
        values.x = 10
        data = Bytes('a')
        data.y = [1]
        self.assertEqual(values.__reduce__(), (Set, ([1],), {'x': 10}))
        self.assertEqual(data.__reduce__(), (Bytes, (u'a', 'latin-1'), {'y': [1]}))
        self.assertEqual(data.__reduce_ex__(2), (Bytes, (u'a', 'latin-1'), {'y': [1]}))
        self.assertEqual(Iterated([1]).__reduce__(), (Iterated, ([9],), {}))
        self.assertEqual((set([1]).__reduce__(), bytearray('a').__reduce__()), ((set, ([1],), None), (bytearray, (u'a', 'latin-1'), None)))
        self.assertEqual(xrange(3).__reduce__(1, 2), (xrange, (0, 3, 1)))
        def message(function, *args, **keywords):
            try:
                function(*args, **keywords)
            except TypeError as exception:
                return str(exception)
            raise AssertionError('no TypeError')
        self.assertEqual(message(set().__reduce__, 1), '__reduce__() takes no arguments (1 given)')
        self.assertEqual(message(frozenset().__reduce__, k=1), '__reduce__() takes no keyword arguments')
        self.assertEqual(message(slice(1).__reduce__, 1, 2), '__reduce__() takes no arguments (2 given)')

    def test_initializer_argument_errors(self):
        class Set(set):
            pass
        def message(function, *args, **keywords):
            try:
                function(*args, **keywords)
            except TypeError as exception:
                return str(exception)
            raise AssertionError('no TypeError')
        self.assertEqual(message([].__init__, 1, x=1), 'list() takes at most 1 argument (2 given)')
        self.assertEqual(message([].__init__, x=1), "'x' is an invalid keyword argument for this function")
        self.assertEqual(message(list, x=1), "'x' is an invalid keyword argument for this function")
        self.assertEqual(message(tuple, x=1), "'x' is an invalid keyword argument for this function")
        self.assertEqual(message(set().__init__, 'a', k=1), 'set() does not take keyword arguments')
        self.assertEqual(message(Set, k=1), 'set() does not take keyword arguments')
        self.assertEqual(message(Set().__init__, 1, 2), 'Set expected at most 1 arguments, got 2')
        self.assertEqual(message({}.__init__, 1, 2), 'dict expected at most 1 arguments, got 2')
        self.assertEqual(list(sequence=(1, 2)), [1, 2])
        target = [5]
        target.__init__(sequence='ab')
        self.assertEqual(target, ['a', 'b'])

    def test_dict_ordering_falls_back_to_cmp(self):
        calls = []
        class Compared(dict):
            def __cmp__(self, other):
                calls.append('cmp')
                return 0
        class Plain(dict):
            pass
        self.assertEqual(({}.__lt__({}), {1: 2}.__gt__({}), {}.__le__(1), {}.__eq__({}), {}.__ne__([])), (NotImplemented, NotImplemented, NotImplemented, True, NotImplemented))
        self.assertEqual((Compared() >= Compared(), Compared() < {}, {1: 2} < {1: 3}), (True, False, True))
        self.assertEqual(calls, ['cmp', 'cmp'])
        with self.assertRaises(TypeError) as failure:
            Plain().__cmp__(1)
        self.assertEqual(str(failure.exception), "Plain.__cmp__(x,y) requires y to be a 'Plain', not a 'int'")

    def test_unhashable_messages_name_the_type(self):
        def message(value):
            try:
                hash(value)
            except TypeError as exception:
                return str(exception)
            raise AssertionError('no TypeError')
        self.assertEqual([message(bytearray()), message({}.viewkeys()), message({}.viewitems()), message(memoryview('a')), message([])], ["unhashable type: 'bytearray'", "unhashable type: 'dict_keys'", "unhashable type: 'dict_items'", "unhashable type: 'memoryview'", "unhashable type: 'list'"])
        self.assertRaises(TypeError, set().discard, memoryview('ab'))
        self.assertEqual({}.pop(bytearray('a'), 1), 1)

    def test_sequence_index_bounds_require_indices(self):
        class Index(object):
            def __index__(self):
                return 1
        for bound in (None, 1.0, '1'):
            with self.assertRaises(TypeError) as failure:
                [1, 2].index(2, bound)
            self.assertEqual(str(failure.exception), 'slice indices must be integers or have an __index__ method')
            self.assertRaises(TypeError, (1, 2).index, 2, 0, bound)
        self.assertEqual(([1, 2].index(2, Index()), (1, 2).index(2, 0, 2 ** 70)), (1, 1))

    def test_reversed_requires_a_sequence_length(self):
        class Error(Exception):
            pass
        class Classic:
            def __getitem__(self, index):
                return index
        def message(error, value):
            try:
                reversed(value)
            except error as exception:
                return str(exception)
            raise AssertionError('no %s' % error.__name__)
        self.assertEqual(message(TypeError, Error()), "object of type 'Error' has no len()")
        self.assertEqual(message(TypeError, memoryview('ab')), "object of type 'memoryview' has no len()")
        self.assertEqual(message(TypeError, {}), 'argument to reversed() must be a sequence')
        self.assertEqual(message(AttributeError, Classic()), "Classic instance has no attribute '__len__'")

    def test_simple_slices_use_the_sequence_slot(self):
        calls = []
        class Tuple(tuple):
            def __getitem__(self, index):
                calls.append('getitem')
                return 'item'
        class Text(unicode):
            def __getitem__(self, index):
                return None
        class Sized(list):
            def __len__(self):
                calls.append('len')
                return 5
        class Deleting(list):
            def __delitem__(self, index):
                calls.append('delitem')
            def __setitem__(self, index, value):
                calls.append('setitem')
        class Error(Exception):
            def __len__(self):
                calls.append('len')
                return 2
        self.assertEqual((Tuple((1, 2, 3))[1:3], Text(u'abc')[0:0], Sized([1, 2])[-1:-1]), ((2, 3), u'', []))
        values = Deleting([1, 2, 3])
        del values[0:1]
        values[0:1] = [7]
        self.assertEqual(values, [7, 3])
        self.assertEqual(Tuple((1, 2))[::2], 'item')
        self.assertEqual((Error(1, 2)[-1], Exception(1, 2)[-1], Exception(1, 2, 3)[-2:], Exception(1)[-100:-100]), (2, 2, (2, 3), ()))
        self.assertEqual(calls, ['len', 'getitem', 'len'])
        for key in (slice(None, None, 2), (1, 2)):
            with self.assertRaises(TypeError) as failure:
                Exception(1, 2, 3)[key]
            self.assertEqual(str(failure.exception), "sequence index must be integer, not '%s'" % type(key).__name__)
        self.assertRaises(IndexError, lambda: Exception(1)[1])

    def test_item_methods_defined_in_pairs(self):
        class Deleter(object):
            def __delitem__(self, key):
                pass
        class Setter(object):
            def __setitem__(self, key, value):
                pass
        def message(function):
            try:
                function()
            except AttributeError as exception:
                return str(exception)
            raise AssertionError('no AttributeError')
        def store():
            Deleter()[0] = 1
        def remove():
            del Setter()[0]
        def remove_slice():
            del Setter()[0:1]
        self.assertEqual((message(store), message(remove), message(remove_slice)), ('__setitem__', '__delitem__', '__delitem__'))
