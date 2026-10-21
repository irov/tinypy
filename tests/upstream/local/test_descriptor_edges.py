"""Project-authored descriptor state, optional argument and weakref tests."""

import _weakref as weakref
import sys
import unittest


def read(instance):
    """original getter"""
    return instance


def write(instance, value):
    return None


def remove(instance):
    return None


class DescriptorEdges(unittest.TestCase):
    def test_property_copy_none_preserves_each_accessor(self):
        original = property(read, write, remove, 'explicit')
        for method in ('getter', 'setter', 'deleter'):
            copied = getattr(original, method)(None)
            self.assertIsNot(copied, original)
            self.assertIs(copied.fget, read)
            self.assertIs(copied.fset, write)
            self.assertIs(copied.fdel, remove)
            self.assertEqual(copied.__doc__, 'explicit')

    def test_property_subclass_derived_doc_uses_instance_dictionary(self):
        class Field(property):
            pass
        value = Field(read)
        self.assertEqual(value.__doc__, 'original getter')
        self.assertIs(property.__dict__['__doc__'].__get__(value, Field), None)
        copied = value.setter(write)
        self.assertIs(type(copied), Field)
        self.assertEqual(copied.__doc__, 'original getter')
        self.assertIs(copied.fset, write)

    def test_property_suppresses_exception_from_getter_doc(self):
        class Getter(object):
            def __getattribute__(self, name):
                if name == '__doc__':
                    raise KeyError('getter doc')
                return object.__getattribute__(self, name)
        getter = Getter()
        value = property(getter)
        self.assertIs(value.fget, getter)
        self.assertIs(value.__doc__, None)
        self.assertEqual(property(read).__doc__, 'original getter')

    def test_property_base_exception_leaves_updated_fields(self):
        class Getter(object):
            def __getattribute__(self, name):
                if name == '__doc__':
                    raise KeyboardInterrupt('getter doc')
                return object.__getattribute__(self, name)
        getter = Getter()
        value = property(read, doc='old')
        with self.assertRaises(KeyboardInterrupt) as caught:
            value.__init__(getter, write, remove)
        self.assertEqual(caught.exception.args, ('getter doc',))
        self.assertIs(value.fget, getter)
        self.assertIs(value.fset, write)
        self.assertIs(value.fdel, remove)
        self.assertIs(value.__doc__, None)
        value.__init__(read)
        self.assertEqual(value.__doc__, 'original getter')

    def test_property_getter_doc_observes_new_fields(self):
        events = []
        value = property(read, doc='old')
        class Getter(object):
            def __getattribute__(self, name):
                if name == '__doc__':
                    events.append((value.fget is self, value.fset is write,
                                   value.fdel is remove, value.__doc__))
                    return 'new'
                return object.__getattribute__(self, name)
        try:
            getter = Getter()
            value.__init__(getter, write, remove)
            self.assertEqual(events, [(True, True, True, None)])
            self.assertEqual(value.__doc__, 'new')
        finally:
            value.__init__()

    def test_property_doc_callback_reinitializes_same_property(self):
        value = property()
        class Getter(object):
            def __getattribute__(self, name):
                if name == '__doc__':
                    value.__init__(read, doc='inner')
                    return 'outer'
                return object.__getattribute__(self, name)
        try:
            value.__init__(Getter())
            self.assertIs(value.fget, read)
            self.assertEqual(value.__doc__, 'outer')
        finally:
            value.__init__()

    def test_property_explicit_doc_skips_getter_lookup(self):
        class Getter(object):
            def __getattribute__(self, name):
                if name == '__doc__':
                    raise AssertionError('unexpected doc lookup')
                return object.__getattribute__(self, name)
        self.assertEqual(property(Getter(), doc='explicit').__doc__, 'explicit')

    def test_property_invalid_arguments_preserve_old_state(self):
        value = property(read, write, remove, 'old')
        for arguments, keywords in (((None,), {'fget': write}),
                                    ((), {'unknown': 7}),
                                    ((None,) * 5, {})):
            self.assertRaises(TypeError, value.__init__, *arguments, **keywords)
            self.assertIs(value.fget, read)
            self.assertIs(value.fset, write)
            self.assertIs(value.fdel, remove)
            self.assertEqual(value.__doc__, 'old')

    def test_property_keyword_subtype_equality_controls_lookup(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append(other)
                return False
            def __hash__(self):
                return str.__hash__(self)
        value = property(**{Keyword('fget'): read})
        self.assertIs(value.fget, None)
        self.assertEqual(events, ['fget'])

    def test_property_keyword_lookup_suppresses_errors_and_preserves_handler(self):
        events = []
        for error_type in (KeyError, KeyboardInterrupt, SystemExit):
            class Keyword(str):
                def __eq__(self, other):
                    events.append(other)
                    raise error_type('lookup')
                def __hash__(self):
                    return str.__hash__(self)
            try:
                raise ValueError('handled')
            except ValueError as handled:
                value = property(**{Keyword('fget'): read})
                self.assertIs(value.fget, None)
                self.assertIs(value.__doc__, None)
                self.assertIs(sys.exc_info()[1], handled)
            sys.exc_clear()
        self.assertEqual(events, ['fget', 'fget', 'fget'])

    def test_callable_descriptors_reinit_and_argument_errors(self):
        for factory in (staticmethod, classmethod):
            value = factory(read)
            self.assertIs(value.__init__(7), None)
            self.assertEqual(value.__func__, 7)
            self.assertRaises(TypeError, value.__init__)
            self.assertEqual(value.__func__, 7)
            self.assertRaises(TypeError, value.__init__, read, ignored=7)
            self.assertEqual(value.__func__, 7)
            with self.assertRaises(TypeError) as caught:
                value.__init__(ignored=7)
            self.assertEqual(caught.exception.args,
                             (factory.__name__ + ' expected 1 arguments, got 0',))
            self.assertIs(factory.__new__(factory, read, ignored=7).__func__, None)

    def test_weakref_noncallable_callbacks_are_accepted_at_construction(self):
        class Target(object):
            pass
        target = Target()
        for factory in (weakref.ref, weakref.proxy):
            for callback in (1, 'callback', object()):
                value = factory(target, callback)
                self.assertIsNot(value, None)
                del value
        self.assertEqual(weakref.getweakrefcount(target), 0)

    def test_weakref_new_ignores_keywords_subclass_init_receives_them(self):
        class Target(object):
            pass
        class Ref(weakref.ref):
            def __init__(self, target, marker=None):
                self.marker = marker
        target = Target()
        self.assertIs(weakref.ref.__new__(weakref.ref, target, ignored=7)(), target)
        value = Ref(target, marker=7)
        self.assertEqual(value.marker, 7)
        self.assertIs(value(), target)
        self.assertRaises(TypeError, weakref.ref, target, ignored=7)

    def test_weakref_reinit_keeps_original_referent(self):
        class Target(object):
            pass
        first, second = Target(), Target()
        value = weakref.ref(first)
        self.assertIs(value.__init__(second), None)
        self.assertIs(value(), first)
        self.assertRaises(TypeError, value.__init__, second, ignored=7)
        self.assertIs(value(), first)

    def test_weakref_cross_subtype_comparisons_use_reference_identity(self):
        class Target(object):
            def __eq__(self, other):
                return 'equal'
            def __ne__(self, other):
                return 'different'
        class Ref(weakref.ref):
            pass
        target = Target()
        base, child = weakref.ref(target), Ref(target)
        self.assertIs(base == child, False)
        self.assertIs(child == base, False)
        self.assertIs(base != child, True)
        self.assertIs(weakref.ref.__eq__(base, child), NotImplemented)
        self.assertEqual(base == base, 'equal')
        self.assertEqual(child != child, 'different')

    def test_weakref_callbacks_release_after_entire_callback_batch(self):
        events = []
        class Target(object):
            pass
        class Callback(object):
            def __init__(self, name):
                self.name = name
                self.other = None
            def __call__(self, reference):
                events.append((self.name, reference() is None,
                               self.other() is not None))
            def __del__(self):
                events.append(('released', self.name))
        target = Target()
        first, second = Callback('first'), Callback('second')
        first_reference, second_reference = weakref.ref(first), weakref.ref(second)
        first.other, second.other = second_reference, first_reference
        references = [weakref.ref(target, first), weakref.ref(target, second)]
        del first, second, target
        self.assertEqual(events, [('second', True, True), ('first', True, True),
                                  ('released', 'first'), ('released', 'second')])
        self.assertIs(first_reference(), None)
        self.assertIs(second_reference(), None)
        self.assertTrue(all(reference() is None for reference in references))

    def test_weakref_callback_mutation_keeps_pending_callbacks_and_handled_error(self):
        events = []
        class Target(object):
            pass
        references = []
        def first(reference):
            events.append(('first', reference() is None))
        def second(reference):
            events.append(('second', reference() is None))
            references[:] = []
        target = Target()
        references.extend((weakref.ref(target, first), weakref.ref(target, second)))
        try:
            raise ValueError('handled')
        except ValueError as error:
            del target
            self.assertIs(sys.exc_info()[1], error)
        sys.exc_clear()
        self.assertEqual(events, [('second', True), ('first', True)])

    def test_uninitialized_callable_descriptors_raise_when_bound(self):
        class Old:
            pass
        for factory, message in ((staticmethod, 'uninitialized staticmethod object'),
                                 (classmethod, 'uninitialized classmethod object')):
            Old.member = factory.__new__(factory)
            for read in (lambda: Old.member, lambda: Old().member):
                with self.assertRaises(RuntimeError) as caught:
                    read()
                self.assertEqual(caught.exception.args, (message,))
            self.assertFalse(hasattr(Old(), 'member'))
            created = type('Created', (object,), {'__new__': factory.__new__(factory)})
            with self.assertRaises(RuntimeError) as caught:
                created()
            self.assertEqual(caught.exception.args, (message,))
        sys.exc_clear()

    def test_new_is_bound_as_an_attribute_of_the_type(self):
        class Late(object):
            pass
        Late.__new__ = lambda cls: 'late'
        with self.assertRaises(TypeError) as caught:
            Late()
        self.assertEqual(caught.exception.args, ('unbound method <lambda>() must be called with Late instance as first argument (got type instance instead)',))
        class Binder(object):
            def __get__(self, instance, owner):
                return lambda *args: ('bound', instance, owner, args)
        class Described(object):
            __new__ = Binder()
        self.assertEqual(Described(1), ('bound', None, Described, (Described, 1)))
        class Static(staticmethod):
            def __get__(self, instance, owner=None):
                return lambda *args: ('own get', len(args))
        class Overridden(object):
            __new__ = Static(lambda cls: 'raw')
        self.assertEqual(Overridden(2), ('own get', 2))
        sys.exc_clear()

    def test_new_is_looked_up_through_the_metaclass_getattribute(self):
        lookups = []
        class Meta(type):
            def __getattribute__(cls, name):
                if name == '__new__':
                    lookups.append(type.__getattribute__(cls, '__name__'))
                    if lookups[-1] == 'Replaced':
                        return lambda *args: ('replaced', len(args))
                return type.__getattribute__(cls, name)
        class Plain(object):
            __metaclass__ = Meta
        class Own(object):
            __metaclass__ = Meta
            def __new__(cls, *args):
                return object.__new__(cls)
            def __init__(self, *args):
                self.args = args
        class Derived(Own):
            pass
        class Number(int):
            __metaclass__ = Meta
        class OwnNumber(int):
            __metaclass__ = Meta
            def __new__(cls, value):
                return int.__new__(cls, value + 1)
        class Replaced(object):
            __metaclass__ = Meta
            def __new__(cls):
                return 'own'
        class Error(Exception):
            __metaclass__ = Meta
            def __new__(cls, *args):
                return Exception.__new__(cls, *args)
        self.assertIs(type(Plain()), Plain)
        self.assertEqual((Own(1).args, Derived(2).args, Number(3), OwnNumber(4), Replaced(5, 6), Error(7).args),
                         ((1,), (2,), 3, 5, ('replaced', 3), (7,)))
        self.assertEqual(lookups, ['Own', 'Derived', 'OwnNumber', 'Replaced', 'Error'])

    def test_init_descriptor_is_released_before_the_call(self):
        log = []
        class Tracked(object):
            def __init__(self, name):
                self.name = name
            def __del__(self):
                log.append(self.name)
        class Bound(Tracked):
            def __call__(self, *args):
                log.append(('call', args))
        class Dropping(Tracked):
            def __get__(self, instance, owner):
                owner.__init__ = None
                return Bound('bound')
        class Initialized(object):
            pass
        Initialized.__init__ = Dropping('descriptor')
        Initialized(1)
        self.assertEqual(log, ['descriptor', ('call', (1,)), 'bound'])

    def test_classic_lookup_calls_every_descriptor_get(self):
        class Slotted(object):
            __slots__ = ('slot',)
        class Base(object):
            pass
        class Old:
            slot = Slotted.__dict__['slot']
            real = int.__dict__['real']
            upper = str.__dict__['upper']
            unbound = super(Base)
        examples = (('slot', "descriptor 'slot' for 'Slotted' objects doesn't apply to 'instance' object"),
                    ('real', "descriptor 'real' for 'int' objects doesn't apply to 'instance' object"),
                    ('upper', "descriptor 'upper' for 'str' objects doesn't apply to 'instance' object"),
                    ('unbound', 'super(type, obj): obj must be an instance or subtype of type'))
        instance = Old()
        for name, message in examples:
            self.assertIs(getattr(Old, name), Old.__dict__[name])
            with self.assertRaises(TypeError) as caught:
                getattr(instance, name)
            self.assertEqual(caught.exception.args, (message,))
            self.assertFalse(hasattr(instance, name))
        Old.fromkeys = dict.__dict__['fromkeys']
        for target in (Old, instance):
            with self.assertRaises(TypeError) as caught:
                target.fromkeys
            self.assertEqual(caught.exception.args, ("descriptor 'fromkeys' for type 'dict' needs a type, not a 'classobj' as arg 2",))
        sys.exc_clear()

    def test_heap_descriptor_get_is_called_unbound(self):
        class Counter(object):
            def __call__(self, *args):
                return len(args)
        class Static(object):
            __get__ = staticmethod(lambda *args: args)
        class Plain(object):
            __get__ = Counter()
        class Owner(object):
            static = Static()
            plain = Plain()
        class Old:
            static = Static()
            plain = Plain()
        for target in (Owner, Owner(), Old, Old()):
            with self.assertRaises(TypeError) as caught:
                target.static
            self.assertEqual(caught.exception.args, ("'staticmethod' object is not callable",))
            self.assertEqual(target.plain, 3)
        sys.exc_clear()

    def test_weakref_getset_requires_a_weak_list(self):
        class Referable(object):
            pass
        class Text(str):
            pass
        class Mixed(Referable, Text):
            pass
        value = Mixed()
        self.assertEqual((hasattr(value, '__weakref__'), Mixed.__weakrefoffset__), (False, 0))
        with self.assertRaises(AttributeError) as caught:
            value.__weakref__
        self.assertEqual(caught.exception.args, ('This object has no __weakref__',))
        sys.exc_clear()

    def test_class_and_slice_members_are_builtin_descriptors(self):
        class Instance(object):
            pass

        instance = Instance()
        receiver = slice(1, 2, 3)
        kinds = [type(object.__dict__['__class__']).__name__] + [type(slice.__dict__[name]).__name__ for name in ('start', 'stop', 'step')]
        self.assertEqual(kinds, ['getset_descriptor'] + ['member_descriptor'] * 3)
        self.assertEqual([repr(object.__dict__['__class__']), repr(slice.__dict__['step'])], ["<attribute '__class__' of 'object' objects>", "<member 'step' of 'slice' objects>"])
        errors = []
        for operation in (lambda: setattr(receiver, 'start', 0), lambda: delattr(receiver, 'stop'), lambda: delattr(instance, '__class__'), lambda: setattr(instance, '__class__', 1)):
            try:
                operation()
            except TypeError as error:
                errors.append(str(error))
        self.assertEqual(errors, ['readonly attribute', 'readonly attribute', "can't delete __class__ attribute", "__class__ must be set to new-style class, not 'int' object"])
        object.__dict__['__class__'].__set__(instance, type('Other', (Instance,), {}))
        self.assertEqual((instance.__class__.__name__, receiver.start, receiver.step), ('Other', 1, 3))
        sys.exc_clear()

    def test_instancemethod_names_its_parts_like_the_interpreter(self):
        class Owner(object):
            def method(self):
                pass
        Method = type(Owner.method)
        class Callable(object):
            __name__ = 'callable name'
            def __call__(self, *args):
                return args
        class Named(object):
            __name__ = 'named'
        self.assertTrue(repr(Method(Callable(), Owner())).startswith('<bound method ?.callable name of '))
        self.assertTrue(repr(Method(Owner().method, Owner())).startswith('<bound method ?.method of '))
        self.assertEqual(repr(Method(len, None, Named())), '<unbound method named.len>')
        examples = ((Callable(), 'Callable object'), (int, 'type object'), (Owner().method, 'method()'),
                    (str.upper, 'method_descriptor object'), (len, 'len()'), (Method(Callable(), 1), 'Callable()'))
        for function, name in examples:
            with self.assertRaises(TypeError) as caught:
                Method(function, None, int)('text')
            self.assertEqual(caught.exception.args, ('unbound method %s must be called with int instance as first argument (got str instance instead)' % name,))
        items = []
        text = 'text'
        owner = Owner()
        self.assertTrue(Method(items.append, owner) == Method(items.append, owner))
        self.assertFalse(Method(items.append, owner) != Method(items.append, owner))
        self.assertEqual(hash(Method(text.upper, owner)), hash(Method(text.upper, owner)))
        sys.exc_clear()

    def test_instancemethod_repr_asks_the_function_first(self):
        lookups = []
        class Owner(object):
            def __getattr__(self, name):
                lookups.append('owner ' + name)
                raise KeyError('owner')
        class Function(object):
            def __call__(self):
                pass
            def __getattr__(self, name):
                lookups.append('function ' + name)
                raise ValueError('function')
        class Named(object):
            __name__ = 'named'
            def __call__(self):
                pass
        Method = type(Named.__call__)
        self.assertRaises(ValueError, repr, Method(Function(), 1, Owner()))
        self.assertEqual(lookups, ['function __name__'])
        del lookups[:]
        self.assertRaises(KeyError, repr, Method(Named(), 1, Owner()))
        self.assertEqual(lookups, ['owner __name__'])
        class Plain(object):
            __name__ = 'shadowed'
            def method(self):
                pass
        self.assertEqual(repr(Plain.method), '<unbound method Plain.method>')
        self.assertEqual(repr(Method(Named(), None, Plain)), '<unbound method Plain.named>')
        self.assertEqual(repr(Method(Plain.__dict__['method'], 1)), '<bound method ?.method of 1>')
        sys.exc_clear()

    def test_instancemethod_compares_like_instancemethod_compare(self):
        calls = []
        class Value(object):
            def __init__(self, value):
                self.value = value
            def method(self):
                pass
            def other(self):
                pass
            def __eq__(self, other):
                calls.append('eq')
                return False
            def __cmp__(self, other):
                calls.append('cmp')
                return cmp(self.value, other.value)
        first, second, third = Value(1), Value(2), Value(3)
        methods = [third.method, first.method, second.method]
        self.assertEqual([method.im_self.value for method in sorted(methods)], [1, 2, 3])
        self.assertEqual((max(methods).im_self.value, min(methods).im_self.value), (3, 1))
        self.assertEqual((cmp(third.method, first.method), cmp(first.method, Value(1).method)), (1, 0))
        self.assertTrue(first.method < second.method <= third.method)
        del calls[:]
        self.assertTrue(first.method == Value(1).method)
        self.assertFalse(first.method != Value(1).method)
        self.assertEqual(calls, ['cmp', 'cmp'])
        del calls[:]
        self.assertFalse(first.method == first.other)
        self.assertTrue(first.method in [first.method, first.other])
        self.assertEqual(calls, [])
        self.assertEqual(cmp(first.method, first.other), -cmp(first.other, first.method))
        self.assertEqual((cmp(Value.method, first.method), cmp(first.method, Value.method)), (-1, 1))
        self.assertEqual((first.method.__cmp__(second.method), type(first.method).__cmp__(second.method, first.method)), (-1, 1))
        with self.assertRaises(TypeError) as caught:
            first.method.__cmp__(1)
        self.assertEqual(caught.exception.args, ("instancemethod.__cmp__(x,y) requires y to be a 'instancemethod', not a 'int'",))
        class Failing(object):
            def method(self):
                pass
            def __cmp__(self, other):
                raise KeyError('cmp')
        self.assertRaises(KeyError, cmp, Failing().method, Failing().method)
        self.assertRaises(KeyError, lambda: Failing().method == Failing().method)
        sys.exc_clear()

    def test_reprs_show_names_as_c_strings(self):
        def head(text):
            return text.split(' at 0x')[0]
        class Plain(object):
            pass
        class Classic:
            pass
        Plain.__module__ = 'a\x00b'
        Classic.__module__ = 'c\x00d'
        self.assertEqual(repr(Plain), "<class 'a.Plain'>")
        self.assertEqual(head(repr(Plain())), '<a.Plain object')
        self.assertEqual(head(repr(Classic)), '<class c.Classic')
        self.assertEqual(head(repr(Classic())), '<c.Classic instance')
        self.assertEqual(str(Classic), 'c\x00d.Classic')
        Plain.__module__ = '__builtin__\x00b'
        self.assertEqual((repr(Plain), head(repr(Plain()))), ("<class 'Plain'>", '<Plain object'))
        Named = type(Classic)('k\x00z', (), {'__module__': 'm'})
        self.assertEqual((head(repr(Named)), head(repr(Named())), str(Named)), ('<class m.k', '<m.k instance', 'm.k\x00z'))
        def function(self):
            pass
        function.__name__ = 'f\x00g'
        self.assertEqual(head(repr(function)), '<function f')
        class Shown(object):
            def __repr__(self):
                return 'a\x00b'
        Shown.method = function
        self.assertEqual(repr(Shown().method), '<bound method Shown.f of a>')
        module = type(sys)('n\x00o')
        self.assertEqual(repr(module), "<module 'n' (built-in)>")
        module.__file__ = 'f\x00g'
        self.assertEqual(repr(module), "<module 'n' from 'f'>")
        module = type(sys)('x' * 100)
        module.__file__ = 'y' * 400
        self.assertEqual(repr(module), "<module '%s' from '%s'>" % ('x' * 100, 'y' * 400))

    def test_property_copy_methods_take_one_argument(self):
        examples = ((lambda: property().getter(), 'getter() takes exactly one argument (0 given)'),
                    (lambda: property().setter(1, 2), 'setter() takes exactly one argument (2 given)'),
                    (lambda: property().deleter(function=1), 'deleter() takes no keyword arguments'),
                    (lambda: property.getter(1, 2), "descriptor 'getter' requires a 'property' object but received a 'int'"))
        for call, message in examples:
            with self.assertRaises(TypeError) as caught:
                call()
            self.assertEqual(caught.exception.args, (message,))
        sys.exc_clear()
