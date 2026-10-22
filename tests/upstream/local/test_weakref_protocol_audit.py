"""Project-authored weakref argument, ordering and representation contracts."""

import _weakref as weakref
import sys
import unittest


class Target(object):
    pass


class Reference(weakref.ref):
    pass


class WeakrefProtocolAudit(unittest.TestCase):
    def test_proxy_subscript_unwraps_key_in_slot_and_direct_wrapper(self):
        class Integer(int):
            pass
        class Dictionary(dict):
            pass
        class Mapping(object):
            def __getitem__(self, item):
                return item is key, type(item)
        key = Integer(3)
        key_proxy = weakref.proxy(key)
        mapping = Dictionary({key: 'found'})
        proxy = weakref.proxy(mapping)
        self.assertEqual(proxy[key_proxy], 'found')
        self.assertEqual(type(proxy).__getitem__(proxy, key_proxy), 'found')
        owner = Mapping()
        proxy = weakref.proxy(owner)
        self.assertEqual(proxy[key_proxy], (True, Integer))
        self.assertEqual(type(proxy).__getitem__(proxy, key_proxy), (True, Integer))
        self.assertEqual(proxy.__getitem__(key_proxy), (False, type(key_proxy)))

    def test_proxy_subscript_detects_dead_key_before_calling_mapping(self):
        events = []
        class Mapping(object):
            def __getitem__(self, key):
                events.append('called')
                return 7
        owner = Mapping()
        proxy = weakref.proxy(owner)
        dead = weakref.proxy(Target())
        self.assertRaises(ReferenceError, lambda: proxy[dead])
        self.assertRaises(ReferenceError, type(proxy).__getitem__, proxy, dead)
        self.assertEqual(events, [])
        self.assertEqual(proxy.__getitem__(dead), 7)

    def test_proxy_write_delete_and_contains_preserve_key_and_value(self):
        events = []
        class Mapping(object):
            def __setitem__(self, key, value):
                events.append(('set', key is key_proxy, value is value_proxy))
            def __delitem__(self, key):
                events.append(('del', key is key_proxy))
            def __contains__(self, key):
                events.append(('contains', key is key_proxy))
                return True
        key = Target()
        value = Target()
        key_proxy = weakref.proxy(key)
        value_proxy = weakref.proxy(value)
        owner = Mapping()
        proxy = weakref.proxy(owner)
        proxy[key_proxy] = value_proxy
        del proxy[key_proxy]
        self.assertTrue(key_proxy in proxy)
        self.assertEqual(events, [('set', True, True), ('del', True), ('contains', True)])

    def test_proxy_modular_power_accepts_proxies_in_every_position(self):
        class Integer(int):
            pass
        owners = [Integer(2), Integer(3), Integer(5)]
        proxies = map(weakref.proxy, owners)
        for mask in range(8):
            arguments = [proxies[index] if mask & (1 << index) else owners[index]
                         for index in range(3)]
            self.assertEqual(pow(*arguments), 3)
        self.assertEqual(type(proxies[0]).__pow__(proxies[0], proxies[1], proxies[2]), 3)

    def test_proxy_power_checks_dead_operands_before_user_method(self):
        events = []
        class Number(object):
            def __pow__(self, exponent, modulus=None):
                events.append((exponent, modulus))
                return 19
        owner = Number()
        proxy = weakref.proxy(owner)
        dead = weakref.proxy(Target())
        self.assertRaises(ReferenceError, pow, proxy, 3, dead)
        self.assertRaises(ReferenceError, type(proxy).__pow__, proxy, 3, dead)
        self.assertEqual(events, [])
        self.assertEqual(pow(owner, 3, dead), 19)
        self.assertEqual(len(events), 1)

    def test_proxy_modular_power_preserves_callback_exception_identity(self):
        marker = LookupError('power')
        class Number(object):
            def __pow__(self, exponent, modulus=None):
                raise marker
        owner = Number()
        proxy = weakref.proxy(owner)
        for operation in (lambda: pow(proxy, 3, 5),
                          lambda: type(proxy).__pow__(proxy, 3, 5)):
            with self.assertRaises(LookupError) as caught:
                operation()
            self.assertIs(caught.exception, marker)

    def test_proxy_direct_getattribute_unwraps_unicode_proxy_name(self):
        class Text(unicode):
            pass
        class Owner(object):
            value = 73
        class CallableOwner(Owner):
            def __call__(self):
                return None
        name = Text(u'value')
        name_proxy = weakref.proxy(name)
        for factory in (Owner, CallableOwner):
            owner = factory()
            proxy = weakref.proxy(owner)
            self.assertEqual(type(proxy).__getattribute__(proxy, name_proxy), 73)
            self.assertRaises(TypeError, getattr, proxy, name_proxy)

    def test_proxy_direct_getattribute_checks_dead_receiver_and_name(self):
        class Text(unicode):
            pass
        class Owner(object):
            value = 73
        for dead_receiver in (False, True):
            owner = Owner()
            proxy = weakref.proxy(owner)
            name = Text(u'value')
            name_proxy = weakref.proxy(name)
            if dead_receiver:
                owner = None
            else:
                name = None
            self.assertRaises(ReferenceError, type(proxy).__getattribute__, proxy, name_proxy)

    def test_proxy_class_comes_from_the_referent(self):
        class Base(object):
            def method(self):
                return 'base'
        class Derived(Base):
            def method(self):
                return ('derived', super(Derived, self).method())
        class Old:
            pass
        target = Derived()
        proxy = weakref.proxy(target)
        self.assertIs(proxy.__class__, Derived)
        self.assertTrue(isinstance(proxy, Base))
        self.assertEqual(Derived.method(proxy), ('derived', 'base'))
        self.assertEqual(super(Derived, proxy).method(), 'base')
        self.assertIs(object.__reduce_ex__(proxy, 2)[1][0], Derived)
        old = Old()
        self.assertIs(weakref.proxy(old).__class__, Old)
        function = lambda: None
        self.assertIs(weakref.proxy(function).__class__, type(function))
        dead = weakref.proxy(Derived())
        self.assertRaises(ReferenceError, getattr, dead, '__class__')

    def test_proxy_class_assignment_reaches_the_referent(self):
        class First(object):
            pass
        class Second(object):
            pass
        target = First()
        proxy = weakref.proxy(target)
        proxy.__class__ = Second
        self.assertIs(type(target), Second)
        with self.assertRaises(TypeError) as error:
            del proxy.__class__
        self.assertEqual(error.exception.args, ("can't delete __class__ attribute",))

    def test_proxy_attribute_wrappers_check_names_after_the_referent(self):
        events = []
        class Target(object):
            def __getattr__(self, name):
                events.append(name)
                return name
        target = Target()
        target.present = 1
        proxy = weakref.proxy(target)
        cases = ((weakref.ProxyType.__getattribute__, (proxy, 5)), (weakref.ProxyType.__getattribute__, (proxy, None)),
                 (weakref.ProxyType.__setattr__, (proxy, 5, 1)), (weakref.ProxyType.__delattr__, (proxy, 123456789)),
                 (weakref.ProxyType.__delattr__, (proxy, 10 ** 30)), (weakref.ProxyType.__delattr__, (proxy, 2.5)))
        for wrapper, arguments in cases:
            with self.assertRaises(TypeError) as error:
                wrapper(*arguments)
            self.assertEqual(error.exception.args, ("attribute name must be string, not '%s'" % type(arguments[1]).__name__,))
        self.assertEqual(events, [])
        weakref.ProxyType.__setattr__(proxy, u'added', 2)
        self.assertEqual((weakref.ProxyType.__getattribute__(proxy, u'added'), target.added), (2, 2))
        weakref.ProxyType.__delattr__(proxy, u'added')
        self.assertNotIn('added', target.__dict__)
        dead = weakref.proxy(Target())
        for wrapper, arguments in ((weakref.ProxyType.__getattribute__, (dead, 5)), (weakref.ProxyType.__setattr__, (dead, 5, 1)),
                                   (weakref.ProxyType.__delattr__, (dead, 5))):
            self.assertRaises(ReferenceError, wrapper, *arguments)

    def test_proxy_assignment_and_deletion_reach_the_referent(self):
        events = []
        class Hooked(object):
            def __setattr__(self, name, value):
                events.append(('set', name, value))
            def __delattr__(self, name):
                events.append(('del', name))
        class Plain(object):
            pass
        hooked = Hooked()
        proxy = weakref.proxy(hooked)
        proxy.first = 1
        del proxy.first
        setattr(proxy, u'second', 2)
        self.assertEqual(events, [('set', 'first', 1), ('del', 'first'), ('set', 'second', 2)])
        plain = Plain()
        proxy = weakref.proxy(plain)
        proxy.value = 3
        self.assertEqual(plain.__dict__, {'value': 3})
        del proxy.value
        with self.assertRaises(AttributeError) as error:
            del proxy.value
        self.assertEqual(error.exception.args, ('value',))
        for wrapper in (object.__setattr__, object.__delattr__):
            arguments = (proxy, 'value', 1) if wrapper is object.__setattr__ else (proxy, 'value')
            with self.assertRaises(TypeError) as error:
                wrapper(*arguments)
            self.assertEqual(error.exception.args, ("can't apply this %s to weakproxy object" % wrapper.__name__,))
        self.assertTrue(set(['__setattr__', '__delattr__', '__str__']) <= set(weakref.ProxyType.__dict__))

    def test_proxy_str_wrapper_converts_the_referent(self):
        class Text(object):
            def __call__(self):
                return None
            def __str__(self):
                return 'text'
        target = Text()
        proxy = weakref.proxy(target)
        self.assertEqual((type(proxy).__str__(proxy), str(proxy), unicode(proxy), u'%s' % proxy), ('text', 'text', u'text', u'text'))
        target = None
        self.assertRaises(ReferenceError, unicode, proxy)
        self.assertRaises(ReferenceError, type(proxy).__str__, proxy)

    def test_proxy_compare_wrapper_takes_proxies_alone(self):
        events = []
        class Ordered(object):
            def __init__(self, value):
                self.value = value
            def __cmp__(self, other):
                events.append(other.value)
                return cmp(self.value, other.value)
        first, second = Ordered(1), Ordered(2)
        left, right = weakref.proxy(first), weakref.proxy(second)
        self.assertEqual((cmp(left, right), events), (-1, [2]))
        self.assertEqual(weakref.ProxyType.__cmp__(left, right), -1)
        with self.assertRaises(TypeError) as error:
            weakref.ProxyType.__cmp__(left, 5)
        self.assertEqual(error.exception.args, ("weakproxy.__cmp__(x,y) requires y to be a 'weakproxy', not a 'int'",))
        with self.assertRaises(TypeError) as error:
            weakref.ProxyType.__cmp__(left)
        self.assertEqual(error.exception.args, ('expected 1 arguments, got 0',))

    def test_proxy_orders_as_a_number_against_other_types(self):
        class Coercing(object):
            def __coerce__(self, other):
                return None
        target = Target()
        proxy = weakref.proxy(target)
        self.assertEqual((proxy < u'a', proxy < 'a', proxy < [], proxy < (1,), u'a' < proxy, cmp(proxy, u'a')),
                         (True, True, True, True, False, -1))
        self.assertIs(min([u'a', proxy]), proxy)
        self.assertRaises(TypeError, lambda: Coercing() == proxy)

    def test_proxy_unary_length_and_iteration_follow_the_builtins(self):
        class Old:
            pass
        class Sized(object):
            def __init__(self, length):
                self.length = length
            def __len__(self):
                return self.length
        class OldSized:
            def __len__(self):
                return 3L
        old = Old()
        with self.assertRaises(AttributeError) as error:
            abs(weakref.proxy(old))
        self.assertEqual(error.exception.args, ("Old instance has no attribute '__abs__'",))
        for length, kind, message in ((-1, ValueError, '__len__() should return >= 0'), ('3', TypeError, 'an integer is required')):
            sized = Sized(length)
            with self.assertRaises(kind) as error:
                len(weakref.proxy(sized))
            self.assertEqual(error.exception.args, (message,))
        sized = Sized(3.0)
        self.assertEqual(len(weakref.proxy(sized)), 3)
        old_sized = OldSized()
        with self.assertRaises(TypeError) as error:
            len(weakref.proxy(old_sized))
        self.assertEqual(error.exception.args, ('__len__() should return an int',))
        target = Target()
        with self.assertRaises(TypeError) as error:
            next(weakref.proxy(target))
        self.assertEqual(error.exception.args, ("'Target' object is not iterable",))

    def test_proxy_slice_wrappers_read_c_long_bounds(self):
        events = []
        class Sliced(object):
            def __getslice__(self, start, stop):
                return start, stop
            def __setslice__(self, start, stop, value):
                events.append((start, stop, value))
            def __getitem__(self, key):
                return key
        class Integral(object):
            def __int__(self):
                return 2
            def __index__(self):
                return 1
        target = Sliced()
        proxy = weakref.proxy(target)
        self.assertEqual(weakref.ProxyType.__getslice__(proxy, Integral(), True), (2, 1))
        self.assertEqual(proxy[Integral():2], (1, 2))
        weakref.ProxyType.__setslice__(proxy, 1, 2, 'value')
        self.assertEqual(events, [(1, 2, 'value')])
        for bound, kind, message in (('a', TypeError, 'an integer is required'), (2.5, TypeError, 'integer argument expected, got float'),
                                     (10 ** 20, OverflowError, 'Python int too large to convert to C long')):
            with self.assertRaises(kind) as error:
                weakref.ProxyType.__getslice__(proxy, bound, 2)
            self.assertEqual(error.exception.args, (message,))
        with self.assertRaises(TypeError) as error:
            weakref.ProxyType.__getslice__(proxy, 1)
        self.assertEqual(error.exception.args, ('function takes exactly 2 arguments (1 given)',))
        dead = weakref.proxy(Sliced())
        self.assertRaises(TypeError, weakref.ProxyType.__getslice__, dead, 'a', 2)
        self.assertRaises(ReferenceError, weakref.ProxyType.__getslice__, dead, 1, 2)

    def test_classic_call_through_getattr_makes_a_callable_proxy(self):
        class Old:
            def __getattr__(self, name):
                if name == '__call__':
                    return lambda *arguments: arguments
                raise AttributeError(name)
        target = Old()
        proxy = weakref.proxy(target)
        self.assertIs(type(proxy), weakref.CallableProxyType)
        self.assertEqual(proxy(1, 2), (1, 2))

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

    def test_base_weak_list_is_cleared_after_subclass_del(self):
        # subtype_dealloc clears a weak list the class added before __del__;
        # the dealloc of set, frozenset and partial clears their own after
        # it, with the callbacks of the references __del__ made.
        from _functools import partial
        events = []
        references = []
        def finalizer(self):
            events.append(('del', references[0]() is self))
            references.append(weakref.ref(self, lambda item: events.append('made in del')))
        for base, arguments, expected in ((set, ((1,),), [('del', True), 'made in del', 'callback']),
                                          (frozenset, ((1,),), [('del', True), 'made in del', 'callback']),
                                          (partial, (len,), [('del', True), 'made in del', 'callback']),
                                          (object, (), ['callback', ('del', False)])):
            value = type('Finalized', (base,), {'__del__': finalizer})(*arguments)
            references[:] = [weakref.ref(value, lambda item: events.append('callback'))]
            del events[:]
            value = None
            self.assertEqual(events, expected)

    def test_base_weak_list_survives_resurrecting_del(self):
        events = []
        kept = []
        class Resurrecting(set):
            def __del__(self):
                if not events:
                    kept.append(self)
                events.append('del')
        reference = weakref.ref(Resurrecting((1,)), lambda item: events.append('callback'))
        self.assertIs(reference(), kept[0])
        self.assertEqual(events, ['del'])
        del kept[:]
        self.assertIs(reference(), None)
        self.assertEqual(events, ['del', 'del', 'callback'])

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

    def test_reversed_refuses_proxies_and_dictproxy(self):
        class Items(list):
            pass
        class Reversible(object):
            def __reversed__(self):
                return iter('rev')
        class Indexed(object):
            def __getitem__(self, index):
                return index
            def __len__(self):
                return 2
        keep = [Items([1, 2]), Reversible(), Indexed()]
        for target in keep + [type.__dict__['__dict__'].__get__(Indexed)]:
            argument = weakref.proxy(target) if target in keep else target
            with self.assertRaises(TypeError) as caught:
                reversed(argument)
            self.assertEqual(str(caught.exception), 'argument to reversed() must be a sequence')
        self.assertEqual((list(reversed(keep[0])), list(reversed(keep[2]))), ([2, 1], [1, 0]))

    def test_proxy_slices_report_an_unsliceable_referent(self):
        class Plain(object):
            pass
        class Setter(object):
            def __setitem__(self, key, value):
                pass
        plain = Plain()
        proxy = weakref.proxy(plain)
        setter = Setter()
        setter_proxy = weakref.proxy(setter)
        def get():
            return proxy[1:2]
        def store():
            proxy[1:2] = [1]
        def remove():
            del proxy[1:2]
        def remove_setter():
            del setter_proxy[1:2]
        failures = ((get, TypeError, "'Plain' object is unsliceable"), (store, TypeError, "'Plain' object doesn't support slice assignment"),
                    (remove, TypeError, "'Plain' object doesn't support slice assignment"), (remove_setter, AttributeError, '__delitem__'),
                    (lambda: proxy.__setitem__(slice(1, 2), [1]), AttributeError, "'Plain' object has no attribute '__setitem__'"))
        for function, exception, message in failures:
            with self.assertRaises(exception) as caught:
                function()
            self.assertEqual(str(caught.exception), message)
        setter_proxy[1:2] = [1]

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
