"""Project-authored native metadata readiness and C descriptor regressions.

Each test runs in a fresh interpreter. Assertions that inspect a native type
name occur after the operation whose readiness transition is being checked.
"""

import sys
import unittest


class NativeReadinessAudit(unittest.TestCase):
    def assert_error(self, operation, kind, message):
        with self.assertRaises(kind) as caught:
            operation()
        self.assertEqual(caught.exception.args, (message,))

    def assert_cold(self, value, name):
        self.assert_error(lambda: setattr(value, '__name__', 17), TypeError,
                          "'%s' object has only read-only attributes (assign to .__name__)" % name)

    def assert_warm(self, value, name):
        if name == 'method_descriptor':
            self.assert_error(lambda: setattr(value, '__name__', 17), TypeError,
                              'readonly attribute')
        else:
            self.assert_error(lambda: setattr(value, '__name__', 17), AttributeError,
                              "attribute '__name__' of 'method-wrapper' objects is not writable")

    def test_wrapper_cold_writes_do_not_ready_type(self):
        value = (7).__add__
        self.assert_cold(value, 'method-wrapper')
        self.assert_error(lambda: delattr(value, '__self__'), TypeError,
                          "'method-wrapper' object has only read-only attributes (del .__self__)")
        self.assert_cold(value, 'method-wrapper')

    def test_method_descriptor_cold_writes_do_not_ready_type(self):
        value = list.append
        self.assert_cold(value, 'method_descriptor')
        self.assert_error(lambda: delattr(value, '__objclass__'), TypeError,
                          "'method_descriptor' object has only read-only attributes (del .__objclass__)")
        self.assert_cold(value, 'method_descriptor')

    def test_wrapper_nonattribute_operations_remain_cold(self):
        value = (7).__add__
        for operation in (lambda: repr(value), lambda: str(value), lambda: hash(value),
                          lambda: bool(value), lambda: value == value,
                          lambda: callable(value), lambda: isinstance(value, object),
                          lambda: issubclass(type(value), object), lambda: value(2)):
            operation()
            self.assert_cold(value, 'method-wrapper')

    def test_descriptor_nonattribute_operations_remain_cold(self):
        value = list.append
        for operation in (lambda: repr(value), lambda: str(value), lambda: bool(value),
                          lambda: value == value, lambda: callable(value),
                          lambda: isinstance(value, object),
                          lambda: issubclass(type(value), object), lambda: value([], 2)):
            operation()
            self.assert_cold(value, 'method_descriptor')

    def test_descriptor_hash_readies_type(self):
        value = list.append
        hash(value)
        self.assert_warm(value, 'method_descriptor')

    def test_wrapper_hash_failure_preserves_cold_state(self):
        value = [].__len__
        self.assert_error(lambda: hash(value), TypeError, "unhashable type: 'list'")
        self.assert_cold(value, 'method-wrapper')

    def test_successful_instance_read_readies_wrapper(self):
        value = (7).__add__
        self.assertEqual(value.__name__, '__add__')
        self.assert_warm(value, 'method-wrapper')

    def test_failed_instance_read_readies_descriptor(self):
        value = list.append
        self.assert_error(lambda: getattr(value, 'missing'), AttributeError,
                          "'method_descriptor' object has no attribute 'missing'")
        self.assert_warm(value, 'method_descriptor')

    def test_suppressed_instance_read_readies_wrapper(self):
        value = (7).__add__
        self.assertFalse(hasattr(value, 'missing'))
        self.assert_warm(value, 'method-wrapper')

    def test_successful_type_read_readies_descriptor(self):
        value = list.append
        self.assertEqual(type(value).__name__, 'method_descriptor')
        self.assert_warm(value, 'method_descriptor')

    def test_failed_type_read_readies_wrapper(self):
        value = (7).__add__
        self.assert_error(lambda: getattr(type(value), 'missing'), AttributeError,
                          "type object 'method-wrapper' has no attribute 'missing'")
        self.assert_warm(value, 'method-wrapper')

    def test_dir_readies_descriptor(self):
        value = list.append
        self.assertIn('__name__', dir(value))
        self.assert_warm(value, 'method_descriptor')

    def test_class_read_readies_wrapper(self):
        value = (7).__add__
        self.assertIs(value.__class__, type(value))
        self.assert_warm(value, 'method-wrapper')

    def test_type_constructor_failure_does_not_ready_wrapper(self):
        value = (7).__add__
        self.assert_error(lambda: type(value)(), TypeError,
                          "cannot create 'method-wrapper' instances")
        self.assert_cold(value, 'method-wrapper')

    def test_immutable_type_write_does_not_ready_descriptor(self):
        value = list.append
        self.assert_error(lambda: setattr(type(value), 'missing', 17), TypeError,
                          "can't set attributes of built-in/extension type 'method_descriptor'")
        self.assert_cold(value, 'method_descriptor')

    def test_direct_object_setter_checks_cold_slot_before_name(self):
        value = (7).__add__
        self.assert_error(lambda: object.__setattr__(value, 17, 2), TypeError,
                          "can't apply this __setattr__ to method-wrapper object")
        self.assert_error(lambda: object.__delattr__(value, 17), TypeError,
                          "can't apply this __delattr__ to method-wrapper object")
        self.assert_cold(value, 'method-wrapper')
        value.__name__
        self.assert_error(lambda: object.__setattr__(value, 17, 2), TypeError,
                          "attribute name must be string, not 'int'")
        self.assert_error(lambda: object.__delattr__(value, 17), TypeError,
                          "attribute name must be string, not 'int'")

    def test_direct_object_getter_readies_descriptor(self):
        value = list.append
        self.assertEqual(object.__getattribute__(value, '__name__'), 'append')
        self.assert_warm(value, 'method_descriptor')

    def test_failed_name_validation_does_not_ready_wrapper(self):
        value = (7).__add__
        self.assert_error(lambda: object.__getattribute__(value, 17), TypeError,
                          "attribute name must be string, not 'int'")
        self.assert_error(lambda: getattr(value, 17), TypeError,
                          'getattr(): attribute name must be string')
        self.assert_error(lambda: getattr(value, 17, 'default'), TypeError,
                          'getattr(): attribute name must be string')
        for operation in (lambda: getattr(value, u'\xff'),
                          lambda: setattr(value, u'\xff', 17),
                          lambda: delattr(value, u'\xff')):
            with self.assertRaises(UnicodeEncodeError):
                operation()
            self.assert_cold(value, 'method-wrapper')

    def test_cold_error_name_limits_and_nul(self):
        value = list.append
        for key, rendered in (('missing\0suffix', 'missing'), ('x' * 160, 'x' * 100)):
            self.assert_error(lambda: setattr(value, key, 17), TypeError,
                              "'method_descriptor' object has only read-only attributes (assign to .%s)" % rendered)
            self.assert_error(lambda: delattr(value, key), TypeError,
                              "'method_descriptor' object has only read-only attributes (del .%s)" % rendered)

    def test_readiness_is_shared_by_all_instances_of_actual_type(self):
        first, second = (7).__add__, (9).__sub__
        self.assert_cold(first, 'method-wrapper')
        second.__name__
        self.assert_warm(first, 'method-wrapper')
        self.assert_warm(second, 'method-wrapper')

    def test_direct_safe_type_fields_expose_cold_none(self):
        value = (7).__add__
        owner = type(value)
        for field in ('__dict__', '__base__', '__mro__'):
            self.assertIs(type.__dict__[field].__get__(owner, type), None)
            self.assert_cold(value, 'method-wrapper')
        flags = type.__dict__['__flags__']
        self.assertEqual(flags.__get__(owner, type) & (1 << 12), 0)
        self.assertEqual(type.__dict__['__name__'].__get__(owner, type), 'method-wrapper')
        self.assert_cold(value, 'method-wrapper')
        value.__name__
        self.assertNotEqual(flags.__get__(owner, type) & (1 << 12), 0)
        self.assertIs(type.__dict__['__base__'].__get__(owner, type), object)
        self.assertIn(owner, type.__dict__['__mro__'].__get__(owner, type))
        self.assertIn('__name__', type.__dict__['__dict__'].__get__(owner, type))

    def test_c_descriptor_metadata_uses_native_members(self):
        for value in (type.__dict__['__name__'], type.__dict__['__basicsize__']):
            owner = type(value)
            for field in ('__name__', '__objclass__'):
                member = owner.__dict__[field]
                self.assertEqual(type(member).__name__, 'member_descriptor')
                self.assertIs(member.__objclass__, owner)
                self.assertEqual(member.__get__(value, owner), getattr(value, field))
                self.assert_error(lambda: setattr(value, field, 17), TypeError, 'readonly attribute')
                self.assert_error(lambda: delattr(value, field), TypeError, 'readonly attribute')
            doc = owner.__dict__['__doc__']
            self.assertEqual(type(doc).__name__, 'getset_descriptor')
            self.assertIs(doc.__objclass__, owner)
            self.assert_error(lambda: setattr(value, '__doc__', 17), AttributeError,
                              "attribute '__doc__' of '%s' objects is not writable" % owner.__name__)

    def test_c_descriptor_metadata_rejects_wrong_receiver(self):
        for owner in (type(type.__dict__['__name__']), type(type.__dict__['__basicsize__'])):
            descriptor = owner.__dict__['__name__']
            self.assert_error(lambda: descriptor.__get__(17, int), TypeError,
                              "descriptor '__name__' for '%s' objects doesn't apply to 'int' object" % owner.__name__)

    def test_c_descriptor_unknown_setter_raises_attribute_error(self):
        for descriptor in (type.__dict__['__name__'], type.__dict__['__basicsize__']):
            for field in ('__self__', '__module__', 'missing'):
                message = "'%s' object has no attribute '%s'" % (type(descriptor).__name__, field)
                self.assert_error(lambda: setattr(descriptor, field, 17), AttributeError, message)
                self.assert_error(lambda: delattr(descriptor, field), AttributeError, message)

    def test_readiness_errors_preserve_handled_exception(self):
        original = ValueError('handled')
        try:
            raise original
        except ValueError:
            value = (7).__add__
            self.assert_cold(value, 'method-wrapper')
            self.assertIs(sys.exc_info()[1], original)
            self.assertFalse(hasattr(value, 'missing'))
            self.assert_warm(value, 'method-wrapper')
            self.assertIs(sys.exc_info()[1], original)

    def test_numeric_fields_use_physical_c_descriptors(self):
        for owner in (int, long, float, complex):
            fields = ('real', 'imag', 'numerator', 'denominator') if owner in (int, long) else ('real', 'imag')
            for field in fields:
                descriptor = owner.__dict__[field]
                expected = 'member_descriptor' if owner is complex else 'getset_descriptor'
                self.assertEqual(type(descriptor).__name__, expected)
                self.assertEqual(descriptor.__name__, field)
                self.assertIs(descriptor.__objclass__, owner)
                self.assertIs(descriptor.__get__(None, owner), descriptor)
        self.assertNotIn('real', bool.__dict__)
        self.assertIs(bool.real, int.real)

    def test_numeric_fields_keep_readonly_error_kind_and_owner(self):
        for owner, value in ((int, 7), (long, 7L), (float, 2.5), (complex, 2 + 3j), (bool, True)):
            fields = ('real', 'imag', 'numerator', 'denominator') if owner in (int, long, bool) else ('real', 'imag')
            for field in fields:
                descriptor = getattr(owner, field)
                kind = TypeError if owner is complex else AttributeError
                message = 'readonly attribute' if owner is complex else "attribute '%s' of '%s' objects is not writable" % (field, descriptor.__objclass__.__name__)
                for operation in (lambda: setattr(value, field, 17),
                                  lambda: delattr(value, field),
                                  lambda: descriptor.__set__(value, 17),
                                  lambda: descriptor.__delete__(value)):
                    self.assert_error(operation, kind, message)

    def test_numeric_field_subclass_results_are_canonical_values(self):
        for base, number in ((int, 7), (long, 7L), (float, 2.5), (complex, 2 + 3j)):
            child = type('NumberChild', (base,), {})
            value = child(number)
            fields = ('real', 'imag', 'numerator', 'denominator') if base in (int, long) else ('real', 'imag')
            for field in fields:
                descriptor = getattr(base, field)
                result = descriptor.__get__(value, child)
                self.assertEqual(result, getattr(number, field))
                self.assertIs(type(result), float if base is complex else base)
                self.assertIsNot(result, value)
        self.assertIs(type(int.real.__get__(True, bool)), int)

    def test_numeric_descriptors_validate_receiver_before_readonly(self):
        for owner in (int, long, float, complex):
            descriptor = owner.real
            message = "descriptor 'real' for '%s' objects doesn't apply to 'str' object" % owner.__name__
            for operation in (lambda: descriptor.__get__('wrong', str),
                              lambda: descriptor.__set__('wrong', 17),
                              lambda: descriptor.__delete__('wrong')):
                self.assert_error(operation, TypeError, message)

    def test_numeric_descriptor_priority_and_subclass_override(self):
        class Number(int):
            pass
        value = Number(7)
        value.__dict__['real'] = 23
        self.assertEqual(value.real, 7)
        self.assert_error(lambda: setattr(value, 'real', 17), AttributeError,
                          "attribute 'real' of 'int' objects is not writable")
        class Override(int):
            real = 'class field'
        changed = Override(7)
        self.assertEqual(changed.real, 'class field')
        changed.real = 'instance field'
        self.assertEqual(changed.real, 'instance field')
        self.assertEqual(int.real.__get__(changed, Override), 7)
