"""Project-authored descriptor transitions, mutable binding and state recovery."""

import sys
import unittest


class ObjectStateAudit(unittest.TestCase):
    def test_set_only_descriptor_reports_missing_delete(self):
        events = []
        class Descriptor(object):
            def __set__(self, instance, value):
                events.append(value)
        class Owner(object):
            member = Descriptor()
        value = Owner()
        value.__dict__['member'] = 17
        with self.assertRaises(AttributeError) as error:
            del value.member
        self.assertEqual(error.exception.args, ('__delete__',))
        self.assertEqual(value.member, 17)
        value.member = 23
        self.assertEqual(events, [23])
        self.assertEqual(value.member, 17)
        sys.exc_clear()

    def _ordered_sets(self, left_kind, right_kind, size, changed, equality):
        events, state = [], {'armed': False}
        class Key(object):
            def __init__(self, name):
                self.name = name
            def __hash__(self):
                return 1
            def __eq__(self, other):
                events.append((self.name, other.name, sys.exc_info()[0]))
                if state['armed']:
                    state['armed'] = False
                    if changed != 'none':
                        state[changed].add(2)
                if equality == 'error':
                    raise LookupError('comparison')
                return equality == 'equal'
        class Subset(set):
            pass
        factories = {'set': set, 'subtype': Subset, 'frozen': frozenset}
        originals = Key('left'), Key('right')
        left_items, right_items = [originals[0]], [originals[1]]
        if size == 'left':
            left_items.append(2)
        elif size == 'right':
            right_items.append(2)
        left, right = factories[left_kind](left_items), factories[right_kind](right_items)
        state.update(left=left, right=right, armed=True)
        return left, right, originals, events, state

    def test_set_order_size_guards_skip_comparison(self):
        checks = (('lt', 'none'), ('lt', 'left'), ('le', 'left'),
                  ('gt', 'none'), ('gt', 'right'), ('ge', 'right'))
        for operation, size in checks:
            left, right, originals, events, state = self._ordered_sets('set', 'set', size, 'none', 'error')
            try:
                result = {'lt': lambda: left < right, 'le': lambda: left <= right,
                          'gt': lambda: left > right, 'ge': lambda: left >= right}[operation]()
                self.assertIs(result, False)
                self.assertEqual(events, [])
            finally:
                state.clear()

    def test_set_order_uses_oriented_subset_for_subtype(self):
        for operation in ('lt', 'le', 'gt', 'ge'):
            for left_kind, right_kind in (('set', 'set'), ('set', 'subtype'), ('subtype', 'set'), ('frozen', 'set')):
                size = 'right' if operation == 'lt' else 'left' if operation == 'gt' else 'none'
                left, right, originals, events, state = self._ordered_sets(left_kind, right_kind, size, 'none', 'equal')
                try:
                    result = {'lt': lambda: left < right, 'le': lambda: left <= right,
                              'gt': lambda: left > right, 'ge': lambda: left >= right}[operation]()
                    self.assertIs(result, True)
                    names = ('right', 'left') if operation in ('lt', 'le') else ('left', 'right')
                    self.assertEqual([(a, b) for a, b, handled in events], [names])
                finally:
                    state.clear()

    def test_set_order_observes_growing_source_once(self):
        for operation, changed in (('le', 'left'), ('ge', 'right')):
            left, right, originals, events, state = self._ordered_sets('set', 'set', 'none', changed, 'equal')
            try:
                result = left <= right if operation == 'le' else left >= right
                self.assertIs(result, False)
                names = ('right', 'left') if operation == 'le' else ('left', 'right')
                self.assertEqual([(a, b) for a, b, handled in events], [names])
                self.assertEqual(len(state[changed]), 2)
            finally:
                state.clear()

    def test_set_order_error_preserves_handler_and_recovers(self):
        for operation in ('lt', 'le', 'gt', 'ge'):
            size = 'right' if operation == 'lt' else 'left' if operation == 'gt' else 'none'
            left, right, originals, events, state = self._ordered_sets('set', 'set', size, 'none', 'error')
            try:
                try:
                    raise KeyError('outer')
                except KeyError:
                    with self.assertRaises(LookupError) as error:
                        {'lt': lambda: left < right, 'le': lambda: left <= right,
                         'gt': lambda: left > right, 'ge': lambda: left >= right}[operation]()
                    self.assertEqual(error.exception.args, ('comparison',))
                self.assertEqual([handled for a, b, handled in events], [KeyError])
                self.assertTrue(set([1]) < set([1, 2]))
            finally:
                state.clear()
        sys.exc_clear()

    def test_negative_hint_slice_entrypoints_keep_callback_errors(self):
        for consumer in ('direct', 'legacy', 'direct-step', 'syntax', 'syntax-step', 'sorted'):
            for raised in (False, True):
                target, events = [5, 6, 7], []
                failure = SystemError('error return without exception set')
                class Iterator(object):
                    def __iter__(self):
                        events.append('iter')
                        return self
                    def __length_hint__(self):
                        events.append(('hint', sys.exc_info()[0]))
                        target.append(9)
                        if raised:
                            raise failure
                        return -1
                    def next(self):
                        raise AssertionError('negative hint stops iteration')
                def invoke():
                    source = Iterator()
                    if consumer == 'direct':
                        return target.__setitem__(slice(None), source)
                    if consumer == 'legacy':
                        return target.__setslice__(0, 3, source)
                    if consumer == 'direct-step':
                        return target.__setitem__(slice(None, None, 2), source)
                    if consumer == 'syntax':
                        target[:] = source
                    elif consumer == 'syntax-step':
                        target[::2] = source
                    else:
                        return sorted(source)
                try:
                    raise KeyError('outer')
                except KeyError:
                    if raised or consumer in ('syntax', 'syntax-step', 'sorted'):
                        with self.assertRaises(SystemError) as error:
                            invoke()
                        self.assertEqual(error.exception.args, failure.args)
                        self.assertEqual(error.exception is failure, raised)
                    else:
                        self.assertIs(invoke(), None)
                self.assertEqual(target, [5, 6, 7, 9])
                self.assertEqual(events, ['iter', ('hint', KeyError)] if consumer == 'sorted'
                                 else ['iter', 'iter', ('hint', KeyError)])
                target[:] = [31]
                self.assertEqual(target, [31])
        sys.exc_clear()

    def test_legacy_slice_clamps_after_replacement_callbacks(self):
        expected = {
            (0, 100): ([17, 19], [17, 19, 9], [17, 19]),
            (-2, 100): ([17, 19], [5, 17, 19, 9], [5, 17, 19]),
            (0, -1): ([17, 19, 5, 6, 7, 9], [17, 19, 7, 9], [17, 19, 7, 9]),
            (100, 100): ([5, 6, 7, 9, 17, 19], [5, 6, 7, 17, 19, 9], [5, 6, 7, 9, 17, 19])
        }
        for bounds in ((0, 100), (-2, 100), (0, -1), (100, 100)):
            for index, consumer in enumerate(('legacy', 'mapping', 'syntax')):
                target = [5, 6, 7]
                class Iterator(object):
                    def __init__(self):
                        self.index = 0
                    def __iter__(self):
                        return self
                    def __length_hint__(self):
                        target.append(9)
                        return 0
                    def next(self):
                        if self.index == 2:
                            raise StopIteration
                        value = (17, 19)[self.index]
                        self.index += 1
                        return value
                if consumer == 'legacy':
                    target.__setslice__(bounds[0], bounds[1], Iterator())
                elif consumer == 'mapping':
                    target.__setitem__(slice(*bounds), Iterator())
                else:
                    target[bounds[0]:bounds[1]] = Iterator()
                self.assertEqual(target, expected[bounds][index])
        class Sublist(list):
            def __setitem__(self, key, value):
                raise AssertionError('legacy slices use the sequence slot')
        target = Sublist([5, 6, 7])
        target[:] = [17, 19]
        self.assertEqual(target, [17, 19])

    def test_delete_only_descriptor_reports_missing_set(self):
        events = []
        class Descriptor(object):
            def __delete__(self, instance):
                events.append('delete')
        class Owner(object):
            member = Descriptor()
        value = Owner()
        value.__dict__['member'] = 17
        with self.assertRaises(AttributeError) as error:
            value.member = 23
        self.assertEqual(error.exception.args, ('__set__',))
        del value.member
        self.assertEqual(events, ['delete'])
        self.assertEqual(value.member, 17)
        sys.exc_clear()

    def test_missing_property_accessors_report_exact_errors(self):
        field = property()
        class Owner(object):
            member = field
        value = Owner()
        value.__dict__['member'] = 17
        for callback, message in ((lambda: field.__get__(value, Owner), 'unreadable attribute'),
                                  (lambda: field.__set__(value, 23), "can't set attribute"),
                                  (lambda: field.__delete__(value), "can't delete attribute")):
            with self.assertRaises(AttributeError) as error:
                callback()
            self.assertEqual(error.exception.args, (message,))
        self.assertEqual(value.__dict__['member'], 17)
        sys.exc_clear()

    def test_descriptor_callbacks_pin_removed_descriptor(self):
        for deleting in (False, True):
            for failing in (False, True):
                events = []
                class Descriptor(object):
                    def __set__(self, instance, value):
                        events.append('set')
                        del type(instance).member
                        events.append('removed')
                        if failing:
                            raise ValueError('setter failure')
                    def __delete__(self, instance):
                        events.append('delete')
                        del type(instance).member
                        events.append('removed')
                        if failing:
                            raise ValueError('deleter failure')
                    def __del__(self):
                        events.append('released')
                class Owner(object):
                    member = Descriptor()
                value = Owner()
                def invoke():
                    if deleting:
                        del value.member
                    else:
                        value.member = 17
                if failing:
                    with self.assertRaises(ValueError):
                        invoke()
                    # The caught traceback is released by the adapter/reference
                    # context before this point; escaped locals are out of scope.
                    sys.exc_clear()
                else:
                    invoke()
                self.assertEqual(events, ['delete' if deleting else 'set', 'removed', 'released'])
                value.member = 23
                self.assertEqual(value.member, 23)

    def test_descriptor_status_updates_after_method_mutation(self):
        class Descriptor(object):
            def __get__(self, instance, owner):
                return 19
        class Owner(object):
            member = Descriptor()
        value = Owner()
        value.member = 17
        self.assertEqual(value.member, 17)
        def write(self, instance, item):
            instance.__dict__['written'] = item
        Descriptor.__set__ = write
        self.assertEqual(value.member, 19)
        value.member = 23
        self.assertEqual(value.written, 23)
        self.assertEqual(value.__dict__['member'], 17)
        del Descriptor.__set__
        self.assertEqual(value.member, 17)
        value.member = 29
        self.assertEqual(value.member, 29)

    def test_descriptor_changes_bases_and_invalidates_cached_attributes(self):
        class Root(object):
            pass
        class First(Root):
            marker = 17
        class Second(Root):
            marker = 23
        class Descriptor(object):
            def __set__(self, instance, value):
                type(instance).__bases__ = (Second,)
                raise ValueError('changed')
        class Owner(First):
            member = Descriptor()
        value = Owner()
        self.assertEqual((value.marker, Owner.marker), (17, 17))
        with self.assertRaises(ValueError) as error:
            value.member = 29
        self.assertEqual(error.exception.args, ('changed',))
        self.assertEqual((value.marker, Owner.marker), (23, 23))
        self.assertEqual([item.__name__ for item in Owner.__mro__], ['Owner', 'Second', 'Root', 'object'])
        sys.exc_clear()

    def test_function_metadata_rejects_values_and_deletion_without_changes(self):
        def sample(value=17):
            return value
        for names, message in ((('func_name', '__name__'), '__name__ must be set to a string object'),
                               (('func_code', '__code__'), '__code__ must be set to a code object')):
            for name in names:
                previous = getattr(sample, name)
                for replacement in (None, 23, u'name'):
                    with self.assertRaises(TypeError) as error:
                        setattr(sample, name, replacement)
                    self.assertEqual(error.exception.args, (message,))
                    self.assertIs(getattr(sample, name), previous)
                with self.assertRaises(TypeError) as error:
                    delattr(sample, name)
                self.assertEqual(error.exception.args, (message,))
        self.assertEqual(sample(), 17)
        sys.exc_clear()

    def test_function_defaults_validation_and_reset(self):
        def sample(value=17):
            return value
        for name in ('func_defaults', '__defaults__'):
            with self.assertRaises(TypeError) as error:
                setattr(sample, name, [23])
            self.assertEqual(error.exception.args, ('__defaults__ must be set to a tuple object',))
            self.assertEqual(sample(), 17)
            setattr(sample, name, (29,))
            self.assertEqual(sample(), 29)
            delattr(sample, name)
            self.assertIs(getattr(sample, name), None)
            self.assertEqual(sample(31), 31)
            setattr(sample, name, (17,))
        sys.exc_clear()

    def test_function_dictionary_validation_and_replacement(self):
        def sample():
            return 17
        for name in ('func_dict', '__dict__'):
            previous = getattr(sample, name)
            with self.assertRaises(TypeError) as error:
                setattr(sample, name, None)
            self.assertEqual(error.exception.args, ("setting function's dictionary to a non-dict",))
            with self.assertRaises(TypeError) as error:
                delattr(sample, name)
            self.assertEqual(error.exception.args, ("function's dictionary may not be deleted",))
            self.assertIs(getattr(sample, name), previous)
            namespace = {'marker': 23}
            setattr(sample, name, namespace)
            self.assertIs(sample.__dict__, namespace)
            self.assertEqual(sample.marker, 23)
        sys.exc_clear()

    def test_function_code_freevar_error_uses_name_and_keeps_code(self):
        def plain():
            return 17
        captured = 19
        def nested():
            return captured
        class Name(str):
            def __str__(self):
                raise ValueError('name conversion must be bypassed')
        for target, source, old_count, new_count in ((plain, nested, 0, 1), (nested, plain, 1, 0)):
            for name in ('renamed', Name('prefix\0suffix')):
                target.func_name = name
                previous = target.func_code
                with self.assertRaises(ValueError) as error:
                    target.func_code = source.func_code
                self.assertEqual(error.exception.args, ('%s() requires a code object with %d free vars, not %d' % (name.split('\0')[0], old_count, new_count),))
                self.assertIs(target.func_code, previous)
            self.assertEqual(target(), 17 if old_count == 0 else 19)
        sys.exc_clear()

    def test_keyword_callback_uses_original_defaults_and_code(self):
        def sample(required, fallback=17):
            return 'old', required, fallback
        def replacement(required, fallback=29):
            return 'new', required, fallback
        retained_code = sample.func_code
        retained_defaults = sample.func_defaults
        class Keyword(str):
            def __hash__(self):
                return str.__hash__(self)
            def __eq__(self, other):
                sample.func_defaults = (29,)
                sample.func_code = replacement.func_code
                return str.__eq__(self, other)
        self.assertEqual(sample(**{Keyword('required'): 23}), ('old', 23, 17))
        self.assertEqual(sample(31), ('new', 31, 29))
        self.assertEqual(retained_code.co_name, 'sample')
        self.assertEqual(retained_defaults, (17,))

    def test_generator_binding_snapshots_defaults_and_code(self):
        def sample(required, fallback=17):
            yield 'old', required, fallback
        def replacement(required, fallback=29):
            yield 'new', required, fallback
        retained_code = sample.func_code
        retained_defaults = sample.func_defaults
        class Keyword(str):
            def __hash__(self):
                return str.__hash__(self)
            def __eq__(self, other):
                sample.func_defaults = (29,)
                sample.func_code = replacement.func_code
                return str.__eq__(self, other)
        value = sample(**{Keyword('required'): 23})
        try:
            self.assertEqual(value.next(), ('old', 23, 17))
        finally:
            value.close()
        self.assertEqual(retained_code.co_name, 'sample')
        self.assertEqual(retained_defaults, (17,))
        value = sample(31)
        try:
            self.assertEqual(value.next(), ('new', 31, 29))
        finally:
            value.close()

    def test_classic_dynamic_call_preserves_callbacks_and_errors(self):
        events = []
        class Classic:
            def __getattr__(self, name):
                events.append(name)
                return lambda value=17: value
        value = Classic()
        self.assertEqual(value(), 17)
        self.assertEqual(value(23), 23)
        self.assertEqual(events, ['__call__', '__call__'])
        class Missing:
            def __getattr__(self, name):
                raise AttributeError('missing')
        with self.assertRaises(AttributeError) as error:
            Missing()()
        self.assertEqual(error.exception.args, ('Missing instance has no __call__ method',))
        for failure in (ValueError, KeyboardInterrupt):
            class Failed:
                def __getattr__(self, name):
                    raise failure('lookup failure')
            with self.assertRaises(failure) as error:
                Failed()()
            self.assertEqual(error.exception.args, ('lookup failure',))
        self.assertEqual(value(29), 29)
        sys.exc_clear()

    def test_noncallable_type_error_preserves_handled_exception(self):
        class Missing(object):
            pass
        class NoneCall(object):
            __call__ = None
        for value, name in ((17, 'int'), (None, 'NoneType'), ([], 'list'), ({}, 'dict'),
                            (property(), 'property'), (Missing(), 'Missing'), (NoneCall(), 'NoneType')):
            try:
                raise KeyError('outer')
            except KeyError:
                with self.assertRaises(TypeError) as error:
                    value()
                self.assertEqual(error.exception.args, ("'%s' object is not callable" % name,))
                # CPython2 records the handled inner exception until exc_clear;
                # the next successful operation must have no pending error.
                self.assertEqual((lambda item: item)(23), 23)
        sys.exc_clear()

    def test_classic_call_lookup_preserves_resumer_handler(self):
        events = []
        for mode in ('callable', 'missing', 'none', 'value', 'base'):
            class Classic:
                def __getattr__(self, name):
                    events.append((name, sys.exc_info()[0]))
                    if mode == 'missing':
                        raise AttributeError('absent')
                    if mode == 'none':
                        return None
                    if mode == 'value':
                        raise ValueError('lookup failure')
                    if mode == 'base':
                        raise KeyboardInterrupt('lookup failure')
                    return lambda: sys.exc_info()[0]
            try:
                raise KeyError('outer')
            except KeyError:
                if mode == 'callable':
                    self.assertIs(Classic()(), KeyError)
                    self.assertIs(sys.exc_info()[0], KeyError)
                else:
                    kind = {'missing': AttributeError, 'none': TypeError,
                            'value': ValueError, 'base': KeyboardInterrupt}[mode]
                    with self.assertRaises(kind):
                        Classic()()
            self.assertEqual(events[-1], ('__call__', KeyError))
        self.assertEqual(len(events), 5)
        sys.exc_clear()

    def test_generator_finally_observes_resumer_handler(self):
        for operation in ('send', 'close', 'throw'):
            events = []
            def iterate():
                try:
                    raise ValueError('inner')
                except ValueError:
                    try:
                        yield 17
                    finally:
                        events.append(sys.exc_info()[0])
                yield 19
            value = iterate()
            try:
                raise KeyError('outer')
            except KeyError:
                self.assertEqual(value.next(), 17)
                if operation == 'send':
                    self.assertEqual(value.send(None), 19)
                elif operation == 'close':
                    self.assertIs(value.close(), None)
                else:
                    with self.assertRaises(TypeError) as error:
                        value.throw(TypeError('injected'))
                    self.assertEqual(error.exception.args, ('injected',))
                self.assertEqual(events, [KeyError])
            value.close()
            self.assertIs(value.gi_frame, None)
        sys.exc_clear()
