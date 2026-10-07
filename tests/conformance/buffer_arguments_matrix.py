"""Finite native buffer, memoryview and bytearray argument products.

Physical bytearray capacity, seeded hash values and address-bearing repr output
are compared by their protocol invariants rather than allocation layout.
"""

import sys


class Bytes(bytearray):
    pass


identities = set()


def normalized(value, name, receiver):
    if name == '__alloc__':
        return type(value).__name__, value >= len(receiver) + (1 if len(receiver) else 0)
    if name == '__hash__':
        return type(value).__name__, value == hash(str(receiver))
    if name == '__repr__':
        return type(value).__name__, value.startswith('<') and value.endswith('>')
    if isinstance(value, memoryview):
        return type(value).__name__, value.tobytes(), value.readonly
    if isinstance(value, (bytearray, buffer)):
        return type(value).__name__, str(value)
    if isinstance(value, tuple):
        return tuple(normalized(item, '', receiver) for item in value)
    if isinstance(value, list):
        return [normalized(item, '', receiver) for item in value]
    return type(value).__name__, value


def caught(callback, name='', receiver=None):
    try:
        return 'value', normalized(callback(), name, receiver)
    except BaseException as failure:
        return 'error', type(failure).__name__, failure.args


def emit(identity, callback):
    assert identity not in identities
    identities.add(identity)
    print identity + '\t' + repr(callback())
    sys.exc_clear()


specs = (
    ('buffer', buffer, lambda: buffer('ab'),
     ('__len__', '__hash__', '__str__', '__repr__', '__getitem__', '__getslice__',
      '__add__', '__mul__', '__rmul__', '__cmp__', '__setitem__', '__setslice__',
      '__delitem__', '__delslice__')),
    ('memoryview', memoryview, lambda: memoryview('ab'),
     ('__len__', '__repr__', '__getitem__', '__setitem__', '__delitem__',
      '__eq__', '__ne__', '__lt__', '__le__', '__gt__', '__ge__', 'tobytes', 'tolist')),
)
byte_methods = ('__add__', '__mul__', '__rmul__', '__iadd__', '__imul__',
                'append', 'extend', 'find', 'rfind', 'index', 'rindex', 'insert',
                'pop', 'remove', 'reverse', '__alloc__', 'fromhex', 'capitalize',
                'center', 'count', 'decode', 'endswith', 'expandtabs', 'isalnum',
                'isalpha', 'isdigit', 'islower', 'isspace', 'istitle', 'isupper',
                'join', 'ljust', 'lower', 'lstrip', 'partition', 'replace',
                'rjust', 'rpartition', 'rsplit', 'rstrip', 'split', 'splitlines',
                'startswith', 'strip', 'swapcase', 'title', 'translate', 'upper', 'zfill')
specs += (('bytearray', bytearray, lambda: bytearray('ab'), byte_methods),
          ('subtype', Bytes, lambda: Bytes('ab'), byte_methods))
arguments = ((), (0,), ('a',), (u'a',), (None,), (0, 1), (0, 'a'),
             ('a', 'b'), (0, 1, 2), (0, 1, 2, 3), ('a', 1, 2),
             ('a', None, None), (('a', u'bad'),), ((u'bad', 'a'),))


def method_call(owner, factory, name, bound, args, keyword):
    receiver = factory()
    callback = getattr(receiver if bound else owner, name)
    positional = args if bound or name == 'fromhex' else (receiver,) + args
    kwargs = {'extra': 1} if keyword else {}
    result = caught(lambda: callback(*positional, **kwargs), name, receiver)
    return result, normalized(receiver, '', receiver)


for label, owner, factory, methods in specs:
    for name in methods:
        for bound in (False, True):
            for index, args in enumerate(arguments):
                for keyword in (False, True):
                    emit('method/%s/%s/%d/%d/%d' % (label, name, bound, index, keyword),
                         lambda: method_call(owner, factory, name, bound, args, keyword))


constructor_args = ((), ('ab',), ('ab', 0), ('ab', 0, 1), ('ab', 0, 1, 2),
                    (1,), ('ab', -1), ('ab', 0, -2), ('ab', 'bad'))
constructor_keywords = ({}, {'object': 'ab'}, {'extra': 1}, {'object': 'ab', 'extra': 1})
for owner in (buffer, memoryview):
    for index, args in enumerate(constructor_args):
        for keyword, kwargs in enumerate(constructor_keywords):
            emit('constructor/%s/%d/%d' % (owner.__name__, index, keyword),
                 lambda: caught(lambda: owner(*args, **kwargs)))


sources = (lambda text: text, lambda text: unicode(text), lambda text: buffer(text),
           lambda text: bytearray(text), lambda text: memoryview(text), lambda text: None)
for owner in (bytearray, Bytes):
    for index, factory in enumerate(sources):
        for text in ('61', '61 62', '', '6g'):
            emit('fromhex/%s/%d/%s' % (owner.__name__, index, text),
                 lambda: caught(lambda: owner.fromhex(factory(text))))


prefixes = ('a', 'b', '', u'a', None, bytearray('a'), buffer('a'),
            ('a', u'bad'), (u'bad', 'a'))
bounds = ((0, 2), (0, None), (None, None), (-10, 100), (10, 100),
          (1, 0), (-1, 2), ('bad', 2), (0, 'bad'))
for owner in (bytearray, Bytes):
    for name in ('startswith', 'endswith'):
        for index, prefix in enumerate(prefixes):
            for boundary, pair in enumerate(bounds):
                emit('prefix/%s/%s/%d/%d' % (owner.__name__, name, index, boundary),
                     lambda: caught(lambda: getattr(owner('ab'), name)(prefix, *pair)))


for owner in (str, unicode, bytearray):
    for name in ('Xx-Yy', 'xx yy', 'a', ''):
        def decode():
            value = owner('ab')
            callback = value.encode if owner is unicode else value.decode
            return callback(name)
        emit('codec/%s/%s' % (owner.__name__, name), lambda: caught(decode))


# The host coordinator additionally checks this exact, positive cardinality.
assert len(identities) == 7456
