"""Project-authored Python 2 iterator builtin and container callback witnesses."""

import unittest
import _weakref as weakref
import sys


class BuiltinEdges(unittest.TestCase):
    def test_length_float_conversion_prevents_hint_fallback(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([2, 1])
            def __len__(self):
                events.append('len')
                return 2.75
            def __length_hint__(self):
                raise AssertionError('length is available')
        operations = [(list, [2, 1]), (tuple, (2, 1)),
                      (sorted, [1, 2]), (lambda x: filter(None, x), [2, 1]),
                      (lambda x: map(lambda value: value, x), [2, 1]),
                      (lambda x: zip(x), [(2,), (1,)])]
        for operation, expected in operations:
            events[:] = []
            self.assertEqual(operation(Source()), expected)
            self.assertEqual(events, ['len', 'iter'] if operation is operations[-1][0] else ['iter', 'len'])

    def test_extend_accepts_float_length_result(self):
        events = []
        class Source(object):
            def __iter__(self):
                return iter([2, 1])
            def __len__(self):
                events.append('len')
                return 2.75
            def __length_hint__(self):
                raise AssertionError('length is available')
        target = []
        target.extend(Source())
        self.assertEqual(target, [2, 1])
        self.assertEqual(events, ['len'])

    def test_length_reads_long_subtype_payload(self):
        events = []
        class Wide(long):
            def __int__(self):
                events.append('int')
                return 7
        class Source(object):
            def __len__(self):
                return Wide(2)
            def __iter__(self):
                return iter([1])
        self.assertEqual(len(Source()), 2)
        self.assertTrue(bool(Source()))
        self.assertEqual(list(Source()), [1])
        self.assertEqual(events, [])

    def test_zero_length_long_subtype_does_not_convert(self):
        class Wide(long):
            def __int__(self):
                raise AssertionError('stored value is required')
        class Source(object):
            def __len__(self):
                return Wide(0)
        self.assertEqual(len(Source()), 0)
        self.assertFalse(bool(Source()))

    def test_length_custom_integer_conversion_called_once(self):
        events = []
        class Number(object):
            def __int__(self):
                events.append('int')
                return 2
        class Source(object):
            def __len__(self):
                events.append('len')
                return Number()
            def __iter__(self):
                events.append('iter')
                return iter([1])
        self.assertEqual(list(Source()), [1])
        self.assertEqual(events, ['iter', 'len', 'int'])

    def test_classic_length_long_is_unavailable_for_materialization(self):
        events = []
        class Source:
            def __iter__(self):
                events.append('iter')
                return iter([1])
            def __len__(self):
                events.append('len')
                return 2L
            def __length_hint__(self):
                raise AssertionError('classic hints are ignored')
        self.assertEqual(list(Source()), [1])
        self.assertEqual(events, ['iter', 'len'])

    def test_length_overflow_precedes_consumption(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return self
            def __len__(self):
                events.append('len')
                return 1L << 80
            def next(self):
                raise AssertionError('no item may be consumed')
        self.assertRaises(OverflowError, list, Source())
        self.assertEqual(events, ['iter', 'len'])

    def test_float_length_overflow_is_reported(self):
        class Source(object):
            def __len__(self):
                return 1e100
            def __iter__(self):
                return iter([])
        self.assertRaises(OverflowError, len, Source())
        self.assertRaises(OverflowError, bool, Source())
        self.assertRaises(OverflowError, list, Source())

    def test_nonnumeric_length_falls_back_to_hint(self):
        events = []
        class Source(object):
            def __iter__(self):
                return iter([1])
            def __len__(self):
                events.append('len')
                return '1'
            def __length_hint__(self):
                events.append('hint')
                return 1
        self.assertEqual(list(Source()), [1])
        self.assertEqual(events, ['len', 'hint'])
        self.assertRaises(TypeError, len, Source())

    def test_hint_float_only_protocol_requires_integer_conversion(self):
        events = []
        class Number(object):
            def __float__(self):
                events.append('float')
                return 1.0
        class Source(object):
            def __iter__(self):
                return iter([1])
            def __length_hint__(self):
                return Number()
        self.assertRaises(TypeError, list, Source())
        self.assertEqual(events, [])

    def test_hint_reads_long_subtype_payload(self):
        class Wide(long):
            def __int__(self):
                raise AssertionError('stored value is required')
        class Source(object):
            def __iter__(self):
                return iter([1])
            def __length_hint__(self):
                return Wide(1)
        self.assertEqual(list(Source()), [1])

    def test_sorted_reverse_converts_before_and_after_materialization(self):
        events = []
        class Reverse(object):
            def __int__(self):
                events.append('int')
                return 1
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1, 2])
            def __len__(self):
                events.append('len')
                return 2
        self.assertEqual(sorted(Source(), reverse=Reverse()), [2, 1])
        self.assertEqual(events, ['int', 'iter', 'len', 'int'])

    def test_sorted_invalid_reverse_prevents_iteration(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1, 2])
        self.assertRaises(TypeError, sorted, Source(), reverse='1')
        self.assertEqual(events, [])

    def test_sorted_unknown_keyword_prevents_iteration(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1, 2])
        self.assertRaises(TypeError, sorted, Source(), unknown=1)
        self.assertEqual(events, [])

    def test_sorted_reverse_conversion_precedes_unknown_keyword(self):
        events = []
        class Reverse(object):
            def __int__(self):
                events.append('int')
                return 1
        self.assertRaises(TypeError, sorted, [2, 1], reverse=Reverse(), unknown=1)
        self.assertEqual(events, ['int'])

    def test_sorted_duplicate_argument_prevents_iteration(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1, 2])
        self.assertRaises(TypeError, sorted, Source(), None, cmp=None)
        self.assertEqual(events, [])

    def test_sorted_keyword_iterable_is_forwarded_to_list_sort(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1, 2])
            def __len__(self):
                events.append('len')
                return 2
        self.assertRaises(TypeError, sorted, iterable=Source())
        self.assertEqual(events, ['iter', 'len'])

    def test_sorted_second_reverse_conversion_controls_result(self):
        events = []
        class Reverse(object):
            def __int__(self):
                events.append('int')
                return len(events) - 1
        self.assertEqual(sorted([1, 2], reverse=Reverse()), [2, 1])
        self.assertEqual(events, ['int', 'int'])

    def test_sort_keyword_subtype_equality_can_ignore_known_keyword(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append(other)
                return False
            def __hash__(self):
                return str.__hash__(self)
        for name, value in [('reverse', True), ('key', lambda value: -value),
                            ('cmp', lambda left, right: cmp(right, left))]:
            events[:] = []
            self.assertEqual(sorted([2, 1], **{Keyword(name): value}), [1, 2])
            self.assertEqual(events, [name, name])
            events[:] = []
            values = [2, 1]
            values.sort(**{Keyword(name): value})
            self.assertEqual(values, [1, 2])
            self.assertEqual(events, [name])

    def test_sort_keyword_subtype_equality_can_accept_known_keyword(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append(other)
                return str.__eq__(self, other)
            def __hash__(self):
                return str.__hash__(self)
        self.assertEqual(sorted([1, 2], **{Keyword('reverse'): True}), [2, 1])
        self.assertEqual(events, ['reverse', 'reverse'])
        events[:] = []
        values = [1, 2]
        values.sort(**{Keyword('reverse'): True})
        self.assertEqual(values, [2, 1])
        self.assertEqual(events, ['reverse'])

    def test_sort_keyword_lookup_suppresses_errors_and_preserves_handled_state(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append(other)
                raise failure('keyword comparison')
            def __hash__(self):
                return str.__hash__(self)
        for failure in (ValueError, KeyboardInterrupt):
            try:
                raise LookupError('handled')
            except LookupError as handled:
                for name, value in [('reverse', True), ('key', lambda value: -value),
                                    ('cmp', lambda left, right: cmp(right, left))]:
                    events[:] = []
                    self.assertEqual(sorted([2, 1], **{Keyword(name): value}), [1, 2])
                    self.assertEqual(events, [name, name])
                    self.assertIs(sys.exc_info()[1], handled)
                    events[:] = []
                    values = [2, 1]
                    values.sort(**{Keyword(name): value})
                    self.assertEqual(values, [1, 2])
                    self.assertEqual(events, [name])
                    self.assertIs(sys.exc_info()[1], handled)
            sys.exc_clear()

    def test_fromkeys_populates_alternate_constructor_mapping(self):
        events = []
        class Mapping(object):
            def __init__(self):
                self.entries = []
            def __setitem__(self, key, value):
                events.append('setitem')
                self.entries.append((key, value))
        class Dictionary(dict):
            def __new__(cls):
                events.append('new')
                return Mapping()
        result = Dictionary.fromkeys(['x', 'y'], 7)
        self.assertIs(type(result), Mapping)
        self.assertEqual(result.entries, [('x', 7), ('y', 7)])
        self.assertEqual(events, ['new', 'setitem', 'setitem'])

    def test_fromkeys_empty_source_can_return_non_mapping(self):
        class Dictionary(dict):
            def __new__(cls):
                return 7
        self.assertEqual(Dictionary.fromkeys([]), 7)
        self.assertRaises(TypeError, Dictionary.fromkeys, ['x'])

    def test_fromkeys_reuses_cached_hashes_from_exact_sources(self):
        events = []
        class Key(object):
            def __hash__(self):
                events.append('hash')
                return 1
        key = Key()
        sources = [{key: 3}, set([key]), frozenset([key])]
        for source in sources:
            events[:] = []
            result = dict.fromkeys(source, 7)
            self.assertEqual(events, [])
            self.assertEqual(len(result), 1)
            self.assertEqual(result.values(), [7])

    def test_fromkeys_dict_subtype_uses_generic_hash_and_assignment(self):
        events = []
        class Key(object):
            def __hash__(self):
                events.append('hash')
                return 1
        class Dictionary(dict):
            def __setitem__(self, key, value):
                events.append('setitem')
                dict.__setitem__(self, key, value)
        key = Key()
        source = {key: 3}
        events[:] = []
        result = Dictionary.fromkeys(source, 7)
        self.assertIs(type(result), Dictionary)
        self.assertEqual(events, ['setitem', 'hash'])

    def test_all_any_stop_at_first_decisive_value(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                yield first
                raise AssertionError('unreachable tail')
        first = False
        self.assertFalse(all(Source()))
        first = True
        self.assertTrue(any(Source()))
        self.assertEqual(events, ['iter', 'iter'])

    def test_min_max_keep_first_tied_item(self):
        left, right = object(), object()
        events = []
        def key(value):
            events.append(value)
            return 1
        self.assertIs(min([left, right], key=key), left)
        self.assertEqual(events, [left, right])
        events[:] = []
        self.assertIs(max([left, right], key=key), left)
        self.assertEqual(events, [left, right])

    def test_min_key_none_is_checked_only_after_first_item(self):
        self.assertRaises(ValueError, min, [], key=None)
        self.assertRaises(TypeError, min, [1], key=None)

    def test_zip_consumes_longer_left_before_short_right_stops(self):
        left = iter([1, 2])
        right = iter([3])
        self.assertEqual(zip(left, right), [(1, 3)])
        self.assertRaises(StopIteration, next, left)

    def test_map_does_not_resume_exhausted_source(self):
        events = []
        class Source(object):
            def __init__(self):
                self.index = 0
            def __iter__(self):
                return self
            def next(self):
                self.index += 1
                events.append(self.index)
                if self.index == 2 or self.index >= 4:
                    raise StopIteration
                return self.index
        self.assertEqual(map(None, Source(), [4, 5, 6]), [(1, 4), (None, 5), (None, 6)])
        self.assertEqual(events, [1, 2])

    def test_reduce_accepts_uncallable_when_no_call_is_needed(self):
        self.assertEqual(reduce(None, [7]), 7)
        self.assertEqual(reduce(None, [], 8), 8)
        self.assertRaises(TypeError, reduce, None, [7, 8])

    def test_sum_uses_binary_addition_for_custom_start(self):
        events = []
        class Accumulator(object):
            def __add__(self, other):
                events.append(('add', other))
                return self
            def __iadd__(self, other):
                raise AssertionError('sum must not mutate via inplace addition')
        start = Accumulator()
        self.assertIs(sum([1, 2], start), start)
        self.assertEqual(events, [('add', 1), ('add', 2)])

    def test_min_releases_discarded_temporary_keys(self):
        references = []
        class Key(object):
            def __init__(self, value):
                self.value = value
            def __lt__(self, other):
                return self.value < other.value
        def key(value):
            result = Key(value)
            references.append(weakref.ref(result))
            return result
        self.assertEqual(min([2, 1], key=key), 1)
        self.assertEqual([reference() for reference in references], [None, None])
