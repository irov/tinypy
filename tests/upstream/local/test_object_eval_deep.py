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

    def test_print_reports_lost_stdout_and_none_target(self):
        class Collect(object):
            def __init__(self):
                self.parts = []

            def write(self, text):
                self.parts.append(text)
        saved = sys.stdout
        collect = Collect()
        try:
            sys.stdout = collect
            print >>None, 'to', 1
            del sys.stdout
            with self.assertRaises(RuntimeError) as caught:
                print 'lost'
            self.assertEqual(str(caught.exception), 'lost sys.stdout')
            with self.assertRaises(RuntimeError):
                print >>None
        finally:
            sys.stdout = saved
        self.assertEqual(collect.parts, ['to', ' ', '1', '\n'])

    def test_callee_exception_clear_is_undone_when_handler_frame_returns(self):
        def clear():
            sys.exc_clear()
        def handler():
            try:
                raise TypeError
            except TypeError:
                clear()
                return sys.exc_info()[0]
        try:
            raise ValueError
        except ValueError:
            self.assertIs(handler(), None)
            self.assertIs(sys.exc_info()[0], ValueError)

    def test_generator_yield_restores_caller_exception(self):
        def generate():
            try:
                raise ZeroDivisionError
            except ZeroDivisionError:
                yield sys.exc_info()[0]
                yield sys.exc_info()[0]
        try:
            raise ValueError
        except ValueError:
            iterator = generate()
            self.assertIs(next(iterator), ZeroDivisionError)
            self.assertIs(sys.exc_info()[0], ValueError)
            self.assertIs(next(iterator), ValueError)

    def test_legacy_exception_names_follow_handler_frames(self):
        def handled():
            try:
                raise KeyError('k')
            except KeyError:
                return sys.exc_type, sys.exc_value
        sys.exc_clear()
        kind, value = handled()
        self.assertIs(kind, KeyError)
        self.assertEqual(value.args, ('k',))
        self.assertIs(sys.exc_type, None)
        self.assertFalse(hasattr(sys, 'exc_value'))
        self.assertFalse(hasattr(sys, 'exc_traceback'))
        sys.exc_clear()
        self.assertEqual((sys.exc_type, sys.exc_value, sys.exc_traceback), (None, None, None))

    def test_unbound_method_rebinds_only_for_subclasses(self):
        class Classic:
            def method(self):
                return self.__class__.__name__
        class Derived(Classic):
            alias = Classic.method
        class Unrelated:
            alias = Classic.method
        self.assertEqual(Derived().alias(), 'Derived')
        self.assertIs(Unrelated.alias.im_self, None)
        self.assertRaises(TypeError, Unrelated().alias)
        class Meta(type):
            def __subclasscheck__(cls, other):
                return True
        class Base(object):
            __metaclass__ = Meta
            def method(self):
                return 'base'
        class Other(object):
            alias = Base.method
        self.assertEqual(Other().alias(), 'base')

    def test_raise_normalizes_through_the_metaclass_call(self):
        calls = []
        class Meta(type):
            def __call__(cls, *args):
                calls.append(args)
                return type.__call__(cls, *args)
        class Failure(Exception):
            __metaclass__ = Meta
        try:
            raise Failure, 'value'
        except Failure as error:
            self.assertEqual(error.args, ('value',))
        self.assertEqual(calls, [('value',)])
        class Bad(Exception):
            def __new__(cls, *args):
                return 5
        with self.assertRaises(TypeError) as caught:
            raise Bad
        self.assertEqual(str(caught.exception), "calling Bad() should have returned an instance of BaseException, not 'int'")

    def test_classic_constructor_errors(self):
        class Plain:
            pass
        class Returning:
            def __init__(self):
                return 1
        with self.assertRaises(TypeError) as caught:
            Plain(1)
        self.assertEqual(str(caught.exception), 'this constructor takes no arguments')
        with self.assertRaises(TypeError) as caught:
            Returning()
        self.assertEqual(str(caught.exception), '__init__() should return None')

    def test_super_messages_and_mro_before_members(self):
        class Base(object):
            __self__ = 'base self'
        class Derived(Base):
            pass
        instance = Derived()
        self.assertEqual(super(Derived, instance).__self__, 'base self')
        self.assertIs(super(Base, instance).__self__, instance)
        cases = (
            (lambda: super(Derived, instance).missing, AttributeError, "'super' object has no attribute 'missing'"),
            (lambda: super(Derived).missing, AttributeError, "'super' object has no attribute 'missing'"),
            (lambda: super(5), TypeError, 'super() argument 1 must be type, not int'),
            (lambda: super(), TypeError, 'super() takes at least 1 argument (0 given)'),
            (lambda: super(Derived, instance, 3), TypeError, 'super() takes at most 2 arguments (3 given)'),
            (lambda: super(Derived, x=1), TypeError, 'super does not take keyword arguments'),
        )
        for function, kind, message in cases:
            with self.assertRaises(kind) as caught:
                function()
            self.assertEqual(str(caught.exception), message)

    def test_generator_repr_names_its_code(self):
        def produce():
            yield 1
        iterator = produce()
        self.assertTrue(repr(iterator).startswith('<generator object produce at 0x'))
        self.assertEqual(iterator.__repr__(), repr(iterator))

    def test_classic_attribute_hooks_are_called_unbound(self):
        events = []
        class Failing(object):
            def __get__(self, instance, owner):
                raise ValueError('get')
            def __call__(self, *args):
                events.append(len(args))
                return 'called'
        class Hooked:
            __getattr__ = Failing()
            __setattr__ = Failing()
            __delattr__ = Failing()
        instance = Hooked()
        self.assertEqual(instance.missing, 'called')
        instance.value = 1
        del instance.value
        self.assertEqual(events, [2, 3, 2])
        instance.__dict__['__setattr__'] = lambda *args: events.append('instance')
        instance.other = 2
        self.assertEqual(events, [2, 3, 2, 3])
        class Init:
            __init__ = Failing()
        self.assertRaises(ValueError, Init)

    def test_classic_class_str_uses_module(self):
        class Classic:
            pass
        expected = '%s.Classic' % Classic.__module__
        self.assertEqual((str(Classic), '%s' % Classic, '%s' % (Classic,)), (expected, expected, expected))
        self.assertTrue(repr(Classic).startswith('<class %s at 0x' % expected))
        del Classic.__module__
        self.assertEqual(str(Classic), 'Classic')
        self.assertIs(Classic.__doc__, None)

    def test_slot_and_layout_errors(self):
        class Base(object):
            __slots__ = ('a',)
        class Other(object):
            __slots__ = ('b',)
        instance = Base()
        with self.assertRaises(AttributeError) as caught:
            instance.a
        self.assertEqual(caught.exception.args, ('a',))
        with self.assertRaises(AttributeError) as caught:
            del instance.a
        self.assertEqual(caught.exception.args, ('a',))
        with self.assertRaises(AttributeError) as caught:
            instance.c = 1
        self.assertEqual(str(caught.exception), "'Base' object has no attribute 'c'")
        with self.assertRaises(TypeError) as caught:
            type('Both', (Base, Other), {})
        self.assertEqual(str(caught.exception), 'multiple bases have instance lay-out conflict')

    def test_generic_attribute_assignment_errors(self):
        class Owner(object):
            def method(self):
                pass
        holder = Owner()
        holder.present = 1
        cases = (
            (lambda: setattr(Owner.method, 'x', 1), "'instancemethod' object has no attribute 'x'"),
            (lambda: setattr(5, 'x', 1), "'int' object has no attribute 'x'"),
            (lambda: setattr(5, '__doc__', 1), "'int' object attribute '__doc__' is read-only"),
            (lambda: delattr(object(), '__doc__'), "'object' object attribute '__doc__' is read-only"),
            (lambda: delattr(Owner, 'missing'), 'missing'),
            (lambda: delattr(holder, 'missing'), 'missing'),
            (lambda: delattr(Owner(), 'missing'), "'Owner' object has no attribute 'missing'"),
            (lambda: ValueError().missing, "'exceptions.ValueError' object has no attribute 'missing'"),
            (lambda: ValueError.missing, "type object 'exceptions.ValueError' has no attribute 'missing'"),
        )
        for function, message in cases:
            with self.assertRaises(AttributeError) as caught:
                function()
            self.assertEqual(str(caught.exception), message)

    def test_builtin_types_reject_attribute_assignment(self):
        for kind, name in ((list, 'list'), (dict, 'dict'), (ValueError, 'exceptions.ValueError'), (type(len), 'builtin_function_or_method')):
            with self.assertRaises(TypeError) as caught:
                kind.extra = 1
            self.assertEqual(str(caught.exception), "can't set attributes of built-in/extension type '%s'" % name)
        class Owner(object):
            pass
        with self.assertRaises(TypeError) as caught:
            del Owner.__module__
        self.assertEqual(str(caught.exception), "can't delete Owner.__module__")

    def test_exception_keyword_and_text_details(self):
        class Plain(ValueError):
            pass
        cases = (
            (lambda: ValueError(x=1), 'exceptions.ValueError does not take keyword arguments'),
            (lambda: Plain(x=1), 'Plain does not take keyword arguments'),
            (lambda: Exception.__init__(Plain(), x=1), 'Plain does not take keyword arguments'),
            (lambda: BaseException.__str__(1), "descriptor '__str__' requires a 'exceptions.BaseException' object but received a 'int'"),
        )
        for function, message in cases:
            with self.assertRaises(TypeError) as caught:
                function()
            self.assertEqual(str(caught.exception), message)
        self.assertEqual(repr(BaseException.__dict__['args']), "<attribute 'args' of 'exceptions.BaseException' objects>")
        self.assertEqual(str(EnvironmentError(None, 'x')), '[Errno None] x')
        self.assertEqual(str(EnvironmentError(None, None, None)), '[Errno None] None: None')
        self.assertEqual(str(EnvironmentError(1)), '1')
        error = EnvironmentError()
        error.errno = 1
        error.strerror = 'set'
        self.assertEqual(str(error), '[Errno 1] set')
        self.assertEqual(str(SyntaxError('m', ('a/b.py', 3, 1, 't'))), 'm (b.py, line 3)')
        self.assertEqual(str(SyntaxError('m', ('a/b.py', None, 1, 't'))), 'm (b.py)')
        self.assertEqual(str(SyntaxError('m', (None, True, 1, 't'))), 'm (line 1)')
        self.assertEqual(str(SyntaxError()), 'None')

    def test_classic_repr_hash_and_iteration_use_getattr(self):
        calls = []
        class Dynamic:
            def __getattr__(self, name):
                calls.append(name)
                if name == '__str__':
                    return lambda: 'dynamic'
                raise AttributeError(name)
        instance = Dynamic()
        self.assertEqual(str(instance), 'dynamic')
        self.assertTrue(repr(instance).startswith('<'))
        self.assertTrue(hash(instance) != -1)
        with self.assertRaises(TypeError) as caught:
            iter(instance)
        self.assertEqual(str(caught.exception), 'iteration over non-sequence')
        self.assertEqual(calls, ['__str__', '__repr__', '__hash__', '__eq__', '__cmp__', '__iter__', '__getitem__'])
        class Failing:
            def __getattr__(self, name):
                raise ValueError(name)
        for function in (repr, str, hash, iter):
            with self.assertRaises(ValueError):
                function(Failing())

    def test_classic_index_and_item_protocols_use_getattr(self):
        class Index:
            def __getattr__(self, name):
                if name == '__index__':
                    return lambda: 1
                raise AttributeError(name)
        self.assertEqual([1, 2, 3][Index()], 2)
        self.assertEqual([1, 2, 3][Index():], [2, 3])
        self.assertEqual('abc'[Index()], 'b')
        self.assertEqual(xrange(5)[Index()], 1)
        class Empty:
            pass
        instance = Empty()
        def assign():
            instance['key'] = 1
        def delete():
            del instance[0:1]
        for function, name in ((lambda: instance['key'], '__getitem__'), (lambda: instance[0:1], '__getitem__'), (assign, '__setitem__'), (delete, '__delitem__')):
            with self.assertRaises(AttributeError) as caught:
                function()
            self.assertEqual(str(caught.exception), "Empty instance has no attribute '%s'" % name)
        class Failing:
            def __getattr__(self, name):
                raise ValueError(name)
        with self.assertRaises(ValueError) as caught:
            [1, 2][Failing()]
        self.assertEqual(str(caught.exception), '__index__')
        with self.assertRaises(ValueError) as caught:
            Failing()[0]
        self.assertEqual(str(caught.exception), '__getitem__')

    def test_code_names_are_copied_to_exact_strings(self):
        class Name(str):
            def __eq__(self, other):
                raise AssertionError('subclass comparison')
            def __hash__(self):
                return str.__hash__(self)
        def read():
            return value
        code = read.func_code
        names = tuple(Name(name) for name in code.co_names)
        copied = type(code)(code.co_argcount, code.co_nlocals, code.co_stacksize, code.co_flags, code.co_code, code.co_consts, names, code.co_varnames, code.co_filename, code.co_name, code.co_firstlineno, code.co_lnotab)
        self.assertEqual([type(name) for name in copied.co_names], [str])
        self.assertIs(type(names[0]), Name)
        self.assertEqual(type(read)(copied, {'value': 41})(), 41)
        with self.assertRaises(TypeError) as caught:
            type(code)(0, 0, 1, 0, code.co_code, (None,), (5,), (), 'f', 'g', 1, '')
        self.assertEqual(str(caught.exception), "name tuples must contain only strings, not 'int'")

    def test_dead_proxy_binary_operation_reports_one_error(self):
        import _weakref
        class Target(object):
            pass
        left = Target()
        right = Target()
        left_proxy = _weakref.proxy(left)
        right_proxy = _weakref.proxy(right)
        del left, right
        for operation in (lambda: left_proxy + right_proxy, lambda: left_proxy ** right_proxy, lambda: left_proxy.__sub__(right_proxy)):
            self.assertRaises(ReferenceError, operation)

    def test_finalizer_weak_references_and_deep_chains(self):
        import _weakref
        references = []
        callbacks = []
        class Late(object):
            def __del__(self):
                references.append(_weakref.ref(self, callbacks.append))
        class ClassicLate:
            def __del__(self):
                references.append(_weakref.ref(self, callbacks.append))
        value = Late()
        del value
        value = ClassicLate()
        del value
        self.assertEqual([reference() for reference in references], [None, None])
        self.assertEqual(callbacks, [])
        events = []
        class Node(object):
            def __init__(self, index, child):
                self.index = index
                self.child = child
            def __del__(self):
                events.append(self.index)
        chain = None
        for index in xrange(200):
            chain = Node(199 - index, chain)
        del chain
        self.assertEqual(events, range(200))

    def test_instance_dictionary_deletion_and_metaclass_layout(self):
        class Plain(object):
            pass
        instance = Plain()
        instance.value = 1
        with self.assertRaises(TypeError) as caught:
            instance.__dict__ = 5
        self.assertEqual(str(caught.exception), "__dict__ must be set to a dictionary, not a 'int'")
        del instance.__dict__
        self.assertEqual(instance.__dict__, {})
        instance.other = 2
        self.assertEqual(instance.other, 2)
        class Meta(type):
            pass
        self.assertNotIn('__dict__', Meta.__dict__)

    def test_builtin_class_methods_bind_as_builtin_methods(self):
        class Mapping(dict):
            pass
        for method, owner in ((float.fromhex, float), (dict.fromkeys, dict), (Mapping.fromkeys, Mapping)):
            self.assertIs(type(method), type(len))
            self.assertIs(method.__self__, owner)
            self.assertRaises(AttributeError, getattr, method, '__func__')
        self.assertIs(type(Mapping.fromkeys('a')), Mapping)
        self.assertTrue(repr(float.fromhex).startswith('<built-in method fromhex of type object at 0x'))
        with self.assertRaises(TypeError) as caught:
            dict.__dict__['fromkeys'].__get__(None, int)
        self.assertEqual(str(caught.exception), "descriptor 'fromkeys' for type 'dict' doesn't apply to type 'int'")
        class Holder(object):
            bound = classmethod(len)
        self.assertEqual(type(Holder.bound).__name__, 'instancemethod')

    def test_classic_truth_comparison_and_containment_use_getattr(self):
        calls = []
        class Dynamic:
            def __getattr__(self, name):
                calls.append(name)
                if name == '__nonzero__':
                    return lambda: False
                raise AttributeError(name)
        instance = Dynamic()
        self.assertFalse(instance)
        with self.assertRaises(TypeError) as caught:
            1 in instance
        self.assertEqual(str(caught.exception), "argument of type 'instance' is not iterable")
        self.assertTrue(unicode(instance).startswith(u'<'))
        self.assertEqual(calls, ['__nonzero__', '__contains__', '__iter__', '__getitem__', '__unicode__', '__str__', '__repr__'])
        class Failing:
            def __getattr__(self, name):
                raise ValueError(name)
        for function in (bool, lambda value: value == 1, lambda value: value < 1, lambda value: 1 in value):
            self.assertRaises(ValueError, function, Failing())
        class Coerce:
            def __getattr__(self, name):
                if name == '__coerce__':
                    raise ValueError('coerce lookup')
                raise AttributeError(name)
        with self.assertRaises(ValueError) as caught:
            cmp(Coerce(), Coerce())
        self.assertEqual(str(caught.exception), 'coerce lookup')

    def test_builtin_argument_messages(self):
        class Empty(object):
            pass
        class Classic:
            pass
        cases = (
            (lambda: len(x=1), TypeError, 'len() takes no keyword arguments'),
            (lambda: len(Empty()), TypeError, "object of type 'Empty' has no len()"),
            (lambda: len(Classic()), AttributeError, "Classic instance has no attribute '__len__'"),
            (lambda: setattr(Empty, 3, 4), TypeError, "attribute name must be string, not 'int'"),
            (lambda: delattr(Empty, 3), TypeError, "attribute name must be string, not 'int'"),
            (lambda: hasattr(Empty, 3), TypeError, 'hasattr(): attribute name must be string'),
        )
        for function, kind, message in cases:
            with self.assertRaises(kind) as caught:
                function()
            self.assertEqual(str(caught.exception), message)
        nested = int
        for index in xrange(3000):
            nested = (nested,)
        for function, message in ((isinstance, 'in __instancecheck__'), (issubclass, 'in __subclasscheck__')):
            with self.assertRaises(RuntimeError) as caught:
                function(int, nested)
            self.assertEqual(str(caught.exception), 'maximum recursion depth exceeded ' + message)

    def test_object_attribute_wrappers_check_their_receiver(self):
        class Owner(object):
            pass
        class Classic:
            pass
        cases = (
            (lambda: object.__setattr__(Owner, 'x', 1), "can't apply this __setattr__ to type object"),
            (lambda: object.__delattr__(Owner, 'x'), "can't apply this __delattr__ to type object"),
            (lambda: object.__setattr__(Classic(), 'x', 1), "can't apply this __setattr__ to instance object"),
            (lambda: object.__setattr__(Classic, 'x', 1), "can't apply this __setattr__ to classobj object"),
        )
        for function, message in cases:
            with self.assertRaises(TypeError) as caught:
                function()
            self.assertEqual(str(caught.exception), message)
        type.__setattr__(Owner, 'x', 1)
        self.assertEqual(Owner.x, 1)
        type.__delattr__(Owner, 'x')
        self.assertFalse(hasattr(Owner, 'x'))

    def test_object_new_requires_an_object_static_base(self):
        class Failure(ValueError):
            pass
        cases = (
            (ValueError, 'object.__new__(exceptions.ValueError) is not safe, use exceptions.ValueError.__new__()'),
            (Failure, 'object.__new__(Failure) is not safe, use exceptions.ValueError.__new__()'),
        )
        for kind, message in cases:
            with self.assertRaises(TypeError) as caught:
                object.__new__(kind)
            self.assertEqual(str(caught.exception), message)
        class Plain(object):
            pass
        self.assertIs(type(object.__new__(Plain)), Plain)
