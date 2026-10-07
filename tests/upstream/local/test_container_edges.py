"""Project-authored Python 2 buffer, slice and iterator boundary witnesses."""

import unittest


class ContainerEdges(unittest.TestCase):
    def test_buffer_constructor_uses_integer_conversion(self):
        class Integer(object):
            def __int__(self):
                return 1
        class Long(long):
            def __int__(self):
                return 1
        class Index(object):
            def __index__(self):
                return 1
        for value in (Integer(), Long(2)):
            self.assertEqual(str(buffer('abcd', value)), 'bcd')
            self.assertEqual(str(buffer('abcd', 0, value)), 'a')
        for value in (Index(), 1.0, '1'):
            self.assertRaises(TypeError, buffer, 'abcd', value)
            self.assertRaises(TypeError, buffer, 'abcd', 0, value)

    def test_buffer_constructor_conversion_precedes_owner_validation(self):
        events = []
        class Failed(object):
            def __int__(self):
                events.append('int')
                raise LookupError('conversion failed')
        self.assertRaises(LookupError, buffer, object(), Failed())
        self.assertEqual(events, ['int'])

    def test_buffer_constructor_reports_integer_overflow(self):
        for value in (1L << 100, -(1L << 100)):
            self.assertRaises(OverflowError, buffer, 'abcd', value)
            self.assertRaises(OverflowError, buffer, 'abcd', 0, value)

    def test_buffer_item_index_callback_uses_current_owner(self):
        target = bytearray('abcd')
        class Index(object):
            def __index__(self):
                target[:] = 'xyz'
                return -1
        self.assertEqual(buffer(target)[Index()], 'z')
        self.assertEqual(str(target), 'xyz')

    def test_buffer_index_callback_errors_are_preserved(self):
        class Failed(object):
            def __index__(self):
                raise LookupError('index failed')
        class Invalid(object):
            def __index__(self):
                return '1'
        value = buffer('abcd')
        for key in (Failed(), slice(Failed(), None)):
            self.assertRaises(LookupError, value.__getitem__, key)
        for key in (Invalid(), slice(Invalid(), None)):
            self.assertRaises(TypeError, value.__getitem__, key)

    def test_buffer_huge_item_and_slice_indices(self):
        value = buffer('abcd')
        for bound in (1L << 100, -(1L << 100)):
            self.assertRaises(IndexError, value.__getitem__, bound)
            for step in (1, -1, 1L << 100, -(1L << 100)):
                for key in (slice(bound, None, step), slice(None, bound, step)):
                    self.assertEqual(value[key], 'abcd'[key])

    def test_buffer_slice_index_order_and_mutation(self):
        events = []
        target = bytearray('abcd')
        class Index(object):
            def __init__(self, name, value):
                self.name, self.value = name, value
            def __index__(self):
                events.append(self.name)
                target[:] = 'xyz'
                return self.value
        key = slice(Index('start', 1), Index('stop', 3), Index('step', 1))
        self.assertEqual(buffer(target)[key], 'yz')
        self.assertEqual(events, ['step', 'start', 'stop'])

    def test_empty_buffer_concatenation_returns_right_operand(self):
        for value in ('abcd', u'abcd', bytearray('abcd'), buffer('abcd')):
            self.assertIs(buffer('') + value, value)
            self.assertIs(buffer('abcd', 10) + value, value)

    def test_set_iterator_size_error_remains_sticky(self):
        target = set([1, 2])
        iterator = iter(target)
        target.add(3)
        self.assertRaises(RuntimeError, next, iterator)
        target.remove(3)
        self.assertRaises(RuntimeError, next, iterator)
        self.assertEqual(iterator.__length_hint__(), 0)

    def test_dict_and_set_iterator_length_hints_track_validity(self):
        for target, add, remove in (({1: 2, 3: 4}, lambda value: value.update({5: 6}), lambda value: value.pop(5)),
                                    (set([1, 3]), lambda value: value.add(5), lambda value: value.remove(5))):
            factories = (iter,) if isinstance(target, set) else (iter, dict.itervalues, dict.iteritems)
            for factory in factories:
                iterator = factory(target)
                self.assertEqual(iterator.__length_hint__(), 2)
                add(target)
                self.assertEqual(iterator.__length_hint__(), 0)
                remove(target)
                self.assertEqual(iterator.__length_hint__(), 2)
                next(iterator)
                self.assertEqual(iterator.__length_hint__(), 1)

    def test_list_slice_converts_indices_before_replacement_iteration(self):
        for step in (1, 2):
            events = []
            class Index(object):
                def __index__(self):
                    events.append('index')
                    return 1
            class Source(object):
                def __iter__(self):
                    events.append('iter')
                    return iter([9])
            target = [1, 2, 3, 4]
            target[slice(Index(), 2, step)] = Source()
            self.assertEqual(events, ['index', 'iter'])
            self.assertEqual(target, [1, 9, 3, 4])

    def test_list_invalid_slice_never_iterates_replacement(self):
        events = []
        class Failed(object):
            def __index__(self):
                raise LookupError('index failed')
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([9])
        target = [1, 2, 3]
        self.assertRaises(LookupError, target.__setitem__, slice(Failed(), 2), Source())
        self.assertEqual(events, [])
        self.assertEqual(target, [1, 2, 3])

    def test_list_slice_retains_bounds_during_replacement_growth(self):
        for key, replacement, expected in ((slice(-1, None), [9], [1, 2, 3, 9, 8]),
                                            (slice(None, None, 2), [9, 10], [9, 2, 10, 4, 8]),
                                            (slice(None, None, -1), [5, 6, 7, 9], [9, 7, 6, 5, 8])):
            target = [1, 2, 3, 4]
            class Source(object):
                def __iter__(self):
                    target.append(8)
                    return iter(replacement)
            target[key] = Source()
            self.assertEqual(target, expected)

    def test_list_slice_materialization_uses_length_hint(self):
        events = []
        class Iterator(object):
            def __init__(self):
                self.done = False
            def __iter__(self):
                events.append('iterator')
                return self
            def __length_hint__(self):
                events.append('hint')
                return 1
            def next(self):
                if self.done:
                    raise StopIteration
                self.done = True
                return 9
        class Source(object):
            def __iter__(self):
                events.append('source')
                return Iterator()
            def __len__(self):
                raise AssertionError('source length must not run')
        target = [1, 2, 3]
        target[1:2] = Source()
        self.assertEqual(target, [1, 9, 3])
        self.assertEqual(events, ['source', 'iterator', 'hint'])

    def test_list_self_slice_ignores_subclass_iterator(self):
        class Stored(list):
            def __iter__(self):
                raise AssertionError('self iterator must not run')
        for key, expected in ((slice(1, 2), [1, 1, 2, 3, 3]), (slice(None, None, -1), [3, 2, 1])):
            target = Stored([1, 2, 3])
            target[key] = target
            self.assertEqual(target, expected)

    def test_bytearray_slice_repeats_conversion_after_copying_source(self):
        for source, expected_calls in (([9], ['index', 'index']), (bytearray([9]), ['index'])):
            events = []
            class Index(object):
                def __index__(self):
                    events.append('index')
                    return 1
            target = bytearray([1, 2, 3])
            target[slice(Index(), 2)] = source
            self.assertEqual(list(target), [1, 9, 3])
            self.assertEqual(events, expected_calls)

    def test_bytearray_slice_second_conversion_can_fail(self):
        events = []
        class Index(object):
            def __index__(self):
                events.append('index')
                if len(events) == 2:
                    raise LookupError('second index failed')
                return 1
        target = bytearray([1, 2, 3])
        self.assertRaises(LookupError, target.__setitem__, slice(Index(), 2), [9])
        self.assertEqual(list(target), [1, 2, 3])
        self.assertEqual(events, ['index', 'index'])

    def test_bytearray_item_converts_index_before_element(self):
        events = []
        class Index(object):
            def __index__(self):
                events.append('index')
                return 1
        class Element(object):
            def __index__(self):
                events.append('element')
                return 65
        target = bytearray('abcd')
        target[Index()] = Element()
        self.assertEqual(str(target), 'aAcd')
        self.assertEqual(events, ['index', 'element'])

    def test_bytearray_slice_rejects_numeric_iterable_sources(self):
        class Integer(object):
            def __int__(self):
                return 1
            def __iter__(self):
                raise AssertionError('numeric source must be rejected')
        class Floating(object):
            def __float__(self):
                return 1.0
            def __iter__(self):
                raise AssertionError('numeric source must be rejected')
        target = bytearray('abcd')
        for source in (Integer(), Floating()):
            self.assertRaises(TypeError, target.__setitem__, slice(None), source)
            self.assertEqual(str(target), 'abcd')

    def test_bytearray_unicode_constructor_ignores_encode_override(self):
        class Unicode(unicode):
            def encode(self, *args, **kwargs):
                raise AssertionError('Unicode encode override must not run')
        source = Unicode(u'a\u00e9')
        self.assertEqual(str(bytearray(source, 'utf-8')), 'a\xc3\xa9')
        target = bytearray('old')
        target.__init__(source, 'latin-1')
        self.assertEqual(str(target), 'a\xe9')

    def test_dict_value_membership_returns_match_before_size_error(self):
        target = {}
        class Stored(object):
            def __eq__(self, other):
                target.clear()
                return True
        target[1] = Stored()
        self.assertTrue('needle' in target.viewvalues())
        self.assertEqual(target, {})

    def test_dict_value_membership_allows_same_size_replacement(self):
        for matches in (False, True):
            target = {}
            class Stored(object):
                def __eq__(self, other):
                    target[1] = 'replacement'
                    return matches
            target[1] = Stored()
            try:
                self.assertEqual('needle' in target.viewvalues(), matches)
                self.assertEqual(target, {1: 'replacement'})
            finally:
                target.clear()

    def test_dict_value_membership_checks_size_before_next_value(self):
        target = {}
        class Stored(object):
            def __eq__(self, other):
                target.clear()
                return False
        target[1] = Stored()
        self.assertRaises(RuntimeError, lambda: 'needle' in target.viewvalues())
        self.assertEqual(target, {})

    def test_memoryview_huge_indices_raise_index_error(self):
        view = memoryview(bytearray('abcd'))
        for key in (1L << 100, -(1L << 100)):
            self.assertRaises(IndexError, view.__getitem__, key)
            self.assertRaises(IndexError, view.__setitem__, key, 'z')
        self.assertEqual(view.tobytes(), 'abcd')

    def test_memoryview_assignment_converts_index_before_replacement(self):
        for key_is_slice in (False, True):
            events = []
            class Index(object):
                def __index__(self):
                    events.append('index')
                    return 1
            key = slice(Index(), 2) if key_is_slice else Index()
            view = memoryview(bytearray('abcd'))
            self.assertRaises(TypeError, view.__setitem__, key, 1)
            self.assertEqual(events, ['index'])
            self.assertEqual(view.tobytes(), 'abcd')

    def test_memoryview_slice_errors_precede_replacement_errors(self):
        view = memoryview(bytearray('abcd'))
        self.assertRaises(ValueError, view.__setitem__, slice(None, None, 0), 1)
        self.assertRaises(NotImplementedError, view.__setitem__, slice(None, None, 2), 1)
        self.assertEqual(view.tobytes(), 'abcd')

    def test_memoryview_readonly_error_precedes_index_callback(self):
        class Index(object):
            def __index__(self):
                raise AssertionError('readonly index must not run')
        view = memoryview('abcd')
        for key in (Index(), slice(Index(), 2)):
            self.assertRaises(TypeError, view.__setitem__, key, 'z')

    def test_memoryview_index_callback_exceptions_are_preserved(self):
        class Failed(object):
            def __index__(self):
                raise OverflowError('index callback failed')
        view = memoryview(bytearray('abcd'))
        self.assertRaises(OverflowError, view.__getitem__, Failed())
        self.assertRaises(OverflowError, view.__setitem__, Failed(), 'z')

    def test_memoryview_overlapping_assignment_and_child_exports(self):
        target = bytearray('abcd')
        parent = memoryview(target)
        child = memoryview(parent)[1:]
        parent[1:] = parent[:-1]
        self.assertEqual(str(target), 'aabc')
        self.assertEqual(child.tobytes(), 'abc')
        del parent
        self.assertRaises(BufferError, target.append, 122)
        del child
        target.append(122)
        self.assertEqual(str(target), 'aabcz')

    def test_bytearray_slice_index_only_source_uses_constructor_count(self):
        class Index(object):
            def __index__(self):
                return 2
            def __iter__(self):
                raise AssertionError('count source must not be iterated')
        target = bytearray('abcd')
        target[1:3] = Index()
        self.assertEqual(str(target), 'a\0\0d')
