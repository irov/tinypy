"""Bounded native Unicode buffers and bytearray callback state products.

Mutations keep byte storage lengths unchanged. No reference-runtime source
pointer invalidation or malformed native input is part of these products.
"""

import sys


class Bytes(bytearray):
    pass


class Text(unicode):
    pass


def emit(identity, operation):
    print identity + '\t' + repr(operation())


def result(operation):
    try:
        value = operation()
        if isinstance(value, (bytearray, buffer)):
            return 'value', type(value).__name__, str(value)
        if isinstance(value, list):
            return 'value', [str(item) for item in value]
        return 'value', value
    except BaseException as error:
        return 'error', type(error).__name__, error.args


def unicode_case(text, subtype, offset, size):
    source = Text(text) if subtype else text
    view = buffer(source, offset, size)
    nested = buffer(view, 1, 3)
    return str(view).encode('hex'), len(view), str(nested).encode('hex'), memoryview(view).tolist(), str(buffer(source)).encode('hex')


def bound_case(method, subtype, mutation, value, position):
    target, needle = (Bytes if subtype else bytearray)('aba'), bytearray('a')
    events = []
    class Bound(object):
        def __index__(self):
            events.append('index')
            if mutation == 'target':
                target[:] = 'ccc'
            elif mutation == 'argument':
                needle[:] = 'b'
            elif mutation == 'error':
                raise LookupError('bound')
            return value
    arguments = (needle, Bound()) if position == 'start' else (needle, 0, Bound())
    answer = result(lambda: getattr(target, method)(*arguments))
    return answer, str(target), str(needle), events, bytearray('aba').find('b')


def scalar_case(method, subtype, mutation, kind, value):
    target = (Bytes if subtype else bytearray)('aba')
    argument, replacement, events = bytearray('a'), bytearray('x'), []
    def callback(name):
        events.append(name)
        if mutation == 'target':
            target[:] = 'CCC'
        elif mutation == 'argument':
            argument[:] = 'b'
            replacement[:] = 'y'
        elif mutation == 'error':
            raise LookupError('number')
        return value
    class Integer(object):
        def __int__(self):
            return callback('int')
    class Index(object):
        def __index__(self):
            return callback('index')
    number = {'int': lambda: value, 'long': lambda: long(value), 'float': lambda: float(value),
              'int-hook': Integer, 'index-hook': Index}[kind]()
    def invoke():
        if method == 'replace':
            return target.replace(argument, replacement, number)
        if method in ('split', 'rsplit'):
            return getattr(target, method)(argument, number)
        return getattr(target, method)(number)
    return result(invoke), str(target), str(argument), str(replacement), events, str(bytearray('a').upper())


def join_case(subtype, source_kind, mutation, policy, hint):
    separator = (Bytes if subtype else bytearray)('-')
    item, events = bytearray('a'), []
    second = {'bytes': 'b', 'unicode': u'b', 'buffer': buffer('b'), 'view': memoryview('b'), 'tail': None}[policy]
    values = [item, second]
    class Iterator(object):
        def __init__(self):
            self.index = 0
        def __iter__(self):
            events.append('iter')
            if mutation == 'separator':
                separator[:] = '_'
            return self
        def __length_hint__(self):
            events.append('hint')
            if hint == 'error':
                raise LookupError('hint')
            return -1 if hint == 'negative' else 2
        def next(self):
            events.append(('next', self.index))
            if self.index == 2:
                if mutation == 'item':
                    item[:] = 'z'
                if policy == 'tail':
                    raise LookupError('tail')
                raise StopIteration
            value = values[self.index]
            self.index += 1
            return value
    class Sequence(list):
        def __iter__(self):
            return Iterator()
    class Iterable(object):
        def __iter__(self):
            events.append('outer-iter')
            return Iterator()
        def __len__(self):
            raise AssertionError('hint is requested on returned iterator')
    source = {'list': lambda: values, 'subtype': lambda: Sequence(values),
              'iterable': Iterable, 'iterator': Iterator}[source_kind]()
    return result(lambda: separator.join(source)), str(separator), str(item), events, str(bytearray('-').join(['a', 'b']))


def hex_case(text, unicode_source, subtype):
    source = unicode(text) if unicode_source else text
    receiver = Bytes if subtype else bytearray
    return result(lambda: receiver.fromhex(source)), str(receiver.fromhex('61'))


def view_case(readonly, direct, key_kind, source_kind):
    target, events = bytearray('ab'), []
    view = memoryview('ab') if readonly else memoryview(target)
    class Index(object):
        def __index__(self):
            events.append('index')
            target[1] = 90
            return 0
    keys = {'zero': lambda: 0, 'negative': lambda: -1, 'slice': lambda: slice(None),
            'outside': lambda: 3, 'float': lambda: 1.5, 'index': Index}
    sources = {'str': lambda: 'z', 'bytes': lambda: bytearray('z'), 'buffer': lambda: buffer('z'),
               'view': lambda: memoryview('z'), 'long': lambda: 'zz', 'empty': lambda: '',
               'unicode': lambda: u'z', 'none': lambda: None}
    key, source = keys[key_kind](), sources[source_kind]()
    def invoke():
        if direct:
            return view.__setitem__(key, source)
        view[key] = source
    answer = result(invoke)
    observed = view.tobytes()
    view = None
    target.append(33)
    return answer, observed, events, str(target)


def main():
    for text_id, text in enumerate((u'', u'a', u'\x00\xe9', u'\u4e2d', u'\U0001f642', u'\ud800')):
        for subtype in (False, True):
            for offset in (0, 1, 3, 8, 20):
                for size in (-1, 0, 1, 3, 20):
                    emit('unicode/%s/%s/%s/%s' % (text_id, subtype, offset, size),
                         lambda: unicode_case(text, subtype, offset, size))
    for method in ('find', 'rfind', 'index', 'rindex', 'count', 'startswith', 'endswith'):
        for subtype in (False, True):
            for mutation in ('none', 'target', 'argument', 'error'):
                for value in (-1, 0, 2, 5):
                    for position in ('start', 'stop'):
                        emit('bound/%s/%s/%s/%s/%s' % (method, subtype, mutation, value, position),
                             lambda: bound_case(method, subtype, mutation, value, position))
    for method in ('center', 'ljust', 'rjust', 'zfill', 'expandtabs', 'replace', 'split', 'rsplit', 'splitlines'):
        for subtype in (False, True):
            for mutation in ('none', 'target', 'argument', 'error'):
                for kind in ('int', 'long', 'float', 'int-hook', 'index-hook'):
                    for value in (0, 2):
                        emit('scalar/%s/%s/%s/%s/%s' % (method, subtype, mutation, kind, value),
                             lambda: scalar_case(method, subtype, mutation, kind, value))
    for subtype in (False, True):
        for source_kind in ('list', 'subtype', 'iterable', 'iterator'):
            for mutation in ('none', 'separator', 'item'):
                for policy in ('bytes', 'unicode', 'buffer', 'view', 'tail'):
                    for hint in ('normal', 'negative', 'error'):
                        emit('join/%s/%s/%s/%s/%s' % (subtype, source_kind, mutation, policy, hint),
                             lambda: join_case(subtype, source_kind, mutation, policy, hint))
    for text_id, text in enumerate(('', '61', '61 62', ' 61 ', '6', '6x', 'x6', '61 6x', '\t61', '6 1', '00ff', '61\x00')):
        for unicode_source in (False, True):
            for subtype in (False, True):
                emit('hex/%s/%s/%s' % (text_id, unicode_source, subtype),
                     lambda: hex_case(text, unicode_source, subtype))
    for readonly in (False, True):
        for direct in (False, True):
            for key_kind in ('zero', 'negative', 'slice', 'outside', 'float', 'index'):
                for source_kind in ('str', 'bytes', 'buffer', 'view', 'long', 'empty', 'unicode', 'none'):
                    emit('view/%s/%s/%s/%s' % (readonly, direct, key_kind, source_kind),
                         lambda: view_case(readonly, direct, key_kind, source_kind))


if __name__ == '__main__':
    main()
