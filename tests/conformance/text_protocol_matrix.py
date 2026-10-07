"""Finite Python 2 text, conversion and constructor protocol outcomes."""

events = []


def emit(identity, operation):
    events[:] = []
    try:
        value = operation()
        result = ('ok', type(value).__name__, repr(value), events[:])
    except Exception as error:
        detail = None
        if isinstance(error, UnicodeEncodeError):
            detail = (error.encoding, repr(error.object), error.start, error.end, error.reason)
        result = ('error', type(error).__name__, str(error), detail, events[:])
    print identity + '\t' + repr(result)


class NumericString(str):
    def __int__(self):
        events.append('int')
        return 7
    def __long__(self):
        events.append('long')
        return 8L
    def __float__(self):
        events.append('float')
        return 9.5
    def __complex__(self):
        events.append('complex')
        return 10+2j


class NumericUnicode(unicode):
    __int__ = NumericString.__dict__['__int__']
    __long__ = NumericString.__dict__['__long__']
    __float__ = NumericString.__dict__['__float__']
    __complex__ = NumericString.__dict__['__complex__']


numeric_sources = ('', '0', '-1', '1.25', '+ 1', '  bad  ', '1\0x',
                   u'', u'\u0661\u0662', u'\u00a012\u00a0', u'\xe9',
                   u'\u1234', u'12\u1234\u1235x\u1236', u'1\0x',
                   NumericString('2'), NumericUnicode(u'2'),
                   bytearray('12'), buffer('12'), memoryview('12'))
for i, source in enumerate(numeric_sources):
    for j, convert in enumerate((int, long, float, complex)):
        emit('convert-%d-%d' % (i, j), lambda: convert(source))


class Base(object):
    def __int__(self):
        events.append('int')
        return 16
    def __index__(self):
        raise AssertionError('base uses int')


class LongBase(long):
    def __int__(self):
        events.append('long')
        return 2


bases = (0, 2, 10, 16, 36, 1, 2.0, None, 2**32, -(2**32), 2**65,
         Base(), LongBase(16))
for i, source in enumerate(('10', '  1.2  ', '1\0x', u'\u0661\u0662',
                             u'\xe9', u'1\0x', NumericString('10'),
                             bytearray('10'), buffer('10'))):
    for j, base in enumerate(bases):
        for k, convert in enumerate((int, long)):
            emit('base-%d-%d-%d' % (i, j, k), lambda: convert(source, base))


returns = (True, 1, 1L, 1.25, 1+2j, '12', None)
for i, answer in enumerate(returns):
    class Source(object):
        def __int__(self):
            events.append('int')
            return answer
        def __long__(self):
            events.append('long')
            return answer
        def __float__(self):
            events.append('float')
            return answer
        def __complex__(self):
            events.append('complex')
            return answer
    for j, convert in enumerate((int, long, float, complex)):
        emit('return-%d-%d' % (i, j), lambda: convert(Source()))


constructor_rows = (((), {'extra':1}), ((1,), {'extra':1}),
                    ((1, 2, 3, 4), {}), ((), {'x':'12'}),
                    ((1,), {'x':2}), ((), {'base':2}),
                    ((), {'real':1, 'imag':2}), ((1,), {'imag':2}),
                    ((), {'imag':2}), ((1,), {'real':2}),
                    ((), {'object':'abc'}), ((), {'string':'abc'}),
                    ((), {'source':'abc'}), (('abc',), {'encoding':'ascii'}),
                    (('abc',), {'errors':'ignore'}),
                    ((), {'encoding':'ascii'}), ((), {'errors':'ignore'}),
                    ((), {'encoding':None}), ((), {'errors':1}),
                    ((), {'encoding':'a\0b'}), ((), {'errors':'a\0b'}),
                    ((), {'encoding':u'a\0b'}), ((), {'encoding':u'\xe9'}),
                    (('abc',), {'encoding':1, 'extra':2}),
                    (('abc',), {'encoding':'ascii', 'errors':1, 'extra':2}))
for i, convert in enumerate((str, unicode, bytearray, int, long, float, complex)):
    for j, (arguments, keywords) in enumerate(constructor_rows):
        emit('keywords-%d-%d' % (i, j), lambda: convert(*arguments, **keywords))


class ComplexSource(object):
    def __complex__(self):
        events.append('complex')
        return 2+3j


class FloatSource(int):
    def __float__(self):
        events.append('float')
        return 2.5


for i, first in enumerate((0, 1+3j, '1', u'1', ComplexSource())):
    for j, second in enumerate((None, 'a', u'a', 1, 1+2j, FloatSource(1))):
        emit('complex-pair-%d-%d' % (i, j), lambda: complex(first, second))


class UnicodeSource(unicode):
    def __unicode__(self):
        events.append('unicode')
        return u'converted'


class StringSource(str):
    def __unicode__(self):
        events.append('unicode')
        return u'converted'
    def decode(self, *args):
        raise AssertionError('builtin decoder required')


class ArraySource(bytearray):
    def __str__(self):
        events.append('str')
        return 'converted'


for i, source in enumerate(('abc', u'abc', bytearray('abc'), buffer('abc'),
                             1, UnicodeSource(u'abc'), StringSource('abc'),
                             ArraySource('abc'))):
    for j, keywords in enumerate(({}, {'encoding':'ascii'}, {'errors':'ignore'},
                                  {'encoding':None}, {'errors':None},
                                  {'encoding':'ascii', 'errors':'ignore'})):
        emit('unicode-%d-%d' % (i, j), lambda: unicode(source, **keywords))
    emit('string-%d' % i, lambda: str(object=source))


mapping_answers = (None, 0, 97, 0x10ffff, 0x110000, -1, True, 97L, 2**65,
                   'x', u'x', u'', [], {}, object())
for i, answer in enumerate(mapping_answers):
    emit('unicode-translate-answer-%d' % i, lambda: u'a'.translate({97:answer}))
for i, table in enumerate((None, 1, [], [None], {}, u'a', bytearray('abc'), buffer('abc'))):
    for j, source in enumerate((u'', u'a', u'\0')):
        emit('unicode-translate-table-%d-%d' % (i, j), lambda: source.translate(table))


class Mapping(object):
    def __getitem__(self, ordinal):
        events.append(ordinal)
        if mapping_mode == 0:
            raise LookupError('missing')
        return (None, u'x', 1L)[mapping_mode - 1]


for mapping_mode in range(4):
    emit('unicode-translate-callback-%d' % mapping_mode,
         lambda: u'aaaa'.translate(Mapping()))

identity_table = ''.join(chr(i) for i in range(256))
translate_tables = (None, identity_table, u'x' * 256,
                    bytearray(identity_table), buffer(identity_table),
                    memoryview(identity_table), 1)
translate_deletions = ('', u'', u'a', bytearray('a'), buffer('a'),
                       memoryview('a'), None, 1)
for i, table in enumerate(translate_tables):
    for j, deletions in enumerate(translate_deletions):
        emit('bytes-translate-%d-%d' % (i, j), lambda: 'abc'.translate(table, deletions))
    emit('bytes-translate-one-%d' % i, lambda: 'abc'.translate(table))


class Keyword(str):
    def __eq__(self, other):
        events.append(('eq', str.__str__(self), other))
        if keyword_mode == 'raise':
            raise ValueError('keyword callback')
        return keyword_mode == 'true'
    def __hash__(self):
        return str.__hash__(self)


for i, (convert, name) in enumerate(((str, 'object'), (int, 'x'), (long, 'x'),
                                    (float, 'x'), (complex, 'real'),
                                    (unicode, 'string'), (bytearray, 'source'))):
    for j, keyword_mode in enumerate(('false', 'true', 'raise')):
        keywords = {Keyword(name):'12'}
        emit('keyword-subtype-%d-%d' % (i, j), lambda: convert(**keywords))
