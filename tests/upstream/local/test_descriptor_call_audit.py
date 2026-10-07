"""Direct descriptor calls preserve optional binding and parser semantics."""

import sys
import unittest


class DescriptorCallAudit(unittest.TestCase):
    def assert_error(self, operation, kind, message):
        try:
            operation()
        except BaseException as error:
            self.assertIs(type(error), kind)
            self.assertEqual(error.args, (message,))
        else:
            self.fail('operation succeeded')

    def test_function_implicit_owner_is_none(self):
        def read(value):
            return value
        for receiver in (object(), 7, [], None):
            if receiver is None:
                self.assert_error(lambda: read.__get__(receiver), TypeError,
                                  '__get__(None, None) is invalid')
            else:
                bound = read.__get__(receiver)
                self.assertIs(bound.im_class, None)
                self.assertIs(bound.im_self, receiver)
                self.assertIs(bound.im_func, read)
                self.assertIs(bound(), receiver)

    def test_unbound_method_implicit_owner_is_none(self):
        class Owner(object):
            def read(self):
                return self
        descriptor = Owner.read
        receiver = object()
        bound = descriptor.__get__(receiver)
        self.assertIs(bound.im_class, None)
        self.assertIs(bound.im_self, receiver)
        self.assertIs(bound.im_func, descriptor.im_func)
        self.assertIs(bound(), receiver)

    def test_unbound_method_explicit_none_owner_rebinds(self):
        class Owner(object):
            def read(self):
                return self
        descriptor = Owner.read
        receiver = object()
        bound = descriptor.__get__(receiver, None)
        self.assertIs(bound.im_class, None)
        self.assertIs(bound.im_self, receiver)
        self.assertIs(bound(), receiver)

    def test_classic_method_rebinds_to_classic_owner(self):
        class Owner:
            def read(self):
                return self
        class Child(Owner):
            pass
        descriptor = Owner.read
        receiver = Child()
        bound = descriptor.__get__(receiver, Child)
        self.assertIs(bound.im_class, Child)
        self.assertIs(bound.im_self, receiver)
        self.assertIs(bound.im_func, descriptor.im_func)
        self.assertIs(bound(), receiver)
        unbound = descriptor.__get__(None, Child)
        self.assertIs(unbound.im_class, Child)
        self.assertIs(unbound.im_self, None)
        self.assertIs(unbound(receiver), receiver)

    def test_classic_method_implicit_owner_is_none(self):
        class Owner:
            def read(self):
                return self
        descriptor = Owner.read
        receiver = object()
        bound = descriptor.__get__(receiver)
        self.assertIs(bound.im_class, None)
        self.assertIs(bound.im_self, receiver)
        self.assertIs(bound(), receiver)

    def test_unrelated_owner_keeps_unbound_method_identity(self):
        class Owner(object):
            def read(self):
                return self
        class Other(object):
            pass
        descriptor = Owner.read
        self.assertIs(descriptor.__get__(Other(), Other), descriptor)

    def test_bound_method_keeps_original_owner_and_receiver(self):
        class Owner(object):
            def read(self):
                return self
        receiver = Owner()
        bound = receiver.read
        for owner in (None, Owner, 17):
            self.assertIs(bound.__get__(object(), owner), bound)
            self.assertIs(bound.im_self, receiver)
            self.assertIs(bound.im_class, Owner)

    def test_unbound_method_validates_explicit_owner(self):
        class Owner(object):
            def read(self):
                return self
        self.assert_error(lambda: Owner.read.__get__(Owner(), 17), TypeError,
                          'issubclass() arg 1 must be a class')

    def test_unbound_method_subclass_callback_controls_binding(self):
        events = []
        class Meta(type):
            def __subclasscheck__(cls, candidate):
                events.append(candidate)
                return cls.accept
        class Owner(object):
            __metaclass__ = Meta
            accept = False
            def read(self):
                return self
        class Other(object):
            pass
        descriptor = Owner.read
        receiver = Other()
        self.assertIs(descriptor.__get__(receiver, Other), descriptor)
        Owner.accept = True
        bound = descriptor.__get__(receiver, Other)
        self.assertIs(bound.im_self, receiver)
        self.assertIs(bound.im_class, Other)
        self.assertIs(bound(), receiver)
        self.assertEqual(events, [Other, Other])

    def test_subclass_callback_error_preserves_handler_and_recovers(self):
        events = []
        failures = [ValueError('subclass marker'), KeyboardInterrupt('interrupt marker')]
        class Meta(type):
            def __subclasscheck__(cls, candidate):
                events.append(sys.exc_info()[1])
                if cls.failure is not None:
                    raise cls.failure
                return True
        class Owner(object):
            __metaclass__ = Meta
            failure = None
            def read(self):
                return self
        class Other(object):
            pass
        descriptor = Owner.read
        receiver = Other()
        prior = LookupError('outer marker')
        def capture_failure():
            try:
                descriptor.__get__(receiver, Other)
            except BaseException as error:
                return error
            self.fail('subclass callback succeeded')
        try:
            raise prior
        except LookupError:
            for failure in failures:
                Owner.failure = failure
                self.assertIs(capture_failure(), failure)
                self.assertIs(sys.exc_info()[1], prior)
            Owner.failure = None
            self.assertIs(descriptor.__get__(receiver, Other)(), receiver)
            self.assertIs(sys.exc_info()[1], prior)
        self.assertEqual(events, [prior, prior, prior])

    def test_all_descriptor_getters_share_canonical_argument_errors(self):
        def read(value):
            return 17
        class Owner(object):
            __slots__ = ('leaf',)
        descriptors = (read, read.__get__(object(), object), property(read),
                       staticmethod(read), classmethod(read), Owner.leaf,
                       type(read).__dict__['func_name'], list.append, int.__add__)
        for descriptor in descriptors:
            self.assert_error(lambda: descriptor.__get__(), TypeError,
                              ' expected at least 1 arguments, got 0')
            self.assert_error(lambda: descriptor.__get__(object(), object, 17), TypeError,
                              ' expected at most 2 arguments, got 3')
            self.assert_error(lambda: descriptor.__get__(None), TypeError,
                              '__get__(None, None) is invalid')
            self.assert_error(lambda: descriptor.__get__(object(), missing=17), TypeError,
                              "wrapper __get__ doesn't take keyword arguments")

    def test_property_setter_argument_errors_skip_callbacks(self):
        events = []
        def write(receiver, value):
            events.append(value)
        field = property(None, write)
        receiver = object()
        self.assert_error(lambda: field.__set__(receiver), TypeError,
                          ' expected 2 arguments, got 1')
        self.assert_error(lambda: field.__set__(receiver, 7, missing=9), TypeError,
                          "wrapper __set__ doesn't take keyword arguments")
        self.assertEqual(events, [])
        self.assertIs(field.__set__(receiver, 11), None)
        self.assertEqual(events, [11])

    def test_property_delete_argument_errors_skip_callbacks(self):
        events = []
        def remove(receiver):
            events.append(receiver)
        field = property(None, None, remove)
        receiver = object()
        self.assert_error(lambda: field.__delete__(), TypeError,
                          'expected 1 arguments, got 0')
        self.assert_error(lambda: field.__delete__(receiver, missing=9), TypeError,
                          "wrapper __delete__ doesn't take keyword arguments")
        self.assertEqual(events, [])
        self.assertIs(field.__delete__(receiver), None)
        self.assertEqual(events, [receiver])

    def test_c_descriptor_set_delete_and_repr_argument_errors(self):
        def read(value):
            return value
        descriptors = (type(read).__dict__['func_globals'], type(read).__dict__['func_name'], int.real)
        for descriptor in descriptors:
            self.assert_error(lambda: descriptor.__set__(read), TypeError,
                              ' expected 2 arguments, got 1')
            self.assert_error(lambda: descriptor.__delete__(), TypeError,
                              'expected 1 arguments, got 0')
            self.assert_error(lambda: descriptor.__repr__(17), TypeError,
                              'expected 0 arguments, got 1')
            self.assert_error(lambda: descriptor.__repr__(missing=17), TypeError,
                              "wrapper __repr__ doesn't take keyword arguments")

    def test_function_c_descriptor_receiver_errors_precede_readonly(self):
        def read(value):
            return value
        for field in ('func_globals', 'func_name'):
            descriptor = type(read).__dict__[field]
            message = "descriptor '%s' for 'function' objects doesn't apply to 'int' object" % field
            for operation in (lambda: descriptor.__get__(17),
                              lambda: descriptor.__set__(17, 23),
                              lambda: descriptor.__delete__(17)):
                self.assert_error(operation, TypeError, message)

    def test_c_descriptor_errors_preserve_handler_and_field_state(self):
        class Owner(object):
            __slots__ = ('leaf',)
        value = Owner()
        value.leaf = 37
        descriptor = Owner.leaf
        prior = LookupError('outer marker')
        try:
            raise prior
        except LookupError:
            for operation in (lambda: descriptor.__get__(17),
                              lambda: descriptor.__set__(17, 23),
                              lambda: descriptor.__delete__(17)):
                self.assert_error(operation, TypeError,
                                  "descriptor 'leaf' for 'Owner' objects doesn't apply to 'int' object")
                self.assertIs(sys.exc_info()[1], prior)
                self.assertEqual(value.leaf, 37)
            self.assertIs(descriptor.__set__(value, 41), None)
            self.assertEqual(value.leaf, 41)

    def test_long_slot_receiver_diagnostics_keep_get_set_limits(self):
        field_name = 'leaf' + 'x' * 220
        owner_name = 'Owner' + 'x' * 130
        receiver_name = 'Receiver' + 'x' * 130
        owner = type(owner_name, (object,), {'__slots__': (field_name,)})
        receiver = type(receiver_name, (object,), {})()
        descriptor = owner.__dict__[field_name]
        full = "descriptor '%s' for '%s' objects doesn't apply to '%s' object" % (field_name, owner_name, receiver_name)
        limited = "descriptor '%s' for '%s' objects doesn't apply to '%s' object" % (field_name[:200], owner_name[:100], receiver_name[:100])
        self.assert_error(lambda: descriptor.__get__(receiver), TypeError, full)
        self.assert_error(lambda: descriptor.__set__(receiver, 17), TypeError, limited)
        self.assert_error(lambda: descriptor.__delete__(receiver), TypeError, limited)


if __name__ == '__main__':
    unittest.main()
