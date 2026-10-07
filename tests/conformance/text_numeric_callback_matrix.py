"""Finite numeric payload, codec error-state, join and SRE callback outcomes."""

import _codecs
import _sre

events = []


def emit(identity, operation):
    events[:] = []
    try:
        value = operation()
        result = ('ok', type(value).__name__, repr(value), events[:])
    except Exception as error:
        details = None
        if type(error) in (UnicodeEncodeError, UnicodeDecodeError, UnicodeTranslateError):
            details = (repr(error.encoding) if hasattr(error, 'encoding') else None,
                       repr(error.object), error.start, error.end, repr(error.reason))
        result = ('error', type(error).__name__, str(error), details, events[:])
    print identity + '\t' + repr(result)


class Integer(int):
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


class Long(long):
    __int__ = Integer.__dict__['__int__']
    __long__ = Integer.__dict__['__long__']
    __float__ = Integer.__dict__['__float__']
    __complex__ = Integer.__dict__['__complex__']


class Float(float):
    __int__ = Integer.__dict__['__int__']
    __long__ = Integer.__dict__['__long__']
    __float__ = Integer.__dict__['__float__']
    __complex__ = Integer.__dict__['__complex__']


class Complex(complex):
    __int__ = Integer.__dict__['__int__']
    __long__ = Integer.__dict__['__long__']
    __float__ = Integer.__dict__['__float__']
    __complex__ = Integer.__dict__['__complex__']


numbers = (False, True, -2, 2L, 1.75, 2+3j, Integer(2), Long(2),
           Float(1.75), Complex(2+3j), Long((1L << 53) + 1),
           Long(1L << 1100), Float('nan'), Float('inf'), Float('-inf'))
unary_names = ('real', 'imag', 'numerator', 'denominator', 'conjugate',
               '__getnewargs__', '__trunc__', 'bit_length')


def unary(value, name):
    result = getattr(value, name)
    return result() if callable(result) else result


def coerce(left, right):
    result = left.__coerce__(right)
    if result is NotImplemented:
        return 'NotImplemented'
    return (type(result[0]).__name__, repr(result[0]), result[0] is left,
            type(result[1]).__name__, repr(result[1]), result[1] is right)


for i, value in enumerate(numbers):
    for j, name in enumerate(unary_names):
        emit('numeric-unary-%d-%d' % (i, j), lambda: unary(value, name))
    for j, other in enumerate(numbers):
        emit('numeric-coerce-%d-%d' % (i, j), lambda: coerce(value, other))

for i, value in enumerate((1j, Complex(1j))):
    for j, convert in enumerate((int, long, float)):
        emit('complex-convert-%d-%d' % (i, j), lambda: convert(value))
        emit('complex-direct-%d-%d' % (i, j), lambda: getattr(complex, ('__int__', '__long__', '__float__')[j])(value))


class Position(object):
    def __init__(self, answer=1, failure=False):
        self.answer, self.failure = answer, failure
    def __int__(self):
        events.append('int')
        if self.failure:
            raise ValueError('position callback')
        return self.answer


class ClassicPosition:
    def __int__(self):
        events.append('classic-int')
        return 1


class IndexOnly(object):
    def __index__(self):
        raise AssertionError('positions use int protocol')


class IntPosition(int):
    def __int__(self):
        events.append('subtype-int')
        return 0


class LongPosition(long):
    __int__ = IntPosition.__dict__['__int__']


class Unicode(unicode):
    def __unicode__(self):
        raise AssertionError('replacement uses payload')
    def __str__(self):
        raise AssertionError('replacement uses payload')


class Answer(tuple):
    pass


positions = (True, 1, 1L, IntPosition(1), LongPosition(1), Position(),
             ClassicPosition(), IndexOnly(), Position(failure=True),
             Position(1.5), 1.5, None, 1L << 100, -1, -2, 2)
codec_modes = ((False, 'ascii', u'\xff'), (False, 'latin-1', u'\u1234'),
               (True, 'ascii', '\xff'), (True, 'utf-8', '\xff'))


def convert(source, decode, encoding, handler):
    return source.decode(encoding, handler) if decode else source.encode(encoding, handler)


def with_position(source, decode, encoding, position, replacement):
    calls = []
    def handler(error):
        calls.append(1)
        events.append(('handler', error.start, error.end))
        return Answer((replacement, position if len(calls) == 1 else error.end))
    _codecs.register_error('matrix-callback-position', handler)
    return convert(source, decode, encoding, 'matrix-callback-position')


for i, (decode, encoding, source) in enumerate(codec_modes):
    for j, position in enumerate(positions):
        for k, replacement in enumerate(('X', u'X', Unicode(u'X\0Y'), u'\u1234')):
            emit('codec-position-%d-%d-%d' % (i, j, k),
                 lambda: with_position(source, decode, encoding, position, replacement))


def with_shape(source, decode, encoding, answer):
    def handler(error):
        events.append(('handler', error.start, error.end))
        return answer
    _codecs.register_error('matrix-callback-shape', handler)
    return convert(source, decode, encoding, 'matrix-callback-shape')


for i, (decode, encoding, source) in enumerate(codec_modes):
    for j, answer in enumerate((None, [], [u'X', 1], (u'X',), (u'X', 1, 2),
                               ('X', 1), (7, 1), Answer((u'X', Position())))):
        emit('codec-shape-%d-%d' % (i, j), lambda: with_shape(source, decode, encoding, answer))


def state_outcome(source, decode, encoding, mutate):
    seen = []
    def replacement(error):
        events.append('replacement')
        return u'Y', error.end
    def handler(error):
        events.append((error.start, error.end, error.reason, error.encoding,
                       repr(error.object), bool(seen) and seen[-1] is error))
        seen.append(error)
        next_position = error.end
        if mutate == 0:
            _codecs.register_error('matrix-callback-state', replacement)
        elif mutate == 1:
            error.reason, error.start, error.end = 'edited', -9, -8
        else:
            error.encoding = 'edited'
            error.object = 'edited' if decode else u'edited'
        return u'X', next_position
    _codecs.register_error('matrix-callback-state', handler)
    result = convert(source, decode, encoding, 'matrix-callback-state')
    return result, len(seen), seen[0] is seen[-1]


for i, (decode, encoding, unused) in enumerate(codec_modes):
    source = '\xff\0\xff' if decode else u'\u1234\0\u1234'
    for j in range(3):
        emit('codec-state-%d-%d' % (i, j), lambda: state_outcome(source, decode, encoding, j))


def object_change_outcome(source, decode, encoding, phase, changed, position):
    calls = []
    class InputPosition(object):
        def __init__(self, error):
            self.error = error
        def __int__(self):
            events.append('position')
            self.error.object = changed if decode else unicode(changed)
            return position
    def handler(error):
        calls.append(error)
        events.append(('handler', error.start, error.end))
        if len(calls) > 1:
            return u'Y', 2
        if phase == 0:
            error.object = changed if decode else unicode(changed)
            return u'X', position
        return u'X', InputPosition(error)
    _codecs.register_error('matrix-callback-input-change', handler)
    return convert(source, decode, encoding, 'matrix-callback-input-change')


for i, (decode, encoding, unused) in enumerate(codec_modes):
    source = '\xffa' if decode else u'\u1234a'
    for j, phase in enumerate((0, 1)):
        for k, changed in enumerate(('Z', 'WXYZ')):
            for m, position in enumerate((0, 1, 2, -1, -2, 3)):
                emit('codec-input-change-%d-%d-%d-%d' % (i, j, k, m),
                     lambda: object_change_outcome(source, decode, encoding, phase, changed, position))


def position_release_outcome(source, decode, encoding, replacement):
    seen = []
    class ReleasedPosition(object):
        def __init__(self, error):
            self.error = error
        def __int__(self):
            events.append('int')
            return 1
        def __del__(self):
            events.append('del')
            self.error.reason = 'destroyed'
    def handler(error):
        seen.append(error)
        return replacement, ReleasedPosition(error)
    _codecs.register_error('matrix-callback-position-release', handler)
    result = convert(source, decode, encoding, 'matrix-callback-position-release')
    return result, seen[0].reason


for i, (decode, encoding, source) in enumerate(codec_modes):
    for j, replacement in enumerate((u'X', u'\u1234')):
        emit('codec-position-release-%d-%d' % (i, j),
             lambda: position_release_outcome(source, decode, encoding, replacement))


class Encode(UnicodeEncodeError):
    def __getattribute__(self, name):
        if name in ('object', 'start', 'end'):
            events.append(name)
            raise ValueError('field callback')
        return UnicodeEncodeError.__getattribute__(self, name)


class Decode(UnicodeDecodeError):
    __getattribute__ = Encode.__dict__['__getattribute__']


class Translate(UnicodeTranslateError):
    __getattribute__ = Encode.__dict__['__getattribute__']


errors = (ValueError('bad'), ValueError, 7, UnicodeError('bad'),
          UnicodeEncodeError('ascii', u'a\xff', 1, 2, 'bad'),
          UnicodeDecodeError('ascii', 'a\xff', 1, 2, 'bad'),
          UnicodeTranslateError(u'a\xff', 1, 2, 'bad'),
          Encode('ascii', u'a\xff', 1, 2, 'bad'),
          Decode('ascii', 'a\xff', 1, 2, 'bad'),
          Translate(u'a\xff', 1, 2, 'bad'))
for i, error in enumerate(errors):
    for j, mode in enumerate(('strict', 'ignore', 'replace', 'xmlcharrefreplace', 'backslashreplace')):
        emit('codec-builtin-%d-%d' % (i, j), lambda: _codecs.lookup_error(mode)(error))


for i, separator in enumerate(('', u'', str.__new__(str, '\0'), unicode.__new__(unicode, u'\0'))):
    for j, values in enumerate(([1, u'x'], [u'x', 1], ['a', 1], ['\xff', 1],
                                [u'x', '\xff', 1], [], ['a\0', u'b'],
                                [bytearray('a')], [buffer('a')], [memoryview('a')])):
        emit('join-%d-%d' % (i, j), lambda: separator.join(values))

letter = _sre.compile('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1],
                      0, {}, [None])


def substitute(subject, answer, count, with_count):
    def replacement(match):
        events.append((match.span(), repr(match.group()), match.string is subject))
        return answer
    return (letter.subn if with_count else letter.sub)(replacement, subject, count)


for i, subject in enumerate(('a\0a', u'a\0a', 'ba', u'ba')):
    for j, answer in enumerate((None, 'X\0', u'X\0', bytearray('X'), buffer('X'), memoryview('X'), 7)):
        for k, count in enumerate((0, 1, -1)):
            for m, with_count in enumerate((False, True)):
                emit('sre-sub-%d-%d-%d-%d' % (i, j, k, m),
                     lambda: substitute(subject, answer, count, with_count))
