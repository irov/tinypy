"""Project-authored native descriptor and bound slot wrapper regressions."""

import sys
import unittest


class WrapperMetadataAudit(unittest.TestCase):
    def test_bound_slots_use_distinct_wrapper_type(self):
        for value in (object().__repr__, (7).__add__, [].__len__, len.__call__):
            self.assertEqual(type(value).__name__, 'method-wrapper')
        self.assertEqual(type([].append).__name__, 'builtin_function_or_method')
        self.assertEqual(type(list.append).__name__, 'method_descriptor')
        self.assertEqual(type(object.__repr__).__name__, 'wrapper_descriptor')

    def test_wrapper_owner_self_and_name(self):
        class Subject(object):
            pass
        subject = Subject()
        wrapper = subject.__repr__
        self.assertIs(wrapper.__self__, subject)
        self.assertIs(wrapper.__objclass__, object)
        self.assertEqual(wrapper.__name__, '__repr__')
        self.assertIs(len.__call__.__objclass__, type(len))

    def test_wrapper_lacks_descriptor_and_module_protocols(self):
        wrapper = (7).__add__
        for field in ('__module__', '__get__', '__eq__', '__ne__', '__lt__', '__le__', '__gt__', '__ge__'):
            self.assertFalse(hasattr(wrapper, field))
        self.assertTrue(hasattr(wrapper, '__cmp__'))

    def test_native_descriptor_comparison_surface(self):
        for descriptor in (object.__repr__, list.append):
            for field in ('__cmp__', '__eq__', '__ne__', '__lt__', '__le__', '__gt__', '__ge__', '__module__'):
                self.assertFalse(hasattr(descriptor, field))
            self.assertTrue(hasattr(descriptor, '__get__'))

    def test_metadata_descriptor_kinds_and_owners(self):
        wrapper_type, builtin_type = type((7).__add__), type(len)
        for owner, field, kind in ((wrapper_type, '__self__', 'member_descriptor'),
                                   (wrapper_type, '__name__', 'getset_descriptor'),
                                   (wrapper_type, '__objclass__', 'getset_descriptor'),
                                   (builtin_type, '__name__', 'getset_descriptor'),
                                   (builtin_type, '__self__', 'getset_descriptor'),
                                   (builtin_type, '__module__', 'member_descriptor'),
                                   (type(list.append), '__name__', 'member_descriptor'),
                                   (type(object.__repr__), '__objclass__', 'member_descriptor')):
            descriptor = owner.__dict__[field]
            self.assertEqual(type(descriptor).__name__, kind)
            self.assertIs(descriptor.__objclass__, owner)

    def test_wrapper_self_is_readonly_member(self):
        wrapper = (7).__add__
        self.assertEqual(wrapper.__name__, '__add__')
        for action in (lambda: setattr(wrapper, '__self__', 9), lambda: delattr(wrapper, '__self__')):
            with self.assertRaises(TypeError) as caught:
                action()
            self.assertEqual(caught.exception.args, ('readonly attribute',))

    def test_wrapper_getsets_report_readonly_field(self):
        wrapper = (7).__add__
        self.assertEqual(wrapper.__name__, '__add__')
        for field in ('__name__', '__objclass__', '__doc__'):
            for action in (lambda: setattr(wrapper, field, 9), lambda: delattr(wrapper, field)):
                with self.assertRaises(AttributeError) as caught:
                    action()
                self.assertEqual(caught.exception.args,
                                 ("attribute '%s' of 'method-wrapper' objects is not writable" % field,))

    def test_builtin_name_and_self_are_readonly_getsets(self):
        bound = [].append
        for field in ('__name__', '__self__', '__doc__'):
            with self.assertRaises(AttributeError) as caught:
                setattr(bound, field, 9)
            self.assertEqual(caught.exception.args,
                             ("attribute '%s' of 'builtin_function_or_method' objects is not writable" % field,))

    def test_builtin_module_can_store_and_delete_arbitrary_objects(self):
        bound, marker = [].append, object()
        self.assertIs(bound.__module__, None)
        bound.__module__ = marker
        self.assertIs(bound.__module__, marker)
        del bound.__module__
        self.assertIs(bound.__module__, None)
        self.assertIs([].append.__module__, None)

    def test_builtin_global_module_restore(self):
        previous = len.__module__
        try:
            len.__module__ = 17
            self.assertEqual(len.__module__, 17)
            del len.__module__
            self.assertIs(len.__module__, None)
        finally:
            len.__module__ = previous

    def test_unknown_native_attributes_report_attribute_error(self):
        for bound, kind in (((7).__add__, 'method-wrapper'), ([].append, 'builtin_function_or_method')):
            self.assertTrue(isinstance(bound.__name__, str))
            with self.assertRaises(AttributeError) as caught:
                setattr(bound, 'missing', 17)
            self.assertEqual(caught.exception.args, ("'%s' object has no attribute 'missing'" % kind,))

    def test_wrapper_repr_has_name_receiver_and_address_shape(self):
        wrapper = (7).__add__
        text = repr(wrapper)
        self.assertTrue(text.startswith("<method-wrapper '__add__' of int object at 0x"))
        self.assertTrue(text.endswith('>'))
        self.assertEqual(wrapper.__repr__(), text)
        self.assertEqual(repr(list.append), "<method 'append' of 'list' objects>")

    def test_native_types_cannot_be_constructed(self):
        for value in ((7).__add__, len, list.append, object.__repr__):
            with self.assertRaises(TypeError) as caught:
                type(value)()
            self.assertEqual(caught.exception.args, ("cannot create '%s' instances" % type(value).__name__,))

    def test_native_wrappers_cannot_be_reduced(self):
        for value in ((7).__add__, len, list.append, object.__repr__):
            for protocol in (0, 2):
                with self.assertRaises(TypeError) as caught:
                    value.__reduce_ex__(protocol)
                self.assertEqual(caught.exception.args, ("can't pickle %s objects" % type(value).__name__,))

    def test_descriptor_get_none_requires_explicit_non_none_owner(self):
        for descriptor in (dict.__repr__, dict.get):
            for arguments in ((None,), (None, None)):
                with self.assertRaises(TypeError) as caught:
                    descriptor.__get__(*arguments)
                self.assertEqual(caught.exception.args, ('__get__(None, None) is invalid',))
            self.assertIs(descriptor.__get__(None, 17), descriptor)

    def test_descriptor_get_ignores_owner_for_physical_receiver(self):
        target = {'a': 17}
        for descriptor in (dict.__repr__, dict.get):
            for owner in (None, 17, list, dict):
                bound = descriptor.__get__(target, owner)
                self.assertIs(bound.__self__, target)
                self.assertEqual(bound.__name__, descriptor.__name__)
        self.assertEqual(dict.get.__get__(target, 17)('a'), 17)

    def test_descriptor_get_checks_arguments_and_keywords(self):
        descriptor = dict.get
        for arguments, message in (((), ' expected at least 1 arguments, got 0'),
                                   (({}, dict, 17), ' expected at most 2 arguments, got 3')):
            with self.assertRaises(TypeError) as caught:
                descriptor.__get__(*arguments)
            self.assertEqual(caught.exception.args, (message,))
        with self.assertRaises(TypeError) as caught:
            descriptor.__get__(instance={})
        self.assertEqual(caught.exception.args, ("wrapper __get__ doesn't take keyword arguments",))

    def test_direct_metadata_descriptor_validates_receiver(self):
        owner = type((7).__add__)
        descriptor = owner.__dict__['__name__']
        with self.assertRaises(TypeError) as caught:
            descriptor.__get__(17)
        self.assertEqual(caught.exception.args,
                         ("descriptor '__name__' for 'method-wrapper' objects doesn't apply to 'int' object",))
        self.assertEqual(descriptor.__get__((7).__add__), '__add__')

    def test_repeated_wrapper_and_builtin_binding_equality(self):
        target = []
        for name in ('__len__', 'append'):
            first, second = getattr(target, name), getattr(target, name)
            self.assertIsNot(first, second)
            self.assertTrue(first == second)
            self.assertFalse(first != second)
            self.assertEqual(cmp(first, second), 0)
        self.assertFalse(target.append == [].append)

    def test_wrapper_receiver_comparison_callbacks(self):
        events = []
        class Subject(object):
            def __cmp__(self, other):
                events.append('compare')
                return 0
        left, right = Subject(), Subject()
        for call, expected in ((lambda: left.__repr__ == right.__repr__, True),
                               (lambda: left.__repr__ != right.__repr__, False),
                               (lambda: cmp(left.__repr__, right.__repr__), 0),
                               (lambda: left.__repr__ <= right.__repr__, True),
                               (lambda: left.__repr__ > right.__repr__, False)):
            events[:] = []
            self.assertEqual(call(), expected)
            self.assertEqual(events, ['compare'])

    def test_wrapper_receiver_hash_signed_int_protocol(self):
        events = []
        class Subject(object):
            def __hash__(self):
                events.append('hash')
                return 17
        target = Subject()
        wrapper = target.__repr__
        expected = (hash(object.__repr__) ^ 17) & 0xffffffffL
        if expected >= 0x80000000L:
            expected -= 0x100000000L
        if expected == -1:
            expected = -2
        self.assertEqual(hash(wrapper), expected)
        self.assertEqual(events, ['hash'])
        self.assertEqual(hash(target.__repr__), expected)
        self.assertEqual(events, ['hash', 'hash'])

    def test_native_bound_hash_propagates_unhashable_receiver(self):
        for bound, kind in (([].__len__, 'list'), ([].append, 'list'), ({}.get, 'dict')):
            with self.assertRaises(TypeError) as caught:
                hash(bound)
            self.assertEqual(caught.exception.args, ("unhashable type: '%s'" % kind,))

    def test_wrapper_callback_error_identity_and_handled_state_recovery(self):
        failure = KeyboardInterrupt('callback')
        events = []
        class Subject(object):
            fail = True
            def __hash__(self):
                events.append('hash')
                if self.fail:
                    raise failure
                return 17
            def __cmp__(self, other):
                events.append('compare')
                if self.fail:
                    raise failure
                return 0
        left, right = Subject(), Subject()
        def check_failure(call):
            try:
                call()
            except KeyboardInterrupt as caught:
                self.assertIs(caught, failure)
            else:
                self.fail('callback error was not raised')
        try:
            raise ValueError('handled')
        except ValueError as handled:
            for call in (lambda: hash(left.__repr__), lambda: left.__repr__ == right.__repr__):
                check_failure(call)
                self.assertIs(sys.exc_info()[1], handled)
            left.fail = False
            self.assertEqual(cmp(left.__repr__, right.__repr__), 0)
            self.assertIs(type(hash(left.__repr__)), int)
            self.assertIs(sys.exc_info()[1], handled)
        self.assertEqual(events, ['hash', 'compare', 'compare', 'hash'])

    def test_direct_wrapper_method_argument_errors(self):
        for bound in ((7).__add__, len, list.append):
            for field in ('__repr__', '__hash__'):
                method = getattr(bound, field)
                with self.assertRaises(TypeError) as caught:
                    method(17)
                self.assertEqual(caught.exception.args, ('expected 0 arguments, got 1',))
                with self.assertRaises(TypeError) as caught:
                    method(argument=17)
                self.assertEqual(caught.exception.args, ("wrapper %s doesn't take keyword arguments" % field,))
        wrapper = (7).__add__
        with self.assertRaises(TypeError) as caught:
            wrapper.__cmp__(17)
        self.assertEqual(caught.exception.args,
                         ("method-wrapper.__cmp__(x,y) requires y to be a 'method-wrapper', not a 'int'",))

    def test_mixed_native_method_cache_preserves_type_and_receiver(self):
        target = []
        for index in xrange(80):
            wrapper = target.__len__
            self.assertEqual(type(wrapper).__name__, 'method-wrapper')
            self.assertIs(wrapper.__self__, target)
            self.assertEqual(wrapper(), index)
            del wrapper
            bound = target.append
            self.assertEqual(type(bound).__name__, 'builtin_function_or_method')
            self.assertIs(bound.__self__, target)
            bound(index)
            del bound
        self.assertEqual(target, range(80))
