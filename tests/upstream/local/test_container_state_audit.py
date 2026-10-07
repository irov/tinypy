"""Project-authored Python 2 container construction and callback state tests."""

import unittest
from _functools import partial


class ContainerStateAudit(unittest.TestCase):
    def _cached_mapping(self):
        events = []
        class Key(object):
            failed = False
            def __hash__(self):
                events.append('hash')
                if self.failed:
                    raise LookupError('stored hash')
                return 1
        key = Key()
        source = {key: 7}
        key.failed = True
        events[:] = []
        return source, key, events

    def test_set_constructor_reuses_exact_dict_hash(self):
        source, key, events = self._cached_mapping()
        result = set(source)
        self.assertTrue(next(iter(result)) is key)
        self.assertEqual(events, [])

    def test_frozenset_constructor_reuses_exact_dict_hash(self):
        source, key, events = self._cached_mapping()
        result = frozenset(source)
        self.assertTrue(next(iter(result)) is key)
        self.assertEqual(events, [])

    def test_set_update_reuses_exact_dict_hash(self):
        source, key, events = self._cached_mapping()
        result = set([2])
        result.update(source)
        self.assertEqual(len(result), 2)
        self.assertTrue(any(item is key for item in result))
        self.assertEqual(events, [])

    def test_set_union_reuses_exact_dict_hash(self):
        source, key, events = self._cached_mapping()
        result = set([2]).union(source)
        self.assertEqual(len(result), 2)
        self.assertTrue(any(item is key for item in result))
        self.assertEqual(events, [])

    def test_symmetric_update_reuses_exact_dict_hash(self):
        source, key, events = self._cached_mapping()
        result = set([2])
        result.symmetric_difference_update(source)
        self.assertEqual(len(result), 2)
        self.assertTrue(any(item is key for item in result))
        self.assertEqual(events, [])

    def test_dict_subtype_keeps_iterator_protocol_for_set(self):
        events = []
        class Mapping(dict):
            def __iter__(self):
                events.append('iter')
                return iter([8])
        self.assertEqual(set(Mapping({1: 7})), set([8]))
        self.assertEqual(events, ['iter'])

    def _update_mutation(self, target_kind, source_kind, mutation):
        events, state = [], {}
        class Key(object):
            def __init__(self, name):
                self.name = name
            def __hash__(self):
                return 1
            def __eq__(self, other):
                events.append((self.name, other.name))
                source = state['source']
                if mutation == 'grow':
                    if isinstance(source, dict):
                        source[2] = 7
                    else:
                        source.add(2)
                else:
                    source.clear()
                return False
        original = Key('target'), Key('source')
        source = {original[1]: 3} if source_kind is dict else set([original[1]])
        target = {original[0]: 8} if target_kind is dict else set([original[0]])
        state['source'] = source
        try:
            target.update(source)
            self.assertEqual(len(target), 3 if mutation == 'grow' else 2)
            self.assertEqual(len(source), 2 if mutation == 'grow' else 0)
            self.assertEqual(events, [('target', 'source')])
        finally:
            state.clear()

    def test_set_update_allows_set_source_growth(self):
        self._update_mutation(set, set, 'grow')

    def test_set_update_allows_set_source_clear(self):
        self._update_mutation(set, set, 'clear')

    def test_set_update_allows_dict_source_growth(self):
        self._update_mutation(set, dict, 'grow')

    def test_dict_update_allows_source_growth(self):
        self._update_mutation(dict, dict, 'grow')

    def test_dict_update_allows_source_clear(self):
        self._update_mutation(dict, dict, 'clear')

    def test_dict_pair_list_subtype_uses_iter(self):
        events = []
        class Pair(list):
            def __iter__(self):
                events.append('iter')
                return iter([7, 8])
        self.assertEqual(dict([Pair([1, 2])]), {7: 8})
        self.assertEqual(events, ['iter'])

    def test_dict_pair_tuple_subtype_uses_iter(self):
        events = []
        class Pair(tuple):
            def __iter__(self):
                events.append('iter')
                return iter([7, 8])
        self.assertEqual(dict([Pair([1, 2])]), {7: 8})
        self.assertEqual(events, ['iter'])

    def test_dict_pair_drains_before_length_validation(self):
        events = []
        def pair():
            for item in (1, 2, 3):
                events.append(item)
                yield item
            events.append('tail')
            raise LookupError('tail')
        target = {0: 9}
        with self.assertRaises(LookupError) as caught:
            target.update([pair()])
        self.assertEqual(caught.exception.args, ('tail',))
        self.assertEqual(events, [1, 2, 3, 'tail'])
        self.assertEqual(target, {0: 9})
        target.update([(7, 8)])
        self.assertEqual(target, {0: 9, 7: 8})

    def test_dict_pair_reports_index_and_full_length(self):
        target = {}
        with self.assertRaises(ValueError) as caught:
            target.update([(7, 8), iter([1, 2, 3, 4])])
        self.assertEqual(caught.exception.args, ('dictionary update sequence element #1 has length 4; 2 is required',))
        self.assertEqual(target, {7: 8})

    def test_dict_pair_conversion_error_keeps_completed_pairs(self):
        target = {}
        with self.assertRaises(TypeError) as caught:
            target.update([(7, 8), None])
        self.assertEqual(caught.exception.args, ('cannot convert dictionary update sequence element #1 to a sequence',))
        self.assertEqual(target, {7: 8})
        target.update([(9, 10)])
        self.assertEqual(target, {7: 8, 9: 10})

    def test_dict_pair_rewrites_tail_typeerror(self):
        def pair():
            yield 7
            yield 8
            raise TypeError('tail')
        with self.assertRaises(TypeError) as caught:
            dict([pair()])
        self.assertEqual(caught.exception.args, ('cannot convert dictionary update sequence element #0 to a sequence',))

    def test_dict_pair_length_hint_is_on_iterator(self):
        events = []
        class Pair(object):
            def __iter__(self):
                events.append('iter')
                return iter([7, 8])
            def __len__(self):
                raise AssertionError('original pair length is ignored')
        self.assertEqual(dict([Pair()]), {7: 8})
        self.assertEqual(events, ['iter'])

    def test_dict_pair_length_hint_failure_precedes_items(self):
        events = []
        class Pair(object):
            def __iter__(self):
                events.append('iter')
                return self
            def __length_hint__(self):
                events.append('hint')
                raise LookupError('hint')
            def next(self):
                raise AssertionError('hint must fail first')
        with self.assertRaises(LookupError) as caught:
            dict([Pair()])
        self.assertEqual(caught.exception.args, ('hint',))
        self.assertEqual(events, ['iter', 'iter', 'hint'])

    def test_dict_pair_materializes_before_hashing(self):
        events = []
        def pair():
            events.append('key')
            yield []
            events.append('value')
            yield 7
            events.append('end')
        with self.assertRaises(TypeError) as caught:
            dict([pair()])
        self.assertEqual(caught.exception.args, ("unhashable type: 'list'",))
        self.assertEqual(events, ['key', 'value', 'end'])

    def test_negative_pair_hint_has_consumer_message_and_recovers(self):
        events = []
        class Pair(object):
            def __iter__(self):
                events.append('iter')
                return self
            def __length_hint__(self):
                events.append('hint')
                return -1
            def next(self):
                raise AssertionError('invalid hint prevents iteration')
        for consumer in ('new', 'init', 'update'):
            target = {}
            events[:] = []
            with self.assertRaises(SystemError) as caught:
                if consumer == 'new':
                    dict([Pair()])
                elif consumer == 'init':
                    dict.__init__(target, [Pair()])
                else:
                    target.update([Pair()])
            message = 'error return without exception set' if consumer == 'update' else 'NULL result without error in PyObject_Call'
            self.assertEqual(caught.exception.args, (message,))
            self.assertEqual(events, ['iter', 'iter', 'hint'])
            target.update([(7, 8)])
            self.assertEqual(target, {7: 8})

    def test_negative_hint_messages_for_sequence_consumers(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return self
            def __length_hint__(self):
                events.append('hint')
                return -1
            def next(self):
                raise AssertionError('invalid hint prevents iteration')
        for consumer in ('list', 'tuple', 'init', 'extend', 'iadd', 'join', 'slots'):
            target = [7]
            events[:] = []
            with self.assertRaises(SystemError) as caught:
                if consumer == 'list':
                    list(Source())
                elif consumer == 'tuple':
                    tuple(Source())
                elif consumer == 'init':
                    list.__init__(target, Source())
                elif consumer == 'extend':
                    target.extend(Source())
                elif consumer == 'iadd':
                    target += Source()
                elif consumer == 'join':
                    ''.join(Source())
                else:
                    type('Owner', (object,), {'__slots__': Source()})
            message = 'error return without exception set' if consumer in ('extend', 'iadd', 'join') else 'NULL result without error in PyObject_Call'
            self.assertEqual(caught.exception.args, (message,))
            self.assertEqual(events, ['iter', 'iter', 'hint'] if consumer == 'join' else ['iter', 'hint'])
            target.extend([8])
            self.assertEqual(target, [8] if consumer == 'init' else [7, 8])

    def test_length_hint_user_systemerror_identity_is_preserved(self):
        failure = SystemError('user hint')
        class Source(object):
            def __iter__(self):
                return self
            def __length_hint__(self):
                raise failure
            def next(self):
                raise AssertionError('hint fails first')
        for operation in (list, tuple, lambda source: dict([source]), lambda source: ''.join(source)):
            with self.assertRaises(SystemError) as caught:
                operation(Source())
            self.assertTrue(caught.exception is failure)

    def test_other_negative_hints_keep_reason_and_recovery(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return self
            def __length_hint__(self):
                events.append('hint')
                return self.hint
            def next(self):
                events.append('next')
                raise StopIteration
        for hint in (-2, -3):
            source = Source()
            source.hint = hint
            events[:] = []
            self.assertEqual(list(source), [])
            self.assertEqual(events, ['iter', 'hint', 'next'])
            events[:] = []
            with self.assertRaises(SystemError) as caught:
                tuple(source)
            # The reference diagnostic prefixes its own C build source path;
            # the Python error category and reason are the portable contract.
            self.assertTrue(str(caught.exception).endswith('bad argument to internal function'))
            self.assertEqual(events, ['iter', 'hint'])
            source.hint = 0
            self.assertEqual(tuple(source), ())

    def test_partial_keyword_copy_callback_updates_callable_before_call(self):
        events, state = [], {}
        def old(**kwargs):
            events.append('old')
            return 'old'
        def new(**kwargs):
            events.append('new')
            return 'new'
        class Key(str):
            def __hash__(self):
                return 1
            def __eq__(self, other):
                events.append((str(self), str(other)))
                if state.get('armed'):
                    state['armed'] = False
                    state['partial'].__setstate__((new, (), {}, None))
                return str.__eq__(self, other)
        originals = Key('a'), Key('b')
        mapping = {originals[0]: 7, originals[1]: 8}
        target = partial(old)
        target.__setstate__((old, (), mapping, None))
        state.update(partial=target, armed=True)
        events[:] = []
        try:
            self.assertEqual(target(), 'new')
            self.assertEqual(events, [('a', 'b'), ('a', 'b'), 'new'])
            self.assertTrue(target.func is new)
            self.assertTrue(target.keywords is not mapping)
            self.assertEqual(len(mapping), 2)
        finally:
            state.clear()

    def test_partial_keyword_copy_failure_prevents_call_and_recovers(self):
        events = []
        class Key(str):
            failed = False
            def __hash__(self):
                return 1
            def __eq__(self, other):
                events.append((str(self), str(other)))
                if self.failed:
                    raise LookupError('copy')
                return str.__eq__(self, other)
        def body(**kwargs):
            events.append('body')
            return sorted((str(key), value) for key, value in kwargs.items())
        originals = Key('a'), Key('b')
        mapping = {originals[0]: 7, originals[1]: 8}
        target = partial(body)
        target.__setstate__((body, (), mapping, None))
        Key.failed = True
        events[:] = []
        with self.assertRaises(LookupError) as caught:
            target()
        self.assertEqual(caught.exception.args, ('copy',))
        self.assertEqual(events, [('a', 'b')])
        self.assertTrue(target.keywords is mapping)
        Key.failed = False
        events[:] = []
        self.assertEqual(target(), [('a', 7), ('b', 8)])
        self.assertEqual(events, [('a', 'b'), ('a', 'b'), 'body'])

    def _sets(self, mutation_side, equal):
        events, state = [], {'armed': False}
        class Key(object):
            def __init__(self, name):
                self.name = name
            def __hash__(self):
                return 1
            def __eq__(self, other):
                events.append((self.name, other.name))
                if state['armed']:
                    state['armed'] = False
                    state[mutation_side].add(2)
                return equal
        originals = Key('left'), Key('right')
        left, right = set([originals[0]]), set([originals[1]])
        state.update(left=left, right=right, armed=True)
        return left, right, originals, events, state

    def test_isdisjoint_equal_size_probes_left(self):
        left, right, originals, events, state = self._sets('right', False)
        try:
            self.assertTrue(left.isdisjoint(right))
            self.assertEqual(events, [('left', 'right')])
            self.assertEqual(len(right), 2)
        finally:
            state.clear()

    def test_subset_visits_new_source_member(self):
        left, right, originals, events, state = self._sets('left', True)
        try:
            self.assertFalse(left.issubset(right))
            self.assertEqual(len(left), 2)
            self.assertEqual(events, [('right', 'left')])
        finally:
            state.clear()

    def test_set_equality_visits_new_source_member(self):
        left, right, originals, events, state = self._sets('left', True)
        try:
            self.assertFalse(left == right)
            self.assertEqual(events, [('right', 'left')])
        finally:
            state.clear()

    def test_difference_update_accepts_source_growth(self):
        left, right, originals, events, state = self._sets('right', True)
        try:
            left.difference_update(right)
            self.assertEqual(left, set())
            self.assertEqual(len(right), 2)
            self.assertEqual(events, [('left', 'right')])
        finally:
            state.clear()

    def test_symmetric_update_preserves_destination_callback_mutation(self):
        left, right, originals, events, state = self._sets('left', True)
        try:
            left.symmetric_difference_update(right)
            self.assertEqual(left, set([2]))
            self.assertEqual(events, [('left', 'right')])
        finally:
            state.clear()

    def test_xor_copies_right_and_walks_current_left(self):
        left, right, originals, events, state = self._sets('left', True)
        try:
            self.assertEqual(left ^ right, set([2]))
            self.assertEqual(events, [('right', 'left')])
        finally:
            state.clear()

    def test_symmetric_update_keeps_progress_on_error(self):
        events = []
        class Key(object):
            def __hash__(self):
                return 1
            def __eq__(self, other):
                events.append('eq')
                raise LookupError('compare')
        originals = Key(), Key()
        left, right = set([originals[0]]), set([0, originals[1]])
        with self.assertRaises(LookupError) as caught:
            left.symmetric_difference_update(right)
        self.assertEqual(caught.exception.args, ('compare',))
        self.assertEqual(len(left), 2)
        self.assertTrue(0 in left)
        self.assertTrue(any(item is originals[0] for item in left))
        self.assertEqual(events, ['eq'])
        left.clear()
        left.symmetric_difference_update([7])
        self.assertEqual(left, set([7]))

    def test_symmetric_update_self_clears_same_object(self):
        target = set([1, 2])
        alias = target
        target ^= target
        self.assertTrue(target is alias)
        self.assertEqual(target, set())

    def test_actual_set_intersection_observes_source_growth(self):
        for operation in ('method', 'operator', 'update'):
            left, right, originals, events, state = self._sets('left', False)
            right.add(2)
            try:
                if operation == 'method':
                    result = left.intersection(right)
                elif operation == 'operator':
                    result = left & right
                else:
                    left.intersection_update(right)
                    result = left
                self.assertEqual(result, set([2]))
                self.assertEqual(events, [('right', 'left')])
            finally:
                state.clear()

    def test_set_iterator_still_rejects_size_change(self):
        target = set([1])
        iterator = iter(target)
        target.add(2)
        with self.assertRaises(RuntimeError) as caught:
            next(iterator)
        self.assertEqual(caught.exception.args, ('Set changed size during iteration',))

    def test_dict_iterator_still_rejects_size_change(self):
        target = {1: 7}
        iterator = iter(target)
        target[2] = 8
        with self.assertRaises(RuntimeError) as caught:
            next(iterator)
        self.assertEqual(caught.exception.args, ('dictionary changed size during iteration',))
