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
