"""Project-authored Python 2 container callback and direct slice witnesses."""

import unittest


class ContainerCallbackAudit(unittest.TestCase):
    def test_direct_slice_clamps_negative_start(self):
        for value in ([1, 2, 3], (1, 2, 3), 'abc', u'abc'):
            self.assertEqual(value.__getslice__(-1, 3), value)

    def test_direct_slice_clamps_negative_stop(self):
        for value in ([1, 2, 3], (1, 2, 3), 'abc', u'abc'):
            self.assertEqual(value.__getslice__(0, -1), value[0:0])

    def test_direct_slice_accepts_int_protocol_in_order(self):
        events = []
        class Bound(object):
            def __init__(self, name, result):
                self.name, self.result = name, result
            def __int__(self):
                events.append(self.name)
                return self.result
        self.assertEqual([1, 2, 3].__getslice__(Bound('start', -1), Bound('stop', 2)), [1, 2])
        self.assertEqual(events, ['start', 'stop'])

    def test_direct_slice_ignores_int_subtype_override(self):
        class Bound(int):
            def __int__(self):
                raise AssertionError('stored int value is required')
        self.assertEqual([1, 2, 3].__getslice__(Bound(1), 3), [2, 3])

    def test_direct_slice_uses_long_subtype_conversion(self):
        events = []
        class Bound(long):
            def __int__(self):
                events.append('int')
                return 2
        self.assertEqual([1, 2, 3].__getslice__(Bound(1), 3), [3])
        self.assertEqual(events, ['int'])

    def test_direct_slice_rejects_index_only(self):
        class Bound(object):
            def __index__(self):
                raise AssertionError('index protocol is not used')
        with self.assertRaises(TypeError) as caught:
            [1, 2].__getslice__(Bound(), 2)
        self.assertEqual(caught.exception.args, ('an integer is required',))

    def test_direct_slice_rejects_none(self):
        with self.assertRaises(TypeError) as caught:
            [1, 2].__getslice__(None, 2)
        self.assertEqual(caught.exception.args, ('an integer is required',))

    def test_direct_slice_rejects_float(self):
        with self.assertRaises(TypeError) as caught:
            [1, 2].__getslice__(1.5, 2)
        self.assertEqual(caught.exception.args, ('integer argument expected, got float',))

    def test_complex_conversion_errors_name_target(self):
        for name, converter in (('int', int), ('long', long), ('float', float)):
            with self.assertRaises(TypeError) as caught:
                converter(1j)
            self.assertEqual(caught.exception.args, ("can't convert complex to " + name,))
            with self.assertRaises(TypeError) as caught:
                getattr(1j, '__' + name + '__')()
            self.assertEqual(caught.exception.args, ("can't convert complex to " + name,))

    def test_complex_slice_subtype_override_is_used(self):
        events = []
        class Bound(complex):
            def __int__(self):
                events.append('int')
                return 1
        self.assertEqual([1, 2, 3].__getslice__(Bound(1j), 3), [2, 3])
        self.assertEqual(events, ['int'])

    def test_insert_conversion_result_and_overflow_diagnostics(self):
        class Bad(object):
            def __int__(self):
                return 1.5
        class Big(object):
            def __int__(self):
                return 10L ** 40
        value = [1, 2]
        with self.assertRaises(TypeError) as caught:
            value.insert(Bad(), 3)
        self.assertEqual(caught.exception.args, ('__int__ method should return an integer',))
        for bound in (10L ** 40, -(10L ** 40), Big()):
            with self.assertRaises(OverflowError) as caught:
                value.insert(bound, 3)
            self.assertEqual(caught.exception.args, ('Python int too large to convert to C long',))
        self.assertEqual(value, [1, 2])
        value.insert(1, 3)
        self.assertEqual(value, [1, 3, 2])

    def test_list_pop_conversion_and_bounds_diagnostics(self):
        value = [1, 2]
        with self.assertRaises(TypeError) as caught:
            value.pop(1.5)
        self.assertEqual(caught.exception.args, ('integer argument expected, got float',))
        with self.assertRaises(IndexError) as caught:
            value.pop(3)
        self.assertEqual(caught.exception.args, ('pop index out of range',))
        self.assertEqual(value, [1, 2])
        self.assertEqual(value.pop(1), 2)

    def test_direct_slice_rejects_noninteger_int_result(self):
        class Bound(object):
            def __int__(self):
                return '1'
        with self.assertRaises(TypeError) as caught:
            [1, 2].__getslice__(Bound(), 2)
        self.assertEqual(caught.exception.args, ('__int__ method should return an integer',))

    def test_direct_slice_overflow_precedes_second_conversion(self):
        class Bound(object):
            def __int__(self):
                raise AssertionError('first argument must fail')
        for start in (10L ** 40, -(10L ** 40)):
            with self.assertRaises(OverflowError) as caught:
                [1, 2].__getslice__(start, Bound())
            self.assertEqual(caught.exception.args, ('Python int too large to convert to C long',))

    def test_direct_slice_conversion_error_preserves_list(self):
        class Bound(object):
            def __int__(self):
                raise LookupError('bound')
        value = [1, 2, 3]
        with self.assertRaises(LookupError) as caught:
            value.__setslice__(0, Bound(), [8])
        self.assertEqual(caught.exception.args, ('bound',))
        self.assertEqual(value, [1, 2, 3])
        value.__setslice__(0, 1, [8])
        self.assertEqual(value, [8, 2, 3])

    def test_direct_setslice_clamps_negative_bounds(self):
        value = [1, 2, 3]
        value.__setslice__(-1, 3, [8])
        self.assertEqual(value, [8])
        value.__setslice__(0, -1, [9])
        self.assertEqual(value, [9, 8])

    def test_direct_delslice_clamps_negative_bounds(self):
        value = [1, 2, 3]
        value.__delslice__(0, -1)
        self.assertEqual(value, [1, 2, 3])
        value.__delslice__(-1, 3)
        self.assertEqual(value, [])

    def test_syntax_slice_uses_index_protocol(self):
        class Bound(object):
            def __int__(self):
                raise AssertionError('syntax must use index')
            def __index__(self):
                return -1
        self.assertEqual([1, 2, 3][Bound():], [3])

    def test_dictproxy_copy_reuses_stored_key_hash(self):
        events = []
        class Name(str):
            failed = False
            def __hash__(self):
                events.append('hash')
                if self.failed:
                    raise LookupError('hash')
                return str.__hash__(self)
        name = Name('member')
        owner = type('Owner', (object,), {name: 7})
        name.failed = True
        events[:] = []
        copied = owner.__dict__.copy()
        self.assertEqual(events, [])
        self.assertTrue(any(key is name for key in copied))
        self.assertEqual(copied['member'], 7)

    def test_dict_equality_allows_value_replacement(self):
        events = []
        left = {}
        class Value(object):
            def __eq__(self, other):
                events.append('eq')
                left[1] = 17
                return True
        original = Value()
        other = object()
        left[1] = original
        self.assertTrue(left == {1: other})
        self.assertEqual(events, ['eq'])
        self.assertEqual(left[1], 17)

    def test_dict_equality_releases_retained_key_before_value(self):
        events = []
        left = {}
        class Key(object):
            def __init__(self, name):
                self.name = name
            def __hash__(self):
                return 1
            def __eq__(self, other):
                return True
            def __del__(self):
                if self.name == 'left':
                    events.append('key')
        class Value(object):
            def __eq__(self, other):
                left.clear()
                return True
            def __del__(self):
                events.append('value')
        left[Key('left')] = Value()
        right = {Key('right'): 2}
        self.assertTrue(left == right)
        self.assertEqual(events, ['key', 'value'])

    def test_dict_equality_rehashes_keys(self):
        events = []
        class Key(object):
            def __hash__(self):
                events.append('hash')
                return 1
        key = Key()
        left, right = {key: 2}, {key: 2}
        events[:] = []
        self.assertTrue(left == right)
        self.assertEqual(events, ['hash'])

    def test_dict_equality_suppresses_lookup_hash_error_and_recovers(self):
        events = []
        class Key(object):
            failed = False
            def __hash__(self):
                events.append('hash')
                if self.failed:
                    raise LookupError('hash')
                return 1
        key = Key()
        left, right = {key: 2}, {key: 2}
        key.failed = True
        events[:] = []
        self.assertFalse(left == right)
        self.assertEqual(events, ['hash'])
        key.failed = False
        self.assertTrue(left == right)

    def test_dict_equality_propagates_value_error_and_recovers(self):
        class Value(object):
            failed = True
            def __eq__(self, other):
                if self.failed:
                    raise LookupError('value')
                return True
        value = Value()
        left, right = {1: value}, {1: object()}
        with self.assertRaises(LookupError) as caught:
            left == right
        self.assertEqual(caught.exception.args, ('value',))
        value.failed = False
        self.assertTrue(left == right)

    def test_item_view_suppresses_hash_failure_and_recovers(self):
        class Key(object):
            def __hash__(self):
                raise LookupError('hash')
        value = {1: 2}
        self.assertFalse((Key(), 2) in value.viewitems())
        self.assertFalse(([], 2) in value.viewitems())
        self.assertTrue((1, 2) in value.viewitems())
        with self.assertRaises(LookupError) as caught:
            Key() in value.viewkeys()
        self.assertEqual(caught.exception.args, ('hash',))

    def test_item_view_suppresses_key_comparison_failure(self):
        class Key(object):
            failed = False
            def __hash__(self):
                return 1
            def __eq__(self, other):
                if self.failed:
                    raise LookupError('key')
                return True
        key, query = Key(), Key()
        value = {key: 2}
        key.failed = True
        self.assertFalse((query, 2) in value.viewitems())
        key.failed = False
        self.assertTrue((query, 2) in value.viewitems())

    def test_key_view_comparison_allows_value_replacement(self):
        events = []
        left = {}
        class Key(object):
            def __init__(self, name):
                self.name = name
            def __hash__(self):
                return 1
            def __eq__(self, other):
                events.append((self.name, other.name))
                left[left_key] = 2
                return True
        left_key, right_key = Key('left'), Key('right')
        left[left_key] = 1
        right = {right_key: 9}
        self.assertTrue(left.viewkeys() == right.viewkeys())
        self.assertEqual(events, [('right', 'left')])
        self.assertEqual(left.values(), [2])
        self.assertTrue(left.viewkeys() <= right.viewkeys())
        self.assertTrue(right.viewkeys() >= left.viewkeys())

    def test_intersection_streams_hash_and_equality(self):
        events = []
        class Key(object):
            def __init__(self, name):
                self.name = name
            def __hash__(self):
                events.append(('hash', self.name))
                return 1
            def __eq__(self, other):
                events.append(('eq', self.name, other.name))
                return True
        left, one, two = Key('left'), Key('one'), Key('two')
        value = set([left])
        events[:] = []
        result = value.intersection([one, two])
        self.assertEqual(events, [('hash', 'one'), ('eq', 'left', 'one'),
                                  ('hash', 'two'), ('eq', 'left', 'two'),
                                  ('eq', 'one', 'two')])
        self.assertIs(next(iter(result)), one)
        self.assertIs(next(iter(value)), left)

    def test_difference_streams_hash_and_equality(self):
        events = []
        class Key(object):
            def __init__(self, name):
                self.name = name
            def __hash__(self):
                events.append(('hash', self.name))
                return 1
            def __eq__(self, other):
                events.append(('eq', self.name, other.name))
                return True
        left, one, two = Key('left'), Key('one'), Key('two')
        value = set([left])
        events[:] = []
        self.assertEqual(value.difference([one, two]), set())
        self.assertEqual(events, [('hash', 'one'), ('eq', 'left', 'one'), ('hash', 'two')])
        self.assertIs(next(iter(value)), left)

    def test_difference_set_probes_other_in_original_order(self):
        events = []
        class Key(object):
            def __init__(self, name):
                self.name = name
            def __hash__(self):
                return 1
            def __eq__(self, other):
                events.append((self.name, other.name))
                return True
        left, right = Key('left'), Key('right')
        value, other = set([left]), set([right])
        self.assertEqual(value.difference(other), set())
        self.assertEqual(events, [('right', 'left')])

    def test_difference_exact_dictionary_reuses_cached_hash(self):
        events = []
        class Key(object):
            failed = False
            def __hash__(self):
                events.append('hash')
                if self.failed:
                    raise LookupError('hash')
                return 1
        key = Key()
        value, other = set([key]), {key: 7}
        key.failed = True
        events[:] = []
        self.assertEqual(value.difference(other), set())
        self.assertEqual(events, [])

    def test_intersection_drains_iterator_after_match(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                yield 1
                events.append('tail')
                raise LookupError('tail')
        value = set([1])
        with self.assertRaises(LookupError) as caught:
            value.intersection(Source())
        self.assertEqual(caught.exception.args, ('tail',))
        self.assertEqual(events, ['iter', 'tail'])
        self.assertEqual(value.intersection([1]), set([1]))

    def test_intersection_update_error_preserves_original(self):
        class Source(object):
            def __iter__(self):
                yield 1
                raise LookupError('tail')
        value = set([1, 2])
        with self.assertRaises(LookupError) as caught:
            value.intersection_update(Source())
        self.assertEqual(caught.exception.args, ('tail',))
        self.assertEqual(value, set([1, 2]))

    def test_difference_error_preserves_original(self):
        class Source(object):
            def __iter__(self):
                yield 1
                raise LookupError('tail')
        value = set([1, 2])
        with self.assertRaises(LookupError) as caught:
            value.difference(Source())
        self.assertEqual(caught.exception.args, ('tail',))
        self.assertEqual(value, set([1, 2]))

    def test_intersection_observes_source_mutation_between_items(self):
        value = set([1])
        class Source(object):
            def __iter__(self):
                value.add(2)
                yield 2
        self.assertEqual(value.intersection(Source()), set([2]))
        self.assertEqual(value, set([1, 2]))

    def test_frozenset_intersection_preserves_incoming_representative(self):
        class Key(object):
            def __hash__(self):
                return 1
            def __eq__(self, other):
                return True
        original, incoming = Key(), Key()
        result = frozenset([original]).intersection([incoming])
        self.assertIs(type(result), frozenset)
        self.assertIs(next(iter(result)), incoming)

    def test_set_results_preserve_python_two_subtype(self):
        class Mutable(set):
            pass
        class Frozen(frozenset):
            pass
        for kind in (Mutable, Frozen):
            value = kind([1, 2])
            for source in ([1], set([1]), {1: 9}):
                intersection = value.intersection(source)
                difference = value.difference(source)
                self.assertIs(type(intersection), kind)
                self.assertIs(type(difference), kind)
                self.assertEqual(intersection, set([1]))
                self.assertEqual(difference, set([2]))
