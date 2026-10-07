"""Project-authored Python 2 functional consumer and partial state witnesses."""

import unittest
import sys
import _weakref as weakref
from _functools import partial
from _functools import reduce as functools_reduce


def record(*arguments, **keywords):
    return arguments, keywords


class FunctionalEdges(unittest.TestCase):
    def test_partial_state_canonicalizes_tuple_subtype_through_iterator(self):
        events = []
        class Arguments(tuple):
            def __iter__(self):
                events.append('iter')
                return iter([7, 8])
        value = partial(record, 1)
        source = Arguments([2])
        value.__setstate__((record, source, {}, None))
        self.assertIs(type(value.args), tuple)
        self.assertEqual(value(3), ((7, 8, 3), {}))
        self.assertEqual(events, ['iter'])

    def test_partial_state_copies_dictionary_subtype_without_hooks(self):
        class Keywords(dict):
            def keys(self):
                raise AssertionError('stored dictionary entries are required')
            def __iter__(self):
                raise AssertionError('stored dictionary entries are required')
        source = Keywords(x=2)
        value = partial(record)
        value.__setstate__((record, (1,), source, None))
        self.assertIs(type(value.keywords), dict)
        self.assertIsNot(value.keywords, source)
        source['x'] = 5
        self.assertEqual(value(), ((1,), {'x': 2}))

    def test_partial_state_retains_exact_tuple_and_dictionary(self):
        arguments, keywords, attributes = (1,), {'x': 2}, {'tag': 3}
        value = partial(record)
        value.__setstate__((record, arguments, keywords, attributes))
        self.assertIs(value.args, arguments)
        self.assertIs(value.keywords, keywords)
        self.assertIs(value.__dict__, attributes)
        keywords['x'] = 5
        self.assertEqual(value(), ((1,), {'x': 5}))

    def test_partial_state_conversion_error_preserves_previous_fields(self):
        class Arguments(tuple):
            def __iter__(self):
                raise LookupError('argument conversion')
        value = partial(record, 1, x=2)
        with self.assertRaises(LookupError) as caught:
            value.__setstate__((record, Arguments([7]), {'y': 8}, None))
        self.assertEqual(caught.exception.args, ('argument conversion',))
        self.assertEqual(value(), ((1,), {'x': 2}))
        value.__setstate__((record, (3,), {}, None))
        self.assertEqual(value(), ((3,), {}))

    def test_partial_state_validation_precedes_conversion(self):
        class Arguments(tuple):
            def __iter__(self):
                raise AssertionError('state must be validated first')
        value = partial(record, 1)
        self.assertRaises(TypeError, value.__setstate__, (record, Arguments([7]), [], None))
        self.assertEqual(value(), ((1,), {}))

    def test_partial_state_callback_observes_sequential_field_replacement(self):
        events = []
        class Previous(object):
            def __call__(self):
                return None
            def __del__(self):
                value = self.target()
                events.append((value.func is record, value.args,
                               dict(value.keywords), dict(value.__dict__)))
        previous = Previous()
        value = partial(previous, 1, x=2)
        value.label = 'old'
        previous.target = weakref.ref(value)
        del previous
        value.__setstate__((record, (3,), {'y': 4}, {'label': 'new'}))
        self.assertEqual(events, [(True, (1,), {'x': 2}, {'label': 'old'})])
        self.assertEqual(value(), ((3,), {'y': 4}))
        self.assertEqual(value.label, 'new')

    def test_partial_constructor_copies_cached_keyword_hashes(self):
        events = []
        class Keyword(str):
            def __hash__(self):
                events.append('hash')
                return str.__hash__(self)
        source = {Keyword('x'): 2}
        events[:] = []
        value = partial(record, **source)
        self.assertEqual(events, [])
        self.assertIsNot(value.keywords, source)
        self.assertEqual(len(value.keywords), 1)

    def test_partial_call_copies_cached_keyword_hashes(self):
        events = []
        class Keyword(str):
            def __hash__(self):
                events.append('hash')
                return str.__hash__(self)
        value = partial(record)
        value.keywords[Keyword('x')] = 2
        events[:] = []
        self.assertEqual(value()[0], ())
        # Binding the Python target's **kwargs hashes the key once.
        self.assertEqual(events, ['hash'])

    def test_partial_keyword_merge_error_prevents_target_and_recovers(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append('eq')
                raise LookupError('keyword merge')
            def __hash__(self):
                return str.__hash__(self)
        def target(**keywords):
            events.append('target')
            return keywords
        value = partial(target)
        value.keywords[Keyword('x')] = 1
        with self.assertRaises(LookupError) as caught:
            value(x=2)
        self.assertEqual(caught.exception.args, ('keyword merge',))
        self.assertEqual(events, ['eq'])
        value.keywords.clear()
        self.assertEqual(value(x=3), {'x': 3})
        self.assertEqual(events, ['eq', 'target'])

    def test_partial_call_overrides_keywords_without_mutating_stored_state(self):
        value = partial(record, 1, x=2)
        self.assertEqual(value(3, x=4, y=5), ((1, 3), {'x': 4, 'y': 5}))
        self.assertEqual(value.args, (1,))
        self.assertEqual(value.keywords, {'x': 2})

    def test_partial_state_none_creates_new_keywords_and_clears_attributes(self):
        value = partial(record, x=2)
        value.label = 'old'
        previous = value.keywords
        value.__setstate__((record, (), None, None))
        self.assertEqual(value(), ((), {}))
        self.assertIsNot(value.keywords, previous)
        self.assertRaises(AttributeError, getattr, value, 'label')

    def test_partial_nested_callable_preserves_outer_and_inner_order(self):
        inner = partial(record, 1, x=2)
        outer = partial(inner, 3, y=4)
        self.assertIs(outer.func, inner)
        self.assertEqual(outer(5, x=6), ((1, 3, 5), {'x': 6, 'y': 4}))

    def test_enumerate_canonicalizes_small_integer_counter_types(self):
        class Integer(int):
            def __add__(self, other):
                raise AssertionError('counter is an exact int')
        class Wide(long):
            def __int__(self):
                raise AssertionError('counter uses stored value')
            def __add__(self, other):
                raise AssertionError('counter is an exact int')
        for start in (Integer(5), 5L, Wide(5), True):
            result = list(enumerate(['a', 'b'], start))
            self.assertIs(type(result[0][0]), int)
            self.assertIs(type(result[1][0]), int)
            self.assertEqual(result[0][0], int(start) if type(start) is not Wide else 5)

    def test_enumerate_counter_promotes_at_integer_boundary(self):
        maximum = (1L << 63) - 1
        result = list(enumerate(['a', 'b'], maximum))
        self.assertIs(type(result[0][0]), int)
        self.assertIs(type(result[1][0]), long)
        self.assertEqual(result, [(maximum, 'a'), (maximum + 1, 'b')])

    def test_enumerate_converts_index_before_requesting_iterator(self):
        events = []
        class Start(object):
            def __index__(self):
                events.append('index')
                return 7L
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter(['a'])
        self.assertEqual(list(enumerate(Source(), Start())), [(7, 'a')])
        self.assertEqual(events, ['index', 'iter'])

    def test_enumerate_known_keyword_can_be_ignored_by_subtype_equality(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append(other)
                return False
            def __hash__(self):
                return str.__hash__(self)
        self.assertEqual(list(enumerate([3], **{Keyword('start'): 7})), [(0, 3)])
        self.assertEqual(events, ['start'])

    def test_enumerate_keyword_lookup_errors_preserve_handled_state(self):
        class Keyword(str):
            def __eq__(self, other):
                raise KeyboardInterrupt('keyword lookup')
            def __hash__(self):
                return str.__hash__(self)
        try:
            raise ValueError('handled')
        except ValueError as handled:
            self.assertEqual(list(enumerate([3], **{Keyword('start'): 7})), [(0, 3)])
            self.assertIs(sys.exc_info()[1], handled)
        sys.exc_clear()

    def test_enumerate_total_argument_count_precedes_keyword_lookup(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append(other)
                return str.__eq__(self, other)
            def __hash__(self):
                return str.__hash__(self)
        with self.assertRaises(TypeError):
            enumerate([1], **{Keyword('sequence'): [2], Keyword('start'): 7})
        self.assertEqual(events, [])

    def test_reversed_accepts_float_and_custom_integer_length(self):
        events = []
        class Number(object):
            def __int__(self):
                events.append('int')
                return 2
        class Source(object):
            def __len__(self):
                events.append('len')
                return self.length
            def __getitem__(self, index):
                events.append(index)
                return index
        for length, expected in ((2.75, ['len', 'len', 1, 0]),
                                 (Number(), ['len', 'int', 'len', 'int', 1, 0])):
            events[:] = []
            source = Source()
            source.length = length
            self.assertEqual(list(reversed(source)), [1, 0])
            self.assertEqual(events, expected)

    def test_reversed_classic_length_remains_strict_integer(self):
        class Source:
            def __len__(self):
                return 2.75
            def __getitem__(self, index):
                return index
        self.assertRaises(TypeError, reversed, Source())

    def test_reversed_failed_length_conversion_recovers(self):
        class Number(object):
            def __int__(self):
                raise LookupError('length conversion')
        class Source(object):
            def __len__(self):
                return self.length
            def __getitem__(self, index):
                return index
        source = Source()
        source.length = Number()
        self.assertRaises(LookupError, reversed, source)
        source.length = 2
        self.assertEqual(list(reversed(source)), [1, 0])

    def test_xrange_uses_int_protocol_and_long_subtype_conversion(self):
        events = []
        class Wide(long):
            def __int__(self):
                events.append('int')
                return 3
        class Number(object):
            def __int__(self):
                events.append('int')
                return 2
        self.assertEqual(list(xrange(Wide(1))), [0, 1, 2])
        self.assertEqual(list(xrange(Number())), [0, 1])
        self.assertEqual(events, ['int', 'int'])

    def test_xrange_rejects_index_only_and_float_without_callbacks(self):
        class Index(object):
            def __index__(self):
                raise AssertionError('xrange uses __int__')
        self.assertRaises(TypeError, xrange, Index())
        self.assertRaises(TypeError, xrange, 2.75)
        self.assertEqual(list(xrange(2)), [0, 1])

    def test_xrange_argument_conversion_order_precedes_zero_step(self):
        events = []
        class Number(object):
            def __init__(self, name, value):
                self.name, self.value = name, value
            def __int__(self):
                events.append(self.name)
                return self.value
        self.assertRaises(ValueError, xrange, Number('start', 0), Number('stop', 3), Number('step', 0))
        self.assertEqual(events, ['start', 'stop', 'step'])

    def test_min_max_lookup_key_before_rejecting_extra_keyword(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append(other)
                return str.__eq__(self, other)
            def __hash__(self):
                return str.__hash__(self)
        for operation in (min, max):
            events[:] = []
            with self.assertRaises(TypeError):
                operation([1], **{Keyword('key'): lambda value: value, 'extra': 2})
            self.assertEqual(events, ['key'])

    def test_min_max_keyword_lookup_error_is_replaced_with_type_error(self):
        class Keyword(str):
            def __eq__(self, other):
                raise LookupError('key lookup')
            def __hash__(self):
                return str.__hash__(self)
        for operation in (min, max):
            with self.assertRaises(TypeError):
                operation([1], **{Keyword('key'): lambda value: value})
            self.assertEqual(operation([2, 1]), 1 if operation is min else 2)

    def test_reduce_replaces_initial_iterator_error_and_recovers(self):
        class Source(object):
            def __iter__(self):
                raise LookupError('source iteration')
        for operation in (reduce, functools_reduce):
            with self.assertRaises(TypeError) as caught:
                operation(lambda left, right: left + right, Source())
            self.assertEqual(caught.exception.args, ('reduce() arg 2 must support iteration',))
            self.assertEqual(operation(lambda left, right: left + right, [1, 2]), 3)

    def test_map_general_path_replaces_iterator_error_and_shortcut_preserves_it(self):
        class Source(object):
            def __iter__(self):
                raise LookupError('source iteration')
        self.assertRaises(LookupError, map, None, Source())
        with self.assertRaises(TypeError) as caught:
            map(lambda value: value, Source())
        self.assertEqual(caught.exception.args, ('argument 2 to map() must support iteration',))
        with self.assertRaises(TypeError) as caught:
            map(None, [1], Source())
        self.assertEqual(caught.exception.args, ('argument 3 to map() must support iteration',))
        self.assertEqual(map(lambda value: value + 1, [1, 2]), [2, 3])

    def test_reduce_retains_previous_accumulator_through_iterator_exhaustion(self):
        for operation in (reduce, functools_reduce):
            events, observed = [], []
            class Total(object):
                def __init__(self, name):
                    self.name = name
                def __del__(self):
                    events.append('released:' + self.name)
            class Source(object):
                def __init__(self):
                    self.position = 0
                def __iter__(self):
                    return self
                def next(self):
                    events.append((self.position, observed[0]() is not None if observed else None))
                    self.position += 1
                    if self.position > 3:
                        raise StopIteration
                    return self.position
            def combine(left, right):
                value = Total(str(right))
                observed.append(weakref.ref(value))
                return value
            value = operation(combine, Source())
            self.assertEqual(events, [(0, None), (1, None), (2, True), (3, True), 'released:2'])
            self.assertEqual(value.name, '3')
            del value

    def test_partial_readonly_attribute_errors_preserve_callable_state(self):
        value = partial(record, 1, x=2)
        for name in ('func', 'args', 'keywords'):
            self.assertRaises(TypeError, setattr, value, name, 3)
            self.assertRaises(TypeError, delattr, value, name)
        self.assertRaises(TypeError, setattr, value, '__dict__', [])
        self.assertRaises(TypeError, delattr, value, '__dict__')
        self.assertEqual(value(), ((1,), {'x': 2}))
