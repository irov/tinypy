"""Bounded text result, codec parser and valid SRE callback products."""

import _codecs
import _sre
import sys


events = []
marker = ValueError('matrix callback')


def describe(value):
    if isinstance(value, (list, tuple)):
        return type(value).__name__, tuple(describe(item) for item in value)
    if isinstance(value, buffer):
        return 'buffer', repr(str(value))
    return type(value).__name__, repr(value)


def emit(identity, operation):
    events[:] = []
    try:
        result = 'ok', describe(operation()), events[:]
    except Exception as error:
        result = 'error', type(error).__name__, str(error), error is marker, events[:]
    print identity + '\t' + repr(result)


class Text(str):
    pass


class Unicode(unicode):
    pass


class Index(object):
    def __init__(self, mode, name='index'):
        self.mode, self.name = mode, name
    def __index__(self):
        events.append(self.name)
        if self.mode == 1:
            raise marker
        return 0 if self.mode == 0 else 1.5


class Integer(object):
    def __init__(self, mode, name='integer'):
        self.mode, self.name = mode, name
    def __int__(self):
        events.append(self.name)
        if self.mode == 1:
            raise marker
        return 1 if self.mode == 0 else 1.5


receivers = (str, unicode, Text, Unicode)
candidates = ('a', u'a', Text('a'), Unicode(u'a'), buffer('a'), bytearray('a'),
              memoryview('a'), object(), None, 1, ('z', object()), ('a', object()))
methods = ('find', 'count', 'startswith', 'endswith', 'partition', 'rpartition',
           'split', 'rsplit', 'strip', 'lstrip', 'rstrip')
for i, receiver in enumerate(receivers):
    for j, source in enumerate(('', 'aba', 'a\0b')):
        text = receiver(source)
        for k, method in enumerate(methods):
            for n, candidate in enumerate(candidates):
                emit('argument-%d-%d-%d-%d' % (i, j, k, n),
                     lambda: getattr(text, method)(candidate))

for i, receiver in enumerate(receivers):
    text = receiver('aba')
    for j, method in enumerate(('find', 'count', 'startswith', 'endswith',
                                 'split', 'rsplit', 'replace')):
        for k, candidate in enumerate(candidates[:10]):
            for mode in range(3):
                limit = Integer(mode) if method in ('split', 'rsplit', 'replace') else Index(mode)
                args = (candidate, 'X', limit) if method == 'replace' else (candidate, limit)
                emit('conversion-%d-%d-%d-%d' % (i, j, k, mode),
                     lambda: getattr(text, method)(*args))

identity_methods = (('strip', ()), ('lstrip', ()), ('rstrip', ()),
                    ('replace', ('z', 'X')), ('replace', ('a', 'X', 0)),
                    ('split', ('z',)), ('rsplit', ('z',)), ('partition', ('z',)),
                    ('rpartition', ('z',)), ('lower', ()), ('upper', ()),
                    ('capitalize', ()), ('title', ()), ('swapcase', ()),
                    ('zfill', (0,)), ('center', (0,)), ('ljust', (0,)),
                    ('rjust', (0,)), ('expandtabs', (0,)), ('join', None))
for i, receiver in enumerate(receivers):
    for j, source in enumerate(('', 'abc', 'ABC', 'Abc', '123 !', '\0', u'\xdf', u'\xe9')):
        if receiver in (str, Text) and isinstance(source, unicode):
            text = receiver(source.encode('latin1'))
        else:
            text = receiver(source)
        for k, (method, args) in enumerate(identity_methods):
            def observe():
                value = getattr(text, method)(*((text,),) if args is None else args)
                if isinstance(value, (list, tuple)):
                    item = value[2 if method == 'rpartition' else 0]
                else:
                    item = value
                return describe(value), type(item).__name__, item is text
            emit('identity-%d-%d-%d' % (i, j, k), observe)


def transform(*args):
    events.append(('transform', len(args), tuple(type(value).__name__ for value in args)))
    return 'converted', None


def search(encoding):
    events.append(('search', encoding))
    if encoding == 'tinypy_eighth_matrix':
        return transform, transform, None, None


_codecs.register(search)
# Prime this codec independently of individual observations; the product checks
# callback arity and parser order without making registry cache hits an axis.
_codecs.lookup('tinypy_eighth_matrix')
codec_arguments = (None, 'ascii', u'ascii', Text('ascii'), 'ascii\0',
                   u'ascii\0', u'\xe9', object(), 1)
for i, receiver in enumerate(receivers):
    text = receiver('abc')
    for j, method in enumerate(('encode', 'decode')):
        for k, value in enumerate(codec_arguments):
            for slot in (0, 1):
                args = (value,) if slot == 0 else ('tinypy_eighth_matrix', value)
                emit('codec-method-%d-%d-%d-%d' % (i, j, k, slot),
                     lambda: getattr(text, method)(*args))
                emit('codec-module-%d-%d-%d-%d' % (i, j, k, slot),
                     lambda: getattr(_codecs, method)(text, *args))
        emit('codec-module-default-%d-%d' % (i, j), lambda: getattr(_codecs, method)(text))
        emit('codec-module-custom-%d-%d' % (i, j),
             lambda: getattr(_codecs, method)(text, 'tinypy_eighth_matrix'))
        emit('codec-method-custom-%d-%d' % (i, j),
             lambda: getattr(text, method)('tinypy_eighth_matrix'))


class Key(str):
    mode = 0
    def __eq__(self, other):
        events.append(('equal', str(self), other))
        if self.mode == 2:
            raise marker
        return self.mode == 0
    __hash__ = str.__hash__


for i, receiver in enumerate(receivers):
    text = receiver('abc')
    for j, method in enumerate(('encode', 'decode')):
        for k, name in enumerate(('encoding', 'errors', 'other')):
            for mode in range(3):
                key = Key(name)
                key.mode = mode
                kwargs = {key: 'ascii'}
                def observe():
                    try:
                        raise marker
                    except ValueError:
                        value = getattr(text, method)(**kwargs)
                        return value, sys.exc_info()[1] is marker
                emit('codec-keyword-%d-%d-%d-%d' % (i, j, k, mode), observe)


letter = _sre.compile('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1],
                      0, {}, [None])


class Subject(str):
    reported = 3
    def __len__(self):
        events.append('length')
        return self.reported
    def __getslice__(self, start, end):
        events.append(('slice', start, end))
        return str.__getslice__(self, start, end)


class UnicodeSubject(unicode):
    reported = 3
    __len__ = Subject.__dict__['__len__']
    def __getslice__(self, start, end):
        events.append(('slice', start, end))
        return unicode.__getslice__(self, start, end)


class Bytes(bytearray):
    reported = 3
    __len__ = Subject.__dict__['__len__']


def regex_observe(method, source, args):
    if method in ('sub', 'subn'):
        return getattr(letter, method)('X', source, *args)
    result = getattr(letter, method)(source, *args)
    if method == 'finditer':
        return [(match.span(), match.pos, match.endpos, match.string is source) for match in result]
    if method == 'scanner':
        result = result.search()
    if method in ('match', 'search', 'scanner'):
        return None if result is None else (result.span(), result.pos, result.endpos, result.string is source)
    return result


regex_methods = ('match', 'search', 'findall', 'finditer', 'scanner', 'split', 'sub', 'subn')
for i, factory in enumerate((Subject, UnicodeSubject, Bytes)):
    for reported in (1, 2, 3):
        source = factory('aba')
        source.reported = reported
        for j, method in enumerate(regex_methods):
            for k, args in enumerate(((), (Integer(0, 'position-or-count'),),
                                       (Integer(1, 'position-or-count'),))):
                emit('sre-state-%d-%d-%d-%d' % (i, reported, j, k),
                     lambda: regex_observe(method, source, args))

sre_calls = (((), {}), ((), {'other': 1}), ((), {'string': 'aba'}),
             (('aba',), {'pattern': 'aba'}), ((), {'string': 'aba', 'pattern': 'aba'}),
             (('aba', 0, 3, 9), {}), (('aba',), {u'other': 1}),
             (('aba', 0), {'pos': 1}), ((), {'source': 'aba'}),
             (('X', 'aba'), {'count': Integer(0)}),
             ((), {'repl': 'X', 'string': 'aba', 'count': Integer(0)}))
for i, method in enumerate(regex_methods):
    for j, (args, kwargs) in enumerate(sre_calls):
        def observe():
            result = getattr(letter, method)(*args, **kwargs)
            if method == 'finditer':
                return [match.span() for match in result]
            if method == 'scanner':
                result = result.search()
            if method in ('match', 'search', 'scanner'):
                return None if result is None else result.span()
            return result
        emit('sre-parser-%d-%d' % (i, j), observe)

for i, method in enumerate(('match', 'search', 'findall', 'split')):
    for j, name in enumerate(('string', 'pos', 'endpos', 'other')):
        for mode in range(3):
            key = Key(name)
            key.mode = mode
            kwargs = {key: 'aba' if name == 'string' else 1}
            args = () if name == 'string' else ('aba',)
            def observe():
                try:
                    raise marker
                except ValueError:
                    result = getattr(letter, method)(*args, **kwargs)
                    if method in ('match', 'search'):
                        result = None if result is None else result.span()
                    return result, sys.exc_info()[1] is marker
            emit('sre-keyword-%d-%d-%d' % (i, j, mode), observe)


class Replacement(str):
    def __len__(self):
        events.append('replacement-length')
        return str.__len__(self)


class CallableReplacement(Replacement):
    def __call__(self, match):
        events.append(('replace', match.span()))
        return 'X'


for i, replacement in enumerate((Replacement('X'), CallableReplacement('\\template'),
                                  buffer('X'), bytearray('X'))):
    for j, method in enumerate(('sub', 'subn')):
        for count in (-1, 0, 1):
            emit('sre-replacement-%d-%d-%d' % (i, j, count),
                 lambda: getattr(letter, method)(replacement, Subject('aba'), count))

captured = (_sre.compile('(?P<word>a)?', 0,
                [28, 9, 0, 1, 21, 0, 19, 97, 21, 1, 22, 1],
                1, {'word': 1}, [None, 'word']),
            _sre.compile('(a)(b)?', 0,
                [17, 8, 1, 1, 2, 1, 0, 97, 0, 21, 0, 19, 97, 21, 1,
                 28, 9, 0, 1, 21, 2, 19, 98, 21, 3, 22, 1],
                2, {}, [None, None, None]))
for i, pattern in enumerate(captured):
    for j, factory in enumerate((Subject, UnicodeSubject, Bytes)):
        for k, source in enumerate(('aaa', 'aba', 'bbb')):
            emit('sre-captured-%d-%d-%d' % (i, j, k),
                 lambda: pattern.findall(factory(source)))
