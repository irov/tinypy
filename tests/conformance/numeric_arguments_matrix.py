"""Finite numeric descriptors, argument parsing, snapshots and Unicode specs."""

import sys

class I(int): pass
class L(long): pass
class F(float): pass
class C(complex): pass

def describe(value, original=None):
    if isinstance(value, tuple):
        return tuple(describe(item, original) for item in value)
    return type(value).__name__, repr(value), value is original

def emit(identity, operation):
    try:
        answer = operation()
    except BaseException as error:
        answer = 'error', type(error).__name__, error.args
    print identity + '\t' + repr(answer)
    sys.exc_clear()


def callback_case(owner, direct, policy, handling):
    events = []
    marker = LookupError('format spec')
    outer = KeyError('outer')
    class Specification(unicode):
        def __str__(self):
            events.append(('str', sys.exc_info()[1] is outer))
            if policy == 'raise':
                raise marker
            if policy == 'reenter':
                events.append(('nested', owner(2).__format__('')))
            return '.1f' if policy != 'empty' else ''
    value = owner(3)
    specification = Specification('unconverted')
    def invoke():
        try:
            rendered = value.__format__(specification) if direct else format(value, specification)
            return 'value', type(rendered).__name__, rendered
        except BaseException as error:
            return 'error', type(error).__name__, error.args, error is marker
    if handling:
        try:
            raise outer
        except KeyError:
            answer = invoke()
            preserved = sys.exc_info()[1] is outer
    else:
        answer = invoke()
        preserved = True
    return answer, events, preserved, value.__format__('')

def main():
    for type_id, owner in enumerate((int, long, float, complex)):
        source = owner(3)
        subclass = (I, L, F, C)[type_id](3)
        names = ('conjugate', '__getnewargs__', '__trunc__', 'bit_length', 'is_integer', 'as_integer_ratio', 'hex', '__hex__', '__oct__')
        for name in names:
            if not hasattr(owner, name):
                continue
            for value_id, value in enumerate((source, subclass)):
                for mode in xrange(3):
                    callback = getattr(value, name) if mode == 0 else getattr(owner, name)
                    prefix = () if mode == 0 else (value,)
                    if mode == 2:
                        callback = getattr(owner, name).__get__(value, owner)
                        prefix = ()
                    for count in (0, 1, 2):
                        emit('noargs/%d/%s/%d/%d/%d' % (type_id, name, value_id, mode, count),
                            lambda: describe(callback(*(prefix + (None,) * count)), value))
                    emit('kw/%d/%s/%d/%d' % (type_id, name, value_id, mode),
                        lambda: describe(callback(*prefix, unknown=1), value))
            emit('missing/%d/%s' % (type_id, name), lambda: getattr(owner, name)())
        for other_id, other in enumerate((True, 3, I(3), 3L, L(3), 3.0, F(3), 3j, C(3j), None)):
            emit('coerce/%d/%d' % (type_id, other_id),
                lambda: describe(source.__coerce__(other), source))
            emit('coerce-sub/%d/%d' % (type_id, other_id),
                lambda: describe(subclass.__coerce__(other), subclass))
        for other_id, other in enumerate((source, subclass, True, 3, 3L, 3.0, None)):
            if hasattr(source, '__cmp__'):
                emit('cmp/%d/%d' % (type_id, other_id), lambda: source.__cmp__(other))

    for method in ('__getformat__', '__setformat__', 'fromhex'):
        for count in xrange(4):
            emit('class-arity/%s/%d' % (method, count), lambda: getattr(float, method)(*('float',) * count))
            emit('class-keyword/%s/%d' % (method, count), lambda: getattr(float, method)(*('float',) * count, unknown=1))

    for value_id, value in enumerate((None, 3, u'float', u'\xe9', 'unknown', 'float', 'float\x00tail', 'double')):
        emit('getformat/%d' % value_id, lambda: float.__getformat__(value))

    for value_id, value in enumerate((None, 3, u'1.0', u'\xe9', '1.0', '1\x00tail')):
        emit('fromhex/%d' % value_id, lambda: describe(float.fromhex(value)))

    saved = float.__getformat__('float')
    try:
        for kind_id, kind in enumerate((None, 3, u'float', u'\xe9', 'unknown', 'float', 'float\x00tail')):
            for format_id, new_format in enumerate((None, 3, u'unknown', 'bad', 'unknown', saved, 'unknown\x00tail')):
                emit('setformat/%d/%d' % (kind_id, format_id), lambda: float.__setformat__(kind, new_format))
    finally:
        float.__setformat__('float', saved)

    for value in (True, False):
        emit('bool-getnewargs/%d' % value, lambda: describe(value.__getnewargs__(), value))

    for owner in (int, long, float, complex):
        self = owner(3)
        for name in ('__coerce__', '__cmp__', '__format__', '__hex__', '__oct__'):
            if not hasattr(owner, name):
                continue
            callback = getattr(self, name)
            for count in xrange(4):
                emit('arity/%s/%s/%d' % (owner.__name__, name, count), lambda: callback(*('',) * count))
                emit('keywords/%s/%s/%d' % (owner.__name__, name, count), lambda: callback(*('',) * count, unknown=1))
        for index, spec in enumerate(('', u'', 'd', u'd', u'\u4e2d', None, 3)):
            emit('format/%s/%d' % (owner.__name__, index),
                lambda: (type(self.__format__(spec)).__name__, self.__format__(spec)))

    events = []
    class Spec(unicode):
        def __str__(self):
            events.append('str')
            return '04d'
    for owner in (int, long, float, complex):
        emit('format-subtype/%s' % owner.__name__, lambda: (owner(3).__format__(Spec('bad')), list(events)))
        del events[:]
        emit('format-public/%s' % owner.__name__, lambda: (format(owner(3), Spec('bad')), list(events)))
        del events[:]


    for type_id, owner in enumerate((long, float, complex)):
        subtype = (L, F, C)[type_id]
        for subtype_id, factory in enumerate((owner, subtype)):
            for payload_id, payload in enumerate((0, -3, 2 ** 70)):
                source = factory(payload)
                for mode in (0, 1):
                    callback = source.__getnewargs__ if mode == 0 else getattr(owner, '__getnewargs__')
                    prefix = () if mode == 0 else (source,)
                    def snapshot():
                        first = callback(*prefix)
                        second = callback(*prefix)
                        return describe(first, source), first[0] is second[0], len(first) > 1 and first[0] is first[1]
                    emit('snapshot/%d/%d/%d/%d' % (type_id, subtype_id, payload_id, mode), snapshot)

    for owner_id, owner in enumerate((int, long, float, complex, I, L, F, C)):
        for direct in (False, True):
            for policy in ('empty', 'replace', 'raise', 'reenter'):
                for handling in (False, True):
                    emit('callback/%d/%d/%s/%d' % (owner_id, int(direct), policy, int(handling)),
                         lambda: callback_case(owner, direct, policy, handling))


if __name__ == '__main__':
    main()
