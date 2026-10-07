"""Project-authored native container argument and error-phase regressions."""

import unittest


def _error(operation, *args, **kwargs):
    try:
        operation(*args, **kwargs)
    except BaseException as error:
        return type(error).__name__, error.args
    raise AssertionError('operation must fail')


class ContainerArgumentsAudit(unittest.TestCase):
    def test_list_single_argument_methods(self):
        for name in ('append', 'extend', 'remove', 'count', '__getitem__'):
            method = getattr([1], name)
            self.assertEqual(_error(method), ('TypeError', ('%s() takes exactly one argument (0 given)' % name,)))
            self.assertEqual(_error(method, 1, 2), ('TypeError', ('%s() takes exactly one argument (2 given)' % name,)))
            self.assertEqual(_error(method, other=1), ('TypeError', ('%s() takes no keyword arguments' % name,)))

    def test_tuple_single_argument_methods(self):
        for name in ('count', 'index'):
            expected = 'count() takes exactly one argument (0 given)' if name == 'count' else 'index() takes at least 1 argument (0 given)'
            self.assertEqual(_error(getattr((1,), name)), ('TypeError', (expected,)))

    def test_list_insert_and_optional_pop_argument_counts(self):
        self.assertEqual(_error([1].insert), ('TypeError', ('insert() takes exactly 2 arguments (0 given)',)))
        self.assertEqual(_error([1].pop, 0, 0), ('TypeError', ('pop() takes at most 1 argument (2 given)',)))

    def test_sequence_index_upper_argument_bound(self):
        for value in ([1], (1,)):
            self.assertEqual(_error(value.index, 1, 0, 1, 2), ('TypeError', ('index() takes at most 3 arguments (4 given)',)))

    def test_wrapper_unary_and_binary_counts(self):
        for value in ([1], (1,), {1: 2}, set([1]), frozenset([1])):
            self.assertEqual(_error(value.__len__, 0), ('TypeError', ('expected 0 arguments, got 1',)))
            self.assertEqual(_error(value.__eq__), ('TypeError', ('expected 1 arguments, got 0',)))
            self.assertEqual(_error(value.__eq__, 0, 0), ('TypeError', ('expected 1 arguments, got 2',)))

    def test_bound_wrapper_keywords_keep_descriptor_name(self):
        for value in ([1], (1,), {1: 2}, set([1])):
            self.assertEqual(_error(value.__len__, other=0), ('TypeError', ("wrapper __len__ doesn't take keyword arguments",)))

    def test_wrapper_unpack_counts_preserve_anonymous_name(self):
        for value in ([1], (1,)):
            self.assertEqual(_error(value.__mul__), ('TypeError', (' expected 1 arguments, got 0',)))
        self.assertEqual(_error([1].__setitem__, 0), ('TypeError', (' expected 2 arguments, got 1',)))

    def test_legacy_slice_counts_use_anonymous_parser(self):
        for value in ([1], (1,)):
            self.assertEqual(_error(value.__getslice__, 0), ('TypeError', ('function takes exactly 2 arguments (1 given)',)))
        self.assertEqual(_error([1].__setslice__, 0, 0), ('TypeError', ('function takes exactly 3 arguments (2 given)',)))
        self.assertEqual(_error([1].__delslice__), ('TypeError', ('function takes exactly 2 arguments (0 given)',)))

    def test_direct_sequence_concat_rejects_incompatible_operand(self):
        for value, name in (([1], 'list'), ((1,), 'tuple')):
            self.assertEqual(_error(value.__add__, 0), ('TypeError', ('can only concatenate %s (not "int") to %s' % (name, name),)))

    def test_direct_concat_does_not_invoke_reflected_operand(self):
        events = []
        class Operand(object):
            def __radd__(self, other):
                events.append('reflected')
                return 7
        operand = Operand()
        self.assertEqual(_error([1].__add__, operand), ('TypeError', ('can only concatenate list (not "Operand") to list',)))
        self.assertEqual(events, [])
        self.assertEqual([1] + operand, 7)
        self.assertEqual(events, ['reflected'])

    def test_dict_optional_methods_use_unpack_counts(self):
        for name in ('get', 'pop', 'setdefault'):
            self.assertEqual(_error(getattr({}, name)), ('TypeError', ('%s expected at least 1 arguments, got 0' % name,)))
            self.assertEqual(_error(getattr({}, name), 1, 2, 3), ('TypeError', ('%s expected at most 2 arguments, got 3' % name,)))

    def test_dict_update_count_precedes_input_processing(self):
        self.assertEqual(_error({}.update, object(), object(), member=1), ('TypeError', ('update expected at most 1 arguments, got 2',)))

    def test_dict_no_argument_methods(self):
        for name in ('keys', 'values', 'items', 'iterkeys', 'itervalues', 'iteritems', 'viewkeys', 'viewvalues', 'viewitems', 'clear', 'copy', 'popitem'):
            self.assertEqual(_error(getattr({}, name), 1), ('TypeError', ('%s() takes no arguments (1 given)' % name,)))
            self.assertEqual(_error(getattr({}, name), other=1), ('TypeError', ('%s() takes no keyword arguments' % name,)))

    def test_dict_fromkeys_argument_counts_and_keywords(self):
        for method in (dict.fromkeys, {}.fromkeys):
            self.assertEqual(_error(method), ('TypeError', ('fromkeys expected at least 1 arguments, got 0',)))
            self.assertEqual(_error(method, (), 1, 2), ('TypeError', ('fromkeys expected at most 2 arguments, got 3',)))
            self.assertEqual(_error(method, other=1), ('TypeError', ('fromkeys() takes no keyword arguments',)))

    def test_container_cmp_names_expected_and_actual_types(self):
        for value, name in (({}, 'dict'), (set(), 'set'), (frozenset(), 'frozenset')):
            self.assertEqual(_error(value.__cmp__, 1), ('TypeError', ("%s.__cmp__(x,y) requires y to be a '%s', not a 'int'" % (name, name),)))

    def test_set_single_argument_methods(self):
        for name in ('add', 'discard', 'remove', 'isdisjoint', 'issubset', 'issuperset', 'symmetric_difference', 'symmetric_difference_update', '__contains__'):
            self.assertEqual(_error(getattr(set(), name)), ('TypeError', ('%s() takes exactly one argument (0 given)' % name,)))
            self.assertEqual(_error(getattr(set(), name), other=0), ('TypeError', ('%s() takes no keyword arguments' % name,)))

    def test_set_variadic_methods_reject_keywords_before_iteration(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1])
        for name in ('union', 'intersection', 'difference', 'update', 'intersection_update', 'difference_update'):
            self.assertEqual(_error(getattr(set(), name), Source(), other=0), ('TypeError', ('%s() takes no keyword arguments' % name,)))
        self.assertEqual(events, [])

    def test_set_no_argument_methods(self):
        for value in (set(), frozenset()):
            self.assertEqual(_error(value.copy, 1), ('TypeError', ('copy() takes no arguments (1 given)',)))
        self.assertEqual(_error(set().pop, 1), ('TypeError', ('pop() takes no arguments (1 given)',)))

    def test_view_wrapper_argument_counts(self):
        for view in ({}.viewkeys(), {}.viewvalues(), {}.viewitems()):
            self.assertEqual(_error(view.__iter__, 1), ('TypeError', ('expected 0 arguments, got 1',)))
            self.assertEqual(_error(view.__repr__, other=0), ('TypeError', ("wrapper __repr__ doesn't take keyword arguments",)))

    def test_view_direct_hash_is_distinct_from_builtin_hash(self):
        for view in ({}.viewkeys(), {}.viewitems()):
            self.assertEqual(view.__hash__(), object.__hash__(view))
            self.assertRaises(TypeError, hash, view)

    def test_xrange_method_argument_counts(self):
        value = xrange(1)
        self.assertEqual(_error(value.__getitem__), ('TypeError', ('expected 1 arguments, got 0',)))
        self.assertEqual(_error(value.__reversed__, 1), ('TypeError', ('__reversed__() takes no arguments (1 given)',)))

    def test_iterator_next_and_iter_wrapper_arguments(self):
        for iterator in (iter([1]), iter((1,)), iter({1: 2}), iter(set([1])), reversed((1,))):
            self.assertEqual(_error(iterator.next, 1), ('TypeError', ('expected 0 arguments, got 1',)))
            self.assertEqual(_error(iterator.next, other=1), ('TypeError', ("wrapper next doesn't take keyword arguments",)))
            self.assertEqual(_error(iterator.__iter__, 1), ('TypeError', ('expected 0 arguments, got 1',)))
            self.assertEqual(next(iterator), 1)

    def test_iterator_length_hint_is_an_ordinary_no_argument_method(self):
        for iterator in (iter([1]), iter((1,)), iter({1: 2}), iter(set([1])), reversed((1,))):
            self.assertEqual(_error(iterator.__length_hint__, 1), ('TypeError', ('__length_hint__() takes no arguments (1 given)',)))
            self.assertEqual(_error(iterator.__length_hint__, other=1), ('TypeError', ('__length_hint__() takes no keyword arguments',)))

    def test_getnewargs_counts_preserve_stored_payload(self):
        for value in ('abc', u'abc', (1,)):
            self.assertEqual(_error(value.__getnewargs__, 1), ('TypeError', ('__getnewargs__() takes no arguments (1 given)',)))
            self.assertEqual(_error(value.__getnewargs__, other=1), ('TypeError', ('__getnewargs__() takes no keyword arguments',)))
            self.assertEqual(value.__getnewargs__(), (value,))

    def test_exact_set_constructor_keyword_priority(self):
        for kind in (set, frozenset):
            self.assertEqual(_error(kind, object(), object(), other=1), ('TypeError', ('%s() does not take keyword arguments' % kind.__name__,)))
            self.assertEqual(_error(kind, (), ()), ('TypeError', ('%s expected at most 1 arguments, got 2' % kind.__name__,)))

    def test_xrange_and_reversed_constructor_arguments(self):
        self.assertEqual(_error(xrange), ('TypeError', ('xrange() requires 1-3 int arguments',)))
        self.assertEqual(_error(xrange, other=1), ('TypeError', ('xrange() does not take keyword arguments',)))
        self.assertEqual(_error(xrange, 0, 1, 0), ('ValueError', ('xrange() arg 3 must not be zero',)))
        self.assertEqual(_error(reversed), ('TypeError', ('reversed expected 1 arguments, got 0',)))
        self.assertEqual(_error(reversed, other=1), ('TypeError', ('reversed() does not take keyword arguments',)))

    def test_enumerate_parser_missing_duplicate_and_unknown_keywords(self):
        self.assertEqual(_error(enumerate, other=1), ('TypeError', ("Required argument 'sequence' (pos 1) not found",)))
        self.assertEqual(_error(enumerate, (), sequence=()), ('TypeError', ("Argument given by name ('sequence') and position (1)",)))
        self.assertEqual(_error(enumerate, (), other=1), ('TypeError', ("'other' is an invalid keyword argument for this function",)))
        self.assertEqual(_error(enumerate, (), ()), ('TypeError', ("'tuple' object cannot be interpreted as an index",)))

    def test_set_subtype_new_ignores_arguments_and_keywords(self):
        class Child(set):
            def __init__(self, *args, **kwargs):
                self.marker = kwargs.get('marker')
        value = Child([1], [2], marker=7)
        self.assertEqual(list(value), [])
        self.assertEqual(value.marker, 7)
        self.assertEqual(list(set.__new__(Child, [1], [2], marker=7)), [])

    def test_frozenset_subtype_new_ignores_keywords_and_checks_arity(self):
        class Child(frozenset):
            def __init__(self, *args, **kwargs):
                self.marker = kwargs.get('marker')
        value = Child([1], marker=7)
        self.assertEqual(list(value), [1])
        self.assertEqual(value.marker, 7)
        self.assertEqual(_error(frozenset.__new__, Child, (), ()), ('TypeError', ('Child expected at most 1 arguments, got 2',)))

    def test_list_slice_iteration_error_phase_and_recovery(self):
        marker = TypeError('iterator body')
        class Initial(object):
            def __iter__(self):
                raise TypeError('initial lookup')
        class Body(object):
            def __iter__(self):
                return self
            def next(self):
                raise marker
        value = [1]
        self.assertEqual(_error(value.__setslice__, 0, 0, Initial()), ('TypeError', ('can only assign an iterable',)))
        try:
            value.__setitem__(slice(None), Body())
        except TypeError as error:
            self.assertIs(error, marker)
        else:
            self.fail('iterator body must raise')
        self.assertEqual(value, [1])
        value[:] = [2]
        self.assertEqual(value, [2])

    def test_arity_failure_precedes_integer_conversion_and_mutation(self):
        events = []
        class Index(object):
            def __int__(self):
                events.append('int')
                return 0
        value = [1]
        self.assertEqual(_error(value.pop, Index(), 1), ('TypeError', ('pop() takes at most 1 argument (2 given)',)))
        self.assertEqual(_error(value.insert, Index()), ('TypeError', ('insert() takes exactly 2 arguments (1 given)',)))
        self.assertEqual(events, [])
        self.assertEqual(value, [1])
