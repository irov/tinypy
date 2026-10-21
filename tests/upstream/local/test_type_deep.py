"""Project-authored Python 2 type construction and layout regressions."""

import copy_reg
import _weakref
import unittest


class TypeDeep(unittest.TestCase):
    def test_type_new_and_init_query_forms(self):
        for value in (None, 1, 1L, '', u'', [], (), {}):
            self.assertIs(type.__new__(type, value), type(value))
            self.assertIs(type.__init__(type, value), None)
        class Meta(type):
            pass
        self.assertIs(type.__new__(Meta, 1), int)
        self.assertIs(Meta(1), int)

    def test_type_new_named_arguments(self):
        C = type.__new__(type, name='C', bases=(), dict={'marker': 7})
        self.assertEqual(C.__name__, 'C')
        self.assertEqual(C.marker, 7)
        D = type.__new__(type, 'D', bases=(C,), dict={})
        self.assertIs(D.__base__, C)
        self.assertRaises(TypeError, type, name='C', bases=(), dict={})
        self.assertRaises(TypeError, type.__init__, type, 1, extra=1)

    def test_type_constructor_argument_error_order(self):
        with self.assertRaisesRegexp(TypeError, "Argument given by name"):
            type.__new__(type, 'C', (), name='D')
        with self.assertRaisesRegexp(TypeError, "Required argument 'dict'"):
            type.__new__(type, 'C', (), extra=1)
        with self.assertRaisesRegexp(TypeError, "argument 1 must be string, not unicode"):
            type(u'C', (), None)
        with self.assertRaisesRegexp(TypeError, "argument 2 must be tuple, not None"):
            type('C', None, None)

    def test_winning_metaclass_preserves_argument_shape(self):
        events = []
        class Meta(type):
            def __new__(meta, *args, **kwargs):
                events.append(('new', len(args), sorted(kwargs)))
                return type.__new__(meta, *args, **kwargs)
            def __init__(cls, *args, **kwargs):
                events.append(('init', len(args), sorted(kwargs)))
        class Base(object):
            __metaclass__ = Meta
        events[:] = []
        C = type(name='C', bases=(Base,), dict={})
        self.assertIs(type(C), Meta)
        self.assertEqual(events, [
            ('new', 0, ['bases', 'dict', 'name']),
            ('init', 0, ['bases', 'dict', 'name']),
        ])

    def test_winning_metaclass_foreign_result_skips_init(self):
        events = []
        class Meta(type):
            def __new__(meta, name, bases, namespace):
                if name == 'Foreign':
                    events.append('new')
                    return 7
                return type.__new__(meta, name, bases, namespace)
            def __init__(cls, *args):
                events.append('init')
        Base = Meta('Base', (object,), {})
        events[:] = []
        self.assertEqual(type('Foreign', (Base,), {}), 7)
        self.assertEqual(events, ['new'])

    def test_slots_accept_generic_iterables(self):
        for declaration in ({'a': 1, 'z': 2}, iter(('z', 'a')),
                            (name for name in ('z', 'a'))):
            C = type('C', (object,), {'__slots__': declaration})
            self.assertIs(C.__slots__, declaration)
            value = C()
            value.a, value.z = 1, 2
            self.assertEqual((value.a, value.z), (1, 2))
            self.assertFalse(hasattr(value, '__dict__'))

    def test_slots_subtype_iterator_and_length_hint(self):
        events = []
        class Slots(tuple):
            def __iter__(self):
                events.append('iter')
                return iter(('a', 'z'))
            def __len__(self):
                events.append('len')
                return 2
        C = type('C', (object,), {'__slots__': Slots(('stored',))})
        self.assertEqual(events, ['iter', 'len'])
        self.assertIn('a', C.__dict__)
        self.assertIn('z', C.__dict__)
        self.assertNotIn('stored', C.__dict__)

    def test_slots_materialize_before_validating(self):
        events = []
        def declarations():
            events.append('first')
            yield 1
            events.append('second')
            yield 'x'
        self.assertRaises(TypeError, type, 'C', (object,),
                          {'__slots__': declarations()})
        self.assertEqual(events, ['first', 'second'])

    def test_slots_iterator_error_precedes_name_error(self):
        def declarations():
            yield ''
            raise KeyError('iterator')
        self.assertRaises(KeyError, type, 'C', (object,),
                          {'__slots__': declarations()})

    def test_slots_identifier_validation(self):
        for name in ('', '1abc', 'a b', 'a.b', '\xff', 'a\x00b'):
            with self.assertRaisesRegexp(TypeError, '__slots__ must be identifiers'):
                type('C', (object,), {'__slots__': (name,)})
        self.assertRaises(TypeError, type, 'C', (object,), {'__slots__': None})
        self.assertRaises(TypeError, type, 'C', (object,), {'__slots__': (1,)})

    def test_slots_unicode_encoding_precedes_validation(self):
        for declaration in ((1, u'\u00e9'), ('', u'\u00e9'), (u'\ud800',)):
            self.assertRaises(UnicodeEncodeError, type, 'C', (object,),
                              {'__slots__': declaration})
        C = type('C', (object,), {'__slots__': (u'x',)})
        self.assertIs(type(C.__dict__['x'].__name__), str)

    def test_nonempty_variable_layout_slots_rejected_first(self):
        for base in (long, str, tuple):
            for declaration in (('x',), ('__dict__',), ('__weakref__',),
                                (1,), (u'\u00e9',)):
                with self.assertRaisesRegexp(TypeError, 'nonempty __slots__'):
                    type('C', (base,), {'__slots__': declaration})

    def test_metaclass_nonempty_slots_rejected_before_validation(self):
        class EmptyMeta(type):
            __slots__ = ()
        for base in (type, EmptyMeta):
            for declaration in (('extra',), ('__dict__',), ('__weakref__',),
                                (1,), (u'\u00e9',)):
                with self.assertRaisesRegexp(TypeError, 'nonempty __slots__'):
                    type.__new__(type, name='Meta', bases=(base,),
                                 dict={'__slots__': declaration})
        C = EmptyMeta('C', (object,), {})
        C.marker = 7
        self.assertEqual(C.marker, 7)

    def test_duplicate_ordinary_slots_and_private_mangling(self):
        C = type('_C', (object,), {'__slots__': ('x', 'x', '__p')})
        value = C()
        value.x, value._C__p = 1, 2
        self.assertEqual((value.x, value._C__p), (1, 2))
        self.assertEqual(C.__slots__, ('x', 'x', '__p'))
        for declaration in (('__dict__', '__dict__'),
                            ('__weakref__', '__weakref__')):
            self.assertRaises(TypeError, type, 'C', (object,),
                              {'__slots__': declaration})

    def test_namespace_values_shadow_generated_special_descriptors(self):
        C = type('C', (object,), {'__slots__': ('__dict__', '__weakref__'),
                                 '__dict__': 7, '__weakref__': 8})
        value = C()
        value.marker = 9
        self.assertEqual((C.__dict__['__dict__'], C.__dict__['__weakref__']), (7, 8))
        self.assertEqual((value.__dict__, value.__weakref__, value.marker), (7, 8, 9))
        self.assertIs(_weakref.ref(value)(), value)

    def test_namespace_copied_after_slots_callbacks(self):
        namespace = {}
        events = []
        class Slots(object):
            def __iter__(self):
                events.append('iter')
                namespace['marker'] = 7
                return iter(('x',))
            def __len__(self):
                events.append('len')
                namespace['hint_marker'] = 8
                return 1
        namespace['__slots__'] = Slots()
        C = type('C', (object,), namespace)
        try:
            self.assertEqual((C.marker, C.hint_marker), (7, 8))
            self.assertEqual(events, ['iter', 'len'])
            namespace['marker'] = 10
            self.assertEqual(C.marker, 7)
        finally:
            namespace.clear()
            del C.__slots__

    def test_default_weakrefs_match_variable_layout(self):
        for base in (long, str, tuple):
            C = type('C', (base,), {})
            value = C()
            self.assertTrue(hasattr(value, '__dict__'))
            self.assertFalse(hasattr(C, '__weakref__'))
            self.assertRaises(TypeError, _weakref.ref, value)
        for base in (object, int, float, unicode, list, dict):
            C = type('C', (base,), {})
            value = C()
            self.assertIs(_weakref.ref(value)(), value)

    def test_slot_order_and_declaration_container_do_not_change_layout(self):
        A = type('A', (object,), {'__slots__': ('z', 'a')})
        B = type('B', (object,), {'__slots__': ['a', 'z']})
        value = A()
        value.a, value.z = 7, 8
        value.__class__ = B
        self.assertIs(type(value), B)
        self.assertEqual((value.a, value.z), (7, 8))

    def test_changing_slots_attribute_does_not_change_layout(self):
        A = type('A', (object,), {'__slots__': ('z', 'a')})
        B = type('B', (object,), {'__slots__': ('a', 'z')})
        A.__slots__ = ('different',)
        B.__slots__ = ()
        value = A()
        value.a = 7
        value.__class__ = B
        self.assertEqual(value.a, 7)

    def test_slot_sort_protocol_and_exact_descriptor_names(self):
        events = []
        class Name(str):
            def __lt__(self, other):
                events.append((str(self), str(other)))
                return str(self) < str(other)
            def __hash__(self):
                raise AssertionError('slot names are not dictionary keys')
        C = type('C', (object,), {'__slots__': (Name('z'), Name('a'))})
        self.assertEqual(events, [('a', 'z')])
        for name in ('a', 'z'):
            self.assertIs(type(C.__dict__[name].__name__), str)
        self.assertTrue(all(type(name) is str for name in C.__dict__))

    def test_mro_and_nul_errors_follow_slots_callbacks(self):
        events = []
        class X(object):
            pass
        class Y(object):
            pass
        class A(X, Y):
            pass
        class B(Y, X):
            pass
        class Slots(object):
            def __iter__(self):
                events.append('iter')
                return iter(('x',))
            def __len__(self):
                events.append('len')
                return 1
        self.assertRaises(TypeError, type, 'C', (A, B), {'__slots__': Slots()})
        self.assertEqual(events, ['iter', 'len'])
        events[:] = []
        self.assertRaises(ValueError, type, 'C\x00', (object,), {'__slots__': Slots()})
        self.assertEqual(events, ['iter', 'len'])

    def test_bases_tuple_identity_and_subtype_protocol(self):
        events = []
        class Bases(tuple):
            def __iter__(self):
                events.append('iter')
                return tuple.__iter__(self)
            def __len__(self):
                events.append('len')
                return tuple.__len__(self)
        bases = Bases((object,))
        C = type('C', bases, {})
        self.assertIs(C.__bases__, bases)
        self.assertEqual(events, ['iter', 'len'])
        events[:] = []
        self.assertEqual(type.mro(C), [C, object])
        self.assertEqual(events, ['iter', 'len'])

    def test_bases_iteration_error_is_preserved(self):
        class Bases(tuple):
            def __iter__(self):
                raise KeyError('bases iterator')
        self.assertRaises(KeyError, type, 'C', Bases((object,)), {})

    def test_bases_iteration_merges_types_outside_the_bases(self):
        class Added(object):
            pass
        class Bases(tuple):
            def __iter__(self):
                return iter((Added,))
        C = type('C', Bases((object,)), {})
        self.assertEqual(C.__bases__, (object,))
        self.assertEqual(C.__mro__, (C, object, Added))

    def test_duplicate_bases_name_the_first_by_class_name(self):
        class A(object):
            pass
        class B(object):
            pass
        with self.assertRaises(TypeError) as caught:
            type('C', (A, B, B, A), {})
        self.assertEqual(str(caught.exception), 'duplicate base class A')
        class Named(object):
            def __init__(self, name):
                self.__name__ = name
        class Text(str):
            pass
        class Unnamed(object):
            @property
            def __name__(self):
                raise KeyError('name')
            def __repr__(self):
                return 'unnamed'
        class Unrepresented(Unnamed):
            def __repr__(self):
                raise ValueError('repr')
        for base, name in ((Named(12345), '?'), (Named(u'text'), '?'), (Named(Text('text')), 'text'),
                           (Unnamed(), 'unnamed'), (Unrepresented(), '?'), (12, '12')):
            class Bases(tuple):
                def __iter__(self):
                    return iter((base, base))
            with self.assertRaises(TypeError) as caught:
                type('C', Bases((object,)), {})
            self.assertEqual(str(caught.exception), 'duplicate base class ' + name)

    def test_bases_assignment_updates_every_descendant(self):
        class A(object):
            x = 'A'
        class B(object):
            x = 'B'
        class Mid(A):
            pass
        leaves = [type('Leaf%d' % index, (Mid,), {}) for index in range(12)]
        class Diamond(leaves[0], leaves[1]):
            pass
        Mid.__bases__ = (B,)
        self.assertEqual([leaf.x for leaf in leaves], ['B'] * 12)
        self.assertEqual(Diamond.__mro__, (Diamond, leaves[0], leaves[1], Mid, B, object))
        Mid.__bases__ = (A,)
        self.assertEqual((Diamond.x, leaves[11].__mro__), ('A', (leaves[11], Mid, A, object)))

    def test_slotnames_generic_declarations_and_cache(self):
        C = type('C', (object,), {'__slots__': {'a': 1, 'z': 2}})
        self.assertEqual(sorted(copy_reg._slotnames(C)), ['a', 'z'])
        self.assertIs(copy_reg._slotnames(C), C.__slotnames__)
        for declaration in (iter(('a', 'z')), (name for name in ('a', 'z'))):
            C = type('C', (object,), {'__slots__': declaration})
            self.assertEqual(copy_reg._slotnames(C), [])

    def test_slotnames_subclass_iterator_without_length_hint(self):
        events = []
        class Slots(list):
            def __iter__(self):
                events.append('iter')
                return iter(('x', '__p'))
            def __len__(self):
                events.append('len')
                return 2
        C = type('C', (object,), {'__slots__': Slots(['stored'])})
        events[:] = []
        self.assertEqual(copy_reg._slotnames(C), ['x', '_C__p'])
        self.assertEqual(events, ['iter'])

    def test_slotnames_classic_class_without_slots(self):
        class Classic:
            pass
        self.assertEqual(copy_reg._slotnames(Classic), [])
        self.assertIs(copy_reg._slotnames(Classic), Classic.__slotnames__)
        class Declared:
            __slots__ = ('x',)
        self.assertRaises(AttributeError, copy_reg._slotnames, Declared)

    def test_namespace_dict_subtype_hooks_are_bypassed(self):
        class Namespace(dict):
            def __iter__(self):
                raise AssertionError('iter')
            def keys(self):
                raise AssertionError('keys')
            def __getitem__(self, key):
                raise AssertionError('getitem')
        C = type('C', (object,), Namespace(marker=7))
        self.assertEqual(C.marker, 7)

    def test_hash_of_nested_tuple_subclass_instances(self):
        class Record(tuple):
            pass
        class Shown(Record):
            def __repr__(self):
                return 'Shown'
        def nest(depth):
            value = Record()
            for index in range(depth):
                value = (Record if index % 3 else Shown)((value, index))
            return value
        first = nest(100000)
        self.assertEqual(hash(first), hash(nest(100000)))
        self.assertEqual(hash(first), hash(tuple(first)))
        class Hashed(tuple):
            def __hash__(self):
                return 7
        self.assertEqual(hash((Hashed((1,)), 2)), hash((7, 2)))

    def test_unbindable_hash_probes_comparison_quietly(self):
        class Mapping(dict):
            pass
        Mapping.__hash__ = tuple.__repr__
        Mapping.__eq__ = str.__mul__
        try:
            hash(Mapping(a=1))
        except TypeError as error:
            self.assertEqual(str(error), "unhashable type: 'Mapping'")
        else:
            self.fail('Mapping must be unhashable')
        class Plain(object):
            pass
        Plain.__hash__ = tuple.__repr__
        Plain.__eq__ = str.__mul__
        value = Plain()
        self.assertEqual(hash(value), object.__hash__(value))

    def test_type_defines_its_own_hash_slot_wrapper(self):
        class Classic:
            pass
        class Meta(type):
            pass
        Derived = Meta('Derived', (object,), {})
        self.assertIn('__hash__', type.__dict__)
        self.assertIsNot(type.__hash__, object.__hash__)
        self.assertEqual(repr(type.__hash__), "<slot wrapper '__hash__' of 'type' objects>")
        for descriptor in (object.__init__, str.upper, dict.__dict__['fromkeys']):
            self.assertNotIn('__hash__', type(descriptor).__dict__)
            self.assertEqual(hash(descriptor), object.__hash__(descriptor))
        self.assertIn('__hash__', type(object().__init__).__dict__)
        for value, name in ((1, 'int'), ('a', 'str'), (Classic, 'classobj'), (Classic(), 'instance'), (None, 'NoneType')):
            try:
                type.__hash__(value)
            except TypeError as error:
                self.assertEqual(str(error), "descriptor '__hash__' requires a 'type' object but received a '%s'" % name)
            else:
                self.fail(name)
        for value in (int, Derived, type):
            self.assertEqual(type.__hash__(value), hash(value))
            self.assertEqual(type.__hash__(value), object.__hash__(value))
        self.assertEqual({int: 1, Derived: 2}[Derived], 2)

    def test_python_new_keeps_slot_new_once_removed(self):
        log = []
        class Meta(type):
            def __getattribute__(cls, name):
                if name == '__new__':
                    log.append(cls.__name__)
                return type.__getattribute__(cls, name)
        class Base(object):
            __metaclass__ = Meta
            def __new__(cls, *args):
                return object.__new__(cls)
        class Derived(Base):
            pass
        del Base.__new__
        class Later(Base):
            pass
        self.assertEqual([type(Base()), type(Derived()), type(Later())], [Base, Derived, Later])
        self.assertEqual(log, ['Base', 'Derived', 'Later'])
        del log[:]
        class Fresh(object):
            __metaclass__ = Meta
        class FreshChild(Fresh):
            pass
        Fresh()
        self.assertEqual(log, [])
        Fresh.__new__ = staticmethod(lambda cls: object.__new__(cls))
        del Fresh.__new__
        Fresh()
        FreshChild()
        self.assertEqual(log, ['Fresh', 'FreshChild'])
        del log[:]
        class Python(object):
            def __new__(cls, *args):
                return object.__new__(cls)
        class Neutral(object):
            pass
        class Moved(Neutral):
            __metaclass__ = Meta
        Moved.__bases__ = (Python,)
        Moved.__bases__ = (Neutral,)
        Moved()
        self.assertEqual(log, ['Moved'])
        class Failure(Exception):
            __metaclass__ = Meta
            def __new__(cls, *args):
                return Exception.__new__(cls, *args)
        del Failure.__new__
        del log[:]
        self.assertEqual(Failure('message').args, ('message',))
        self.assertEqual(log, ['Failure'])

    def test_builtin_new_keeps_the_inherited_constructor(self):
        log = []
        class Meta(type):
            def __getattribute__(cls, name):
                if name == '__new__':
                    log.append(cls.__name__)
                return type.__getattribute__(cls, name)
        class Mapping(dict):
            __metaclass__ = Meta
        Mapping.__new__ = dict.__new__
        self.assertEqual(Mapping(key=1), {'key': 1})
        class Other(dict):
            __metaclass__ = Meta
        Other.__new__ = list.__new__
        self.assertEqual(Other(key=1), {'key': 1})
        class Plain(object):
            __metaclass__ = Meta
            __new__ = dict.__new__
        self.assertIs(type(Plain()), Plain)
        class Failure(ValueError):
            __metaclass__ = Meta
        Failure.__new__ = KeyError.__new__
        self.assertEqual(Failure('message').args, ('message',))
        self.assertEqual(log, [])
        class Static(dict):
            __new__ = staticmethod(list.__new__)
        self.assertRaises(TypeError, Static)

    def test_builtin_new_entry_is_the_constructor_function(self):
        entry = int.__dict__['__new__']
        self.assertIs(entry, int.__new__)
        self.assertEqual(type(entry).__name__, 'builtin_function_or_method')
        self.assertFalse(hasattr(entry, '__get__'))
        self.assertEqual(entry(int, '5'), 5)
        class Assigned(object):
            pass
        Assigned.__new__ = int.__dict__['__new__']
        class Stored(object):
            __new__ = int.__dict__['__new__']
        self.assertEqual([type(Assigned()), type(Stored())], [Assigned, Stored])
        class Static(object):
            __new__ = staticmethod(int.__new__)
        self.assertRaises(TypeError, Static)

    def test_new_update_holds_a_subclass_its_lookup_releases(self):
        release = [None]
        class Key(str):
            def __hash__(self):
                return hash('__new__')
            def __eq__(self, other):
                pending = release[0]
                release[0] = None
                if pending is not None:
                    pending()
                return False
        class Base(object):
            pass
        holder = [type('Derived', (Base,), {Key('key'): 1})]
        release[0] = holder.pop
        Base.__new__ = staticmethod(lambda cls: object.__new__(cls))
        self.assertEqual((holder, type(Base()).__name__), ([], 'Base'))
        class Python(object):
            def __new__(cls, *args):
                return object.__new__(cls)
        holder.append(type('Derived', (Python,), {Key('key'): 1}))
        release[0] = holder.pop
        del Python.__new__
        self.assertEqual((holder, type(Python()).__name__), ([], 'Python'))

    def test_object_init_takes_arguments_while_slot_new_stays(self):
        class Removed(object):
            def __new__(cls, *args):
                return object.__new__(cls)
            def __init__(self, *args):
                object.__init__(self, *args)
        del Removed.__new__
        self.assertIs(type(Removed(1)), Removed)
        class Plain(object):
            def __init__(self, *args):
                object.__init__(self, *args)
        self.assertRaises(TypeError, Plain, 1)
