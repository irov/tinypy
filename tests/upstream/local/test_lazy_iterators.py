"""Project-authored lazy builtin checks; Python 2.7 itertools is the oracle."""

import unittest
import _weakref

try:
    xzip, xmap, xfilter
except NameError:
    from itertools import izip as xzip, imap as xmap, ifilter as xfilter

try:
    xchain, xslice
except NameError:
    from itertools import chain as xchain, islice as xslice


class Source(object):
    def __init__(self, values, events):
        self.values = values
        self.events = events
        self.index = 0
    def __iter__(self):
        self.events.append('iter')
        return self
    def __len__(self):
        raise AssertionError('a lazy constructor must not ask for a length')
    def __length_hint__(self):
        raise AssertionError('a lazy constructor must not ask for a hint')
    def next(self):
        self.events.append(('next', self.index))
        if self.index == len(self.values):
            raise StopIteration
        value = self.values[self.index]
        self.index += 1
        return value


class LazyIterators(unittest.TestCase):
    def test_zip_zero_one_and_many_inputs(self):
        self.assertEqual(list(xzip()), [])
        self.assertEqual(list(xzip([1, 2])), [(1,), (2,)])
        self.assertEqual(list(xzip([1, 2], 'ab', (3, 4))),
                         [(1, 'a', 3), (2, 'b', 4)])

    def test_map_one_and_many_inputs(self):
        self.assertEqual(list(xmap(abs, [-1, 2])), [1, 2])
        self.assertEqual(list(xmap(pow, [2, 3], [3, 2])), [8, 9])

    def test_map_none_yields_tuples_including_single_input(self):
        self.assertEqual(list(xmap(None, [1, 2])), [(1,), (2,)])
        self.assertEqual(list(xmap(None, [1, 2], [3])), [(1, 3)])

    def test_zip_and_map_stop_at_shortest_input(self):
        for factory, prefix in ((xzip, ()), (xmap, (None,))):
            self.assertEqual(list(factory(*(prefix + ([1, 2, 3], [4])))), [(1, 4)])
            self.assertEqual(list(factory(*(prefix + ([], [4])))), [])

    def test_filter_none_and_predicate(self):
        self.assertEqual(list(xfilter(None, [0, 1, '', 'a', None])), [1, 'a'])
        self.assertEqual(list(xfilter(lambda x: x % 2, xrange(6))), [1, 3, 5])

    def test_filter_text_and_tuple_yield_individual_values(self):
        self.assertEqual(list(xfilter(None, 'ab')), ['a', 'b'])
        self.assertEqual(list(xfilter(None, u'ab')), [u'a', u'b'])
        self.assertEqual(list(xfilter(None, (0, 1, 2))), [1, 2])

    def test_construction_acquires_iterators_without_consuming_or_sizing(self):
        for factory, prefix in ((xzip, ()), (xmap, (None,)), (xfilter, (None,))):
            events = []
            iterator = factory(*(prefix + (Source([1, 2], events),)))
            self.assertEqual(events, ['iter'])
            self.assertIs(iter(iterator), iterator)
            self.assertEqual(events, ['iter'])
            next(iterator)
            self.assertEqual(events, ['iter', ('next', 0)])

    def test_break_does_not_call_map_for_remaining_values(self):
        events = []
        def record(value):
            events.append(value)
            return value * 2
        iterator = xmap(record, xrange(1000000000))
        self.assertEqual(events, [])
        for item in iterator:
            self.assertEqual(item, 0)
            break
        self.assertEqual(events, [0])
        self.assertEqual(next(iterator), 2)
        self.assertEqual(events, [0, 1])

    def test_filter_only_checks_until_next_match(self):
        events = []
        def record(value):
            events.append(value)
            return value % 2
        iterator = xfilter(record, xrange(10))
        self.assertEqual(events, [])
        self.assertEqual(next(iterator), 1)
        self.assertEqual(events, [0, 1])
        self.assertEqual(next(iterator), 3)
        self.assertEqual(events, [0, 1, 2, 3])

    def test_iterators_are_single_pass_without_indexing_or_length(self):
        for iterator in (xzip([1]), xmap(abs, [-1]), xfilter(None, [1])):
            self.assertIs(iter(iterator), iterator)
            self.assertFalse(hasattr(iterator, '__length_hint__'))
            self.assertRaises(TypeError, len, iterator)
            self.assertRaises(TypeError, lambda: iterator[0])
            self.assertEqual(len(list(iterator)), 1)
            self.assertEqual(list(iterator), [])

    def test_direct_next_and_iter_methods(self):
        for iterator in (xzip([1]), xmap(abs, [-1]), xfilter(None, [1])):
            self.assertIs(iterator.__iter__(), iterator)
            self.assertRaises(TypeError, iterator.next, 42)
            self.assertRaises(TypeError, iterator.next, extra=1)
            self.assertIsNot(iterator.next(), None)
            self.assertRaises(StopIteration, iterator.next)
        for factory in (xzip, xmap, xfilter):
            self.assertRaises(TypeError, factory.next, object())

    def test_zip_shared_input_is_advanced_left_to_right(self):
        source = iter(xrange(5))
        self.assertEqual(list(xzip(source, source)), [(0, 1), (2, 3)])

    def test_shorter_right_input_may_consume_one_extra_left_value(self):
        for factory, prefix in ((xzip, ()), (xmap, (None,))):
            left = iter([1, 2, 3])
            self.assertEqual(list(factory(*(prefix + (left, [4])))), [(1, 4)])
            self.assertEqual(list(left), [3])

    def test_mutation_before_consumption_is_visible(self):
        for factory, prefix in ((xzip, ()), (xmap, (None,)), (xfilter, (None,))):
            source = [1]
            iterator = factory(*(prefix + (source,)))
            source[0] = 2
            source.append(3)
            expected = [2, 3] if factory is xfilter else [(2,), (3,)]
            self.assertEqual(list(iterator), expected)

    def test_invalid_source_is_rejected_during_construction(self):
        self.assertRaises(TypeError, xzip, None)
        self.assertRaises(TypeError, xmap, abs, None)
        self.assertRaises(TypeError, xfilter, None, None)

    def test_arity_and_keywords_are_rejected(self):
        self.assertRaises(TypeError, xmap)
        self.assertRaises(TypeError, xmap, abs)
        for args in ((), (None,), (None, [], [])):
            self.assertRaises(TypeError, xfilter, *args)
        for factory, args in ((xzip, ([],)), (xmap, (abs, [])), (xfilter, (None, []))):
            self.assertRaises(TypeError, factory, *args, extra=1)

    def test_noncallable_is_only_rejected_when_a_value_is_consumed(self):
        for factory in (xmap, xfilter):
            self.assertEqual(list(factory(42, [])), [])
            iterator = factory(42, [1])
            self.assertRaises(TypeError, next, iterator)

    def test_callback_failure_consumes_one_value_and_can_resume(self):
        def fail_first(value):
            if value == 1:
                raise ValueError('callback')
            return value
        for factory in (xmap, xfilter):
            iterator = factory(fail_first, [1, 2, 3])
            self.assertRaises(ValueError, next, iterator)
            self.assertEqual(next(iterator), 2)
            self.assertEqual(list(iterator), [3])

    def test_callback_stopiteration_does_not_poison_the_wrapper(self):
        def stop_first(value):
            if value == 1:
                raise StopIteration('callback')
            return value
        for factory in (xmap, xfilter):
            iterator = factory(stop_first, [1, 2])
            self.assertRaises(StopIteration, next, iterator)
            self.assertEqual(next(iterator), 2)

    def test_source_failure_can_resume_without_replaying_partial_rows(self):
        class Failing(Source):
            def next(self):
                if self.index == 1:
                    self.index += 1
                    raise ValueError('source')
                return Source.next(self)
        for factory, prefix in ((xzip, ()), (xmap, (None,)), (xfilter, (None,))):
            iterator = factory(*(prefix + (Failing([1, 2, 3], []),)))
            next(iterator)
            self.assertRaises(ValueError, next, iterator)
            self.assertEqual(list(iterator), [3] if factory is xfilter else [(3,)])

    def test_filter_truth_failure_can_resume(self):
        class Broken(object):
            def __nonzero__(self):
                raise LookupError('truth')
        iterator = xfilter(None, [Broken(), 2])
        self.assertRaises(LookupError, next, iterator)
        self.assertEqual(next(iterator), 2)

    def test_filter_predicate_can_return_the_input_object(self):
        class Item(object):
            pass
        item = Item()
        reference = _weakref.ref(item)
        iterator = xfilter(lambda value: value, [item])
        self.assertIs(next(iterator), item)
        del item
        del iterator
        self.assertIs(reference(), None)

    def test_wrappers_retain_inputs_and_callbacks_until_released(self):
        class Callback(object):
            def __call__(self, value):
                return value
        for factory in (xzip, xmap, xfilter):
            source = Source([1], [])
            callback = Callback()
            source_ref = _weakref.ref(source)
            callback_ref = _weakref.ref(callback)
            args = (source,) if factory is xzip else (callback, source)
            iterator = factory(*args)
            del args
            del source
            del callback
            self.assertIsNot(source_ref(), None)
            if factory is not xzip:
                self.assertIsNot(callback_ref(), None)
            del iterator
            self.assertIs(source_ref(), None)
            self.assertIs(callback_ref(), None)

    def test_constructor_failure_releases_previously_acquired_iterators(self):
        references = []
        class Iterable(object):
            def __iter__(self):
                iterator = Source([1], [])
                references.append(_weakref.ref(iterator))
                return iterator
        for factory, prefix in ((xzip, ()), (xmap, (None,))):
            self.assertRaises(TypeError, factory, *(prefix + (Iterable(), None)))
            self.assertIs(references[-1](), None)

    def test_reentrant_map_uses_independent_argument_rows(self):
        global _active_map
        events = []
        def callback(value):
            events.append(value)
            if value == 1:
                return (value, next(_active_map))
            return value * 10
        _active_map = xmap(callback, [1, 2, 3])
        try:
            self.assertEqual(next(_active_map), (1, 20))
            self.assertEqual(list(_active_map), [30])
            self.assertEqual(events, [1, 2, 3])
        finally:
            _active_map = None

    def test_subclasses_can_delegate_next(self):
        class Mapped(xmap):
            def next(self):
                return xmap.next(self) * 2
        class Filtered(xfilter):
            def next(self):
                return super(Filtered, self).next() * 2
        class Zipped(xzip):
            pass
        self.assertEqual(list(Mapped(abs, [-1, 2])), [2, 4])
        self.assertEqual(list(Filtered(None, [0, 1, 2])), [2, 4])
        self.assertEqual(list(Zipped([1, 2])), [(1,), (2,)])

    def test_reentrant_filter_keeps_its_current_item(self):
        global _active_filter
        events = []
        def predicate(value):
            events.append(value)
            if value == 1:
                self.assertEqual(next(_active_filter), 2)
            return True
        _active_filter = xfilter(predicate, [1, 2, 3])
        try:
            self.assertEqual(next(_active_filter), 1)
            self.assertEqual(list(_active_filter), [3])
            self.assertEqual(events, [1, 2, 3])
        finally:
            _active_filter = None

    def test_original_functions_still_materialize_results(self):
        self.assertEqual(range(3), [0, 1, 2])
        self.assertEqual(zip([1], [2]), [(1, 2)])
        self.assertEqual(map(None, [1], [2, 3]), [(1, 2), (None, 3)])
        self.assertEqual(filter(None, (0, 1, 2)), (1, 2))
        source = xrange(3)
        self.assertEqual(list(source), [0, 1, 2])
        self.assertEqual(list(source), [0, 1, 2])

    def test_map_bound_method_with_many_inputs(self):
        class Callback(object):
            def method(self, *values):
                return sum(values)
        callback = Callback()
        sources = [xrange(3)] * 12
        self.assertEqual(list(xmap(callback.method, *sources)), [0, 12, 24])

    def test_callable_objects_and_native_functions(self):
        class Modern(object):
            def __call__(self, value):
                return value + 1
        class Classic:
            def __call__(self, value):
                return value + 1
        for function in (Modern(), Classic(), abs):
            self.assertEqual(list(xmap(function, [1, 2])), map(function, [1, 2]))
            self.assertEqual(list(xfilter(function, [1, 2])), filter(function, [1, 2]))


class StreamIterators(unittest.TestCase):
    def test_chain_zero_one_and_many_inputs(self):
        self.assertEqual(list(xchain()), [])
        self.assertEqual(list(xchain([1, 2])), [1, 2])
        self.assertEqual(list(xchain([1], (), 'ab', iter([2]))), [1, 'a', 'b', 2])

    def test_chain_acquires_each_input_only_when_reached(self):
        left, right = [], []
        iterator = xchain(Source([1], left), Source([2], right))
        self.assertEqual((left, right), ([], []))
        self.assertEqual(next(iterator), 1)
        self.assertEqual(left, ['iter', ('next', 0)])
        self.assertEqual(right, [])
        self.assertEqual(next(iterator), 2)
        self.assertEqual(right, ['iter', ('next', 0)])

    def test_chain_invalid_later_input_fails_when_reached_then_stops(self):
        iterator = xchain([1], 42, [2])
        self.assertEqual(next(iterator), 1)
        self.assertRaises(TypeError, next, iterator)
        self.assertEqual(list(iterator), [])

    def test_chain_can_resume_after_an_active_source_error(self):
        class Failing(Source):
            def next(self):
                if self.index == 0:
                    self.index += 1
                    raise LookupError('source')
                return Source.next(self)
        iterator = xchain(Failing([1, 2], []), [3])
        self.assertRaises(LookupError, next, iterator)
        self.assertEqual(list(iterator), [2, 3])

    def test_chain_from_iterable_is_lazy_in_both_levels(self):
        events = []
        outer = Source([Source([1], events), Source([2], events)], events)
        iterator = xchain.from_iterable(outer)
        self.assertEqual(events, ['iter'])
        self.assertEqual(next(iterator), 1)
        self.assertEqual(events, ['iter', ('next', 0), 'iter', ('next', 0)])
        self.assertEqual(list(iterator), [2])

    def test_chain_from_iterable_outer_error_is_terminal(self):
        class Failed(object):
            def __iter__(self):
                return self
            def next(self):
                raise LookupError('outer')
        iterator = xchain.from_iterable(Failed())
        self.assertRaises(LookupError, next, iterator)
        self.assertEqual(list(iterator), [])

    def test_chain_releases_sources_at_exhaustion(self):
        source = Source([1], [])
        reference = _weakref.ref(source)
        iterator = xchain(source)
        del source
        self.assertIsNot(reference(), None)
        self.assertEqual(list(iterator), [1])
        self.assertIs(reference(), None)

    def test_slice_stop_start_step_and_none(self):
        source = xrange(8)
        self.assertEqual(list(xslice(source, 3)), [0, 1, 2])
        self.assertEqual(list(xslice(source, 2, 7, 2)), [2, 4, 6])
        self.assertEqual(list(xslice(source, None, None, None)), list(source))
        self.assertEqual(list(xslice(source, 6, None)), [6, 7])

    def test_slice_construction_does_not_consume_or_size_input(self):
        events = []
        iterator = xslice(Source([0, 1, 2, 3], events), 2, None)
        self.assertEqual(events, ['iter'])
        self.assertEqual(next(iterator), 2)
        self.assertEqual(events, ['iter', ('next', 0), ('next', 1), ('next', 2)])

    def test_slice_preserves_unconsumed_tail(self):
        source = iter(xrange(10))
        iterator = xslice(source, 2, 8, 3)
        self.assertEqual(next(iterator), 2)
        self.assertEqual(next(iterator), 5)
        self.assertRaises(StopIteration, next, iterator)
        self.assertEqual(list(source), [8, 9])

    def test_empty_slice_still_consumes_up_to_start(self):
        source = iter(xrange(8))
        self.assertEqual(list(xslice(source, 5, 2)), [])
        self.assertEqual(list(source), [5, 6, 7])

    def test_zero_stop_does_not_consume_any_value(self):
        source = iter(xrange(3))
        self.assertEqual(list(xslice(source, 0)), [])
        self.assertEqual(list(source), [0, 1, 2])

    def test_slice_releases_source_at_stop(self):
        source = Source([1, 2, 3], [])
        reference = _weakref.ref(source)
        iterator = xslice(source, 1)
        del source
        self.assertEqual(list(iterator), [1])
        self.assertIs(reference(), None)

    def test_slice_source_error_is_terminal(self):
        class Failed(Source):
            def next(self):
                if self.index == 1:
                    self.index += 1
                    raise ValueError('source')
                return Source.next(self)
        iterator = xslice(Failed([0, 1, 2], []), None)
        self.assertEqual(next(iterator), 0)
        self.assertRaises(ValueError, next, iterator)
        self.assertEqual(list(iterator), [])

    def test_slice_uses_int_conversion_including_float_truncation(self):
        class Number(object):
            def __int__(self):
                return 2
        class IndexOnly(object):
            def __index__(self):
                return 2
        self.assertEqual(list(xslice(xrange(8), 1.9, 7.8, Number())), [1, 3, 5])
        self.assertRaises(ValueError, xslice, xrange(8), IndexOnly())

    def test_slice_conversion_order_precedes_iterator_acquisition(self):
        events = []
        class Number(object):
            def __init__(self, name, value):
                self.name, self.value = name, value
            def __int__(self):
                events.append(self.name)
                return self.value
        iterator = xslice(Source([0, 1, 2], events), Number('start', 1),
                          Number('stop', 3), Number('step', 1))
        self.assertEqual(events, ['start', 'stop', 'step', 'iter'])
        self.assertEqual(list(iterator), [1, 2])

    def test_slice_reads_integer_subtype_storage_without_int_override(self):
        class Integer(int):
            def __int__(self):
                raise AssertionError('int storage must be read directly')
        class Wide(long):
            def __int__(self):
                raise AssertionError('long storage must be read directly')
        for value in (Integer(1), Wide(1)):
            self.assertEqual(list(xslice(xrange(4), value)), [0])
            self.assertEqual(list(xslice(xrange(4), value, None)), [1, 2, 3])
            self.assertEqual(list(xslice(xrange(4), 0, None, value)), [0, 1, 2, 3])
        self.assertRaises(ValueError, xslice, xrange(4), Wide(2 ** 100))

    def test_slice_rejects_invalid_bounds_and_steps(self):
        for bounds in ((-1,), (-2,), (2 ** 100,), ('2',), (0, -1),
                       (-1, 4), (0, None, 0), (0, None, -1),
                       (0, None, 2 ** 100), (float('nan'),)):
            self.assertRaises(ValueError, xslice, *([1, 2],) + bounds)

    def test_slice_large_step_matches_python27_overflow_rule(self):
        self.assertEqual(list(xslice(xrange(5), 1, None, 2 ** 63 - 1)), [1, 2])

    def test_streams_reject_arity_and_keywords(self):
        self.assertRaises(TypeError, xchain, [], extra=1)
        self.assertRaises(TypeError, xchain.from_iterable)
        self.assertRaises(TypeError, xchain.from_iterable, [], [])
        self.assertRaises(TypeError, xchain.from_iterable, [], extra=1)
        for args in ((), ([],), ([], 1, 2, 3, 4)):
            self.assertRaises(TypeError, xslice, *args)
        self.assertRaises(TypeError, xslice, [], 1, extra=1)

    def test_streams_expose_single_pass_methods(self):
        for iterator in (xchain([1]), xslice([1], None)):
            self.assertIs(iter(iterator), iterator)
            self.assertIs(iterator.__iter__(), iterator)
            self.assertFalse(hasattr(iterator, '__length_hint__'))
            self.assertRaises(TypeError, len, iterator)
            self.assertEqual(iterator.next(), 1)
            self.assertRaises(StopIteration, iterator.next)
            self.assertEqual(list(iterator), [])

    def test_stream_subclasses_and_from_iterable_preserve_type(self):
        class Chained(xchain):
            def next(self):
                return xchain.next(self) * 2
        class Sliced(xslice):
            def next(self):
                return super(Sliced, self).next() * 2
        iterator = Chained.from_iterable([[1], [2]])
        self.assertIs(type(iterator), Chained)
        self.assertEqual(list(iterator), [2, 4])
        self.assertEqual(list(Sliced([1, 2, 3], 2)), [2, 4])

    def test_streaming_pipeline_does_not_materialize_a_large_range(self):
        values = xfilter(lambda x: x % 2, xmap(abs, xchain(xrange(1000000000), [1])))
        self.assertEqual(list(xslice(values, 3)), [1, 3, 5])

    def test_reentrant_stream_keeps_active_source_alive(self):
        global _active_stream
        events = []
        class Reentrant(Source):
            def next(self):
                value = Source.next(self)
                if value == 0:
                    events.append(('inner', list(_active_stream)))
                return value
        for factory, suffix in ((xchain, ()), (xslice, (None,))):
            source = Reentrant([0, 1, 2], [])
            _active_stream = factory(*((source,) + suffix))
            try:
                self.assertEqual(next(_active_stream), 0)
                self.assertEqual(list(_active_stream), [])
                self.assertEqual(events[-1], ('inner', [1, 2]))
            finally:
                _active_stream = None
