"""Finite class metadata mutations and native descriptor receiver products."""

import _sre
import _functools
import sys


def caught(callback):
    try:
        return 'value', callback()
    except BaseException as error:
        return 'error', type(error).__name__, error.args


def emit(identity, callback):
    print identity + '\t' + repr(caught(callback))
    sys.exc_clear()


class Text(str):
    pass


def mutation(classic, field, key_kind, value_kind, deleting):
    class Modern(object):
        pass
    class Classic:
        pass
    owner = Classic if classic else Modern
    original = getattr(owner, field)
    key = (field, unicode(field), Text(field))[key_kind]
    values = ('Changed', Text('Changed'), u'Changed', 'Changed\0tail',
              None, 17, [], {}, (), (object,))
    value = values[value_kind]
    def update():
        if deleting:
            delattr(owner, key)
        else:
            setattr(owner, key, value)
        current = getattr(owner, field)
        return type(current).__name__, current is value
    result = caught(update)
    return result, getattr(owner, field) is original


for classic in (False, True):
    for field in ('__name__', '__dict__', '__bases__'):
        for key_kind in range(3):
            for value_kind in range(10):
                emit('metadata/set/%d/%s/%d/%d' % (classic, field, key_kind, value_kind),
                     lambda: mutation(classic, field, key_kind, value_kind, False))
            emit('metadata/delete/%d/%s/%d' % (classic, field, key_kind),
                 lambda: mutation(classic, field, key_kind, 0, True))


pattern = _sre.compile('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1],
                       0, {}, [None])
values = (pattern, pattern.match('a'), pattern.scanner('a'), lambda: None,
          memoryview('a'), iter([]), iter(()), iter({}), iter(set()),
          iter('a'), iter(u'a'), iter(buffer('a')), reversed([]),
          iter(xrange(1)), enumerate([]), _functools.partial(int, '1'))
for index, value in enumerate(values):
    owner = type(value)
    emit('metadata/native/%d' % index,
         lambda: (owner.__name__, owner.__module__, repr(owner)))


descriptors = (int.conjugate, long.conjugate, float.conjugate,
               complex.conjugate, list.append, list.__add__,
               tuple.__add__, dict.keys, set.add)
for index, descriptor in enumerate(descriptors):
    for receiver_kind, receiver in enumerate((None, '', 17, [], {}, set())):
        args = () if receiver_kind == 0 else (receiver,)
        emit('descriptor/call/%d/%d' % (index, receiver_kind),
             lambda: caught(lambda: descriptor(*args)))
        if receiver_kind != 0:
            def bind():
                bound = descriptor.__get__(receiver, type(receiver))
                return bound.__name__ == descriptor.__name__, bound.__self__ is receiver
            emit('descriptor/bind/%d/%d' % (index, receiver_kind), bind)


for name in ('Plain', 'outer.Qualified', Text('outer.Subtype')):
    for module_kind in range(4):
        namespace = {'__module__': ('custom', None, u'custom', 17)[module_kind]}
        owner = type(name, (object,), namespace)
        emit('metadata/heap/%s/%d' % (name, module_kind),
             lambda: (owner.__name__, owner.__name__ is name,
                      owner.__module__, repr(owner)))
