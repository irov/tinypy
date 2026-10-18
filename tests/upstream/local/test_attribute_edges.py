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

    def test_classic_instance_dictionary_matches_equal_keys(self):
        class Old:
            shared = 'class'
            def method(self):
                return 'method'
        instance = Old()
        instance.__dict__[''.join(['sha', 'red'])] = 'instance'
        self.assertEqual(instance.shared, 'instance')
        class Key(str):
            pass
        other = Old()
        other.__dict__[Key('shared')] = 'key'
        other.__dict__[Key('method')] = lambda: 'instance method'
        other.__dict__['__class__'] = 'ignored'
        self.assertEqual((other.shared, other.method()), ('key', 'instance method'))
        self.assertIs(other.__class__, Old)
        self.assertEqual((Old().shared, Old().method()), ('class', 'method'))
        try:
            Old().missing
        except AttributeError as error:
            self.assertEqual(str(error), "Old instance has no attribute 'missing'")
        else:
            self.fail('no AttributeError')

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

    def test_metaclass_entries_shadowing_type_metadata_use_the_namespace(self):
        class Meta(type):
            pass
        class Shadowed(object):
            __metaclass__ = Meta
            'shadowed doc'
        Shadowed.__doc__ = 'assigned doc'
        self.assertEqual((Shadowed.__dict__['__doc__'], Shadowed.__doc__, Shadowed().__doc__),
                         ('assigned doc', 'assigned doc', 'assigned doc'))
        del Shadowed.__doc__
        self.assertEqual(Shadowed.__doc__, object.__doc__)
        del Shadowed.__module__
        self.assertEqual(Shadowed.__module__, Meta.__module__)
        with self.assertRaises(AttributeError) as caught:
            del Shadowed.__module__
        self.assertEqual(caught.exception.args, ('__module__',))
        class FlagsMeta(type):
            __flags__ = 'meta flags'
        class Flagged(object):
            __metaclass__ = FlagsMeta
        Flagged.__flags__ = 2
        self.assertEqual((Flagged.__flags__, Flagged.__dict__['__flags__']), (2, 2))
        with self.assertRaises(TypeError) as caught:
            Meta.__flags__ = 2
        self.assertEqual(caught.exception.args, ('readonly attribute',))
        sys.exc_clear()

    def test_partial_subclass_overrides_its_fields(self):
        from _functools import partial
        class Overriding(partial):
            @property
            def args(self):
                return 'own args'
            func = property(lambda self: 'own func')
            keywords = 'own keywords'
        class Plain(partial):
            pass
        value = Overriding(len, [1, 2])
        self.assertEqual((value.args, value.func, value.keywords, getattr(value, 'args'), value()),
                         ('own args', 'own func', 'own keywords', 'own args', 2))
        plain = Plain(len, [1], key=3)
        self.assertEqual((plain.func, plain.args, plain.keywords), (len, ([1],), {'key': 3}))

    def test_object_getattribute_is_generic_on_classes(self):
        class Base(object):
            inherited = 'inherited'
        class Meta(type):
            meta = 'meta'
        class Class(Base):
            __metaclass__ = Meta
            own = staticmethod(len)
            def method(self):
                pass
        class Old:
            member = 1
        get = object.__getattribute__
        self.assertIs(get(Class, 'own'), Class.__dict__['own'])
        self.assertIs(get(Class, 'method'), Class.__dict__['method'])
        self.assertEqual((get(Class, 'meta'), get(Class, u'meta'), get(Class, '__name__')), ('meta', 'meta', 'Class'))
        self.assertIs(get(Class, '__class__'), Meta)
        self.assertIs(get(Old, '__class__'), type(Old))
        self.assertEqual(type.__getattribute__(Class, 'inherited'), 'inherited')
        self.assertEqual(type(Old).__getattribute__(Old, 'member'), 1)
        examples = ((Class, 'inherited', "'Meta' object has no attribute 'inherited'"),
                    (Class, 'missing', "'Meta' object has no attribute 'missing'"),
                    (Old, 'member', "'classobj' object has no attribute 'member'"),
                    (Old(), 'member', "'instance' object has no attribute 'member'"))
        for target, name, message in examples:
            with self.assertRaises(AttributeError) as caught:
                get(target, name)
            self.assertEqual(caught.exception.args, (message,))
        sys.exc_clear()

    def test_classic_instance_getattribute_uses_the_class_and_hook(self):
        class Old:
            member = 1
            def __getattr__(self, name):
                return 'hook ' + name
        instance = Old()
        get = type(instance).__getattribute__
        self.assertEqual((get(instance, 'member'), get(instance, 'missing')), (1, 'hook missing'))
        self.assertIs(get(instance, '__class__'), Old)
        self.assertIs(object.__getattribute__(instance, '__class__'), type(instance))

    def test_methods_and_super_objects_under_object_getattribute(self):
        class Base(object):
            member = 'base'
        class Class(Base):
            def method(self):
                pass
        bound = Class().method
        self.assertIs(object.__getattribute__(bound, 'im_func'), Class.__dict__['method'])
        self.assertEqual(type(bound).__getattribute__(bound, 'func_name'), 'method')
        delegating = super(Class, Class())
        self.assertEqual(super.__getattribute__(delegating, 'member'), 'base')
        self.assertIs(object.__getattribute__(delegating, '__thisclass__'), Class)
        for target, name in ((bound, 'func_name'), (delegating, 'member')):
            with self.assertRaises(AttributeError):
                object.__getattribute__(target, name)
        sys.exc_clear()

    def test_getattr_hook_is_fetched_before_getattribute_runs(self):
        class Adding(object):
            def __getattribute__(self, name):
                if name == 'missing' and '__getattr__' not in Adding.__dict__:
                    Adding.__getattr__ = lambda self, name: 'added ' + name
                return object.__getattribute__(self, name)
        with self.assertRaises(AttributeError):
            Adding().missing
        self.assertEqual(Adding().missing, 'added missing')
        class Removing(object):
            def __getattr__(self, name):
                return 'removed ' + name
            def __getattribute__(self, name):
                if name == 'missing':
                    del Removing.__getattr__
                return object.__getattribute__(self, name)
        self.assertEqual(Removing().missing, 'removed missing')
        sys.exc_clear()

    def test_getattr_hook_follows_repeated_lookups(self):
        class Descriptor(object):
            def __init__(self, error):
                self.error = error
            def __get__(self, instance, owner):
                raise self.error
        class Hooked(object):
            data = 'class'
            failing = Descriptor(AttributeError('descriptor'))
            broken = Descriptor(KeyError('descriptor'))
            def __init__(self):
                self.member = 'instance'
            def method(self):
                return 'method'
            @property
            def failing_property(self):
                raise AttributeError('property')
            def __getattr__(self, name):
                if name == 'raising':
                    raise AttributeError('hook ' + name)
                if name == 'callable':
                    return lambda value: 'called ' + value
                return 'hook ' + name
        class Slotted(Hooked):
            __slots__ = ('slot',)
        class Static(object):
            __getattr__ = staticmethod(lambda name: 'static ' + name)
        class Old:
            data = 'old class'
            def __getattr__(self, name):
                return 'old ' + name
        def read(instance):
            values = [instance.member, instance.data, instance.method(), instance.failing, instance.failing_property, instance.absent, instance.callable('value')]
            try:
                instance.broken
            except KeyError as error:
                values.append(error.args)
            try:
                instance.raising
            except AttributeError as error:
                values.append(error.args)
            return values
        expected = ['instance', 'class', 'method', 'hook failing', 'hook failing_property', 'hook absent', 'called value', ('descriptor',), ('hook raising',)]
        for _ in range(3):
            self.assertEqual(read(Hooked()), expected)
            slotted = Slotted()
            self.assertEqual((read(slotted), slotted.slot), (expected, 'hook slot'))
            slotted.slot = 'slot'
            self.assertEqual(slotted.slot, 'slot')
            self.assertEqual((Static().absent, Old().data, Old().absent), ('static absent', 'old class', 'old absent'))
        class Lazy(object):
            def __init__(self):
                self.calls = []
            def __getattr__(self, name):
                self.calls.append(name)
                setattr(self, name, name.upper())
                return name.upper()
        lazy = Lazy()
        self.assertEqual(([lazy.first for _ in range(3)], [lazy.second for _ in range(2)], lazy.calls), (['FIRST'] * 3, ['SECOND'] * 2, ['first', 'second']))
        class Recursive(object):
            def __getattr__(self, name):
                return self.other
        with self.assertRaises(RuntimeError):
            Recursive().value
        sys.exc_clear()

    def test_getattr_hook_changes_take_effect_on_repeated_lookups(self):
        def absent(instance):
            try:
                return instance.absent
            except AttributeError as error:
                return error.args
        class Base(object):
            pass
        class Derived(Base):
            pass
        derived = Derived()
        results = [absent(derived)]
        Base.__getattr__ = lambda self, name: 'base ' + name
        results.append(absent(derived))
        Derived.__getattr__ = lambda self, name: 'derived ' + name
        results.append(absent(derived))
        del Derived.__getattr__
        results.append(absent(derived))
        del Base.__getattr__
        results.append(absent(derived))
        missing = ("'Derived' object has no attribute 'absent'",)
        self.assertEqual(results, [missing, 'base absent', 'derived absent', 'base absent', missing])
        class Hooked(object):
            def __getattr__(self, name):
                return 'hooked ' + name
        class Plain(object):
            pass
        instance = Plain()
        results = []
        for cls in (Plain, Hooked, Plain, Hooked):
            instance.__class__ = cls
            results.append(absent(instance))
        missing = ("'Plain' object has no attribute 'absent'",)
        self.assertEqual(results, [missing, 'hooked absent', missing, 'hooked absent'])
        class Vanishing(object):
            @property
            def absent(self):
                del Vanishing.absent
                raise AttributeError('vanishing')
            def __getattr__(self, name):
                return 'vanishing ' + name
        vanishing = Vanishing()
        self.assertEqual((absent(vanishing), absent(vanishing)), ('vanishing absent', 'vanishing absent'))
        sys.exc_clear()

    def test_iteration_falls_back_to_getitem_when_iter_fails_to_bind(self):
        class Failing(object):
            @property
            def __iter__(self):
                raise KeyError('iter')
        class Sequence(Failing):
            def __getitem__(self, index):
                if index < 2:
                    return index
                raise IndexError(index)
        self.assertEqual(list(Sequence()), [0, 1])
        with self.assertRaises(TypeError) as caught:
            iter(Failing())
        self.assertEqual(caught.exception.args, ("'Failing' object is not iterable",))
        class Raising(object):
            def __init__(self, error):
                self.error = error
            def __get__(self, instance, owner):
                raise self.error
        class Old:
            __iter__ = Raising(AttributeError('iter'))
            def __getitem__(self, index):
                if index < 1:
                    return index
                raise IndexError(index)
        self.assertEqual(list(Old()), [0])
        Old.__getitem__ = Raising(KeyError('getitem'))
        with self.assertRaises(TypeError) as caught:
            iter(Old())
        self.assertEqual(caught.exception.args, ('iteration over non-sequence',))
        Old.__iter__ = Raising(KeyError('iter'))
        with self.assertRaises(KeyError):
            iter(Old())
        sys.exc_clear()

    def test_finalizer_lookup_errors_are_not_reported(self):
        class Stream(object):
            def __init__(self):
                self.parts = []
            def write(self, text):
                self.parts.append(text)
        class Failing(object):
            @property
            def __del__(self):
                raise KeyError('lookup')
        class Descriptor(object):
            def __get__(self, instance, owner):
                raise KeyError('classic lookup')
        class Old:
            __del__ = Descriptor()
        class Raising(object):
            def __del__(self):
                raise ValueError('call')
        stream = Stream()
        saved = sys.stderr
        sys.stderr = stream
        try:
            Failing()
            Old()
            self.assertEqual(stream.parts, [])
            Raising()
        finally:
            sys.stderr = saved
        self.assertIn('ValueError', ''.join(stream.parts))

    def test_type_and_object_method_arity_messages(self):
        class Class(object):
            pass
        instance = Class()
        examples = ((lambda: Class.mro(1), 'mro() takes no arguments (1 given)'),
                    (lambda: Class.__subclasses__(1), '__subclasses__() takes no arguments (1 given)'),
                    (lambda: Class.__instancecheck__(), '__instancecheck__() takes exactly one argument (0 given)'),
                    (lambda: Class.__subclasscheck__(1, 2), '__subclasscheck__() takes exactly one argument (2 given)'),
                    (lambda: Class.__subclasscheck__(key=1), '__subclasscheck__() takes no keyword arguments'),
                    (lambda: object.__setattr__(instance, 'name'), ' expected 2 arguments, got 1'),
                    (lambda: type.__setattr__(Class, 'name'), ' expected 2 arguments, got 1'),
                    (lambda: object.__delattr__(instance), 'expected 1 arguments, got 0'),
                    (lambda: object.__getattribute__(instance), 'expected 1 arguments, got 0'),
                    (lambda: object.__setattr__(instance, 'name', 1, key=2), 'wrapper __setattr__ doesn\'t take keyword arguments'))
        for call, message in examples:
            with self.assertRaises(TypeError) as caught:
                call()
            self.assertEqual(caught.exception.args, (message,))
        sys.exc_clear()
