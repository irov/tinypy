"""Project-authored attribute callback, descriptor and recovery regressions."""

import sys
import unittest
import _functools
import _sre
import _weakref as weakref


class ObjectCallbackAudit(unittest.TestCase):
    def names(self, events):
        class Name(str):
            def __hash__(self):
                events.append('hash')
                return str.__hash__(self)
            def __eq__(self, other):
                events.append(('eq', str(other)))
                return str.__eq__(self, other)
        return Name

    def test_function_fields_use_subclass_name_protocol(self):
        def sample():
            return 17
        events = []
        Name = self.names(events)
        for name in ('func_code', '__code__', '__name__', '__dict__', 'func_defaults'):
            expected = getattr(sample, name)
            for repeat in xrange(2):
                events[:] = []
                self.assertIs(getattr(sample, Name(name)), expected)
                self.assertEqual(events, ['hash', ('eq', name)])

    def test_generator_fields_use_subclass_name_protocol(self):
        def sample():
            yield 17
        value = sample()
        events = []
        Name = self.names(events)
        try:
            for name in ('gi_code', 'gi_frame', 'gi_running'):
                events[:] = []
                self.assertEqual(type(getattr(value, Name(name))), type(getattr(value, name)))
                self.assertEqual(events, ['hash', ('eq', name)])
        finally:
            value.close()

    def test_type_and_method_fields_use_subclass_name_protocol(self):
        class Owner(object):
            def sample(self):
                return 17
        events = []
        Name = self.names(events)
        for value, names in ((Owner, ('__name__', '__dict__', '__mro__')),
                             (Owner().sample, ('im_func', 'im_self', 'im_class'))):
            for name in names:
                expected = getattr(value, name)
                events[:] = []
                actual = getattr(value, Name(name))
                if value is Owner and name in ('__dict__', '__mro__'):
                    self.assertEqual(actual, expected)
                else:
                    self.assertIs(actual, expected)
                self.assertEqual(events, ['hash', ('eq', name)])

    def test_module_dictionary_uses_subclass_name_protocol(self):
        value = type(sys)('temporary')
        events = []
        Name = self.names(events)
        self.assertIs(getattr(value, Name('__dict__')), value.__dict__)
        self.assertEqual(events, ['hash', ('eq', '__dict__')])

    def test_instance_class_lookup_does_not_duplicate_callbacks(self):
        class Owner(object):
            pass
        events = []
        Name = self.names(events)
        self.assertIs(getattr(Owner(), Name('__class__')), Owner)
        self.assertEqual(events, ['hash', 'hash', ('eq', '__class__')])

    def test_function_data_descriptors_precede_dictionary_shadows(self):
        def sample():
            return 17
        fields = ('func_code', '__name__', '__dict__', 'func_defaults')
        expected = [getattr(sample, name) for name in fields]
        events = []
        Name = self.names(events)
        for name in fields:
            sample.__dict__[name] = 'shadow'
        for name, value in zip(fields, expected):
            events[:] = []
            self.assertIs(getattr(sample, Name(name)), value)
            self.assertEqual(events, ['hash', ('eq', name)])
        sample.__dict__.clear()

    def test_function_nondata_descriptor_allows_dictionary_shadow(self):
        def sample():
            return 17
        sample.__dict__['__get__'] = 29
        self.assertEqual(sample.__get__, 29)
        del sample.__dict__['__get__']
        self.assertTrue(callable(sample.__get__))

    def test_name_equality_controls_field_lookup(self):
        def sample():
            return 17
        class Refused(str):
            def __hash__(self):
                return str.__hash__(self)
            def __eq__(self, other):
                return False
        class Deferred(Refused):
            def __eq__(self, other):
                return NotImplemented
        self.assertEqual(getattr(sample, Refused('func_code'), 'missing'), 'missing')
        self.assertFalse(hasattr(sample, Refused('func_code')))
        self.assertIs(getattr(sample, Deferred('func_code')), sample.func_code)
        self.assertIs(getattr(sample, 'func_code'), sample.func_code)

    def test_lookup_errors_are_suppressed_and_recover(self):
        def sample():
            return 17
        sample.__dict__['func_code'] = 'shadow'
        sample.marker = 29
        for failure in (KeyError, KeyboardInterrupt):
            for mode in ('hash', 'eq'):
                events = []
                class Name(str):
                    def __hash__(self):
                        events.append('hash')
                        if mode == 'hash':
                            raise failure('name hash')
                        return str.__hash__(self)
                    def __eq__(self, other):
                        events.append('eq')
                        raise failure('name equality')
                for name in ('func_code', 'marker', 'absent'):
                    self.assertEqual(getattr(sample, Name(name), 'missing'), 'missing')
                    self.assertTrue(events)
                    self.assertIs(getattr(sample, 'func_code'), sample.func_code)
                    self.assertEqual(sample.marker, 29)
        sample.__dict__.clear()
        sys.exc_clear()

    def test_custom_hooks_keep_original_string_subclass_names(self):
        events = []
        Name = self.names(events)
        name = Name('member')
        class Owner(object):
            def __getattribute__(self, key):
                events.append(('get', key is name))
                return 17
            def __setattr__(self, key, value):
                events.append(('set', key is name, value))
            def __delattr__(self, key):
                events.append(('delete', key is name))
        value = Owner()
        self.assertEqual(getattr(value, name), 17)
        setattr(value, name, 23)
        delattr(value, name)
        self.assertEqual(events, [('get', True), ('set', True, 23), ('delete', True)])

    def test_type_mutation_canonicalizes_after_metaclass_hook(self):
        events = []
        Name = self.names(events)
        name = Name('member')
        class Meta(type):
            def __setattr__(cls, key, value):
                events.append(('set', key is name))
                return type.__setattr__(cls, key, value)
            def __delattr__(cls, key):
                events.append(('delete', key is name))
                return type.__delattr__(cls, key)
        class Owner(object):
            __metaclass__ = Meta
        setattr(Owner, name, 23)
        self.assertEqual(Owner.member, 23)
        self.assertIs(type(Owner.__dict__.keys()[Owner.__dict__.keys().index('member')]), str)
        delattr(Owner, name)
        self.assertEqual(events, [('set', True), ('delete', True)])

    def test_type_metadata_mutation_errors_are_exact(self):
        class Owner(object):
            pass
        expected = (('__name__', TypeError, "can only assign string to Owner.__name__, not 'int'", TypeError, "can't delete Owner.__name__"),
                    ('__bases__', TypeError, 'can only assign tuple to Owner.__bases__, not int', TypeError, "can't delete Owner.__bases__"),
                    ('__doc__', AttributeError, "attribute '__doc__' of 'type' objects is not writable", AttributeError, "attribute '__doc__' of 'type' objects is not writable"),
                    ('__dict__', AttributeError, "attribute '__dict__' of 'type' objects is not writable", AttributeError, "attribute '__dict__' of 'type' objects is not writable"),
                    ('__mro__', TypeError, 'readonly attribute', TypeError, 'readonly attribute'))
        events = []
        Name = self.names(events)
        for name, set_kind, set_message, delete_kind, delete_message in expected:
            with self.assertRaises(set_kind) as caught:
                setattr(Owner, Name(name), 17)
            self.assertEqual(caught.exception.args, (set_message,))
            with self.assertRaises(delete_kind) as caught:
                delattr(Owner, Name(name))
            self.assertEqual(caught.exception.args, (delete_message,))
        self.assertEqual(events, [])
        sys.exc_clear()

    def test_type_name_is_published_before_old_name_finalizer(self):
        events = []
        class Name(str):
            def __del__(self):
                events.append(Owner.__name__)
                Owner.__name__ = 'from-finalizer'
        class Owner(object):
            pass
        Owner.__name__ = Name('old')
        Owner.__name__ = 'new'
        self.assertEqual(events, ['new'])
        self.assertEqual(Owner.__name__, 'from-finalizer')

    def test_call_attributes_bind_wrappers_and_allow_shadows(self):
        def sample(value=17):
            return value
        class Owner(object):
            def sample(self, value=23):
                return value
        values = ((sample, (), 17), (Owner().sample, (), 23),
                  (len, ([1, 2],), 2), (_functools.partial(sample, 29), (), 29))
        for value, arguments, expected in values:
            wrapper = value.__call__
            self.assertIsNot(wrapper, value)
            self.assertIs(wrapper.__self__, value)
            self.assertEqual(wrapper(*arguments), expected)
        sample.__dict__['__call__'] = lambda: 31
        self.assertEqual(sample.__call__(), 31)
        self.assertEqual(sample(), 17)
        value = _functools.partial(sample, 29)
        value.__call__ = lambda: 37
        self.assertEqual(value.__call__(), 37)
        self.assertEqual(value(), 29)
        class ClassicMissing:
            pass
        class ClassicCalled:
            def __call__(self, value=41):
                return value
        with self.assertRaises(AttributeError) as caught:
            getattr(ClassicMissing, '__call__')
        self.assertEqual(caught.exception.args, ("class ClassicMissing has no attribute '__call__'",))
        self.assertIsNot(ClassicCalled.__call__, ClassicCalled)
        self.assertIs(ClassicCalled.__call__.im_self, None)
        self.assertIs(ClassicCalled.__call__.im_class, ClassicCalled)
        self.assertEqual(ClassicCalled.__call__(ClassicCalled()), 41)
        class CallableTarget(object):
            def __call__(self, value=43):
                return value
        target = CallableTarget()
        reference = weakref.ref(target)
        wrapper = reference.__call__
        self.assertIsNot(wrapper, reference)
        self.assertIs(wrapper.__self__, reference)
        self.assertIs(wrapper(), target)
        proxy = weakref.proxy(target)
        self.assertIsNot(proxy.__call__, proxy)
        self.assertIs(proxy.__call__.im_self, target)
        self.assertEqual(proxy.__call__(), 43)
        target.__call__ = lambda: 47
        self.assertEqual(proxy.__call__(), 47)
        self.assertEqual(proxy(), 43)

    def test_invalid_int_result_reports_shared_conversion_error(self):
        class Bad(object):
            def __int__(self):
                return '1'
        class Compared(object):
            def __cmp__(self, other):
                return Bad()
        class Hashed(object):
            def __hash__(self):
                return Bad()
        pattern = _sre.compile('a', 0, [19, 97, 1], 0, {}, [None])
        for callback in (lambda: int('1', Bad()), lambda: long('1', Bad()),
                         lambda: cmp(Compared(), Compared()), lambda: hash(Hashed()),
                         lambda: _sre.getlower(Bad(), 0), lambda: pattern.match('a', Bad())):
            with self.assertRaises(TypeError) as caught:
                callback()
            self.assertEqual(caught.exception.args, ('__int__ method should return an integer',))
        self.assertEqual(int('17'), 17)
        sys.exc_clear()

    def test_translate_error_encoding_descriptor_is_present(self):
        class Child(UnicodeTranslateError):
            pass
        for factory in (UnicodeTranslateError, Child):
            value = factory(u'a', 0, 1, 'bad')
            self.assertTrue(hasattr(value, 'encoding'))
            self.assertIs(value.encoding, None)
            descriptor = UnicodeTranslateError.__dict__['encoding']
            self.assertIs(descriptor.__get__(value, factory), None)
            value.encoding = 'assigned'
            self.assertEqual(value.encoding, 'assigned')
