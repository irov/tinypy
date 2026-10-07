"""Project-authored Python 2.7 object and execution protocol matrix."""

import sys
import unittest


class ObjectEvaluationDeep(unittest.TestCase):
    def test_super_none_is_unbound(self):
        class Base(object):
            marker = 23
        class Derived(Base):
            pass
        for value in (super(Derived), super(Derived, None)):
            self.assertIs(value.__thisclass__, Derived)
            self.assertIs(value.__self__, None)
            self.assertIs(value.__self_class__, None)
            with self.assertRaises(AttributeError):
                value.marker

    def test_super_uses_reported_class_for_proxy(self):
        class Base(object):
            marker = 23
        class Derived(Base):
            pass
        class Proxy(object):
            @property
            def __class__(self):
                return Derived
        proxy = Proxy()
        value = super(Derived, proxy)
        self.assertIs(value.__self__, proxy)
        self.assertIs(value.__self_class__, Derived)
        self.assertEqual(value.marker, 23)

    def test_super_does_not_consult_class_when_actual_type_matches(self):
        class Base(object):
            marker = 23
        class Derived(Base):
            @property
            def __class__(self):
                raise AssertionError('actual subtype already known')
        instance = Derived()
        value = super(Derived, instance)
        self.assertIs(value.__self_class__, Derived)
        self.assertEqual(value.marker, 23)

    def test_super_rejects_invalid_reported_class(self):
        class Derived(object):
            pass
        class Proxy(object):
            @property
            def __class__(self):
                return self.reported
        for reported in (17, object, None):
            proxy = Proxy()
            proxy.reported = reported
            self.assertRaises(TypeError, super, Derived, proxy)
        class RaisingProxy(object):
            @property
            def __class__(self):
                raise LookupError('unavailable class')
        self.assertRaises(TypeError, super, Derived, RaisingProxy())

    def test_super_descriptor_ignores_owner(self):
        class Base(object):
            pass
        class Derived(Base):
            pass
        instance = Derived()
        descriptor = super(Derived)
        for owner in (None, 17, Derived):
            if owner is None:
                self.assertRaises(TypeError, descriptor.__get__, None, owner)
            else:
                self.assertIs(descriptor.__get__(None, owner), descriptor)
            bound = descriptor.__get__(instance, owner)
            self.assertIs(bound.__self__, instance)
            self.assertIs(bound.__self_class__, Derived)
            self.assertIs(bound.__get__(object(), owner), bound)

    def test_super_reinitialization_is_validated_before_mutation(self):
        class Base(object):
            pass
        class Derived(Base):
            pass
        instance = Derived()
        value = super(Derived, instance)
        self.assertRaises(TypeError, super.__init__, value, Derived, object())
        self.assertIs(value.__self__, instance)
        self.assertIs(value.__self_class__, Derived)
        self.assertIs(super.__init__(value, Derived, None), None)
        self.assertIs(value.__self__, None)
        self.assertIs(value.__self_class__, None)
        self.assertIs(super.__init__(value, Derived, Derived), None)
        self.assertIs(value.__self__, Derived)
        self.assertIs(value.__self_class__, Derived)

    def test_super_subclass_descriptor_calls_subclass_initializer(self):
        events = []
        class Base(object):
            pass
        class Derived(Base):
            pass
        class Custom(super):
            def __init__(self, *args):
                events.append(len(args))
                super.__init__(self, *args)
        descriptor = Custom(Derived)
        instance = Derived()
        bound = descriptor.__get__(instance, Derived)
        self.assertIs(type(bound), Custom)
        self.assertIs(bound.__self__, instance)
        self.assertEqual(events, [1, 2])

    def test_function_descriptor_accepts_arbitrary_owner(self):
        def invoke(self):
            return self
        receiver = object()
        for owner in (None, 17, object):
            method = invoke.__get__(receiver, owner)
            self.assertIs(method.im_self, receiver)
            self.assertIs(method.im_class, owner)
            self.assertIs(method(), receiver)
        method = invoke.__get__(None, 17)
        self.assertIs(method.im_self, None)
        self.assertEqual(method.im_class, 17)

    def test_descriptors_reject_none_instance_and_none_owner(self):
        def invoke(self):
            return self
        class Slot(object):
            __slots__ = ('value',)
        descriptors = (invoke, property(invoke), staticmethod(invoke),
                       classmethod(invoke), Slot.value,
                       object.__dict__['__class__'])
        for descriptor in descriptors:
            self.assertRaises(TypeError, descriptor.__get__, None, None)
            self.assertRaises(TypeError, descriptor.__get__, None)

    def test_property_static_and_native_descriptors_ignore_owner(self):
        def invoke(self):
            return self
        receiver = object()
        prop = property(invoke)
        static = staticmethod(invoke)
        native = object.__dict__['__class__']
        for owner in (None, 17, object):
            self.assertIs(prop.__get__(receiver, owner), receiver)
            self.assertIs(static.__get__(receiver, owner), invoke)
            self.assertIs(native.__get__(receiver, owner), object)
        for descriptor in (prop, static, native):
            self.assertIs(descriptor.__get__(None, 17),
                          invoke if descriptor is static else descriptor)

    def test_slot_descriptor_ignores_owner_but_checks_receiver(self):
        class Slot(object):
            __slots__ = ('value',)
        instance = Slot()
        instance.value = 31
        for owner in (None, 17, Slot):
            self.assertEqual(Slot.value.__get__(instance, owner), 31)
            self.assertRaises(TypeError, Slot.value.__get__, object(), owner)
        self.assertIs(Slot.value.__get__(None, 17), Slot.value)

    def test_classmethod_descriptor_binds_arbitrary_owner(self):
        def invoke(cls):
            return cls
        descriptor = classmethod(invoke)
        instance = object()
        for owner in (17, object):
            method = descriptor.__get__(instance, owner)
            self.assertIs(method.im_self, owner)
            self.assertIs(method.im_class, type(owner))
            self.assertIs(method(), owner)
        method = descriptor.__get__(instance, None)
        self.assertIs(method.im_self, object)
        self.assertIs(method.im_class, type)

    def test_descriptor_kind_changes_after_type_mutation(self):
        class Descriptor(object):
            def __get__(self, instance, owner):
                return 'descriptor'
        class Record(object):
            value = Descriptor()
        instance = Record()
        instance.value = 'instance'
        self.assertEqual(instance.value, 'instance')
        Descriptor.__set__ = lambda self, instance, value: None
        self.assertEqual(instance.value, 'descriptor')
        del Descriptor.__set__
        self.assertEqual(instance.value, 'instance')

    def test_delete_only_descriptor_keeps_instance_lookup(self):
        class Descriptor(object):
            def __delete__(self, instance):
                instance.deleted = True
        class Record(object):
            value = Descriptor()
        instance = Record()
        instance.__dict__['value'] = 17
        self.assertEqual(instance.value, 17)
        del instance.value
        self.assertTrue(instance.deleted)
        self.assertEqual(instance.__dict__['value'], 17)

    def test_classic_class_descriptor_receives_class_and_instance(self):
        class Descriptor(object):
            def __get__(self, instance, owner):
                return instance, owner
        class Classic:
            value = Descriptor()
        instance = Classic()
        self.assertEqual(Classic.value, (None, Classic))
        self.assertEqual(instance.value, (instance, Classic))

    def test_unicode_attribute_names_are_encoded_before_hooks(self):
        events = []
        class Record(object):
            def __getattribute__(self, name):
                events.append(('get', type(name), name))
                return 19
            def __setattr__(self, name, value):
                events.append(('set', type(name), name, value))
            def __delattr__(self, name):
                events.append(('delete', type(name), name))
        instance = Record()
        self.assertEqual(getattr(instance, u'name'), 19)
        self.assertTrue(hasattr(instance, u'name'))
        setattr(instance, u'name', 23)
        delattr(instance, u'name')
        self.assertEqual(events, [('get', str, 'name'), ('get', str, 'name'),
                                  ('set', str, 'name', 23),
                                  ('delete', str, 'name')])

    def test_unicode_attribute_name_encoding_failure_precedes_hooks(self):
        class Record(object):
            def __getattribute__(self, name):
                raise AssertionError('get hook must not run')
            def __setattr__(self, name, value):
                raise AssertionError('set hook must not run')
            def __delattr__(self, name):
                raise AssertionError('delete hook must not run')
        instance = Record()
        self.assertRaises(UnicodeEncodeError, getattr, instance, u'caf\xe9')
        self.assertRaises(UnicodeEncodeError, setattr, instance, u'caf\xe9', 17)
        self.assertRaises(UnicodeEncodeError, delattr, instance, u'caf\xe9')
        self.assertRaises(UnicodeEncodeError, hasattr, instance, u'caf\xe9')

    def test_unicode_attribute_name_subclass_bypasses_text_overrides(self):
        class Name(unicode):
            def __str__(self):
                raise AssertionError('str override must not run')
            def encode(self, *args):
                raise AssertionError('encode override must not run')
        class Record(object):
            name = 19
        self.assertEqual(getattr(Record(), Name('name')), 19)
        self.assertRaises(UnicodeEncodeError, getattr, Record(), Name(u'caf\xe9'))

    def test_attribute_hooks_apply_to_their_own_names(self):
        class Record(object):
            def __getattribute__(self, name):
                return 'hook:' + name
        instance = Record()
        self.assertEqual(instance.__getattribute__, 'hook:__getattribute__')
        self.assertEqual(instance.__getattr__, 'hook:__getattr__')
        class Missing(object):
            def __getattribute__(self, name):
                raise AttributeError(name)
            def __getattr__(self, name):
                return 'fallback:' + name
        instance = Missing()
        self.assertEqual(instance.__getattribute__, 'fallback:__getattribute__')
        self.assertEqual(instance.__getattr__, 'fallback:__getattr__')

    def test_generator_throw_instance_reuses_exception_with_none_value(self):
        def sequence():
            try:
                yield 'ready'
            except ValueError as error:
                yield error
        for count in (1, 2, 3):
            iterator = sequence()
            self.assertEqual(iterator.next(), 'ready')
            error = ValueError('payload')
            arguments = (error, None, None)[:count]
            self.assertIs(iterator.throw(*arguments), error)
            iterator.close()

    def test_generator_throw_instance_rejects_separate_value(self):
        def sequence():
            yield 'ready'
            yield 'after'
        iterator = sequence()
        self.assertEqual(iterator.next(), 'ready')
        self.assertRaises(TypeError, iterator.throw, ValueError('payload'), 17)
        self.assertEqual(iterator.next(), 'after')
        iterator.close()

    def test_generator_throw_finished_instance_preserves_identity(self):
        def sequence():
            if False:
                yield None
        iterator = sequence()
        self.assertRaises(StopIteration, iterator.next)
        error = ValueError('payload')
        for arguments in ((error,), (error, None), (error, None, None)):
            try:
                iterator.throw(*arguments)
            except ValueError as raised:
                self.assertIs(raised, error)
            else:
                self.fail('throw on closed generator must raise')

    def test_generator_throw_classic_instance_with_none_value(self):
        class Classic:
            pass
        def sequence():
            try:
                yield 'ready'
            except Classic as error:
                yield error
        for count in (1, 2, 3):
            iterator = sequence()
            self.assertEqual(iterator.next(), 'ready')
            error = Classic()
            self.assertIs(iterator.throw(*(error, None, None)[:count]), error)
            iterator.close()

    def test_generator_throw_preserves_requested_type_for_custom_payload(self):
        class PayloadError(ValueError):
            def __new__(cls, *args):
                return 17
        def sequence():
            try:
                yield 'ready'
            except PayloadError as value:
                yield sys.exc_info()[0], value
        iterator = sequence()
        self.assertEqual(iterator.next(), 'ready')
        self.assertEqual(iterator.throw(PayloadError), (PayloadError, 17))
        iterator.close()

    def test_generator_custom_payload_survives_finally(self):
        class PayloadError(ValueError):
            def __new__(cls, *args):
                return 17
        events = []
        def sequence():
            try:
                yield 'ready'
            finally:
                events.append('cleanup')
        iterator = sequence()
        self.assertEqual(iterator.next(), 'ready')
        try:
            iterator.throw(PayloadError)
        except PayloadError as value:
            self.assertIs(sys.exc_info()[0], PayloadError)
            self.assertEqual(value, 17)
        else:
            self.fail('custom payload must keep its requested exception type')
        self.assertEqual(events, ['cleanup'])
        self.assertRaises(StopIteration, iterator.next)

    def test_generator_custom_payload_bare_raise_requires_exception_instance(self):
        class PayloadError(ValueError):
            def __new__(cls, *args):
                return 17
        def sequence():
            try:
                yield 'ready'
            except PayloadError:
                raise
        iterator = sequence()
        self.assertEqual(iterator.next(), 'ready')
        self.assertRaises(TypeError, iterator.throw, PayloadError)
        self.assertRaises(StopIteration, iterator.next)

    def test_generator_finished_throw_preserves_custom_payload_type(self):
        class PayloadError(ValueError):
            def __new__(cls, *args):
                return 17
        def sequence():
            if False:
                yield None
        iterator = sequence()
        self.assertRaises(StopIteration, iterator.next)
        try:
            iterator.throw(PayloadError)
        except PayloadError as value:
            self.assertIs(sys.exc_info()[0], PayloadError)
            self.assertEqual(value, 17)
        else:
            self.fail('closed generator must raise the requested type')

    def test_generator_bare_raise_renormalizes_custom_payload(self):
        events = []
        class PayloadError(ValueError):
            def __new__(cls, *args):
                events.append(args)
                if len(events) <= 2:
                    return 17
                return ValueError.__new__(cls)
        def sequence():
            try:
                yield 'ready'
            except PayloadError as value:
                self.assertEqual(value, 17)
                raise
        iterator = sequence()
        self.assertEqual(iterator.next(), 'ready')
        try:
            iterator.throw(PayloadError)
        except PayloadError as value:
            self.assertIsInstance(value, PayloadError)
            self.assertEqual(value.args, (17,))
        else:
            self.fail('bare raise must normalize the custom payload again')
        self.assertEqual(events, [(), (17,), (17,)])
        self.assertRaises(StopIteration, iterator.next)

    def test_generator_exception_state_matches_python2_resume_rules(self):
        def sequence():
            try:
                raise ValueError('inner')
            except ValueError as error:
                yield sys.exc_info()[1] is error
                yield sys.exc_info()[1] is error
        iterator = sequence()
        try:
            raise KeyError('outer')
        except KeyError as outer:
            self.assertTrue(iterator.next())
            self.assertIs(sys.exc_info()[1], outer)
            self.assertFalse(iterator.next())
            self.assertIs(sys.exc_info()[1], outer)
        iterator.close()

    def test_generator_close_propagates_cleanup_and_rejects_yield(self):
        events = []
        def sequence():
            try:
                yield 'ready'
            finally:
                events.append('cleanup')
        iterator = sequence()
        self.assertEqual(iterator.next(), 'ready')
        self.assertIs(iterator.close(), None)
        self.assertIs(iterator.close(), None)
        self.assertEqual(events, ['cleanup'])
        def ignoring():
            try:
                yield 'ready'
            except GeneratorExit:
                yield 'ignored'
        iterator = ignoring()
        self.assertEqual(iterator.next(), 'ready')
        self.assertRaises(RuntimeError, iterator.close)
        iterator.close()

    def test_exception_reinit_updates_message_only_for_one_argument(self):
        for exception in (BaseException, Exception, ValueError, KeyError):
            error = exception('initial')
            self.assertEqual(error.message, 'initial')
            error.__init__('first', 'second')
            self.assertEqual(error.args, ('first', 'second'))
            self.assertEqual(error.message, 'initial')
            error.__init__()
            self.assertEqual(error.args, ())
            self.assertEqual(error.message, 'initial')
            error.__init__('replacement')
            self.assertEqual(error.message, 'replacement')

    def test_exception_new_ignores_constructor_arguments_and_keywords(self):
        error = BaseException.__new__(ValueError, 'ignored', option=17)
        self.assertEqual(error.args, ())
        self.assertEqual(error.message, '')
        error.__init__('initialized')
        self.assertEqual(error.args, ('initialized',))
        self.assertEqual(error.message, 'initialized')

    def test_exception_message_assignment_is_independent_of_args(self):
        error = ValueError('original')
        error.message = 'custom'
        error.args = ('replacement',)
        self.assertEqual(error.message, 'custom')
        self.assertEqual(str(error), 'replacement')
        del error.message
        self.assertRaises(AttributeError, getattr, error, 'message')
        error.__init__('restored')
        self.assertEqual(error.message, 'restored')

    def test_environment_error_reinit_retains_unspecified_fields(self):
        error = EnvironmentError(2, 'missing', 'asset.bin')
        for arguments in ((), ('replacement',), ('a', 'b', 'c', 'd')):
            error.__init__(*arguments)
            self.assertEqual(error.args, arguments)
            self.assertEqual((error.errno, error.strerror, error.filename),
                             (2, 'missing', 'asset.bin'))
        error.__init__(3, 'denied')
        self.assertEqual(error.args, (3, 'denied'))
        self.assertEqual((error.errno, error.strerror, error.filename),
                         (3, 'denied', 'asset.bin'))
        error.__init__(4, 'new', 'other.bin')
        self.assertEqual(error.args, (4, 'new'))
        self.assertEqual(error.filename, 'other.bin')

    def test_system_exit_reinit_without_arguments_retains_code(self):
        error = SystemExit(17)
        error.__init__()
        self.assertEqual(error.args, ())
        self.assertEqual(error.code, 17)
        error.__init__(19, 23)
        self.assertEqual(error.code, (19, 23))
        error.__init__()
        self.assertEqual(error.code, (19, 23))

    def test_syntax_error_reinit_preserves_unspecified_location(self):
        error = SyntaxError('initial', ('source.py', 3, 7, 'invalid text'))
        error.print_file_and_line = True
        error.__init__()
        self.assertEqual(error.msg, 'initial')
        error.__init__('replacement')
        self.assertEqual(error.msg, 'replacement')
        self.assertEqual((error.filename, error.lineno, error.offset, error.text),
                         ('source.py', 3, 7, 'invalid text'))
        self.assertIs(error.print_file_and_line, True)
        self.assertRaises(IndexError, error.__init__, 'invalid', ('too', 'short'))
        self.assertEqual(error.args, ('invalid', ('too', 'short')))
        self.assertEqual(error.msg, 'invalid')
        self.assertEqual((error.filename, error.lineno, error.offset, error.text),
                         ('source.py', 3, 7, 'invalid text'))

    def test_call_mapping_keywords_and_duplicates(self):
        class Mapping(object):
            def keys(self):
                return [u'key']
            def __getitem__(self, key):
                return 19
        def invoke(key):
            return key
        self.assertEqual(invoke(**Mapping()), 19)
        self.assertRaises(TypeError, lambda: invoke(key=17, **{'key': 23}))

    def test_raise_tuple_selects_first_exception_class(self):
        try:
            raise (ValueError, TypeError), 'payload'
        except ValueError as error:
            self.assertEqual(error.args, ('payload',))

    def test_locals_refresh_preserves_fast_local_bindings(self):
        def invoke():
            value = 17
            locals()['value'] = 23
            return value, locals()['value']
        self.assertEqual(invoke(), (17, 17))
