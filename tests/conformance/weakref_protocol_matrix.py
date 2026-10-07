"""Finite weakref parser, cache, list ordering and native wrapper products.

Addresses and pointer hashes are checked against objects in the same process;
all other values, exception classes/messages and callback traces compare exactly.
Only ordinary live/dead references are used; referents remain owned during calls.
"""

import _weakref as weakref
import sys


class Target(object):
    pass


class CallableTarget(Target):
    def __call__(self, *args, **kwargs):
        return args, sorted(kwargs.items())


class Reference(weakref.ref):
    pass


class Methods(object):
    def method(self):
        pass


class Mutable(set):
    pass


class Frozen(frozenset):
    pass


def generator():
    yield 1


identities = set()


def emit(identity, callback):
    assert identity not in identities
    identities.add(identity)
    print identity + '\t' + repr(callback())
    sys.exc_clear()


def normalized(value, target):
    if type(value) in (weakref.ProxyType, weakref.CallableProxyType):
        return 'proxy', type(value).__name__
    if isinstance(value, weakref.ref):
        return 'ref', type(value).__name__, value() is target
    if value is target:
        return 'target', type(target).__name__
    if isinstance(value, (list, tuple)):
        return type(value).__name__, [normalized(item, target) for item in value]
    if type(value) not in (str, unicode, int, long, float, bool, type(None)) and value is not NotImplemented:
        return 'object', type(value).__name__
    return type(value).__name__, value


def caught(callback, target=None):
    try:
        return 'value', normalized(callback(), target)
    except BaseException as error:
        return 'error', type(error).__name__, error.args


def constructor_row(factory, target_factory, shape, keyword):
    target = target_factory()
    arguments = ((), (target,), (target, None), (target, lambda item: None),
                 (target, 1), (target, None, 1))[shape]
    result = caught(lambda: factory(*arguments, **keyword), target)
    count = weakref.getweakrefcount(target)
    return result, count


factories = (('ref', weakref.ref), ('subtype', Reference), ('proxy', weakref.proxy),
             ('new', lambda *args, **kwargs: weakref.ref.__new__(weakref.ref, *args, **kwargs)))
targets = (('instance', Target), ('callable', CallableTarget), ('int', lambda: 1),
           ('list', lambda: []), ('dict', lambda: {}), ('function', lambda: (lambda: None)),
           ('method', lambda: Methods().method), ('generator', generator),
           ('set', lambda: set((1,))), ('frozenset', lambda: frozenset((1,))),
           ('empty-frozenset', frozenset),
           ('set-subtype', lambda: Mutable((1,))), ('frozenset-subtype', lambda: Frozen((1,))))
keywords = ({}, {'extra': 1}, {'extra': 1, 'other': 2})
for label, factory in factories:
    for target_label, target_factory in targets:
        for shape in range(6):
            for keyword_index, keyword in enumerate(keywords):
                emit('constructor:%s:%s:%d:%d' % (label, target_label, shape, keyword_index),
                     lambda: constructor_row(factory, target_factory, shape, keyword))


def module_row(callback, target_factory, count, keyword):
    target = target_factory()
    reference = weakref.ref(target) if isinstance(target, Target) else None
    return caught(lambda: callback(*((target,) * count), **keyword), target)


for name in ('proxy', 'getweakrefs', 'getweakrefcount'):
    for target_label, target_factory in targets:
        for count in range(4):
            for keyword_index, keyword in enumerate(keywords):
                emit('module:%s:%s:%d:%d' % (name, target_label, count, keyword_index),
                     lambda: module_row(getattr(weakref, name), target_factory, count, keyword))


def reference_row(factory, dead, name, bound, count, keyword):
    target = Target()
    reference = factory(target)
    if dead:
        target = None
    arguments = (1,) * count
    callback = getattr(reference if bound else weakref.ref, name)
    if not bound:
        arguments = (reference,) + arguments
    def invoke():
        result = callback(*arguments, **keyword)
        if name == '__hash__':
            return type(result).__name__, result == hash(target)
        if name == '__repr__':
            expected = '<weakref at %s; dead>' % hex(id(reference)) if dead else "<weakref at %s; to 'Target' at %s>" % (hex(id(reference)), hex(id(target)))
            return type(result).__name__, result == expected
        return result
    return caught(invoke, target)


reference_methods = ('__init__', '__call__', '__hash__', '__repr__', '__eq__',
                     '__ne__', '__lt__', '__le__', '__gt__', '__ge__')
for label, factory in factories[:2]:
    for dead in (False, True):
        for name in reference_methods:
            for bound in (False, True):
                for count in range(4):
                    for keyword_index, keyword in enumerate(keywords):
                        emit('reference:%s:%d:%s:%d:%d:%d' % (label, dead, name, bound, count, keyword_index),
                             lambda: reference_row(factory, dead, name, bound, count, keyword))


def permutations(items):
    if not items:
        yield ()
    for index, item in enumerate(items):
        for tail in permutations(items[:index] + items[index + 1:]):
            yield (item,) + tail


def ordering_row(target_factory, ordering, release):
    target = target_factory()
    factories = (weakref.ref, weakref.proxy, Reference,
                 lambda value: weakref.ref(value, lambda item: None),
                 lambda value: weakref.proxy(value, lambda item: None))
    references = [None] * 5
    for index in ordering:
        references[index] = factories[index](target)
    if release >= 0:
        references[release] = None
    ordered = weakref.getweakrefs(target)
    labels = [next(index for index, candidate in enumerate(references) if candidate is item)
              for item in ordered]
    cache = weakref.ref(target), weakref.proxy(target)
    cache_result = ((references[0] is None or cache[0] is references[0]),
                    (references[1] is None or cache[1] is references[1]))
    return labels, len(ordered), cache_result


for target_label, target_factory in targets[:2]:
    for index, ordering in enumerate(permutations((0, 1, 2, 3, 4))):
        for release in (-1, 0, 1, 2, 3, 4):
            emit('ordering:%s:%d:%d' % (target_label, index, release),
                 lambda: ordering_row(target_factory, ordering, release))


def removal_row(factory, dead, value_kind, keyword):
    target = Target()
    reference = factory(target)
    value = (reference, None, 1, [], {})[value_kind]
    mapping = {'key': value}
    if dead:
        target = None
    result = caught(lambda: weakref._remove_dead_weakref(mapping, 'key', **keyword))
    return result, 'key' in mapping


for label, factory in factories[:3]:
    for dead in (False, True):
        for value_kind in range(5):
            for keyword_index, keyword in enumerate(keywords):
                emit('removal:%s:%d:%d:%d' % (label, dead, value_kind, keyword_index),
                     lambda: removal_row(factory, dead, value_kind, keyword))


def proxy_row(target_factory, dead, name, count, keyword):
    target = target_factory()
    proxy = weakref.proxy(target)
    owner = type(proxy)
    if dead:
        target = None
    def invoke():
        result = getattr(owner, name)(proxy, *((1,) * count), **keyword)
        if name == '__repr__':
            referent = target if target is not None else None
            expected = '<weakproxy at %s to %s at %s>' % (hex(id(proxy)), type(referent).__name__, hex(id(referent)))
            return type(result).__name__, result == expected
        return result
    return caught(invoke, target)


proxy_methods = ('__repr__', '__unicode__', '__pos__', '__neg__', '__abs__',
                 '__invert__', '__int__', '__long__', '__float__', '__index__',
                 '__nonzero__', '__add__', '__radd__', '__sub__', '__rsub__',
                 '__mul__', '__rmul__', '__div__', '__rdiv__', '__pow__', '__rpow__')
for target_label, target_factory in targets[:2]:
    for dead in (False, True):
        for name in proxy_methods:
            for count in range(4):
                for keyword_index, keyword in enumerate(keywords):
                    emit('proxy:%s:%d:%s:%d:%d' % (target_label, dead, name, count, keyword_index),
                         lambda: proxy_row(target_factory, dead, name, count, keyword))


def lifetime_row(factory, callback, proxy_callback):
    events = []
    target = factory()
    reference = weakref.ref(target, (lambda item: events.append(('ref', item() is None))) if callback else None)
    proxy = weakref.proxy(target, (lambda item: events.append(('proxy', type(item).__name__))) if proxy_callback else None)
    before = (reference() is target, weakref.getweakrefcount(target), type(proxy).__name__)
    target = None
    return before, reference() is None, events


for target_label, target_factory in targets[5:]:
    for callback in (False, True):
        for proxy_callback in (False, True):
            emit('lifetime:%s:%d:%d' % (target_label, callback, proxy_callback),
                 lambda: lifetime_row(target_factory, callback, proxy_callback))


def new_type_row(owner, count, keyword):
    target = Target()
    return caught(lambda: weakref.ref.__new__(owner, *((target,) * count), **keyword), target)


for index, owner in enumerate((None, 1, 'name', list, object, weakref.ProxyType, weakref.ref)):
    for count in (0, 1, 3):
        for keyword_index, keyword in enumerate(keywords):
            emit('new-type:%d:%d:%d' % (index, count, keyword_index),
                 lambda: new_type_row(owner, count, keyword))
for keyword_index, keyword in enumerate(keywords):
    emit('new-empty:%d' % keyword_index,
         lambda: caught(lambda: weakref.ref.__new__(**keyword)))


class Missing(KeyError):
    pass


def lookup_error_row(empty, phase, failure_type, dead):
    events = []
    marker = failure_type('lookup') if failure_type is not None else None
    class Key(object):
        failure = None
        def __hash__(self):
            events.append('hash')
            if self.failure is not None and phase == 'hash':
                raise self.failure
            return 17
        def __eq__(self, other):
            events.append('eq')
            if self.failure is not None and phase == 'eq':
                raise self.failure
            return True
    target = Target()
    reference = weakref.ref(target)
    stored = Key()
    mapping = {} if empty else {stored: reference}
    if dead:
        target = None
    query = Key()
    stored.failure = marker
    query.failure = marker
    events[:] = []
    result = caught(lambda: weakref._remove_dead_weakref(mapping, query))
    stored.failure = None
    query.failure = None
    return result, tuple(events), len(mapping)


for empty in (False, True):
    for phase in ('hash', 'eq'):
        for index, failure_type in enumerate((None, KeyError, Missing, ValueError)):
            for dead in (False, True):
                emit('lookup-error:%d:%s:%d:%d' % (empty, phase, index, dead),
                     lambda: lookup_error_row(empty, phase, failure_type, dead))


assert len(identities) == 5032
