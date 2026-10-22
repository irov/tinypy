"""Bounded array module behavior: typecodes, constructors, item conversions,
sequence protocol, methods, operators, comparisons and buffers.

Items compare through their machine bytes, which both runtimes lay out in
the host byte order with the item sizes of the LP64 UCS-4 build. The SPEC
aliases _struct.error to ValueError, so only that exception is normalized.
buffer_info addresses and __sizeof__ measure runtime-specific storage; their
types are compared. Python callbacks resize arrays only where CPython 2.7
reads the array again afterwards; its conversions that store into an array
a callback resized, fromstring of a view of the array itself and pack_into
into an array its callbacks resize read or write released storage.
"""

import _codecs
import _struct
import _weakref
import array
import sys


A = array.array
A.__dict__
TYPECODES = 'cbBuhHiIlLfd'
identities = set()


class Bytes(str):
    pass


class Text(unicode):
    pass


class Long(long):
    pass


class Int(int):
    pass


class Float(float):
    pass


class IntLike(object):
    def __int__(self):
        return 5


class IndexLike(object):
    def __index__(self):
        return 2


class FloatLike(object):
    def __float__(self):
        return 2.5


class IntReturnsFloat(object):
    def __int__(self):
        return 1.5


class IntReturnsLong(object):
    def __int__(self):
        return 2 ** 70


class ClassicInt:
    def __int__(self):
        return 6


class Reflected(object):
    def __radd__(self, other):
        return 'radd'

    def __rmul__(self, other):
        return 'rmul'

    def __add__(self, other):
        return 'add'

    def __mul__(self, other):
        return 'mul'


class Sub(A):
    pass


class Keywords(A):
    def __init__(self, typecode, extra=None):
        A.__init__(self, typecode)
        self.extra = extra


class Slotted(A):
    __slots__ = ('slot',)


class Overrides(A):
    def __len__(self):
        return 99

    def __getitem__(self, key):
        return 7

    def __iter__(self):
        return iter([5, 6])


class LengthList(list):
    def __len__(self):
        return 1

    def __getitem__(self, index):
        return 9


class LengthTuple(tuple):
    def __len__(self):
        return 2


VALUES = [
    0, 1, -1, 127, 128, -128, -129, 255, 256, 32767, 32768, -32768, -32769,
    65535, 65536, 2 ** 31 - 1, 2 ** 31, -2 ** 31, -2 ** 31 - 1, 2 ** 32 - 1,
    2 ** 32, 2 ** 63 - 1, 2 ** 63, -2 ** 63, -2 ** 63 - 1, 2 ** 64 - 1, 2 ** 64,
    0L, 7L, -7L, True, False, 1.5, -0.0, 1e300, 3.4e38, float('inf'),
    float('nan'), 'a', '', 'ab', u'a', u'', u'ab', unichr(0x100),
    unichr(0x1f600), unichr(0xd800), bytearray('a'), None, IntLike(),
    IndexLike(), FloatLike(), IntReturnsFloat(), IntReturnsLong(), ClassicInt(),
    Long(3), Int(4), Float(2.5), Bytes('x'), Text(u'y'), buffer('q'), [1], (1,),
]


def code_points(value):
    data = value.tostring()
    return [ord(data[index]) | ord(data[index + 1]) << 8 | ord(data[index + 2]) << 16 | ord(data[index + 3]) << 24
            for index in xrange(0, len(data), 4)]


def describe(value):
    if isinstance(value, A):
        valid = value.typecode != 'u' or max(code_points(value) + [0]) <= 0x10ffff
        shown = repr(value) if valid else None
        return ('array', type(value).__name__, value.typecode, value.itemsize, len(value), value.tostring(), shown)
    if isinstance(value, buffer):
        return 'buffer', str(value), len(value)
    if isinstance(value, bytearray):
        return 'bytearray', str(value)
    if isinstance(value, float) and value != value:
        return 'nan'
    if isinstance(value, tuple):
        return tuple(describe(item) for item in value)
    if isinstance(value, list):
        return [describe(item) for item in value]
    if isinstance(value, (IntLike, IndexLike, FloatLike, IntReturnsFloat, IntReturnsLong, Reflected)) or type(value).__name__ == 'instance':
        return type(value).__name__
    if type(value).__name__ in ('arrayiterator', 'reversed'):
        return type(value).__name__
    return value


def outcome(operation):
    try:
        return 'value', describe(operation())
    except BaseException as error:
        name = 'ValueError' if type(error) is _struct.error else type(error).__name__
        return 'error', name, error.args


def emit(identity, operation):
    assert identity not in identities, 'duplicate array outcome identity ' + identity
    identities.add(identity)
    print identity + '\t' + repr(outcome(operation))
    sys.exc_clear()


def sample(typecode, count=3):
    if typecode == 'c':
        return A('c', 'abcdefgh'[:count])
    if typecode == 'u':
        return A('u', u'xyzwvuts'[:count])
    if typecode in 'fd':
        return A(typecode, [0.5, -1.25, 2.0, 3.5, -0.0, 8.0, 9.5, 1e10][:count])
    return A(typecode, [1, 2, 3, 4, 5, 6, 7, 8][:count])


def item_of(typecode):
    return {'c': 'z', 'u': u'q', 'f': 4.5, 'd': 4.5}.get(typecode, 9)


def constructor_cases():
    typecodes = list(TYPECODES) + ['x', '\x80', '', 'ab', u'i', u'u', unichr(0x100), u'', 1, None, Bytes('i'), Text(u'd')]
    initializers = [
        (), ([],), ([1, 2],), ((3, 4),), ([-1],), ([255],), ([65535],), ([2 ** 31],),
        ([2 ** 32],), ('ab',), ('abcd',), ('abcdefgh',), (u'ab',), (u'',),
        (bytearray('ab'),), (buffer('abcd'),), (xrange(3),), (iter([5]),),
        (A('i', [7]),), (A('d', [1.5]),), (A('c', 'q'),), ([1.5],), (['a'],),
        ([u'a'],), (None,), (5,), ({'k': 1},), ((x for x in (2, 3)),),
        (LengthList([1, 2, 3]),), (LengthTuple((1, 2, 3)),), (Bytes('ab'),),
        (Text(u'ab'),), (Sub('i', [4]),), (Overrides('i', [1, 2]),),
    ]
    # 24 typecodes x 34 initializers = 816.
    for typecode_id, typecode in enumerate(typecodes):
        for initializer_id, initializer in enumerate(initializers):
            emit('construct/%d/%d' % (typecode_id, initializer_id), lambda: A(typecode, *initializer))
    # 6 argument forms x 2 types = 12.
    forms = [(), ('i', [], 1), ('i', 'ab', 'cd'), (), ('i',), ('i', [1])]
    keywords = [{}, {}, {}, {'typecode': 'i'}, {'initializer': [1]}, {'other': 1}]
    for target_id, target in enumerate((A, Sub)):
        for form_id in xrange(len(forms)):
            emit('construct-arguments/%d/%d' % (target_id, form_id), lambda: target(*forms[form_id], **keywords[form_id]))
    emit('construct-keywords-init', lambda: (Keywords('i', extra=5), Keywords('i', 5).extra))
    emit('construct-empty-kwargs', lambda: A('i', [1], **{}))
    # __new__ receivers: 6.
    emit('new/array', lambda: A.__new__(A, 'i', [1]))
    emit('new/sub', lambda: A.__new__(Sub, 'h', [2]))
    emit('new/int', lambda: A.__new__(int, 'i'))
    emit('new/none', lambda: A.__new__())
    emit('new/value', lambda: A.__new__(1))
    emit('new/object', lambda: object.__new__(A))


def conversion_cases():
    operations = ['append', 'setitem', 'insert', 'extend', 'fromlist', 'construct', 'iterate']

    def convert(typecode, value, operation):
        target = sample(typecode, 1)
        if operation == 'append':
            target.append(value)
        elif operation == 'setitem':
            target[0] = value
        elif operation == 'insert':
            target.insert(0, value)
        elif operation == 'extend':
            target.extend([value])
        elif operation == 'fromlist':
            target.fromlist([value])
        elif operation == 'construct':
            target = A(typecode, [value])
        else:
            target = A(typecode, iter([value]))
        return target

    # 12 typecodes x 63 values x 7 operations = 5292.
    for typecode in TYPECODES:
        for value_id, value in enumerate(VALUES):
            for operation in operations:
                emit('convert/%s/%d/%s' % (typecode, value_id, operation), lambda: convert(typecode, value, operation))


def subscript_cases():
    keys = [0, 1, -1, -3, 3, -4, 2 ** 63, -2 ** 63 - 1, True, 1.0, '0', None, IndexLike(),
            slice(None), slice(1, None), slice(None, -1), slice(None, None, -1),
            slice(None, None, 2), slice(1, 2), slice(2, 1), slice(-100, 100),
            slice(None, None, 0), slice(None, None, 2 ** 100), slice(2 ** 100, None),
            slice('a', None), slice(None, None, -2), slice(2, None, -1), slice(IndexLike(), None)]

    def replacement(typecode, kind):
        if kind == 0:
            return item_of(typecode)
        if kind == 1:
            return sample(typecode, 2)
        if kind == 2:
            return sample(typecode, 1)
        if kind == 3:
            return A('h' if typecode != 'h' else 'i', [1])
        if kind == 4:
            return [item_of(typecode)]
        return None

    def assign(typecode, key, kind):
        target = sample(typecode, 3)
        target[key] = replacement(typecode, kind)
        return target

    def delete(typecode, key):
        target = sample(typecode, 3)
        del target[key]
        return target

    # 6 typecodes x 28 keys x (1 read + 1 delete + 6 assignments) = 1344.
    for typecode in 'cbuHId':
        for key_id, key in enumerate(keys):
            emit('subscript/%s/%d/get' % (typecode, key_id), lambda: sample(typecode, 3)[key])
            emit('subscript/%s/%d/del' % (typecode, key_id), lambda: delete(typecode, key))
            for kind in xrange(6):
                emit('subscript/%s/%d/set/%d' % (typecode, key_id, kind), lambda: assign(typecode, key, kind))
    # Legacy slices and their slot wrappers: 3 typecodes x 7 bounds x 4 forms = 84.
    bounds = [(0, 2), (1, 1), (-1, 2), (2, -1), (-5, 5), (2 ** 100, 1), (1.5, 2)]
    for typecode in 'iuc':
        for bound_id, (low, high) in enumerate(bounds):
            def slice_get():
                return sample(typecode, 3)[low:high]

            def slice_wrapper():
                return sample(typecode, 3).__getslice__(low, high)

            def slice_set():
                target = sample(typecode, 3)
                target.__setslice__(low, high, sample(typecode, 1))
                return target

            def slice_del():
                target = sample(typecode, 3)
                target.__delslice__(low, high)
                return target
            emit('legacy/%s/%d/get' % (typecode, bound_id), slice_get)
            emit('legacy/%s/%d/wrapper' % (typecode, bound_id), slice_wrapper)
            emit('legacy/%s/%d/set' % (typecode, bound_id), slice_set)
            emit('legacy/%s/%d/del' % (typecode, bound_id), slice_del)
    # Slice statements: 3 typecodes x 4 forms = 12.
    for typecode in 'iuc':
        def statement_set():
            target = sample(typecode, 4)
            target[1:3] = sample(typecode, 1)
            target[-1:] = sample(typecode, 2)
            return target

        def statement_del():
            target = sample(typecode, 4)
            del target[-3:-1]
            return target

        def statement_self():
            target = sample(typecode, 4)
            target[1:3] = target
            return target

        def statement_extended_self():
            target = sample(typecode, 4)
            target[::-1] = target
            target[::2] = target
            return target
        emit('statement/%s/set' % typecode, statement_set)
        emit('statement/%s/del' % typecode, statement_del)
        emit('statement/%s/self' % typecode, statement_self)
        emit('statement/%s/extended-self' % typecode, statement_extended_self)


METHOD_ARGUMENTS = [(), (1,), (1, 2), (1, 2, 3), (2.0,), ('c',), (u'u',), ([1, 2],), ((1,),), (None,),
                    (2 ** 100,), (-1,), (IndexLike(),), ('ab',), (u'ab',), (buffer('abcd'),),
                    (bytearray('ab'),), (A('i', [5]),), (A('c', 'qr'),), (A('u', u'w'),), (0, 1), (-1, 'z')]
METHODS = ['append', 'buffer_info', 'byteswap', '__copy__', 'count', '__deepcopy__', 'extend', 'fromfile',
           'fromlist', 'fromstring', 'fromunicode', 'index', 'insert', 'pop', 'read', '__reduce__',
           '__reduce_ex__', 'remove', 'reverse', 'tofile', 'tolist', 'tostring', 'tounicode', 'write',
           '__sizeof__', '__len__', '__getitem__', '__setitem__', '__delitem__', '__getslice__',
           '__setslice__', '__delslice__', '__contains__', '__iter__', '__repr__', '__add__', '__mul__',
           '__rmul__', '__iadd__', '__imul__', '__lt__', '__eq__', '__hash__', '__init__']


def comparable(name, result):
    if name == 'buffer_info':
        return type(result[0]).__name__, result[1]
    if name == '__sizeof__':
        return type(result).__name__, result > 0
    if name == '__hash__':
        return type(result).__name__
    return result


def method_cases():
    def call(typecode, name, arguments, keywords):
        target = sample(typecode, 3)
        result = comparable(name, getattr(target, name)(*arguments, **keywords))
        return result, target

    # 44 methods x 4 typecodes x (22 argument forms + 1 keyword form) = 4048.
    for name in METHODS:
        for typecode in 'ciud':
            for argument_id, arguments in enumerate(METHOD_ARGUMENTS):
                emit('method/%s/%s/%d' % (name, typecode, argument_id), lambda: call(typecode, name, arguments, {}))
            emit('method/%s/%s/keyword' % (name, typecode), lambda: call(typecode, name, (), {'x': 1}))
    # Unbound descriptors: 44 methods x 3 receivers = 132.
    for name in METHODS:
        for receiver_id, receiver in enumerate((Sub('i', [1]), 1, None)):
            emit('unbound/%s/%d' % (name, receiver_id), lambda: comparable(name, getattr(A, name)(receiver)))


def operator_cases():
    rights = [A('i', [3]), A('h', [3]), [3], (3,), 'ab', 1, 0, -1, True, 2.0, 2 ** 100, None,
              Reflected(), IndexLike(), Sub('i', [4]), A('i')]
    lefts = [A('i', [1, 2]), A('c', 'x'), Sub('i', [5]), A('d')]

    def inplace(left, right, operator):
        target = A(left.typecode, left)
        if type(left) is Sub:
            target = Sub(left.typecode, left)
        alias = target
        if operator == '+=':
            alias += right
        else:
            alias *= right
        return alias, target, alias is target

    # 4 left operands x 16 right operands x 6 forms = 384.
    for left_id, left in enumerate(lefts):
        for right_id, right in enumerate(rights):
            identity = 'operator/%d/%d/' % (left_id, right_id)
            emit(identity + 'add', lambda: left + right)
            emit(identity + 'radd', lambda: right + left)
            emit(identity + 'mul', lambda: left * right)
            emit(identity + 'rmul', lambda: right * left)
            emit(identity + 'iadd', lambda: inplace(left, right, '+='))
            emit(identity + 'imul', lambda: inplace(left, right, '*='))


def comparison_cases():
    pairs = [(A('i', [1, 2]), A('i', [1, 2])), (A('i', [1, 2]), A('i', [1, 3])), (A('i', [1, 2]), A('i', [1])),
             (A('i', [1]), A('d', [1.0])), (A('c', 'ab'), A('c', 'b')), (A('d', [float('nan')]), A('d', [float('nan')])),
             (A('i'), A('i')), (A('c', 'a'), A('u', u'a')), (A('I', [2 ** 32 - 1]), A('l', [2 ** 32 - 1])),
             (A('f', [0.1]), A('d', [0.1])), (A('i', [1]), [1]), (A('i', [1]), (1,)), (A('i', [1]), None),
             (A('i', [1]), 1), (A('c', 'a'), 'a'), (A('i', [1]), Sub('i', [1])), (Sub('i', [2]), A('i', [1]))]
    # 17 pairs x (6 operators + cmp) x 2 orders = 238.
    for pair_id, (left, right) in enumerate(pairs):
        for order_id, (first, second) in enumerate(((left, right), (right, left))):
            for operator in ('<', '<=', '==', '!=', '>', '>='):
                emit('compare/%d/%d/%s' % (pair_id, order_id, operator), lambda: eval('first %s second' % operator))
            if pair_id != 5:
                emit('compare/%d/%d/cmp' % (pair_id, order_id), lambda: cmp(first, second))
    emit('compare/nan/count', lambda: (A('d', [float('nan')]).count(float('nan')), float('nan') in A('d', [float('nan')])))


def representation_cases():
    contents = [('c', ''), ('c', 'ab'), ('c', '\x00\xff\'"\\'), ('u', u''), ('u', u'ab'),
                ('u', u'\xe9' + unichr(0x100) + unichr(0x1f600) + unichr(0xdfff)), ('b', [-128, 127]),
                ('B', [0, 255]), ('h', [-32768]), ('H', [65535]), ('i', [-2 ** 31]), ('I', [2 ** 32 - 1]),
                ('l', [-2 ** 63]), ('L', [2 ** 64 - 1]), ('f', [0.1, 1e-50, 3.4e38, float('inf')]),
                ('d', [1.5, float('-inf'), -0.0, 1e300])]
    # 16 contents x 5 forms = 80.
    for content_id, (typecode, content) in enumerate(contents):
        value = A(typecode, content)
        emit('repr/%d/repr' % content_id, lambda: repr(value))
        emit('repr/%d/str' % content_id, lambda: str(value))
        emit('repr/%d/unicode' % content_id, lambda: unicode(value))
        emit('repr/%d/tolist' % content_id, lambda: value.tolist())
        emit('repr/%d/sub' % content_id, lambda: repr(Sub(typecode, content)))


def buffer_cases():
    def live(typecode):
        target = sample(typecode, 4)
        view = buffer(target, 2, 6)
        first = str(view)
        del target[:]
        second = str(view), len(view)
        target.extend(sample(typecode, 4))
        return first, second, str(view), len(view), str(buffer(view, 1))

    # 12 typecodes x 14 forms = 168.
    for typecode in TYPECODES:
        value = sample(typecode, 3)
        identity = 'buffer/%s/' % typecode
        emit(identity + 'str', lambda: str(buffer(value)))
        emit(identity + 'len', lambda: len(buffer(value)))
        emit(identity + 'item', lambda: (buffer(value)[0], buffer(value)[-1]))
        emit(identity + 'slice', lambda: (buffer(value)[1:3], buffer(value, 1)[:], buffer(value, 1, 2)[:]))
        emit(identity + 'hash', lambda: hash(buffer(value)) == hash(value.tostring()))
        emit(identity + 'compare', lambda: (buffer(value) == buffer(value.tostring()), cmp(buffer(value), buffer('b'))))
        emit(identity + 'concat', lambda: (buffer(value) + 'x', buffer(value) * 2))
        emit(identity + 'live', lambda: live(typecode))
        emit(identity + 'memoryview', lambda: memoryview(value))
        emit(identity + 'buffer-memoryview', lambda: memoryview(buffer(value)).tobytes())
        emit(identity + 'bytearray', lambda: bytearray(value))
        emit(identity + 'assign', lambda: buffer(value).__setitem__(0, 'x'))
        emit(identity + 'decode', lambda: _codecs.latin_1_decode(value))
        emit(identity + 'fromstring', lambda: A('c').fromstring(value))
    # Character buffers of buffers over arrays: 18.
    view = buffer(A('c', '12'))
    for name, operation in [('uadd', lambda: u'x' + view), ('uradd', lambda: view + u'x'),
                            ('ueq', lambda: view == u'12'), ('ult', lambda: u'12' < view),
                            ('uin', lambda: view in u'x12'), ('int', lambda: int(view)),
                            ('long', lambda: long(view)), ('float', lambda: float(view)),
                            ('unicode', lambda: unicode(view)), ('decode', lambda: unicode(view, 'ascii')),
                            ('startswith', lambda: '12x'.startswith(view)), ('find', lambda: '012'.find(view)),
                            ('ufind', lambda: u'012'.find(view)), ('split', lambda: 'a12b'.split(view)),
                            ('replace', lambda: 'a12b'.replace(view, 'x')), ('count', lambda: '1212'.count(view)),
                            ('sadd', lambda: 'x' + view), ('format', lambda: u'%s' % view)]:
        emit('char-buffer/' + name, operation)
    # Struct conversions: 16.
    doubles = A('d', [1.5, 2.5])
    emit('struct/unpack', lambda: _struct.unpack('d', A('d', [1.5])))
    emit('struct/unpack-short', lambda: _struct.unpack('d', A('c', 'abc')))
    emit('struct/unpack-from', lambda: _struct.unpack_from('d', doubles, 8))
    emit('struct/unpack-from-keyword', lambda: _struct.unpack_from('d', buffer=doubles))
    emit('struct/unpack-from-short', lambda: _struct.unpack_from('d', A('c', 'abc')))
    emit('struct/unpack-from-buffer', lambda: _struct.unpack_from('d', buffer(doubles), 0))
    emit('struct/compiled', lambda: _struct.Struct('d').unpack(A('d', [2.0])))

    def pack(offset, target):
        answer = _struct.pack_into('d', target, offset, 3.5)
        return answer, target
    emit('struct/pack-into', lambda: pack(8, A('d', [0.0, 0.0])))
    emit('struct/pack-into-negative', lambda: pack(-8, A('d', [0.0])))
    emit('struct/pack-into-short', lambda: pack(0, A('c', 'abc')))
    emit('struct/pack-into-bytes', lambda: pack(1, A('B', [0] * 10)))
    emit('struct/pack-into-buffer', lambda: pack(0, buffer(A('d', [0.0]))))
    emit('struct/pack-into-float', lambda: pack(0, A('d', [0.0])) + (_struct.pack_into('dd', A('d', [0.0] * 2), 0, FloatLike(), 1.0),))
    emit('struct/compiled-pack-into', lambda: (_struct.Struct('d').pack_into(doubles, 0, 9.0), doubles))
    emit('struct/unpack-u', lambda: _struct.unpack('d', A('u', u'ab')))
    emit('struct/unpack-from-empty', lambda: _struct.unpack_from('', A('i')))
    # Codec s* arguments: 7.
    emit('codec/utf8', lambda: _codecs.utf_8_decode(A('c', 'ab\xc3\xa9')))
    emit('codec/utf8-partial', lambda: _codecs.utf_8_decode(A('c', 'a\xc3'), None, False))
    emit('codec/utf8-error', lambda: _codecs.utf_8_decode(A('c', 'a\xff')))
    emit('codec/ascii', lambda: _codecs.ascii_decode(A('B', [104, 105])))
    emit('codec/latin1', lambda: _codecs.latin_1_decode(A('u', u'a')))
    emit('codec/hex', lambda: _codecs.decode(A('c', '6162'), 'hex'))
    emit('codec/encode', lambda: _codecs.utf_8_encode(A('c', 'ab')))


def iteration_cases():
    def regrow():
        target = A('i', [1, 2])
        iterator = iter(target)
        first = [next(iterator), next(iterator)]
        try:
            next(iterator)
        except StopIteration:
            first.append('stop')
        target.append(3)
        first.append(list(iterator))
        del target[:]
        first.append(list(iterator))
        return first

    def mutate():
        target = A('i', [1, 2, 3])
        seen = []
        for item in target:
            seen.append(item)
            if item == 1:
                target.append(4)
                del target[0]
        return seen, target

    iterated = Overrides('i', [1, 2])
    # 26 observations.
    for name, operation in [('type', lambda: (type(iter(A('i'))).__name__, type(iter(A('i'))).__module__)),
                            ('self', lambda: iter(iter(A('i'))) is not None),
                            ('dict', lambda: sorted(type(iter(A('i'))).__dict__)),
                            ('hint', lambda: iter(A('i', [1])).__length_hint__()),
                            ('len', lambda: len(iter(A('i')))),
                            ('regrow', regrow), ('mutate', mutate),
                            ('list', lambda: list(A('d', [1.5, 2.5]))), ('tuple', lambda: tuple(A('c', 'ab'))),
                            ('reversed', lambda: list(reversed(A('i', [1, 2, 3])))),
                            ('reversed-type', lambda: reversed(A('i'))),
                            ('sorted', lambda: sorted(A('h', [3, -1, 2]))), ('sum', lambda: sum(A('I', [1, 2]))),
                            ('min-max', lambda: (min(A('d', [2.5, 1.5])), max(A('b', [-1, 5])))),
                            ('enumerate', lambda: list(enumerate(A('u', u'ab')))),
                            ('map', lambda: map(None, A('i', [1, 2]), A('c', 'x'))),
                            ('join', lambda: ''.join(A('c', 'ab'))), ('ujoin', lambda: u'-'.join(A('u', u'ab'))),
                            ('contains', lambda: (2 in A('i', [1, 2]), 2.0 in A('i', [2]), 'a' in A('i', [1]), u'a' in A('c', 'a'), 'a' in A('u', u'a'))),
                            ('truth', lambda: (bool(A('i')), bool(A('i', [0])), not A('c'))),
                            ('overrides-len', lambda: (len(iterated), iterated[0], list(iterated), iterated.tolist(), 7 in iterated)),
                            ('overrides-construct', lambda: (A('i', iterated), bool(iterated), iterated == A('i', [1, 2]))),
                            ('overrides-concat', lambda: A('i', [0]) + iterated),
                            ('bytearray-extend', lambda: (bytearray('x') + bytearray(A('B', [1])), bytearray('x').extend(A('B', [1])))),
                            ('set', lambda: sorted(set(A('i', [2, 1, 2])))),
                            ('fromkeys', lambda: sorted(dict.fromkeys(A('c', 'ba'))))]:
        emit('iterate/' + name, operation)


def metadata_cases():
    def assign(target, name, value):
        setattr(target, name, value)
        return getattr(target, name)

    def remove(target, name):
        delattr(target, name)

    instance = A('i', [1])
    subclass = Sub('i', [1])
    subclass.attribute = 5
    slotted = Slotted('i', [1])
    slotted.slot = 3
    weak = A('d')
    # 44 observations.
    for name, operation in [
            ('module-names', lambda: sorted(key for key in dir(array) if key != '__file__')),
            ('module-name', lambda: (array.__name__, array.__package__, array.ArrayType is A, array.array is A)),
            ('module-doc', lambda: array.__doc__.splitlines()[:3]),
            ('type-name', lambda: (A.__name__, A.__module__, repr(A), A.__mro__, A.__bases__)),
            ('type-doc', lambda: A.__doc__.splitlines()[:4]),
            ('type-dict', lambda: sorted(A.__dict__)),
            ('type-getsets', lambda: (repr(A.typecode), repr(A.itemsize), type(A.typecode).__name__)),
            ('type-wrappers', lambda: [repr(getattr(A, name)) for name in ('__len__', '__getitem__', '__add__', '__mul__', '__iadd__', '__getslice__', '__lt__', '__iter__', '__reduce__', 'append')]),
            ('type-hash', lambda: (A.__hash__ is object.__hash__, A.__init__ is object.__init__, A.__getattribute__ is object.__getattribute__)),
            ('type-assign', lambda: assign(A, 'attribute', 1)),
            ('type-delete', lambda: remove(A, 'append')),
            ('typecodes', lambda: [(code, A(code).typecode, A(code).itemsize, type(A(code).typecode).__name__) for code in TYPECODES]),
            ('instance-typecode', lambda: assign(instance, 'typecode', 'd')),
            ('instance-delete-typecode', lambda: remove(instance, 'typecode')),
            ('instance-itemsize', lambda: assign(instance, 'itemsize', 2)),
            ('instance-attribute', lambda: assign(instance, 'attribute', 1)),
            ('instance-read', lambda: instance.attribute),
            ('instance-dict', lambda: instance.__dict__),
            ('instance-class', lambda: instance.__class__),
            ('hash', lambda: hash(A('i'))),
            ('hash-method', lambda: type(A('i').__hash__()).__name__),
            ('hash-subclass', lambda: hash(subclass) == subclass.__hash__()),
            ('hash-buffer', lambda: hash(buffer(A('c', 'ab'))) == hash('ab')),
            ('weakref', lambda: (_weakref.ref(weak)() is weak, _weakref.getweakrefcount(weak))),
            ('weakref-attribute', lambda: (hasattr(A, '__weakref__'), hasattr(Sub, '__weakref__'))),
            ('subclass-dict', lambda: subclass.__dict__),
            ('subclass-reduce', lambda: subclass.__reduce__()),
            ('subclass-reduce-ex', lambda: (subclass.__reduce_ex__(0), subclass.__reduce_ex__(2))),
            ('subclass-slots', lambda: (slotted.__reduce__(), slotted.slot)),
            ('subclass-copy', lambda: (type(subclass.__copy__()), type(subclass.__deepcopy__({})), type(subclass[:]), type(subclass + subclass), type(subclass * 2))),
            ('subclass-equal', lambda: (subclass == A('i', [1]), subclass != Sub('i', [2]))),
            ('subclass-mro', lambda: Sub.__mro__),
            ('reduce', lambda: (A('c', 'ab').__reduce__(), A('u', u'x').__reduce_ex__(1), A('d', [1.5]).__reduce_ex__(2))),
            ('reduce-empty', lambda: A('L').__reduce__()),
            ('copy-reg', lambda: __import__('copy_reg')._reduce_ex(A('i', [1]), 0)),
            ('reconstruct', lambda: A(*A('I', [5]).__reduce__()[1])),
            ('sizeof', lambda: (type(A('i').__sizeof__()).__name__, A('i', [1] * 10).__sizeof__() >= 40, sys.getsizeof(A('d')) > 0)),
            ('buffer-info', lambda: (type(A('i').buffer_info()[0]).__name__, A('i', [1, 2]).buffer_info()[1])),
            ('init', lambda: A('i', [1]).__init__('d', [5])),
            ('getattribute', lambda: A('i', [1]).__getattribute__('typecode')),
            ('object-setattr', lambda: object.__setattr__(A('i'), 'x', 1)),
            ('dir', lambda: [name for name in dir(A('i')) if not name.startswith('__')]),
            ('isinstance', lambda: (isinstance(subclass, A), issubclass(Sub, A), isinstance(A('i'), Sub)))]:
        emit('metadata/' + name, operation)


class Counted(object):
    def __init__(self, log, value):
        self.log = log
        self.value = value

    def __int__(self):
        self.log.append('int')
        return self.value

    def __float__(self):
        self.log.append('float')
        return float(self.value)


class Mutator(object):
    def __init__(self, target, grow):
        self.target = target
        self.grow = grow

    def __eq__(self, other):
        if self.grow and len(self.target) < 64:
            self.target.extend([0] * 20)
        elif not self.grow:
            del self.target[:]
        return False

    def __ne__(self, other):
        return True


def callback_cases():
    def counted(typecode, operation):
        log = []
        item = Counted(log, 3)
        target = A(typecode, [1])
        if operation == 'append':
            target.append(item)
        elif operation == 'insert':
            target.insert(0, item)
        elif operation == 'setitem':
            target[0] = item
        elif operation == 'extend':
            target.extend([item, item])
        elif operation == 'extend-iterator':
            target.extend(iter([item]))
        elif operation == 'fromlist':
            target.fromlist([item, item])
        elif operation == 'construct-list':
            target = A(typecode, [item, item])
        elif operation == 'construct-tuple':
            target = A(typecode, (item,))
        else:
            target = A(typecode, iter([item, item]))
        return target, len(log)

    # 5 typecodes x 9 operations = 45.
    for typecode in 'bHiLd':
        for operation in ('append', 'insert', 'setitem', 'extend', 'extend-iterator', 'fromlist',
                          'construct-list', 'construct-tuple', 'construct-iterator'):
            emit('callback/count/%s/%s' % (typecode, operation), lambda: counted(typecode, operation))

    class FailsSecond(object):
        def __init__(self):
            self.calls = 0

        def __int__(self):
            self.calls += 1
            if self.calls == 2:
                raise KeyError('second')
            return 4
    emit('callback/second-fails', lambda: A('i').append(FailsSecond()))

    def shrinking_list(operation):
        source = [1, 2, 3]

        class Shrink(object):
            def __int__(self):
                source.pop()
                return 5
        source.insert(1, Shrink())
        if operation == 'construct':
            return A('i', source)
        target = A('i')
        result = target.fromlist(source)
        return result, target

    def growing_list(operation):
        source = [1, 2]

        class Grow(object):
            def __int__(self):
                source.append(8)
                return 5
        source.insert(1, Grow())
        if operation == 'construct':
            return A('i', source)
        target = A('i')
        result = target.fromlist(source)
        return result, target
    for operation in ('construct', 'fromlist'):
        emit('callback/list-shrink/' + operation, lambda: shrinking_list(operation))
        emit('callback/list-grow/' + operation, lambda: growing_list(operation))

    def mutated(grow, operation):
        target = A('i', [1, 2, 3])
        probe = Mutator(target, grow)
        if operation == 'contains':
            result = probe in target
        else:
            result = getattr(target, operation)(probe)
        return result, len(target)

    # 2 directions x 4 operations = 8.
    for grow in (False, True):
        for operation in ('count', 'index', 'remove', 'contains'):
            emit('callback/compare/%d/%s' % (grow, operation), lambda: mutated(grow, operation))

    class Resizing(float):
        target = None

        def __eq__(self, other):
            if len(Resizing.target) < 64:
                Resizing.target.extend([0.0] * 40)
            return False

        def __ne__(self, other):
            return True

        def __lt__(self, other):
            del Resizing.target[:]
            return True

    def resizing_compare(operator):
        left = A('d', [1.0, 2.0])
        right = A('d', [1.0, 3.0])
        Resizing.target = left
        return eval('left %s right' % operator), len(left)
    for operator in ('==', '<', '>', '!='):
        emit('callback/array-compare/' + operator, lambda: resizing_compare(operator))

    def self_operations():
        target = A('i', [1, 2, 3])
        target.extend(target)
        first = target.tolist()
        target += target
        target[::] = target
        target[1:3] = target
        target[::-1] = target
        target *= 2
        target.fromstring(target.tostring())
        return first, target.tolist()
    emit('callback/self', self_operations)

    def self_string():
        target = A('c', 'ab')
        results = []
        for source in (target, buffer(target), buffer(target, 0, 1)):
            try:
                target.fromstring(source)
            except ValueError as error:
                results.append(error.args)
        empty = A('i')
        empty.fromstring(empty)
        return results, target, empty
    emit('callback/self-string', self_string)

    class IndexRead(object):
        def __init__(self, target, value):
            self.target = target
            self.value = value

        def __index__(self):
            self.target.append(9)
            return self.value

    def index_growth(operation):
        target = A('i', [1, 2, 3])
        if operation == 'get':
            result = target[IndexRead(target, -1)]
        elif operation == 'delete':
            del target[IndexRead(target, -1)]
            result = None
        elif operation == 'slice':
            result = target[IndexRead(target, 1):IndexRead(target, -1)]
        else:
            result = target[IndexRead(target, -2)::IndexRead(target, 2)]
        return result, target
    # 4 index forms.
    for operation in ('get', 'delete', 'slice', 'extended'):
        emit('callback/index/' + operation, lambda: index_growth(operation))


def main():
    constructor_cases()
    conversion_cases()
    subscript_cases()
    method_cases()
    operator_cases()
    comparison_cases()
    representation_cases()
    buffer_cases()
    iteration_cases()
    metadata_cases()
    callback_cases()
    assert len(identities) == 12795, 'array product cardinality changed'


if __name__ == '__main__':
    main()
