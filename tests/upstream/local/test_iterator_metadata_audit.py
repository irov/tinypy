"""Project-authored iterator taxonomy, length callbacks and recovery checks."""

import unittest


def _error(callback, *args, **kwargs):
    try:
        callback(*args, **kwargs)
    except BaseException as error:
        return type(error).__name__, error.args
    raise AssertionError('operation must fail')


class Sequence(object):
    def __getitem__(self, index):
        if index < 2:
            return index
        raise IndexError
    def __len__(self):
        return 2


class IteratorMetadataAudit(unittest.TestCase):
    def test_bytearray_has_its_own_actual_iterator_type(self):
        iterator = iter(bytearray('ab'))
        owner = type(iterator)
        self.assertEqual(owner.__name__, 'bytearray_iterator')
        self.assertEqual(owner.__module__, '__builtin__')
        self.assertEqual(repr(owner), "<type 'bytearray_iterator'>")
        self.assertIsNot(owner, type(iter('ab')))

    def test_bytearray_subtype_uses_the_same_iterator_type(self):
        class Source(bytearray):
            pass
        self.assertIs(type(iter(Source('ab'))), type(iter(bytearray('ab'))))

    def test_builtin_iterator_names_and_modules(self):
        cases = (([1], 'listiterator'), ((1,), 'tupleiterator'),
                 ('a', 'iterator'), (u'a', 'iterator'),
                 (buffer('a'), 'iterator'), ({1: 2}, 'dictionary-keyiterator'),
                 (set([1]), 'setiterator'), (xrange(1), 'rangeiterator'))
        for source, name in cases:
            owner = type(iter(source))
            self.assertEqual(owner.__name__, name)
            self.assertEqual(owner.__module__, '__builtin__')

    def test_mapping_modes_keep_distinct_iterator_types(self):
        source = {1: 2}
        for iterator, name in ((source.iterkeys(), 'dictionary-keyiterator'),
                               (source.itervalues(), 'dictionary-valueiterator'),
                               (source.iteritems(), 'dictionary-itemiterator')):
            self.assertEqual(type(iterator).__name__, name)
            self.assertIs(iter(iterator), iterator)

    def test_reverse_iterator_taxonomy(self):
        for source, name in (([1], 'listreverseiterator'), ((1,), 'reversed'),
                             ('a', 'reversed'), (u'a', 'reversed'),
                             (bytearray('a'), 'reversed'), (xrange(1), 'rangeiterator')):
            self.assertEqual(type(reversed(source)).__name__, name)

    def test_list_reverse_hint_returns_long_including_after_eof(self):
        iterator = reversed([1])
        self.assertIs(type(iterator.__length_hint__()), long)
        self.assertEqual(iterator.__length_hint__(), 1L)
        self.assertEqual(list(iterator), [1])
        self.assertIs(type(iterator.__length_hint__()), long)
        self.assertEqual(iterator.__length_hint__(), 0L)
        self.assertIs(type(reversed((1,)).__length_hint__()), int)

    def test_bound_next_and_iter_are_method_wrappers(self):
        for iterator in (iter([1]), iter((1,)), iter('a'), iter(bytearray('a')),
                         iter({1: 2}), iter(set([1])), iter(xrange(1)),
                         enumerate([1]), reversed((1,)), iter(Sequence())):
            for name in ('next', '__iter__'):
                descriptor = getattr(type(iterator), name)
                bound = getattr(iterator, name)
                self.assertEqual(type(descriptor).__name__, 'wrapper_descriptor')
                self.assertEqual(type(bound).__name__, 'method-wrapper')
                self.assertIs(bound.__self__, iterator)
                self.assertIs(bound.__objclass__, type(iterator))

    def test_length_hint_is_an_ordinary_builtin_method(self):
        iterator = iter(bytearray('a'))
        self.assertEqual(type(iterator.__length_hint__).__name__, 'builtin_function_or_method')
        self.assertFalse(hasattr(iterator.__length_hint__, '__objclass__'))
        self.assertEqual(_error(iterator.__length_hint__, 1),
                         ('TypeError', ('__length_hint__() takes no arguments (1 given)',)))
        self.assertEqual(_error(iterator.__length_hint__, other=1),
                         ('TypeError', ('__length_hint__() takes no keyword arguments',)))

    def test_callable_and_enumerate_do_not_expose_length_hint(self):
        self.assertFalse(hasattr(iter(lambda: 0, 0), '__length_hint__'))
        self.assertFalse(hasattr(enumerate([1]), '__length_hint__'))

    def test_bytearray_next_arity_failure_does_not_advance(self):
        iterator = iter(bytearray('ab'))
        self.assertEqual(_error(iterator.next, 0), ('TypeError', ('expected 0 arguments, got 1',)))
        self.assertEqual(_error(iterator.next, other=0),
                         ('TypeError', ("wrapper next doesn't take keyword arguments",)))
        self.assertEqual(next(iterator), ord('a'))

    def test_bytearray_iterator_tracks_changes_before_exhaustion(self):
        source = bytearray('ab')
        iterator = iter(source)
        self.assertEqual(next(iterator), ord('a'))
        source[1] = ord('c')
        source.append(ord('d'))
        self.assertEqual(iterator.__length_hint__(), 2)
        self.assertEqual(list(iterator), [ord('c'), ord('d')])

    def test_bytearray_exhaustion_is_sticky_after_growth(self):
        source = bytearray('a')
        iterator = iter(source)
        self.assertEqual(list(iterator), [ord('a')])
        source.append(ord('b'))
        self.assertEqual(iterator.__length_hint__(), 0)
        self.assertEqual(next(iterator, 'done'), 'done')

    def test_bytearray_direct_hint_is_signed_after_shrink(self):
        source = bytearray('ab')
        iterator = iter(source)
        self.assertEqual(next(iterator), ord('a'))
        del source[:]
        self.assertEqual(iterator.__length_hint__(), -1)
        self.assertEqual(next(iterator, None), None)
        self.assertEqual(iterator.__length_hint__(), 0)

    def test_specialized_iterators_ignore_subtype_item_and_length_hooks(self):
        events = []
        for base in (list, tuple, bytearray):
            class Source(base):
                def __getitem__(self, index):
                    events.append('item')
                    raise AssertionError('specialized iterator reads stored items')
                def __len__(self):
                    events.append('len')
                    raise AssertionError('specialized hint reads stored length')
            value = Source('a') if base is bytearray else Source((1,))
            iterator = iter(value)
            self.assertEqual(iterator.__length_hint__(), 1)
            self.assertEqual(list(iterator), [ord('a')] if base is bytearray else [1])
        self.assertEqual(events, [])

    def test_text_subtype_iterators_invoke_item_and_length_hooks(self):
        for base in (str, unicode):
            events = []
            class Source(base):
                def __len__(self):
                    events.append('len')
                    return 4
                def __getitem__(self, index):
                    events.append(('item', index))
                    if index < 2:
                        return index + 10
                    raise IndexError
            iterator = iter(Source('ab'))
            self.assertEqual(iterator.__length_hint__(), 4)
            self.assertEqual(next(iterator), 10)
            self.assertEqual(iterator.__length_hint__(), 3)
            self.assertEqual(next(iterator), 11)
            self.assertEqual(next(iterator, None), None)
            self.assertEqual(iterator.__length_hint__(), 0)
            self.assertEqual(events, ['len', ('item', 0), 'len', ('item', 1), ('item', 2)])

    def test_text_subtype_length_only_override_affects_hint(self):
        for base in (str, unicode):
            class Source(base):
                def __len__(self):
                    return 4
            iterator = iter(Source('ab'))
            self.assertEqual(iterator.__length_hint__(), 4)
            self.assertEqual(next(iterator), base('a'))
            self.assertEqual(iterator.__length_hint__(), 3)

    def test_generic_sequence_hint_calls_length_and_subtracts_current_index(self):
        events = []
        class Source(Sequence):
            def __len__(self):
                events.append('len')
                return 3
        iterator = iter(Source())
        self.assertEqual(iterator.__length_hint__(), 3)
        self.assertEqual(next(iterator), 0)
        self.assertEqual(iterator.__length_hint__(), 2)
        self.assertEqual(events, ['len', 'len'])

    def test_generic_sequence_hint_converts_float_length(self):
        class Source(Sequence):
            def __len__(self):
                return 3.5
        iterator = iter(Source())
        self.assertEqual(iterator.__length_hint__(), 3)
        self.assertEqual(next(iterator), 0)
        self.assertEqual(iterator.__length_hint__(), 2)

    def test_classic_sequence_hint_requires_a_stored_integer_length(self):
        class Source:
            def __getitem__(self, index):
                return index
            def __len__(self):
                return 3.5
        self.assertEqual(_error(iter(Source()).__length_hint__),
                         ('TypeError', ('__len__() should return an int',)))

    def test_generic_hint_rejects_none_string_and_negative_lengths(self):
        for result, expected in ((None, ('TypeError', ('an integer is required',))),
                                 ('3', ('TypeError', ('an integer is required',))),
                                 (-2, ('ValueError', ('__len__() should return >= 0',)))):
            class Source(Sequence):
                def __len__(self):
                    return result
            iterator = iter(Source())
            self.assertEqual(_error(iterator.__length_hint__), expected)
            self.assertEqual(next(iterator), 0)

    def test_generic_hint_propagates_length_exception_identity_and_recovers(self):
        marker = ValueError('length marker')
        class Source(Sequence):
            fail = True
            def __len__(self):
                if self.fail:
                    raise marker
                return 2
        source = Source()
        iterator = iter(source)
        try:
            iterator.__length_hint__()
        except ValueError as error:
            self.assertIs(error, marker)
        else:
            self.fail('length callback must fail')
        source.fail = False
        self.assertEqual(iterator.__length_hint__(), 2)
        self.assertEqual(next(iterator), 0)

    def test_missing_sequence_length_is_an_error_before_eof(self):
        class Source(object):
            def __getitem__(self, index):
                raise IndexError
        iterator = iter(Source())
        self.assertEqual(_error(iterator.__length_hint__),
                         ('TypeError', ("object of type 'Source' has no len()",)))
        self.assertEqual(next(iterator, None), None)
        self.assertEqual(iterator.__length_hint__(), 0)

    def test_classic_missing_length_preserves_attribute_error(self):
        class Source:
            def __getitem__(self, index):
                raise IndexError
        iterator = iter(Source())
        self.assertEqual(_error(iterator.__length_hint__),
                         ('AttributeError', ("Source instance has no attribute '__len__'",)))
        self.assertEqual(next(iterator, None), None)
        self.assertEqual(iterator.__length_hint__(), 0)

    def test_hint_arity_failure_precedes_length_callback(self):
        events = []
        class Source(Sequence):
            def __len__(self):
                events.append('len')
                return 2
        iterator = iter(Source())
        self.assertEqual(_error(iterator.__length_hint__, 1),
                         ('TypeError', ('__length_hint__() takes no arguments (1 given)',)))
        self.assertEqual(events, [])

    def test_length_callback_reentry_uses_current_index_and_sticky_eof(self):
        state, events = {}, []
        class Source(Sequence):
            def __len__(self):
                events.append('len')
                if not state.get('drained'):
                    state['drained'] = True
                    for unused in range(3):
                        next(state['iterator'], None)
                return 3
        source = Source()
        iterator = iter(source)
        state['iterator'] = iterator
        try:
            self.assertEqual(iterator.__length_hint__(), 1)
            self.assertEqual(iterator.__length_hint__(), 0)
            self.assertEqual(next(iterator, 'done'), 'done')
            self.assertEqual(events, ['len'])
        finally:
            state.clear()

    def test_sequence_iteration_body_error_keeps_index_for_recovery(self):
        marker = ValueError('item marker')
        events = []
        class Source(Sequence):
            fail = True
            def __getitem__(self, index):
                events.append(index)
                if self.fail:
                    self.fail = False
                    raise marker
                return Sequence.__getitem__(self, index)
        iterator = iter(Source())
        try:
            next(iterator)
        except ValueError as error:
            self.assertIs(error, marker)
        else:
            self.fail('item callback must fail')
        self.assertEqual(next(iterator), 0)
        self.assertEqual(events, [0, 0])

    def test_callable_iterator_equality_error_is_recoverable(self):
        marker = ValueError('equality marker')
        events = []
        class Produced(object):
            fail = True
            def __eq__(self, other):
                events.append('equal')
                if self.fail:
                    self.fail = False
                    raise marker
                return False
        produced, sentinel = Produced(), object()
        values = [produced, produced]
        iterator = iter(lambda: values.pop(0), sentinel)
        try:
            next(iterator)
        except ValueError as error:
            self.assertIs(error, marker)
        else:
            self.fail('equality callback must fail')
        self.assertIs(next(iterator), produced)
        self.assertEqual(events, ['equal', 'equal'])
