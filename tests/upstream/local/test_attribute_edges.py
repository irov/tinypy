"""Project-authored Python 2 namespace, directory and class binding tests."""

import sys
import _weakref as weakref
import unittest


Module = type(sys)


class AttributeEdges(unittest.TestCase):
    def test_module_constructor_keyword_fields(self):
        for arguments, keywords in (((), {'name': 'created'}),
                                    ((), {'name': 'created', 'doc': 17}),
                                    (('created',), {'doc': 17})):
            value = Module(*arguments, **keywords)
            self.assertEqual(value.__name__, 'created')
            self.assertEqual(value.__doc__, keywords.get('doc'))
            self.assertEqual(sorted(value.__dict__), ['__doc__', '__name__'])

    def test_module_initializer_preserves_other_fields(self):
        value = Module('old', 'previous')
        value.keep = 23
        namespace = value.__dict__
        self.assertIs(value.__init__(name='new', doc=17), None)
        self.assertIs(value.__dict__, namespace)
        self.assertEqual((value.__name__, value.__doc__, value.keep),
                         ('new', 17, 23))

    def test_module_new_defers_namespace_creation(self):
        class Child(Module):
            pass
        for factory in (Module, Child):
            for arguments, keywords in (((), {}), (('ignored',), {}),
                                        ((), {'unknown': 17})):
                value = Module.__new__(factory, *arguments, **keywords)
                self.assertIs(value.__dict__, None)
                self.assertFalse(hasattr(value, '__name__'))
                self.assertEqual(repr(value), "<module '?' (built-in)>")
                value.member = 23
                self.assertEqual(value.__dict__, {'member': 23})
                self.assertIs(value.__init__('ready'), None)
                self.assertEqual(value.__name__, 'ready')

    def test_module_argument_errors_precede_state_mutation(self):
        value = Module('old', 'old doc')
        examples = (((), {}, "Required argument 'name' (pos 1) not found"),
                    ((), {'doc': 7}, "Required argument 'name' (pos 1) not found"),
                    ((17,), {'unknown': 7}, 'module.__init__() argument 1 must be string, not int'),
                    (('new',), {'name': 'duplicate'}, "Argument given by name ('name') and position (1)"),
                    (('new', 17), {'doc': 7}, 'module.__init__() takes at most 2 arguments (3 given)'))
        for arguments, keywords, message in examples:
            with self.assertRaises(TypeError) as caught:
                value.__init__(*arguments, **keywords)
            self.assertEqual(caught.exception.args, (message,))
            self.assertEqual((value.__name__, value.__doc__), ('old', 'old doc'))
        sys.exc_clear()

    def test_module_name_keeps_string_subclass_identity(self):
        class Name(str):
            pass
        name = Name('created')
        value = Module(name)
        self.assertIs(value.__name__, name)
        with self.assertRaises(TypeError) as caught:
            Module(u'created')
        self.assertEqual(caught.exception.args,
                         ('module.__init__() argument 1 must be string, not unicode',))

    def test_module_release_clears_string_values_and_retains_keys(self):
        value = Module('temporary', 17)
        value.member = 23
        value._private = 29
        namespace = value.__dict__
        namespace['__builtins__'] = 'retained'
        namespace[7] = 'integer key'
        namespace[u'wide'] = 'unicode key'
        del value
        self.assertEqual(namespace,
                         {'__name__': None, '__doc__': None, 'member': None,
                          '_private': None, '__builtins__': 'retained',
                          7: 'integer key', u'wide': 'unicode key'})

    def test_module_release_private_names_precede_other_values(self):
        events = []
        class Carrier(object):
            def __init__(self, label):
                self.label = label
            def __del__(self):
                events.append(self.label)
        value = Module('temporary')
        value.member = Carrier('public')
        value._private = Carrier('private')
        del value
        self.assertEqual(events, ['private', 'public'])

    def test_module_subclass_slots_release_before_namespace(self):
        events = []
        class Carrier(object):
            def __init__(self, label):
                self.label = label
            def __del__(self):
                events.append(self.label)
        class Child(Module):
            __slots__ = ('slot', '__weakref__')
        value = Child('temporary')
        value.slot = Carrier('slot')
        value.member = Carrier('dictionary')
        namespace = value.__dict__
        reference = weakref.ref(value, lambda ignored: events.append('weakref'))
        del value
        self.assertIs(reference(), None)
        self.assertEqual(events, ['weakref', 'slot', 'dictionary'])
        self.assertEqual(namespace['member'], None)

    def test_module_release_preserves_handled_exception(self):
        try:
            raise LookupError('outer')
        except LookupError:
            original = sys.exc_info()
            value = Module('temporary')
            value.member = object()
            del value
            self.assertEqual(sys.exc_info(), original)
        sys.exc_clear()

    def test_module_release_does_not_restore_keys_deleted_by_finalizer(self):
        for trigger, other in (('_a', '_b'), ('a', 'b')):
            value = Module('temporary')
            namespace = value.__dict__
            class Carrier(object):
                def __del__(self):
                    namespace.pop(other, None)
            namespace[other] = 17
            namespace[trigger] = Carrier()
            del value
            self.assertNotIn(other, namespace)
            self.assertIs(namespace[trigger], None)
            namespace.clear()

    def test_vars_reads_dictionary_once(self):
        events = []
        class Source(object):
            def __getattribute__(self, name):
                if name == '__dict__':
                    events.append(name)
                    return {'calls': len(events)}
                return object.__getattribute__(self, name)
        self.assertEqual(vars(Source()), {'calls': 1})
        self.assertEqual(events, ['__dict__'])

    def test_vars_maps_all_lookup_failures_to_type_error(self):
        for failure in (AttributeError, KeyError, KeyboardInterrupt):
            class Source(object):
                @property
                def __dict__(self):
                    raise failure('dictionary callback')
            with self.assertRaises(TypeError) as caught:
                vars(Source())
            self.assertEqual(caught.exception.args,
                             ('vars() argument must have __dict__ attribute',))
        sys.exc_clear()

    def test_vars_returns_arbitrary_attribute_value(self):
        class Source(object):
            @property
            def __dict__(self):
                return 17
        self.assertEqual(vars(Source()), 17)

    def test_unbound_method_checks_reported_class(self):
        events = []
        class Owner(object):
            def method(self):
                return 'called'
        class Source(object):
            @property
            def __class__(self):
                events.append('class')
                return Owner
        self.assertEqual(Owner.method(Source()), 'called')
        self.assertEqual(events, ['class'])

    def test_unbound_method_honors_metaclass_instance_check(self):
        events = []
        class Meta(type):
            def __instancecheck__(cls, value):
                events.append(value)
                return True
        class Owner(object):
            __metaclass__ = Meta
            def method(self):
                return 'called'
        value = object()
        self.assertEqual(Owner.method(value), 'called')
        self.assertEqual(events, [value])

    def test_unbound_method_error_rereads_reported_class(self):
        class Owner(object):
            def method(self):
                return 'called'
        class Old:
            pass
        for reported, expected in ((int, 'int'), (None, '?'), (Old, 'Old'),
                                   (AttributeError, 'Source'),
                                   (KeyError, 'Source'),
                                   (KeyboardInterrupt, 'Source')):
            events = []
            class Source(object):
                @property
                def __class__(self):
                    events.append('class')
                    if reported in (AttributeError, KeyError, KeyboardInterrupt):
                        raise reported('reported class')
                    return reported
            with self.assertRaises(TypeError) as caught:
                Owner.method(Source())
            self.assertEqual(caught.exception.args,
                             ('unbound method method() must be called with '
                              'Owner instance as first argument (got %s '
                              'instance instead)' % expected,))
            self.assertEqual(events, ['class', 'class'])
        self.assertEqual(Owner().method(), 'called')
        sys.exc_clear()

    def test_unbound_method_propagates_instance_check_error(self):
        class Meta(type):
            def __instancecheck__(cls, value):
                raise KeyboardInterrupt('receiver check')
        class Owner(object):
            __metaclass__ = Meta
            def method(self):
                return 'called'
        with self.assertRaises(KeyboardInterrupt) as caught:
            Owner.method(object())
        self.assertEqual(caught.exception.args, ('receiver check',))
        self.assertEqual(Owner().method(), 'called')

    def test_dir_uses_generic_attribute_protocol_in_order(self):
        events = []
        class Reported(object):
            known = 17
        class Source(object):
            def __getattribute__(self, name):
                events.append(name)
                if name == '__dict__':
                    return {'injected': 23}
                if name == '__members__':
                    return ['member', 7, u'unicode-member']
                if name == '__methods__':
                    return ['method']
                if name == '__class__':
                    return Reported
                return object.__getattribute__(self, name)
        names = dir(Source())
        self.assertEqual([name for name in names if name in
                          ('injected', 'known', 'member', 'method', 'unicode-member')],
                         ['injected', 'known', 'member', 'method'])
        self.assertEqual(events, ['__dict__', '__members__', '__methods__', '__class__'])

    def test_dir_class_uses_metaclass_dictionary_and_bases(self):
        events = []
        class Meta(type):
            def __getattribute__(cls, name):
                events.append(name)
                if name == '__dict__':
                    return {'injected': 17}
                if name == '__bases__':
                    return ()
                return type.__getattribute__(cls, name)
        class Source(object):
            __metaclass__ = Meta
            known = 17
        self.assertEqual(dir(Source), ['injected'])
        self.assertEqual(events, ['__dict__', '__bases__'])

    def test_dir_suppresses_optional_attribute_failures(self):
        class Source(object):
            def __getattribute__(self, name):
                raise KeyboardInterrupt(name)
        self.assertEqual(dir(Source()), [])

    def test_dir_keeps_nonstring_dictionary_keys(self):
        class Source(object):
            @property
            def __dict__(self):
                return {7: 17, 'z': 23}
            @property
            def __class__(self):
                raise AttributeError('hidden class')
        self.assertEqual(dir(Source()), [7, 'z'])

    def test_dir_propagates_class_dictionary_conversion_error(self):
        class Reported(object):
            @property
            def __dict__(self):
                return [(17,)]
        class Source(object):
            @property
            def __class__(self):
                return Reported()
        with self.assertRaises(AttributeError):
            dir(Source())
        self.assertIn('__class__', dir(object()))

    def test_dir_custom_method_requires_list_and_ignores_sort_override(self):
        class Result(list):
            def sort(self):
                raise AssertionError('sort override')
        class Source(object):
            def __dir__(self):
                return Result(['z', 'a'])
        self.assertEqual(dir(Source()), ['a', 'z'])
        Source.__dir__ = lambda self: ('a',)
        with self.assertRaises(TypeError) as caught:
            dir(Source())
        self.assertEqual(caught.exception.args,
                         ('__dir__() must return a list, not tuple',))

    def test_dir_classic_bases_rejects_float_length_without_items(self):
        events = []
        class Base(object):
            inherited = 17
        class Bases:
            def __len__(self):
                events.append('len')
                return 1.5
            def __getitem__(self, index):
                events.append(index)
                return Base
        class Reported(object):
            @property
            def __bases__(self):
                return Bases()
        class Source(object):
            @property
            def __class__(self):
                return Reported()
        self.assertNotIn('inherited', dir(Source()))
        self.assertEqual(events, ['len'])

    def test_dir_classic_dynamic_method_lookup_occurs_once(self):
        events = []
        class Source:
            def __getattr__(self, name):
                events.append(name)
                if name == '__dir__':
                    return lambda: ['dynamic']
                raise AttributeError(name)
        self.assertEqual(dir(Source()), ['dynamic'])
        self.assertEqual(events, ['__dir__'])

    def test_function_type_descriptor_survives_dictionary_creation(self):
        class Owner(object):
            def method(self):
                return 17
        value = Owner()
        function = Owner.__dict__['method']
        self.assertIs(vars(function), function.__dict__)
        self.assertEqual(function.__get__(value, Owner)(), 17)
        self.assertIn('im_func', dir(value.method))
        self.assertEqual(function.__get__(value, Owner)(), 17)

    def test_callable_classic_dynamic_attribute_is_not_called(self):
        events = []
        class Source:
            def __getattr__(self, name):
                events.append(name)
                if name == '__call__':
                    return 17
                raise AttributeError(name)
        self.assertTrue(callable(Source()))
        self.assertEqual(events, ['__call__'])

    def test_callable_classic_suppresses_callback_errors(self):
        for failure in (AttributeError, KeyError, KeyboardInterrupt):
            class Source:
                def __getattr__(self, name):
                    raise failure(name)
            self.assertFalse(callable(Source()))
        self.assertTrue(callable(lambda: 17))

    def test_classic_class_constructor_keywords_and_dictionary_identity(self):
        class Old:
            pass
        namespace = {'marker': 17}
        factory = type(Old)
        created = factory(name='Created', bases=(Old,), dict=namespace)
        self.assertEqual(created.__name__, 'Created')
        self.assertIs(created.__dict__, namespace)
        self.assertEqual(created.__bases__, (Old,))
        self.assertEqual(namespace['__doc__'], None)
        self.assertEqual(namespace['__module__'], __name__)
        self.assertEqual(created().marker, 17)
        self.assertEqual(factory.__new__(factory, 'Other', (), {}).__name__, 'Other')

    def test_classic_class_constructor_mutates_defaults_before_bases_error(self):
        class Old:
            pass
        namespace = {}
        with self.assertRaises(TypeError) as caught:
            type(Old)('Created', [], namespace)
        self.assertEqual(caught.exception.args, ('PyClass_New: bases must be a tuple',))
        self.assertEqual(namespace, {'__doc__': None, '__module__': __name__})

    def test_classic_class_constructor_name_identity_and_mixed_base(self):
        class Old:
            pass
        class Name(str):
            pass
        name = Name('Created')
        created = type(Old)(name, (), {})
        self.assertIs(created.__name__, name)
        mixed = type(Old)('Mixed', (Old, object), {'marker': 17})
        self.assertIs(type(mixed), type)
        self.assertEqual(mixed.__bases__, (Old, object))
        self.assertEqual(mixed().marker, 17)
