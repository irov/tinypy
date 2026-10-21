"""Project-authored Python 2 iterator builtin and container callback witnesses."""

import unittest
import __builtin__
import _codecs
import _struct
import _weakref as weakref
import sys


def error_text(function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except Exception as error:
        return type(error).__name__ + ': ' + str(error)
    raise AssertionError('no exception raised')


def native_failure_source(events, items):
    """Yield items, then fail in list.pop without retaining a Python frame."""
    class Owner(list):
        def __del__(self):
            events.append(('owner',))
    class Source(object):
        def __init__(self, owner):
            self.owner = owner
        def __iter__(self):
            iterator = iter(self.owner.pop, None)
            self.owner = None
            return iterator
    return Source(Owner(items))


class BuiltinEdges(unittest.TestCase):
    def test_set_item_error_releases_iterator_before_key(self):
        events = []
        class Key(list):
            def __del__(self):
                events.append('key')
        class Iterator(object):
            def __iter__(self):
                return self
            def next(self):
                return Key()
            def __del__(self):
                events.append('iterator')
        class Source(object):
            def __iter__(self):
                return Iterator()
        target = set([1])
        operations = (set, frozenset, target.update, target.union,
                      target.intersection, target.intersection_update,
                      target.difference, target.difference_update,
                      target.symmetric_difference,
                      target.symmetric_difference_update,
                      target.issubset, target.issuperset)
        for operation in operations:
            events[:] = []
            with self.assertRaises(TypeError):
                operation(Source())
            self.assertEqual(events, ['iterator', 'key'])
        events[:] = []
        with self.assertRaises(TypeError):
            target.isdisjoint(Source())
        self.assertEqual(events, ['key', 'iterator'])

    def test_set_intersection_item_error_releases_result_before_key(self):
        events = []
        class Key(list):
            def __del__(self):
                events.append('key')
        class Iterator(object):
            def __iter__(self):
                return self
            def next(self):
                return Key()
            def __del__(self):
                events.append('iterator')
        class Source(object):
            def __iter__(self):
                return Iterator()
        class Set(set):
            def __del__(self):
                events.append('result')
        target = Set([1])
        with self.assertRaises(TypeError):
            target.intersection(Source())
        self.assertEqual(events, ['iterator', 'result', 'key'])

    def test_dict_mapping_lookup_error_releases_iterator_before_key(self):
        events = []
        class Key(int):
            def __del__(self):
                events.append('key')
        class Iterator(object):
            def __iter__(self):
                return self
            def next(self):
                return Key(0)
            def __del__(self):
                events.append('iterator')
        class Source(object):
            def __iter__(self):
                return Iterator()
        class Mapping(object):
            __getitem__ = [].pop
            def keys(self):
                return Source()
        for operation in (dict, {}.update):
            events[:] = []
            with self.assertRaises(IndexError) as caught:
                operation(Mapping())
            self.assertEqual(caught.exception.args, ('pop from empty list',))
            self.assertEqual(events, ['iterator', 'key'])

    def test_dict_mapping_insert_error_releases_key_value_iterator(self):
        events = []
        class Key(list):
            def __del__(self):
                events.append('key')
        class Value(object):
            def __del__(self):
                events.append('value')
        class Iterator(object):
            def __iter__(self):
                return self
            def next(self):
                return Key()
            def __del__(self):
                events.append('iterator')
        class Source(object):
            def __iter__(self):
                return Iterator()
        class Mapping(object):
            def keys(self):
                return Source()
            def __getitem__(self, key):
                return Value()
        for operation in (dict, {}.update):
            events[:] = []
            with self.assertRaises(TypeError):
                operation(Mapping())
            self.assertEqual(events, ['key', 'value', 'iterator'])

    def test_dict_invalid_pair_releases_pair_before_converted_items(self):
        events = []
        class Item(object):
            def __init__(self, name):
                self.name = name
            def __del__(self):
                events.append(self.name)
        class Pair(object):
            def __iter__(self):
                return iter([Item('first'), Item('second'), Item('third')])
            def __del__(self):
                events.append('pair')
        class Iterator(object):
            def __iter__(self):
                return self
            def next(self):
                return Pair()
            def __del__(self):
                events.append('iterator')
        class Source(object):
            def __iter__(self):
                return Iterator()
        for operation in (dict, {}.update):
            events[:] = []
            with self.assertRaises(ValueError) as caught:
                operation(Source())
            self.assertEqual(caught.exception.args,
                             ('dictionary update sequence element #0 has length 3; 2 is required',))
            self.assertEqual(events, ['pair', 'third', 'second', 'first', 'iterator'])

    def test_join_iteration_error_releases_items_before_iterator(self):
        events = []
        class Item(object):
            def __del__(self):
                events.append(('item',))
        for separator in ('', u'', bytearray()):
            events[:] = []
            with self.assertRaises(IndexError) as caught:
                separator.join(native_failure_source(events, [Item()]))
            self.assertEqual(caught.exception.args, ('pop from empty list',))
            self.assertEqual(events, [('item',), ('owner',)])

    def test_list_slice_iteration_error_releases_items_before_iterator(self):
        events = []
        class Item(object):
            def __del__(self):
                events.append(('item',))
        for step in (1, 2):
            events[:] = []
            target = [1, 2, 3]
            with self.assertRaises(IndexError) as caught:
                target[::step] = native_failure_source(events, [Item()])
            self.assertEqual(caught.exception.args, ('pop from empty list',))
            self.assertEqual(target, [1, 2, 3])
            self.assertEqual(events, [('item',), ('owner',)])

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

    def test_sum_fast_paths_match_generic_addition(self):
        self.assertEqual(sum([sys.maxint, 1]), sys.maxint + 1)
        self.assertIs(type(sum([sys.maxint, 1])), long)
        self.assertIs(type(sum([1, True, 2])), int)
        self.assertIs(sum([], True), True)
        self.assertEqual(sum([True, True], True), 3)
        self.assertEqual(sum([1, 2.5, 3]), 6.5)
        self.assertEqual(sum([0.5, 1, True], 0.5), 3.0)
        self.assertEqual(sum([0.5, 10 ** 20]), 0.5 + 10 ** 20)
        self.assertIs(type(sum([], 0.0)), float)
        self.assertEqual(sum([2 ** 70, 1, 0.5]), 2 ** 70 + 1.5)
        class Number(int):
            pass
        self.assertIs(type(sum([Number(2)])), int)
        self.assertIs(type(sum([], Number(2))), Number)
        events = []
        class Right(object):
            def __radd__(self, other):
                events.append(other)
                return 1.5
        self.assertEqual(sum([1, 2, Right(), 3]), 4.5)
        self.assertEqual(events, [3])

    def test_sum_does_not_resume_exhausted_source(self):
        class Source(object):
            def __init__(self, items):
                self.items = list(items)
                self.calls = 0
            def __iter__(self):
                return self
            def next(self):
                self.calls += 1
                if self.calls > len(self.items) + 1:
                    raise AssertionError('resumed after StopIteration')
                if self.calls > len(self.items):
                    raise StopIteration
                return self.items[self.calls - 1]
        for items, start, expected in (([1, 2], 0, 3), ([1.5, 2], 0, 3.5), ([1, 2.5], 0, 3.5),
                                       ([2 ** 70], 0, 2 ** 70), ([[1]], [], [1]), ([], 0.5, 0.5)):
            source = Source(items)
            self.assertEqual(sum(source, start), expected)
            self.assertEqual(source.calls, len(items) + 1)
        def failing(values):
            for value in values:
                yield value
            raise KeyError('stop')
        for values in ([1], [1.5], [1, 2 ** 70], [[]]):
            self.assertRaises(KeyError, sum, failing(values), [] if values == [[]] else 0)

    def test_enumerate_pairs_release_previous_items(self):
        released = []
        class Item(object):
            def __init__(self, name):
                self.name = name
            def __del__(self):
                released.append(self.name)
        def source():
            for name in 'abc':
                yield Item(name)
        seen = []
        for index, item in enumerate(source(), 3):
            seen.append((index, item.name, list(released)))
        del item
        self.assertEqual(seen, [(3, 'a', []), (4, 'b', ['a']), (5, 'c', ['a', 'b'])])
        self.assertEqual(released, ['a', 'b', 'c'])
        pairs = enumerate('abc')
        kept = next(pairs)
        self.assertEqual((kept, list(pairs)), ((0, 'a'), [(1, 'b'), (2, 'c')]))
        pairs = enumerate('ab')
        first = next(pairs)
        second = next(pairs)
        self.assertEqual((first, second), ((0, 'a'), (1, 'b')))

    def test_exhausted_enumerate_lets_its_last_pair_go(self):
        # CPython's collector frees a cycle through the pair an exhausted
        # enumerate keeps; tinypy has no collector and drops that pair, which
        # the allocator accounting of the case checks.
        class Node(object):
            pass
        node = Node()
        node.pairs = enumerate([node])
        for pair in node.pairs:
            pass
        del pair
        del node
        # A pair the caller still holds stays as it is.
        released = []
        class Item(object):
            def __del__(self):
                released.append(True)
        pairs = enumerate([Item()])
        kept = next(pairs)
        self.assertEqual(list(pairs), [])
        self.assertEqual((kept[0], type(kept[1]), released), (0, Item, []))
        del kept, pairs
        self.assertEqual(released, [True])

    def test_min_releases_discarded_temporary_keys(self):
        events = []
        class Item(object):
            def __init__(self, value):
                self.value = value
            def __del__(self):
                events.append(('item', self.value))
        class Key(object):
            def __init__(self, item):
                self.value = item.value
                self.item = weakref.ref(item)
            def __lt__(self, other):
                return self.value < other.value
            def __del__(self):
                events.append(('key', self.value, self.item() is not None))
        class Source(object):
            def __init__(self):
                self.value = 0
            def __iter__(self):
                return self
            def next(self):
                self.value += 1
                if self.value > 2:
                    raise StopIteration
                return Item(self.value)
        result = min(Source(), key=Key)
        self.assertEqual((result.value, events),
                         (1, [('item', 2), ('key', 2, False), ('key', 1, True)]))
        del result
        self.assertEqual(events[-1], ('item', 1))

    def test_sum_releases_previous_total_before_current_item(self):
        events = []
        class Accumulator(object):
            def __init__(self, value):
                self.value = value
            def __add__(self, item):
                events.append(('add', self.value, item.value))
                return Accumulator(self.value + item.value)
            def __del__(self):
                events.append(('accumulator', self.value))
        class Item(object):
            def __init__(self, value):
                self.value = value
            def __del__(self):
                events.append(('item', self.value))
        class Source(object):
            def __init__(self):
                self.value = 0
            def __iter__(self):
                return self
            def next(self):
                self.value += 1
                if self.value > 2:
                    raise StopIteration
                return Item(self.value)
        result = sum(Source(), Accumulator(0))
        self.assertEqual((result.value, events),
                         (3, [('add', 0, 1), ('item', 1), ('add', 1, 2),
                              ('accumulator', 1), ('item', 2), ('accumulator', 0)]))
        del result
        self.assertEqual(events[-1], ('accumulator', 3))

    def test_map_releases_callback_items_right_to_left(self):
        events = []
        class Item(object):
            def __init__(self, name):
                self.name = name
            def __del__(self):
                events.append(('item', self.name))
        class Source(object):
            def __init__(self, name):
                self.name = name
                self.done = False
            def __iter__(self):
                return self
            def next(self):
                if self.done:
                    raise StopIteration
                self.done = True
                return Item(self.name)
        def callback(left, right):
            events.append(('call', left.name, right.name))
            return 0
        self.assertEqual(map(callback, Source('left'), Source('right')), [0])
        self.assertEqual(events, [('call', 'left', 'right'),
                                  ('item', 'right'), ('item', 'left')])

        events[:] = []
        class Broken(object):
            def __iter__(self):
                return self
            def next(self):
                raise ValueError('broken')
        self.assertRaises(ValueError, map, callback,
                          Source('left'), Source('middle'), Broken())
        self.assertEqual(events, [('item', 'middle'), ('item', 'left')])

    def test_map_releases_partially_created_iterators_left_to_right(self):
        events = []
        class Iterator(object):
            def __init__(self, name):
                self.name = name
            def __iter__(self):
                return self
            def next(self):
                raise StopIteration
            def __del__(self):
                events.append(self.name)
        class Source(object):
            def __init__(self, name):
                self.name = name
            def __iter__(self):
                return Iterator(self.name)
        class Broken(object):
            def __iter__(self):
                raise ValueError('broken')
        with self.assertRaises(TypeError) as caught:
            map(None, Source('left'), Source('middle'), Broken())
        self.assertEqual(caught.exception.args,
                         ('argument 4 to map() must support iteration',))
        self.assertEqual(events, ['left', 'middle'])

    def test_zip_releases_partial_rows_and_iterators_right_to_left(self):
        events = []
        class Item(object):
            def __init__(self, name):
                self.name = name
            def __del__(self):
                events.append(('item', self.name))
        class Source(object):
            def __init__(self, name, empty=False):
                self.name = name
                self.empty = empty
                self.done = False
            def __iter__(self):
                return self
            def next(self):
                if self.empty or self.done:
                    raise StopIteration
                self.done = True
                return Item(self.name)
        self.assertEqual(zip(Source('left'), Source('middle'), Source('empty', True)), [])
        self.assertEqual(events, [('item', 'middle'), ('item', 'left')])

        events[:] = []
        class Iterator(object):
            def __init__(self, name):
                self.name = name
                self.done = False
            def __iter__(self):
                return self
            def next(self):
                if self.done:
                    raise StopIteration
                self.done = True
                return self.name
            def __del__(self):
                events.append(('iterator', self.name))
        class IteratorSource(object):
            def __init__(self, name):
                self.name = name
            def __iter__(self):
                return Iterator(self.name)
        self.assertEqual(zip(IteratorSource('left'), IteratorSource('right')),
                         [('left', 'right')])
        self.assertEqual(events, [('iterator', 'right'), ('iterator', 'left')])

    def test_min_max_success_releases_best_key_before_iterator(self):
        events = []
        class Key(int):
            def __del__(self):
                events.append(('key',))
        class Iterator(object):
            def __init__(self):
                self.done = False
            def __iter__(self):
                return self
            def next(self):
                if self.done:
                    raise StopIteration
                self.done = True
                return 17
            def __del__(self):
                events.append(('iterator',))
        class Source(object):
            def __iter__(self):
                return Iterator()
        for operation in (min, max):
            events[:] = []
            self.assertEqual(operation(Source(), key=Key), 17)
            self.assertEqual(events, [('key',), ('iterator',)])

    def test_min_max_comparison_error_releases_candidate_key_first(self):
        events = []
        class Item(int):
            def __del__(self):
                events.append(('item', int(self)))
        class Key(complex):
            def __del__(self):
                events.append(('key', int(self.real)))
        class Iterator(object):
            def __init__(self):
                self.value = 0
            def __iter__(self):
                return self
            def next(self):
                self.value += 1
                return Item(self.value)
            def __del__(self):
                events.append(('iterator',))
        class Source(object):
            def __iter__(self):
                return Iterator()
        for operation in (min, max):
            events[:] = []
            with self.assertRaises(TypeError) as caught:
                operation(Source(), key=Key)
            self.assertEqual(caught.exception.args,
                             ('no ordering relation is defined for complex numbers',))
            self.assertEqual(events, [('key', 2), ('item', 2),
                                      ('key', 1), ('item', 1), ('iterator',)])

    def test_min_max_key_error_releases_best_before_iterator(self):
        events = []
        class Item(object):
            def __del__(self):
                events.append(('item',))
        class Sized(Item):
            def __len__(self):
                return 1
        class Iterator(object):
            def __init__(self):
                self.done = False
            def __iter__(self):
                return self
            def next(self):
                if self.done:
                    return Item()
                self.done = True
                return Sized()
            def __del__(self):
                events.append(('iterator',))
        class Source(object):
            def __iter__(self):
                return Iterator()
        for operation in (min, max):
            events[:] = []
            self.assertRaises(TypeError, operation, Source(), key=len)
            self.assertEqual(events, [('item',), ('item',), ('iterator',)])

    def test_min_max_iteration_error_releases_best_before_iterator(self):
        events = []
        class Item(object):
            def __del__(self):
                events.append(('item',))
        class Key(object):
            def __init__(self, item):
                pass
            def __del__(self):
                events.append(('key',))
        for operation in (min, max):
            for keywords in ({}, {'key': Key}):
                events[:] = []
                with self.assertRaises(IndexError) as caught:
                    operation(native_failure_source(events, [Item()]), **keywords)
                self.assertEqual(caught.exception.args, ('pop from empty list',))
                expected = [('key',)] if keywords else []
                self.assertEqual(events, expected + [('item',), ('owner',)])

    def test_filter_iteration_error_releases_selected_before_iterator(self):
        events = []
        class Item(object):
            def __del__(self):
                events.append(('item',))
        for predicate in (None, bool, lambda item: True):
            events[:] = []
            with self.assertRaises(IndexError) as caught:
                filter(predicate, native_failure_source(events, [Item()]))
            self.assertEqual(caught.exception.args, ('pop from empty list',))
            self.assertEqual(events, [('item',), ('owner',)])

    def test_sum_iteration_error_releases_total_before_iterator(self):
        events = []
        class Total(object):
            def __radd__(self, other):
                return self
            def __del__(self):
                events.append(('total',))
        with self.assertRaises(IndexError) as caught:
            sum(native_failure_source(events, [Total()]))
        self.assertEqual(caught.exception.args, ('pop from empty list',))
        self.assertEqual(events, [('total',), ('owner',)])

    def test_tuple_and_star_iteration_error_release_items_before_iterator(self):
        events = []
        class Item(object):
            def __del__(self):
                events.append(('item',))
        class Tuple(tuple):
            pass
        def star(source):
            return (lambda *args: args)(*source)
        tuple_order = [('item',), ('owner',)]
        list_order = [('owner',), ('item',)]
        for operation, expected in ((tuple, tuple_order), (Tuple, tuple_order),
                                    (star, tuple_order), (list, list_order),
                                    (lambda source: map(None, source), list_order)):
            events[:] = []
            with self.assertRaises(IndexError) as caught:
                operation(native_failure_source(events, [Item()]))
            self.assertEqual(caught.exception.args, ('pop from empty list',))
            self.assertEqual(events, expected)

    def test_native_getters_reject_foreign_receivers(self):
        for receiver, kind in ((5, 'int'), ('abc', 'str'), (bytearray('abc'), 'bytearray')):
            for name in ('format', 'itemsize', 'ndim', 'readonly', 'shape', 'strides', 'suboffsets'):
                self.assertEqual(error_text(memoryview.__dict__[name].__get__, receiver),
                                 "TypeError: descriptor '%s' for 'memoryview' objects doesn't apply to '%s' object" % (name, kind))
            for name in ('start', 'stop', 'step'):
                self.assertEqual(error_text(slice.__dict__[name].__get__, receiver),
                                 "TypeError: descriptor '%s' for 'slice' objects doesn't apply to '%s' object" % (name, kind))
        self.assertEqual(memoryview.__dict__['shape'].__get__(memoryview('abc')), (3L,))

    def test_format_spec_errors_follow_the_spec_parser(self):
        cases = [
            ((1.5, 'd'), "ValueError: Unknown format code 'd' for object of type 'float'"),
            ((1, '.2'), 'ValueError: Precision not allowed in integer format specifier'),
            ((1, '9' * 20), 'ValueError: Too many decimal digits in format string'),
            ((1.0, '.' + '9' * 20 + 'f'), 'ValueError: Too many decimal digits in format string'),
            ((1.0, '.%df' % (2 ** 31)), 'ValueError: precision too big'),
            ((1, '.'), 'ValueError: Format specifier missing precision'),
            ((1, 'xx'), 'ValueError: Invalid conversion specification'),
            ((1, ',x'), "ValueError: Cannot specify ',' with 'x'."),
            (('abc', ','), "ValueError: Cannot specify ',' with 's'."),
            ((1, '+c'), "ValueError: Sign not allowed with integer format specifier 'c'"),
            ((-1, 'c'), 'OverflowError: %c arg not in range(0x100)'),
            ((1.5, '#'), 'ValueError: Alternate form (#) not allowed in float format specifier'),
            ((1 + 2j, '010'), 'ValueError: Zero padding is not allowed in complex format specifier'),
            ((1 + 2j, '%'), "ValueError: Unknown format code '%' for object of type 'complex'"),
            (('abc', '+'), 'ValueError: Sign not allowed in string format specifier'),
            (('abc', '#'), 'ValueError: Alternate form (#) not allowed in string format specifier'),
            ((u'abc', '010'), "ValueError: '=' alignment not allowed in string format specifier"),
            ((u'abc', u'\x01'), "ValueError: Unknown format code '\\x1' for object of type 'unicode'"),
            (([1], 'x'), "ValueError: Unknown format code 'x' for object of type 'str'"),
        ]
        for arguments, expected in cases:
            self.assertEqual(error_text(format, *arguments), expected)
        self.assertEqual(error_text('{:<{}}'.format, 'a', 2 ** 63), 'ValueError: Too many decimal digits in format string')
        self.assertEqual(len(format(1.0, '.100001f')), 100003)

    def test_percent_operands_and_messages(self):
        class IntegerWithFloat(int):
            def __float__(self):
                return 2.5
        class Classic:
            pass
        class Mapping(object):
            def __getitem__(self, key):
                return key.upper()
        self.assertEqual('%f' % IntegerWithFloat(1), '2.500000')
        self.assertEqual(u'%f' % IntegerWithFloat(1), u'2.500000')
        self.assertEqual('' % Classic(), '')
        self.assertEqual('%(a)s %%' % Mapping(), 'A %')
        self.assertEqual('%5%|%-3%|' % (), '    %|%  |')
        self.assertEqual(len('%.100001f' % 1.0), 100003)
        cases = [
            (lambda: '%d' % float('nan'), 'TypeError: %d format: a number is required, not float'),
            (lambda: '%i' % 'x', 'TypeError: %d format: a number is required, not str'),
            (lambda: '%x' % [], 'TypeError: %x format: a number is required, not list'),
            (lambda: '%f' % 'x', 'TypeError: float argument required, not str'),
            (lambda: u'%f' % 'x', 'TypeError: a float is required'),
            (lambda: '%c' % 256, 'OverflowError: unsigned byte integer is greater than maximum'),
            (lambda: '%c' % -1, 'OverflowError: unsigned byte integer is less than minimum'),
            (lambda: '%c' % 'ab', 'TypeError: %c requires int or char'),
            (lambda: u'%c' % '\xe9\xe9', 'TypeError: %c requires int or char'),
            (lambda: u'%c' % 0x110000, 'OverflowError: %c arg not in range(0x110000) (wide Python build)'),
            (lambda: '%y' % 1, "ValueError: unsupported format character 'y' (0x79) at index 1"),
            (lambda: u'ab%\u1234' % 1, "ValueError: unsupported format character '?' (0x1234) at index 3"),
            (lambda: '%lld' % 1, "ValueError: unsupported format character 'l' (0x6c) at index 2"),
            (lambda: '%.117d' % 1, 'OverflowError: formatted integer is too long (precision too large?)'),
            (lambda: '%.*f' % (2 ** 31, 1.0), 'OverflowError: Python int too large to convert to C int'),
            (lambda: '%(a)s' % 5, 'TypeError: format requires a mapping'),
            (lambda: '%(a)*d' % {'a': 1}, 'TypeError: not enough arguments for format string'),
        ]
        for function, expected in cases:
            self.assertEqual(error_text(function), expected)

    def test_text_search_bounds_require_index(self):
        self.assertEqual(error_text('abc'.find, 'b', 1.5), 'TypeError: slice indices must be integers or None or have an __index__ method')
        self.assertEqual(error_text(u'abc'.count, u'b', None, 'x'), 'TypeError: slice indices must be integers or None or have an __index__ method')
        self.assertEqual('abc'.find('b', True), 1)

    def test_codec_names_resolve_like_the_encodings_search_function(self):
        for name in (' utf 8 ', 'Latin_1', 'ISO-8859-1', 'US-ASCII', 'iso_646.irv:1991', 'ansi.x3_4.1968', '-UTF-8-'):
            self.assertEqual(u'a'.encode(name), 'a')
        for name in ('u-t-f-8', 'latin.1', 'utf.8', 'iso.646.irv.1991'):
            self.assertRaises(LookupError, u'a'.encode, name)
            self.assertRaises(LookupError, _codecs.lookup, name)
        self.assertEqual(len(_codecs.lookup('  hEx  ')), 4)
        self.assertFalse(hasattr(_codecs, '_search_path'))
        def search(name):
            if name == 'builtin_edges_returns_int':
                return (lambda text, errors='strict': (5, 1), None, None, None)
        _codecs.register(search)
        self.assertEqual(error_text(u'a'.encode, 'builtin_edges_returns_int'), 'TypeError: encoder did not return a string/unicode object (type=int)')
        self.assertEqual(error_text(bytearray, u'a', 'builtin_edges_returns_int'), "TypeError: can't concat int to bytearray")
        self.assertEqual(bytearray('ab', 'builtin_edges_returns_int'), bytearray('ab'))
        self.assertEqual(error_text(_codecs.register_error, 'builtin_edges', 5), 'TypeError: handler must be callable')

    def test_codec_error_handlers_clamp_their_ranges(self):
        replace = _codecs.lookup_error('replace')
        self.assertEqual(replace(UnicodeTranslateError(u'abc', 0, 2 ** 40, 'x')), (u'\ufffd' * 3, 3))
        self.assertEqual(replace(UnicodeEncodeError('ascii', u'abc', 1, 10, 'x')), (u'??', 3))
        self.assertEqual(replace(UnicodeEncodeError('ascii', u'', 0, 0, 'x')), (u'?', 0))
        self.assertRaises(MemoryError, replace, UnicodeEncodeError('ascii', u'abc', 5, 1, 'x'))
        self.assertEqual(_codecs.lookup_error('xmlcharrefreplace')(UnicodeEncodeError('ascii', u'abc', 5, 1, 'x')), (u'', 1))

    def test_bytearray_index_and_resize_errors(self):
        data = bytearray('abc')
        self.assertEqual(error_text(data.__getitem__, 2 ** 70), "IndexError: cannot fit 'long' into an index-sized integer")
        self.assertEqual(error_text(data.__getitem__, 'a'), 'TypeError: bytearray indices must be integers')
        self.assertEqual(error_text(data.__setitem__, 'a', 1), 'TypeError: bytearray indices must be integer')
        self.assertEqual(error_text(buffer('abc').__getitem__, 2 ** 70), "IndexError: cannot fit 'long' into an index-sized integer")
        self.assertEqual(error_text(data.__setitem__, slice(None, None, 2), 'xyz'), 'ValueError: attempt to assign bytes of size 3 to extended slice of size 2')
        view = memoryview(data)
        self.assertEqual(error_text(data.__delitem__, slice(10, None, 2)), 'BufferError: Existing exports of data: object cannot be re-sized')
        del view
        del data[10::2]
        self.assertEqual(data, bytearray('abc'))

    def test_struct_pack_into_argument_messages(self):
        target = bytearray(16)
        self.assertEqual(error_text(_struct.pack_into, '<d', target, 2 ** 70, 1.5), 'OverflowError: long int too large to convert to int')
        self.assertEqual(error_text(_struct.pack_into, '<d', 'abc', 0, 1.5), 'TypeError: argument must be read-write buffer, not str')
        _struct.pack_into('<d', target, 8.7, 1.5)
        self.assertEqual(_struct.unpack_from('<d', target, 8), (1.5,))

    def test_builtin_argument_messages(self):
        class Text(str):
            pass
        class NegativeLength(object):
            def __len__(self):
                return -1
        cases = [
            (lambda: chr(256), 'ValueError: chr() arg not in range(256)'),
            (lambda: chr(65.0), 'TypeError: integer argument expected, got float'),
            (lambda: unichr(0x110000), 'ValueError: unichr() arg not in range(0x110000) (wide Python build)'),
            (lambda: ord('ab'), 'TypeError: ord() expected a character, but string of length 2 found'),
            (lambda: ord(u''), 'TypeError: ord() expected a character, but string of length 0 found'),
            (lambda: ord(5), 'TypeError: ord() expected string of length 1, but int found'),
            (lambda: min(), 'TypeError: min expected 1 arguments, got 0'),
            (lambda: max([]), 'ValueError: max() arg is an empty sequence'),
            (lambda: min([1], foo=1), 'TypeError: min() got an unexpected keyword argument'),
            (lambda: sum(['a'], ''), "TypeError: sum() can't sum strings [use ''.join(seq) instead]"),
            (lambda: sorted([1], foo=1), "TypeError: 'foo' is an invalid keyword argument for this function"),
            (lambda: sorted([1], None, None, False, x=1), 'TypeError: sorted() takes at most 4 arguments (5 given)'),
            (lambda: getattr(1, 5), 'TypeError: getattr(): attribute name must be string'),
            (lambda: hasattr(1, None), 'TypeError: hasattr(): attribute name must be string'),
            (lambda: setattr(Text(), 5, 1), "TypeError: attribute name must be string, not 'int'"),
            (lambda: getattr(1, u'\xe9'), "UnicodeEncodeError: 'ascii' codec can't encode character u'\\xe9' in position 0: ordinal not in range(128)"),
            (lambda: next([1]), 'TypeError: list object is not an iterator'),
            (lambda: zip([1], 5), 'TypeError: zip argument #2 must support iteration'),
            (lambda: apply(len, 5), 'TypeError: apply() arg 2 expected sequence, found int'),
            (lambda: len(5), "TypeError: object of type 'int' has no len()"),
            (lambda: len(NegativeLength()), 'ValueError: __len__() should return >= 0'),
            (lambda: reduce(len), 'TypeError: reduce expected at least 2 arguments, got 1'),
            (lambda: reduce(len, [], x=1), 'TypeError: reduce() takes no keyword arguments'),
            (lambda: len([], x=1), 'TypeError: len() takes no keyword arguments'),
            (lambda: intern(u'x'), 'TypeError: intern() argument 1 must be string, not unicode'),
            (lambda: intern(Text('x')), "TypeError: can't intern subclass of string"),
        ]
        for function, expected in cases:
            self.assertEqual(error_text(function), expected)

    def test_range_falls_back_to_long_bounds(self):
        result = range(0, 2 ** 64, 2 ** 63)
        self.assertEqual(result, [0, 2 ** 63])
        self.assertEqual([type(item) for item in result], [long, long])
        self.assertEqual([type(item) for item in range(5L)], [int] * 5)
        self.assertEqual(error_text(range, 1.5), 'TypeError: range() integer end argument expected, got float.')
        self.assertEqual(error_text(range, 1.5, 2), 'TypeError: range() integer start argument expected, got float.')
        self.assertEqual(error_text(range, 0, 2 ** 63), 'OverflowError: range() result has too many items')
        self.assertEqual(error_text(range, 0, 1, 0), 'ValueError: range() step argument must not be zero')
        self.assertEqual(error_text(xrange, -2 ** 63, 2 ** 63 - 1), 'OverflowError: xrange() result has too many items')

    def test_instance_checks_bound_classinfo_recursion(self):
        nested = str
        for depth in xrange(5000):
            nested = (nested,)
        self.assertEqual(error_text(isinstance, 1, nested), 'RuntimeError: maximum recursion depth exceeded in __instancecheck__')
        self.assertEqual(error_text(issubclass, int, nested), 'RuntimeError: maximum recursion depth exceeded in __subclasscheck__')
        class Bases(object):
            def __init__(self, base):
                self.__bases__ = (base,) if base is not None else ()
        chain = Bases(None)
        for depth in xrange(5000):
            chain = Bases(chain)
        self.assertIs(issubclass(chain, Bases(None)), False)
        class RaisingBases(object):
            @property
            def __bases__(self):
                raise KeyError('bases')
        self.assertRaises(KeyError, issubclass, RaisingBases(), Bases(None))

    def test_setattr_interns_attribute_names(self):
        class Holder(object):
            pass
        holder = Holder()
        setattr(holder, ''.join(['fo', 'o']), 1)
        self.assertIs([key for key in holder.__dict__][0], 'foo')

    def test_dir_merge_survives_dict_replacement(self):
        events = []
        class Key(object):
            def __hash__(self):
                return hash('a')
            def __eq__(self, other):
                if not events:
                    events.append(other)
                    holder.__dict__ = {}
                return False
        class Holder(object):
            pass
        holder = Holder()
        namespace = {Key(): 2, 'a': 1}
        for index in range(20):
            namespace['x%d' % index] = index
        holder.__dict__ = namespace
        del namespace
        self.assertIn('x19', dir(holder))
        self.assertEqual(events, ['a'])

    def test_dir_merges_bases_deeper_than_the_recursion_limit(self):
        # merge_class_dict takes no recursion level, so a hierarchy deeper
        # than the limit lists every inherited name.
        class OldBase:
            inherited = 1
        class NewBase(object):
            inherited = 1
        old, new = OldBase, NewBase
        for index in range(200):
            derived = type(OldBase)('Old%d' % index, (), {})
            derived.__bases__ = (old,)
            old = derived
            new = type('New%d' % index, (new,), {})
        frame = sys._getframe()
        depth = 0
        while frame is not None:
            depth += 1
            frame = frame.f_back
        limit = sys.getrecursionlimit()
        sys.setrecursionlimit(depth + 50)
        try:
            found = ['inherited' in dir(value) for value in (old, old(), new, new())]
        finally:
            sys.setrecursionlimit(limit)
        self.assertEqual(found, [True] * 4)

    def test_import_with_none_parent_is_absolute(self):
        sys.modules['builtin_edges_none_parent'] = None
        try:
            self.assertEqual(error_text(__import__, 'builtin_edges_missing', {'__name__': 'builtin_edges_none_parent.child'}, {}, [], 1),
                             'ImportError: No module named builtin_edges_missing')
        finally:
            del sys.modules['builtin_edges_none_parent']

    def test_output_softspace_is_an_attribute(self):
        stream = sys.stdout
        previous = stream.softspace
        stream.softspace = 1
        self.assertEqual(stream.softspace, 1)
        stream.softspace = previous

    def test_handled_exceptions_do_not_render_messages(self):
        events = []
        class Key(object):
            def __hash__(self):
                return 1
            def __repr__(self):
                events.append('repr')
                return 'Key'
        class Failure(Exception):
            def __str__(self):
                events.append('str')
                return 'failure'
        def fail():
            raise Failure()
        try:
            {}[Key()]
        except KeyError as error:
            self.assertEqual(len(error.args), 1)
        try:
            fail()
        except Failure:
            pass
        self.assertEqual(events, [])
        self.assertEqual(str(Failure()), 'failure')
        self.assertEqual(events, ['str'])

    def test_builtin_exception_initializer_fields(self):
        error = EnvironmentError(2, 'missing', 'name')
        self.assertEqual((error.args, error.errno, error.strerror, error.filename), ((2, 'missing'), 2, 'missing', 'name'))
        error = SyntaxError('bad', ('file', 3, 4, 'text'))
        self.assertEqual((error.msg, error.filename, error.lineno, error.offset, error.text), ('bad', 'file', 3, 4, 'text'))
        error = UnicodeDecodeError('ascii', '\xff', 0, 1, 'bad')
        self.assertEqual((error.encoding, error.object, error.start, error.end, error.reason), ('ascii', '\xff', 0, 1, 'bad'))
        error = UnicodeTranslateError(u'x', 0, 1, 'bad')
        self.assertEqual((error.object, error.start, error.end, error.reason), (u'x', 0, 1, 'bad'))
        self.assertEqual((SystemExit(3).code, SystemExit(3, 4).code, SystemExit().code), (3, (3, 4), None))
        self.assertEqual((KeyError('k').args, LookupError().args), (('k',), ()))
        self.assertEqual(error_text(ValueError, a=1), 'TypeError: exceptions.ValueError does not take keyword arguments')
        class Pair(KeyError):
            def __init__(self, value):
                KeyError.__init__(self, value, value)
        self.assertEqual(Pair(1).args, (1, 1))
        arguments = (1, 2)
        self.assertEqual(ValueError(*arguments).args, arguments)

    def test_native_calls_reenter_with_same_argument_count(self):
        lookup = {'a': 1}
        class Key(object):
            def __init__(self, name):
                self.name = name
            def __hash__(self):
                return hash(lookup.get(self.name, self.name))
            def __eq__(self, other):
                return lookup.get('a', 0) == 1 and self.name == getattr(other, 'name', other)
        table = {Key('k'): 'v'}
        for attempt in range(3):
            self.assertEqual(table.get(Key('k'), 'missing'), 'v')
            self.assertEqual(table.get(Key('z'), 'missing'), 'missing')
            self.assertEqual(lookup.get('a', 0), 1)
        kept = []
        class Keeper(object):
            def __eq__(self, other):
                kept.append(sys.exc_info())
                return False
        self.assertEqual([Keeper()].count(Keeper()), 0)
        self.assertEqual(len(kept), 1)
        self.assertEqual(max(3, 7), 7)
        self.assertEqual(divmod(7, 3), (2, 1))
        self.assertEqual(divmod(9, 4), (2, 1))
        self.assertEqual(isinstance(table, dict), True)

    def test_float_text_rounds_the_exact_binary_value(self):
        formats = ['%.2f', '%.0f', '%#.0f', '%.3f', '%.12g', '%.15g', '%#.6g', '%g']
        cases = [
            (0.125, ['0.12', '0', '0.', '0.125', '0.125', '0.125', '0.125000', '0.125'], '0.125'),
            (0.375, ['0.38', '0', '0.', '0.375', '0.375', '0.375', '0.375000', '0.375'], '0.375'),
            (2.5, ['2.50', '2', '2.', '2.500', '2.5', '2.5', '2.50000', '2.5'], '2.5'),
            (3.5, ['3.50', '4', '4.', '3.500', '3.5', '3.5', '3.50000', '3.5'], '3.5'),
            (1.0005, ['1.00', '1', '1.', '1.000', '1.0005', '1.0005', '1.00050', '1.0005'], '1.0005'),
            (9.999999999999991e-05, ['0.00', '0', '0.', '0.000', '0.0001', '9.99999999999999e-05', '0.000100000', '0.0001'], '0.0001'),
            (99999.99999999999, ['100000.00', '100000', '100000.', '100000.000', '100000', '100000', '100000.', '100000'], '100000.0'),
            (999999999999.5, ['999999999999.50', '1000000000000', '1000000000000.', '999999999999.500', '1e+12', '999999999999.5', '1.00000e+12', '1e+12'], '1e+12'),
            (123456.789, ['123456.79', '123457', '123457.', '123456.789', '123456.789', '123456.789', '123457.', '123457'], '123456.789'),
            (5e-324, ['0.00', '0', '0.', '0.000', '4.94065645841e-324', '4.94065645841247e-324', '4.94066e-324', '4.94066e-324'], '4.94065645841e-324'),
        ]
        for value, expected, text in cases:
            self.assertEqual([format_text % value for format_text in formats], expected)
            self.assertEqual([format_text % -value for format_text in formats], ['-' + item for item in expected])
            self.assertEqual(str(value), text)

    def test_hasattr_suppresses_only_exceptions(self):
        events = []
        class Probe(object):
            present = 1
            @property
            def missing_property(self):
                events.append('property')
                raise AttributeError('missing_property')
            @property
            def failing_property(self):
                raise ValueError('failing_property')
            @property
            def interrupting_property(self):
                raise KeyboardInterrupt
            def __getattr__(self, name):
                events.append(name)
                if name == 'dynamic':
                    return 5
                raise AttributeError(name)
        probe = Probe()
        self.assertEqual([hasattr(probe, name) for name in ('present', 'dynamic', 'absent', 'missing_property', 'failing_property')], [True, True, False, False, False])
        self.assertEqual(events, ['dynamic', 'absent', 'property', 'missing_property'])
        self.assertEqual(sys.exc_info(), (None, None, None))
        self.assertRaises(KeyboardInterrupt, hasattr, probe, 'interrupting_property')
        self.assertEqual((hasattr(1, 'real'), hasattr(1, 'imaginary_part'), hasattr(Probe, 'absent')), (True, False, False))

    def test_plain_text_conversions_keep_subclass_overrides(self):
        class Text(str):
            def __format__(self, spec):
                return 'text:' + spec
            def __str__(self):
                return 'str'
        class Number(int):
            def __format__(self, spec):
                return 'number:' + spec
        class Real(float):
            def __str__(self):
                return 'real'
        values = (Text('a'), Number(1), Real(1.5), 7, 0.25, 'x')
        self.assertEqual('{}|{}|{}|{}|{}|{}'.format(*values), 'text:|number:|real|7|0.25|x')
        self.assertEqual('%s|%s|%s|%s|%s|%s' % values, 'str|1|real|7|0.25|x')
        self.assertEqual(u'{}|{}'.format(7, 'x'), u'7|x')

    def test_round_uses_the_exact_binary_value(self):
        values = (0.125, 0.375, -0.125, 2.675, 1.005, 0.285, -0.0001, 0.5, 2.5)
        self.assertEqual([repr(round(value, 2)) for value in values], ['0.13', '0.38', '-0.13', '2.67', '1.0', '0.28', '-0.0', '0.5', '2.5'])
        self.assertEqual([repr(round(value, digits)) for value, digits in ((123456789.987654321, 5), (1e15 + 0.5, 1), (0.1 + 0.2, 15), (5e-324, 19), (-2.5, 19))],
                         ['123456789.98765', '1000000000000000.5', '0.3', '0.0', '-2.5'])

    def test_classic_instances_supply_index_and_items_through_getattr(self):
        class Proxy:
            def __init__(self, target):
                self.target = target
            def __getattr__(self, name):
                return getattr(self.target, name)
        class Plain:
            pass
        class Failing:
            def __getattr__(self, name):
                if name == '__getitem__':
                    raise KeyError(name)
                raise AttributeError(name)
        class Items(object):
            def __index__(self):
                return 1
            def __iter__(self):
                return iter([7, 8])
            def __getitem__(self, index):
                return [7, 8][index]
            def __len__(self):
                return 2
        class KeyTypes(object):
            def __getitem__(self, key):
                return type(key).__name__
        two = Proxy(2)
        items = Proxy(Items())
        self.assertEqual(bytearray(two), bytearray(2))
        self.assertEqual(bytearray(items), bytearray(1))
        data = bytearray('abcd')
        data.__init__(two)
        self.assertEqual(data, bytearray(2))
        self.assertEqual(bytearray('abcd')[two], 99)
        data = bytearray('abcd')
        data[two] = 65
        del data[items]
        self.assertEqual(data, bytearray('aAd'))
        self.assertEqual(bytearray('ab').__mul__(two), bytearray('abab'))
        self.assertEqual(bytearray('abcd').find('c', two), 2)
        self.assertEqual(list(enumerate('ab', two)), [(2, 'a'), (3, 'b')])
        self.assertEqual('abcd'.find('c', two), 2)
        self.assertEqual(u'abcc'.count(u'c', 0, two), 0)
        self.assertEqual(memoryview('abcd')[two], 'c')
        self.assertEqual(buffer('abcd')[two], 'c')
        self.assertEqual(buffer('ab').__mul__(two), 'abab')
        self.assertEqual(sys.float_info[items], sys.float_info.max_exp)
        self.assertEqual(apply(max, items), 8)
        self.assertEqual('{0[1]}'.format(Proxy(KeyTypes())), 'int')
        self.assertEqual(list(reversed(Proxy('abc'))), ['c', 'b', 'a'])
        for function in (lambda value: bytearray('abc')[value], lambda value: list(enumerate('ab', value)), lambda value: 'abc'.find('b', value),
                         lambda value: memoryview('abc')[value], lambda value: buffer('abc')[value], lambda value: sys.float_info[value]):
            self.assertEqual(error_text(function, Plain()), 'TypeError: object cannot be interpreted as an index')
        self.assertEqual(error_text(apply, max, two), 'TypeError: apply() arg 2 expected sequence, found instance')
        self.assertEqual(error_text(apply, max, Failing()), 'TypeError: apply() arg 2 expected sequence, found instance')
        self.assertEqual(error_text(reversed, Failing()), 'TypeError: argument to reversed() must be a sequence')
        self.assertEqual(error_text(iter, Failing()), 'TypeError: iteration over non-sequence')

    def test_print_function_checks_stdout_before_its_affixes(self):
        print_function = getattr(__builtin__, 'print')
        class Collect(object):
            def __init__(self):
                self.parts = []
            def write(self, text):
                self.parts.append(text)
        saved = sys.stdout
        try:
            sys.stdout = None
            self.assertEqual(print_function(1, sep=5), None)
            self.assertEqual(print_function(end=5, file=None), None)
            self.assertEqual(error_text(print_function, 1, file=Collect(), sep=5), 'TypeError: sep must be None, str or unicode, not int')
            del sys.stdout
            self.assertEqual(error_text(print_function, end=5), 'TypeError: end must be None, str or unicode, not int')
            self.assertEqual(error_text(print_function, 1), 'TypeError: writeobject with NULL file')
            self.assertEqual(error_text(print_function), 'TypeError: writeobject with NULL file')
            collect = Collect()
            print_function(1, 2, file=collect)
            self.assertEqual(collect.parts, ['1', ' ', '2', '\n'])
        finally:
            sys.stdout = saved

    def test_mapping_keys_and_argument_parsing_messages(self):
        class NoKeys(object):
            keys = None
            def __getitem__(self, key):
                raise KeyError(key)
        class Hint(object):
            def __iter__(self):
                return iter([1, 2])
            def __length_hint__(self):
                return -1
        class Integer(object):
            def __init__(self, log, name):
                self.log = log
                self.name = name
            def __int__(self):
                self.log.append(self.name)
                return 0
        not_callable = "TypeError: attribute of type 'NoneType' is not callable"
        self.assertEqual(error_text(lambda: (lambda **kwargs: kwargs)(**NoKeys())), not_callable)
        self.assertEqual(error_text(lambda: '{a}'.format(**NoKeys())), not_callable)
        self.assertEqual(error_text(eval, 'dir()', {}, NoKeys()), not_callable)
        self.assertEqual(error_text(lambda: (lambda *args: args)(*Hint())), 'SystemError: error return without exception set')
        self.assertEqual(error_text(slice(1).indices), 'TypeError: indices() takes exactly one argument (0 given)')
        self.assertEqual(error_text(slice(1).indices, 1, 2), 'TypeError: indices() takes exactly one argument (2 given)')
        self.assertEqual(error_text(slice(1).indices, x=1), 'TypeError: indices() takes no keyword arguments')
        self.assertEqual(error_text(slice(1).__cmp__), 'TypeError: expected 1 arguments, got 0')
        self.assertEqual(error_text(slice(1).__cmp__, 1), "TypeError: slice.__cmp__(x,y) requires y to be a 'slice', not a 'int'")
        self.assertEqual(error_text(slice(1).__repr__, 1), 'TypeError: expected 0 arguments, got 1')
        self.assertEqual(error_text(compile, '1', 5, 'eval', 0, flags=0), 'TypeError: compile() argument 2 must be string, not int')
        self.assertEqual(error_text(compile, '1', 5, 'eval', bogus=0), 'TypeError: compile() argument 2 must be string, not int')
        self.assertEqual(error_text(compile, '1', 5), 'TypeError: compile() argument 2 must be string, not int')
        log = []
        self.assertEqual(error_text(compile, '1', 'f', 'eval', Integer(log, 'flags'), bogus=1), "TypeError: 'bogus' is an invalid keyword argument for this function")
        self.assertEqual(error_text(compile, '1', 'f', 'eval', Integer(log, 'positional'), flags=Integer(log, 'keyword')), "TypeError: Argument given by name ('flags') and position (4)")
        self.assertEqual(log, ['flags'])
        self.assertEqual(error_text(round, 'x', bogus=1), 'TypeError: a float is required')
        float_info = type(sys.float_info)
        long_info = type(sys.long_info)
        self.assertEqual(float_info((1,) * 11).max, 1)
        self.assertEqual(long_info(iter([5, 6]), {}), (5, 6))
        self.assertEqual(repr(long_info.__new__(long_info, [5, 6])), 'sys.long_info(bits_per_digit=5, sizeof_digit=6)')
        reduced = sys.float_info.__reduce__()
        self.assertEqual(reduced[0](*reduced[1]), sys.float_info)
        self.assertEqual(error_text(long_info, (1,)), 'TypeError: sys.long_info() takes a 2-sequence (1-sequence given)')
        self.assertEqual(error_text(long_info, 5), 'TypeError: constructor requires a sequence')
        self.assertEqual(error_text(long_info, (1, 2), 5), 'TypeError: sys.long_info() takes a dict as second arg, if any')
        self.assertEqual(error_text(long_info), "TypeError: Required argument 'sequence' (pos 1) not found")
        self.assertEqual(error_text(long_info, (1, 2), {}, 3), 'TypeError: structseq() takes at most 2 arguments (3 given)')
        self.assertEqual(error_text(type(sys.version_info), (2, 7, 18, 'final', 0)), "TypeError: cannot create 'sys.version_info' instances")
