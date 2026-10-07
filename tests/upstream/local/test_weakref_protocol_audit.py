"""Project-authored weakref argument, ordering and representation contracts."""

import _weakref as weakref
import sys
import unittest


class Target(object):
    pass


class Reference(weakref.ref):
    pass


class WeakrefProtocolAudit(unittest.TestCase):
    def test_constructor_count_precedes_keywords(self):
        with self.assertRaises(TypeError) as error:
            weakref.ref(extra=1)
        self.assertEqual(error.exception.args, ('__new__ expected at least 1 arguments, got 0',))

    def test_constructor_checks_referent_before_keywords(self):
        with self.assertRaises(TypeError) as error:
            weakref.ref(1, extra=1)
        self.assertEqual(error.exception.args, ("cannot create weak reference to 'int' object",))

    def test_constructor_keyword_error_uses_ref_name(self):
        target = Target()
        with self.assertRaises(TypeError) as error:
            weakref.ref(target, extra=1)
        self.assertEqual(error.exception.args, ('ref() does not take keyword arguments',))

    def test_explicit_new_ignores_keywords(self):
        target = Target()
        reference = weakref.ref.__new__(weakref.ref, target, extra=1)
        self.assertIs(reference(), target)

    def test_init_keyword_error_precedes_count(self):
        target = Target()
        reference = weakref.ref(target)
        with self.assertRaises(TypeError) as error:
            reference.__init__(extra=1)
        self.assertEqual(error.exception.args, ('ref() does not take keyword arguments',))

    def test_init_count_uses_named_unpack_diagnostic(self):
        target = Target()
        reference = weakref.ref(target)
        with self.assertRaises(TypeError) as error:
            reference.__init__()
        self.assertEqual(error.exception.args, ('__init__ expected at least 1 arguments, got 0',))

    def test_call_counts_positional_and_keyword_arguments(self):
        target = Target()
        reference = weakref.ref(target)
        for callback in (reference, reference.__call__):
            with self.assertRaises(TypeError) as error:
                callback(1, extra=2)
            self.assertEqual(error.exception.args, ('__call__() takes at most 0 arguments (2 given)',))

    def test_module_single_argument_uses_native_diagnostic(self):
        for name in ('getweakrefs', 'getweakrefcount'):
            with self.assertRaises(TypeError) as error:
                getattr(weakref, name)()
            self.assertEqual(error.exception.args, (name + '() takes exactly one argument (0 given)',))

    def test_module_keyword_error_precedes_count(self):
        with self.assertRaises(TypeError) as error:
            weakref.proxy(extra=1)
        self.assertEqual(error.exception.args, ('proxy() takes no keyword arguments',))

    def test_callbacks_follow_cached_basic_references(self):
        target = Target()
        first = weakref.ref(target, lambda item: None)
        proxy = weakref.proxy(target)
        subclass = Reference(target)
        basic = weakref.ref(target)
        second = weakref.ref(target, lambda item: None)
        references = weakref.getweakrefs(target)
        for actual, expected in zip(references, (basic, proxy, second, subclass, first)):
            self.assertIs(actual, expected)
        self.assertEqual(len(references), 5)

    def test_basic_reference_and_proxy_cache_survive_callbacks(self):
        target = Target()
        reference = weakref.ref(target)
        proxy = weakref.proxy(target)
        extra = [Reference(target), weakref.ref(target, lambda item: None)]
        self.assertIs(weakref.ref(target), reference)
        self.assertIs(weakref.proxy(target), proxy)
        self.assertEqual(weakref.getweakrefcount(target), 4)
        del extra

    def test_proxy_cache_survives_callability_change(self):
        class Changing(object):
            pass
        target = Changing()
        proxy = weakref.proxy(target)
        Changing.__call__ = lambda self: 7
        self.assertIs(weakref.proxy(target), proxy)
        self.assertIs(type(proxy), weakref.ProxyType)
        del Changing.__call__

    def test_callable_proxy_cache_survives_callability_change(self):
        class Changing(object):
            def __call__(self):
                return 7
        target = Changing()
        proxy = weakref.proxy(target)
        del Changing.__call__
        self.assertIs(weakref.proxy(target), proxy)
        self.assertIs(type(proxy), weakref.CallableProxyType)

    def test_ref_repr_includes_name_and_live_identity(self):
        target = Target()
        target.__name__ = 'named'
        reference = weakref.ref(target)
        expected = "<weakref at %s; to 'Target' at %s (named)>" % (hex(id(reference)), hex(id(target)))
        self.assertEqual(repr(reference), expected)
        self.assertEqual(reference.__repr__(), expected)

    def test_ref_repr_ignores_unicode_name_and_truncates_nul(self):
        target = Target()
        reference = weakref.ref(target)
        prefix = "<weakref at %s; to 'Target' at %s" % (hex(id(reference)), hex(id(target)))
        target.__name__ = u'named'
        self.assertEqual(repr(reference), prefix + '>')
        target.__name__ = 'name\x00tail'
        self.assertEqual(repr(reference), prefix + ' (name)>')

    def test_ref_repr_suppresses_name_lookup_error(self):
        events = []
        class Named(object):
            def __getattribute__(self, name):
                if name == '__name__':
                    events.append(name)
                    raise KeyboardInterrupt('name lookup')
                return object.__getattribute__(self, name)
        target = Named()
        reference = weakref.ref(target)
        try:
            raise ValueError('outer')
        except ValueError:
            before = sys.exc_info()
            self.assertTrue(repr(reference).endswith('>'))
            self.assertEqual(sys.exc_info(), before)
        self.assertEqual(events, ['__name__'])
        sys.exc_clear()

    def test_dead_ref_repr_has_dead_marker(self):
        target = Target()
        reference = weakref.ref(target)
        del target
        self.assertEqual(repr(reference), '<weakref at %s; dead>' % hex(id(reference)))

    def test_proxy_repr_uses_proxy_and_referent_identity(self):
        target = Target()
        proxy = weakref.proxy(target)
        expected = '<weakproxy at %s to Target at %s>' % (hex(id(proxy)), hex(id(target)))
        self.assertEqual(repr(proxy), expected)
        self.assertEqual(weakref.ProxyType.__repr__(proxy), expected)

    def test_proxy_unicode_calls_referent_method_directly(self):
        events = []
        class Text(object):
            def __unicode__(self):
                events.append('unicode')
                return u'value'
        target = Text()
        proxy = weakref.proxy(target)
        self.assertEqual(weakref.ProxyType.__unicode__(proxy), u'value')
        self.assertEqual(unicode(proxy), u'value')
        self.assertEqual(events, ['unicode', 'unicode'])

    def test_proxy_unicode_has_no_str_fallback(self):
        target = Target()
        proxy = weakref.proxy(target)
        with self.assertRaises(AttributeError) as error:
            weakref.ProxyType.__unicode__(proxy)
        self.assertEqual(error.exception.args, ("'Target' object has no attribute '__unicode__'",))

    def test_remove_dead_weakref_rejects_non_ref_values(self):
        for value in (None, 1, [], {}):
            mapping = {'key': value}
            with self.assertRaises(TypeError) as error:
                weakref._remove_dead_weakref(mapping, 'key')
            self.assertEqual(error.exception.args, ('not a weakref',))
            self.assertIs(mapping['key'], value)

    def test_remove_dead_weakref_keeps_live_and_ignores_absent(self):
        target = Target()
        reference = weakref.ref(target)
        mapping = {'key': reference}
        self.assertIs(weakref._remove_dead_weakref(mapping, 'key'), None)
        self.assertIs(mapping['key'], reference)
        self.assertIs(weakref._remove_dead_weakref(mapping, 'absent'), None)

    def test_remove_dead_weakref_deletes_dead_ref_and_proxy(self):
        for factory in (weakref.ref, weakref.proxy, Reference):
            target = Target()
            value = factory(target)
            mapping = {'key': value}
            del target
            self.assertIs(weakref._remove_dead_weakref(mapping, 'key'), None)
            self.assertEqual(mapping, {})

    def test_remove_dead_weakref_hashes_once_and_propagates_errors(self):
        events = []
        class Key(object):
            def __hash__(self):
                events.append('hash')
                return 17
        target = Target()
        reference = weakref.ref(target)
        key = Key()
        mapping = {key: reference}
        del target
        del events[:]
        weakref._remove_dead_weakref(mapping, key)
        self.assertEqual(events, ['hash'])
        self.assertEqual(mapping, {})

    def test_function_method_generator_and_sets_support_weakrefs(self):
        class Methods(object):
            def method(self):
                pass
        instance = Methods()
        def generator():
            yield 1
        factories = (lambda: (lambda: None), lambda: instance.method,
                     generator, set, lambda: frozenset((1,)))
        for factory in factories:
            events = []
            value = factory()
            reference = weakref.ref(value, lambda item: events.append(item() is None))
            self.assertIs(reference(), value)
            self.assertEqual(weakref.getweakrefcount(value), 1)
            value = None
            self.assertIs(reference(), None)
            self.assertEqual(events, [True])

    def test_weakref_callback_precedes_generator_finally(self):
        events = []
        def generator():
            try:
                yield 1
            finally:
                events.append('finally')
        value = generator()
        value.next()
        reference = weakref.ref(value, lambda item: events.append('callback'))
        value = None
        self.assertIs(reference(), None)
        self.assertEqual(events, ['callback', 'finally'])

    def test_frozenset_subclass_does_not_copy_source_weakrefs(self):
        class Frozen(frozenset):
            pass
        events = []
        source = frozenset((1, 2))
        original = weakref.ref(source, lambda item: events.append('source'))
        copied = Frozen(source)
        reference = weakref.ref(copied, lambda item: events.append('copy'))
        self.assertIs(original(), source)
        self.assertIs(reference(), copied)
        self.assertIsNot(copied, source)
        copied = None
        self.assertEqual(events, ['copy'])
        self.assertIsNot(original(), None)
        source = None
        self.assertEqual(events, ['copy', 'source'])

    def test_set_subclasses_inherit_weakref_slot(self):
        class Mutable(set):
            pass
        class Frozen(frozenset):
            pass
        for factory in (Mutable, Frozen):
            value = factory((1,))
            reference = weakref.ref(value)
            self.assertIs(reference(), value)
            self.assertTrue(factory.__weakrefoffset__ > 0)

    def test_remove_dead_weakref_propagates_hash_callback_exception(self):
        marker = ValueError('hash')
        class Key(object):
            def __hash__(self):
                raise marker
        with self.assertRaises(ValueError) as error:
            weakref._remove_dead_weakref({}, Key())
        self.assertIs(error.exception, marker)

    def test_proxy_power_and_index_use_native_diagnostics(self):
        target = Target()
        proxy = weakref.proxy(target)
        with self.assertRaises(TypeError) as error:
            weakref.ProxyType.__pow__(proxy)
        self.assertEqual(error.exception.args, (' expected at least 1 arguments, got 0',))
        with self.assertRaises(TypeError) as error:
            weakref.ProxyType.__index__(proxy)
        self.assertEqual(error.exception.args, ("'Target' object cannot be interpreted as an index",))

    def test_empty_frozenset_cache_retains_weak_referent(self):
        events = []
        value = frozenset()
        reference = weakref.ref(value, lambda item: events.append('callback'))
        value = None
        self.assertIsNot(reference(), None)
        self.assertIs(reference(), frozenset())
        self.assertEqual(events, [])

    def test_empty_frozenset_inputs_share_exact_type_cache(self):
        value = frozenset()
        for items in ((), [], set(), iter(())):
            self.assertIs(frozenset(items), value)

    def test_empty_frozenset_subclasses_are_distinct(self):
        class Frozen(frozenset):
            pass
        cached = frozenset()
        first = Frozen()
        second = Frozen()
        self.assertIsNot(first, cached)
        self.assertIsNot(first, second)
        reference = weakref.ref(first)
        first = None
        self.assertIs(reference(), None)

    def test_ref_repr_reads_referent_state_after_name_callback(self):
        owners = []
        events = []
        class Named(object):
            def __getattribute__(self, name):
                if name == '__name__':
                    events.append('name')
                    owners[:] = []
                    return 'named'
                return object.__getattribute__(self, name)
        owners.append(Named())
        reference = weakref.ref(owners[0], lambda item: events.append('released'))
        expected = "<weakref at %s; to 'NoneType' at %s (named)>" % (hex(id(reference)), hex(id(None)))
        self.assertEqual(repr(reference), expected)
        self.assertIs(reference(), None)
        self.assertEqual(events, ['name', 'released'])

    def test_new_checks_class_before_referent_count(self):
        with self.assertRaises(TypeError) as error:
            weakref.ref.__new__(1)
        self.assertEqual(error.exception.args, ('weakref.__new__(X): X is not a type object (int)',))

    def test_new_rejects_incompatible_type_before_referent_count(self):
        with self.assertRaises(TypeError) as error:
            weakref.ref.__new__(list)
        self.assertEqual(error.exception.args, ('weakref.__new__(list): list is not a subtype of weakref',))

    def test_new_requires_class_argument(self):
        with self.assertRaises(TypeError) as error:
            weakref.ref.__new__()
        self.assertEqual(error.exception.args, ('weakref.__new__(): not enough arguments',))

    def test_remove_dead_weakref_suppresses_lookup_keyerror(self):
        class Missing(KeyError):
            pass
        events = []
        class Key(object):
            def __hash__(self):
                events.append('hash')
                raise Missing('hash')
        try:
            raise ValueError('outer')
        except ValueError:
            before = sys.exc_info()
            self.assertIs(weakref._remove_dead_weakref({}, Key()), None)
            self.assertEqual(sys.exc_info(), before)
        self.assertEqual(events, ['hash'])
        sys.exc_clear()


if __name__ == '__main__':
    unittest.main()
