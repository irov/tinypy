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
