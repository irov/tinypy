"""Finite native wrapper metadata, binding and ordinary callback products.

Addresses and address-dependent hash values are compared through shape or
invariants. Documentation contents are not compared; metadata descriptor
behavior is compared. Every native
type namespace is read before writes: CPython's lazy readiness on a first cold
write is a separate source-backed observation, not normalized by this product.
"""

import sys

identities = set()


def summarize(value):
    if value is None or value is NotImplemented or isinstance(value, (int, long, bool, str, unicode)):
        return (type(value).__name__, value)
    if isinstance(value, type):
        return ('type', value.__name__)
    if isinstance(value, tuple):
        return ('tuple', tuple(summarize(item) for item in value))
    return ('object', type(value).__name__, getattr(value, '__name__', None))


def representation(value):
    text = repr(value)
    if ' object at 0x' in text:
        return text.split(' object at 0x')[0] + ' object at <address>>'
    return text


def outcome(call, mode=None):
    try:
        result = call()
        if mode == 'hash':
            return ('value', type(result).__name__)
        if mode == 'doc':
            return ('value', result is None or isinstance(result, str))
        if mode == 'repr':
            if ' object at 0x' in result:
                result = result.split(' object at 0x')[0] + ' object at <address>>'
            return ('value', result)
        return ('value', summarize(result))
    except BaseException as error:
        return ('error', type(error).__name__, error.args)


def emit(identity, result):
    assert identity not in identities, identity
    identities.add(identity)
    print identity + '\t' + repr(result)
    sys.exc_clear()


array, mapping = [], {'member': 17}
selected = (('object-repr', object().__repr__), ('object-hash', object().__hash__),
            ('int-add', (7).__add__), ('int-repr', (7).__repr__),
            ('list-len', array.__len__), ('list-contains', array.__contains__),
            ('builtin-call', len.__call__), ('builtin', len),
            ('append', array.append), ('get', mapping.get),
            ('object-descriptor', object.__repr__), ('int-descriptor', int.__add__),
            ('append-descriptor', list.append), ('get-descriptor', dict.get))
fields = ('__name__', '__self__', '__objclass__', '__module__', '__doc__',
          '__get__', '__call__', '__cmp__', '__eq__', '__ne__', '__lt__', '__le__', '__gt__', '__ge__')
for label, value in selected:
    # Force the reference native type's ordinary attribute namespace ready.
    namespace = type(value).__dict__
    for field in fields:
        emit('read/%s/%s' % (label, field),
             outcome(lambda: getattr(value, field), 'doc' if field == '__doc__' else None))
        if field in namespace:
            descriptor = namespace[field]
            emit('metadata-kind/%s/%s' % (label, field),
                 (type(descriptor).__name__, getattr(descriptor, '__name__', None),
                  getattr(descriptor, '__objclass__', None) is type(value)))
    emit('repr/%s' % label, outcome(lambda: representation(value)))
    emit('construct/%s' % label, outcome(lambda: type(value)()))
    for protocol in (0, 1, 2, 3):
        emit('reduce/%s/%d' % (label, protocol), outcome(lambda: value.__reduce_ex__(protocol)))
    for field in ('__name__', '__self__', '__objclass__', '__doc__', '__module__', 'missing'):
        previous = getattr(value, field, None)
        try:
            emit('write/%s/%s' % (label, field), outcome(lambda: setattr(value, field, 23)))
            emit('delete/%s/%s' % (label, field), outcome(lambda: delattr(value, field)))
        finally:
            if field == '__module__' and type(value).__name__ == 'builtin_function_or_method':
                value.__module__ = previous
    for method in ('__repr__', '__hash__', '__cmp__', '__eq__', '__ne__', '__lt__', '__le__', '__gt__', '__ge__'):
        if not hasattr(value, method):
            emit('method/%s/%s/absent' % (label, method), outcome(lambda: getattr(value, method)))
            continue
        bound = getattr(value, method)
        arguments = ((), (value,), (17,), (None,), (value, 17))
        keywords = ({}, {'argument': 17})
        for argument_index, items in enumerate(arguments):
            for keyword_index, named in enumerate(keywords):
                mode = 'hash' if method == '__hash__' else ('repr' if method == '__repr__' else None)
                emit('method/%s/%s/%d/%d' % (label, method, argument_index, keyword_index),
                     outcome(lambda: bound(*items, **named), mode))

bindings = (('object', object.__repr__, object()), ('int', int.__add__, 7),
            ('list', list.append, []), ('dict', dict.get, {}))
for label, descriptor, receiver in bindings:
    for index, items in enumerate(((), (None,), (None, None), (None, 17), (receiver,),
                                  (receiver, None), (receiver, 17), (receiver, list),
                                  (receiver, type(receiver)), (17,), (None, None, 17))):
        for keyword_index, named in enumerate(({}, {'instance': receiver})):
            emit('bind/%s/%d/%d' % (label, index, keyword_index), outcome(lambda: descriptor.__get__(*items, **named)))

for label, receiver, method in (('object', object(), '__repr__'), ('int', 7, '__add__'),
                                ('list', [], '__len__'), ('append', [], 'append'),
                                ('get', {}, 'get'), ('builtin', len, '__call__')):
    first, second = getattr(receiver, method), getattr(receiver, method)
    emit('binding-identity/%s' % label, (first is second, first == second, first != second, cmp(first, second)))
    emit('binding-hash/%s' % label, outcome(lambda: hash(first) == hash(second)))

events = []
failure_types = (None, ValueError, KeyboardInterrupt)
for result_index, comparison_result in enumerate((-1, 0, 1, NotImplemented)):
    for failure_index, failure_type in enumerate(failure_types):
        failure = failure_type('compare') if failure_type is not None else None
        class Subject(object):
            def __cmp__(self, other):
                events.append('compare')
                if failure is not None:
                    raise failure
                return comparison_result
        left, right = Subject(), Subject()
        a, b = left.__repr__, right.__repr__
        operations = (('eq', lambda: a == b), ('ne', lambda: a != b), ('lt', lambda: a < b),
                      ('le', lambda: a <= b), ('gt', lambda: a > b), ('ge', lambda: a >= b),
                      ('cmp', lambda: cmp(a, b)), ('direct', lambda: a.__cmp__(b)))
        for name, call in operations:
            events[:] = []
            # Two unequal addresses after NotImplemented have no portable order.
            # Keep equality outcomes and callback count; compare order consistency.
            if comparison_result is NotImplemented and name in ('lt', 'le', 'gt', 'ge', 'cmp', 'direct') and failure is None:
                result = outcome(lambda: (cmp(a, b) != 0, cmp(a, b) == -cmp(b, a)))
            else:
                result = outcome(call)
            emit('receiver-compare/%d/%d/%s' % (result_index, failure_index, name), (result, events[:]))

for hash_index, hash_result in enumerate((0, 17, -1, 0x100000011L)):
    for failure_index, failure_type in enumerate(failure_types):
        failure = failure_type('hash') if failure_type is not None else None
        class Subject(object):
            def __hash__(self):
                events.append('hash')
                if failure is not None:
                    raise failure
                return hash_result
        receiver = Subject()
        wrapper = receiver.__repr__
        def hash_invariant():
            result = hash(wrapper)
            receiver_hash = hash_result if hash_result != -1 else -2
            expected = (hash(object.__repr__) ^ receiver_hash) & 0xffffffffL
            if expected >= 0x80000000L:
                expected -= 0x100000000L
            if expected == -1:
                expected = -2
            return (type(result).__name__, result == expected)
        events[:] = []
        emit('receiver-hash/%d/%d' % (hash_index, failure_index), (outcome(hash_invariant), events[:]))

for label, bound in (('builtin', len), ('wrapper', (7).__add__),
                     ('method-descriptor', list.append), ('wrapper-descriptor', object.__repr__)):
    namespace = type(bound).__dict__
    for field in ('__name__', '__self__', '__objclass__', '__module__', '__doc__'):
        if field not in namespace:
            continue
        descriptor = namespace[field]
        for receiver_index, receiver in enumerate((bound, 17)):
            emit('direct-field/%s/%s/%d' % (label, field, receiver_index),
                 outcome(lambda: descriptor.__get__(receiver), 'doc' if field == '__doc__' else None))
            for deletion in (False, True):
                previous = getattr(bound, field, None)
                try:
                    if deletion:
                        call = lambda: descriptor.__delete__(receiver)
                    else:
                        call = lambda: descriptor.__set__(receiver, 23)
                    emit('direct-field-write/%s/%s/%d/%d' % (label, field, receiver_index, deletion), outcome(call))
                finally:
                    if field == '__module__' and receiver is bound:
                        bound.__module__ = previous

for label, factory, arguments in (('add', lambda: (7).__add__, ((3,), (), (3, 4))),
                                  ('len', lambda: [].__len__, ((), (3,), (3, 4))),
                                  ('append', lambda: [].append, ((3,), (), (3, 4))),
                                  ('get', lambda: {'member': 17}.get, (('member',), (), ('missing', 19)))):
    for index, items in enumerate(arguments):
        for keyword_index, named in enumerate(({}, {'argument': 17})):
            bound = factory()
            emit('call/%s/%d/%d' % (label, index, keyword_index), outcome(lambda: bound(*items, **named)))
            emit('call-wrapper/%s/%d/%d' % (label, index, keyword_index), outcome(lambda: bound.__call__(*items, **named)))

for index in xrange(20):
    target = []
    wrapper = target.__len__
    emit('cache/%d/wrapper' % index, (type(wrapper).__name__, wrapper.__self__ is target, wrapper()))
    del wrapper
    method = target.append
    method(index)
    emit('cache/%d/builtin' % index, (type(method).__name__, method.__self__ is target, target))
    del method

del first, second, a, b, bound, descriptor, namespace, selected, bindings
assert len(identities) == 1556, len(identities)
sys.exc_clear()
