"""Project-authored Python 2 tests for the bundled array module."""
import _codecs
import _sre
import _struct
import array
import sys
import unittest


A = array.array


def _mk_bitmap(bits):
    # sre_compile._mk_bitmap of CPython 2.7.18 for a 32-bit code size.
    text = bytes(bits).translate('0' + '1' * 255)[::-1]
    return [int(text[index - 32:index], 2) for index in range(len(text), 0, -32)]


def _bytes_to_codes(block_indices):
    # sre_compile._bytes_to_codes of CPython 2.7.18, which the BIGCHARSET
    # encoding of a unicode character class calls.
    import array
    if _sre.CODESIZE == 2:
        code = 'H'
    else:
        code = 'I'
    words = array.array(code, bytes(block_indices))
    assert words.itemsize == _sre.CODESIZE
    assert len(words) * words.itemsize == len(block_indices)
    return words.tolist()


def _bigcharset(characters):
    # The BIGCHARSET operand that sre_compile._optimize_charset builds.
    charmap = bytearray(65536)
    for character in characters:
        charmap[character] = 1
    charmap = bytes(charmap)
    chunks = {}
    mapping = bytearray(256)
    block = 0
    data = bytearray()
    for start in range(0, 65536, 256):
        chunk = charmap[start:start + 256]
        if chunk in chunks:
            mapping[start // 256] = chunks[chunk]
        else:
            mapping[start // 256] = chunks[chunk] = block
            block += 1
            data += chunk
    return [block] + _bytes_to_codes(mapping) + _mk_bitmap(data)


class Counted(object):
    def __init__(self, log, value):
        self.log = log
        self.value = value

    def __int__(self):
        self.log.append(self.value)
        return self.value


class Reflected(object):
    def __radd__(self, other):
        return 'radd'

    def __rmul__(self, other):
        return 'rmul'


class Sub(A):
    pass


class ArrayAudit(unittest.TestCase):
    def assert_error(self, error_type, message, operation, *args, **kwargs):
        try:
            operation(*args, **kwargs)
        except error_type as error:
            self.assertEqual(error.args, (message,))
        else:
            self.fail('expected ' + error_type.__name__)

    def test_bigcharset_codes_come_from_an_array(self):
        characters = range(ord('a'), ord('z') + 1) + range(0x430, 0x450) + [0x451]
        operand = _bigcharset(characters)
        self.assertEqual(operand[0], 3)
        self.assertEqual(len(operand), 1 + 64 + 3 * 8)
        self.assertEqual(operand[1:3], [16843008, 16843010] if sys.byteorder == 'little' else [65793, 16908545])
        code = [17, 4, 0, 1, 0, 29, 97, 1, 4294967295L, 15, 92, 11] + operand + [0, 1, 1]
        pattern = u'[a-z' + unichr(0x430) + u'-' + unichr(0x44f) + unichr(0x451) + u']+'
        compiled = _sre.compile(pattern, 0, code, 0, {}, [None])
        subject = u'abc ' + unichr(0x436) + unichr(0x451) + unichr(0x130) + u' ' + unichr(0x101)
        self.assertEqual(compiled.findall(subject), [u'abc', unichr(0x436) + unichr(0x451)])
        self.assertEqual(compiled.match(unichr(0x44f) + u'z').span(), (0, 2))

    def test_word_arrays_read_host_order_bytes(self):
        data = '\x01\x02\x03\x04\x05\x06\x07\x08'
        words = A('I', data)
        halves = A('H', data)
        self.assertEqual((words.itemsize, halves.itemsize), (4, 2))
        if sys.byteorder == 'little':
            self.assertEqual(words.tolist(), [0x04030201L, 0x08070605L])
            self.assertEqual(halves.tolist(), [0x0201, 0x0403, 0x0605, 0x0807])
        else:
            self.assertEqual(words.tolist(), [0x01020304L, 0x05060708L])
        self.assertEqual(words.tostring(), data)
        words.byteswap()
        self.assertEqual(words.tostring(), '\x04\x03\x02\x01\x08\x07\x06\x05')

    def test_item_sizes_and_module_surface(self):
        sizes = [(code, A(code).itemsize) for code in 'cbBuhHiIlLfd']
        self.assertEqual(sizes, [('c', 1), ('b', 1), ('B', 1), ('u', 4), ('h', 2), ('H', 2),
                                 ('i', 4), ('I', 4), ('l', 8), ('L', 8), ('f', 4), ('d', 8)])
        self.assertIs(array.ArrayType, A)
        self.assertEqual((A.__name__, A.__module__, repr(A)), ('array', 'array', "<type 'array.array'>"))
        self.assertEqual(type(iter(A('i'))).__name__, 'arrayiterator')
        self.assert_error(ValueError, 'bad typecode (must be c, b, B, u, h, H, i, I, l, L, f or d)', A, 'x')
        self.assert_error(TypeError, 'array.array() does not take keyword arguments', A, 'i', initializer=[])
        self.assert_error(TypeError, "unhashable type: 'array.array'", hash, A('i'))

    def test_integer_conversions_report_their_parser(self):
        self.assert_error(OverflowError, 'signed char is greater than maximum', A, 'b', [128])
        self.assert_error(OverflowError, 'signed short integer is less than minimum', A, 'b', [-32769])
        self.assert_error(OverflowError, 'unsigned byte integer is greater than maximum', A, 'B', [256])
        self.assert_error(OverflowError, 'unsigned short is less than minimum', A, 'H', [-1])
        self.assert_error(OverflowError, 'signed integer is greater than maximum', A, 'H', [2 ** 31])
        self.assert_error(OverflowError, 'unsigned int is greater than maximum', A, 'I', [2 ** 32])
        self.assert_error(OverflowError, "can't convert negative value to unsigned long", A, 'L', [-1L])
        self.assert_error(OverflowError, 'long int too large to convert', A, 'L', [2 ** 64])
        self.assert_error(OverflowError, 'Python int too large to convert to C long', A, 'l', [2 ** 63])
        self.assert_error(TypeError, 'integer argument expected, got float', A, 'i', [1.5])
        self.assert_error(TypeError, 'an integer is required', A, 'h', ['1'])
        self.assert_error(TypeError, 'a float is required', A, 'd', ['1'])
        self.assert_error(TypeError, 'array item must be char', A, 'c', [1])
        self.assert_error(TypeError, 'array item must be unicode character', A, 'u', [u'ab'])
        self.assertEqual(A('L', [2 ** 64 - 1]).tolist(), [2 ** 64 - 1])
        self.assertEqual(repr(A('I', [3])), "array('I', [3L])")

    def test_conversion_callbacks_run_as_in_cpython(self):
        log = []
        target = A('i')
        target.append(Counted(log, 3))
        target.insert(0, Counted(log, 4))
        target.extend(iter([Counted(log, 5)]))
        self.assertEqual(log, [3, 3, 4, 4, 5, 5])
        del log[:]
        target.fromlist([Counted(log, 6)])
        target[0] = Counted(log, 7)
        A('i', [Counted(log, 8)])
        self.assertEqual(log, [6, 7, 8])
        self.assertEqual(target.tolist(), [7, 3, 5, 6])

    def test_sequence_initializers_follow_their_size(self):
        shrinking = [1, 2, 3]

        class Shrink(object):
            def __int__(self):
                shrinking.pop()
                return 5
        shrinking.insert(1, Shrink())
        self.assert_error(IndexError, 'list index out of range', A, 'i', shrinking)
        growing = [1, 2]

        class Grow(object):
            def __int__(self):
                growing.append(9)
                return 5
        growing.insert(1, Grow())
        self.assertEqual(A('i', growing).tolist(), [1, 5, 2])
        listed = [1, 2, 3]

        class ShrinkList(object):
            def __int__(self):
                listed.pop()
                return 5
        listed.insert(1, ShrinkList())
        target = A('i', [0])
        self.assert_error(RuntimeError, 'list changed size during iteration', target.fromlist, listed)
        self.assertEqual(target.tolist(), [0])

    def test_comparisons_reread_resized_arrays(self):
        target = A('i', [1, 2, 3])

        class Grow(object):
            def __eq__(self, other):
                if len(target) < 40:
                    target.extend([0] * 10)
                return False
        self.assertEqual(target.count(Grow()), 0)
        self.assertEqual(len(target), 43)

        class Clear(object):
            def __eq__(self, other):
                del target[:]
                return False
        self.assert_error(ValueError, 'array.remove(x): x not in list', target.remove, Clear())
        self.assertEqual(len(target), 0)

    def test_self_concatenation_and_slices(self):
        target = A('h', [1, 2, 3])
        target.extend(target)
        target += target
        self.assertEqual(target.tolist(), [1, 2, 3] * 4)
        target[1:3] = target
        self.assertEqual(len(target), 22)
        target[::-1] = target
        self.assertEqual(target[:3].tolist(), [3, 2, 1])
        self.assert_error(ValueError, 'attempt to assign array of size 22 to extended slice of size 11',
                          target.__setitem__, slice(None, None, 2), target)
        self.assert_error(TypeError, 'bad argument type for built-in operation',
                          target.__setitem__, slice(0, 1), A('i', [1]))

    def test_fromstring_refuses_its_own_items(self):
        target = A('c', 'ab')
        self.assert_error(ValueError, 'array.fromstring(x): x cannot be self', target.fromstring, target)
        self.assert_error(ValueError, 'array.fromstring(x): x cannot be self', target.fromstring, buffer(target))
        self.assert_error(TypeError, 'fromstring() argument 1 must be string or read-only buffer, not bytearray',
                          target.fromstring, bytearray('x'))
        target.fromstring(buffer('cd'))
        target.fromstring(A('c', 'e'))
        target.fromstring(u'f')
        self.assertEqual(target.tostring(), 'abcdef')

    def test_iterator_keeps_reading_the_array(self):
        target = A('i', [1])
        iterator = iter(target)
        self.assertEqual(list(iterator), [1])
        target.append(2)
        self.assertEqual(list(iterator), [2])
        self.assertRaises(AttributeError, getattr, iterator, '__length_hint__')

    def test_buffer_views_follow_the_items(self):
        target = A('c', 'abcd')
        view = buffer(target, 1, 2)
        self.assertEqual(str(view), 'bc')
        del target[:]
        self.assertEqual((str(view), len(view)), ('', 0))
        target.extend('wxyz')
        self.assertEqual(str(view), 'xy')
        self.assert_error(TypeError, 'cannot make memory view because object does not have the buffer interface',
                          memoryview, target)
        self.assert_error(TypeError, 'coercing to Unicode: need string or buffer, buffer found',
                          lambda: u'x' + view)
        self.assert_error(TypeError, 'char buffer type not available', 'axyb'.find, view)

    def test_struct_reads_and_writes_items(self):
        doubles = A('d', [1.5, 2.5])
        self.assertEqual(_struct.unpack_from('d', doubles, 8), (2.5,))
        self.assertEqual(_struct.unpack('dd', doubles), (1.5, 2.5))
        self.assertIs(_struct.pack_into('d', doubles, 0, 4.0), None)
        self.assertEqual(doubles.tolist(), [4.0, 2.5])
        self.assert_error(TypeError, 'argument must be read-write buffer, not buffer',
                          _struct.pack_into, 'd', buffer(doubles), 0, 1.0)

    def test_codecs_read_arrays_as_buffers(self):
        self.assertEqual(_codecs.utf_8_decode(A('c', 'a\xc3\xa9')), (u'a' + unichr(0xe9), 3))
        self.assertEqual(_codecs.latin_1_decode(A('B', [104, 105])), (u'hi', 2))

    def test_operators_use_the_sequence_slots(self):
        target = A('i', [1, 2])
        self.assertEqual(target + Reflected(), 'radd')
        self.assertEqual(target * Reflected(), 'rmul')
        self.assertEqual(3 * A('c', 'x'), A('c', 'xxx'))
        self.assert_error(TypeError, 'can only append array (not "list") to array', lambda: target + [1])
        self.assert_error(TypeError, 'can only concatenate list (not "array.array") to list', lambda: [1] + target)
        self.assert_error(TypeError, "can't multiply sequence by non-int of type 'float'", lambda: target * 2.0)
        alias = target
        alias += Reflected()
        self.assertEqual(alias, 'radd')
        subclassed = Sub('i', [1])

        def extend_subclass():
            value = subclassed
            value += Reflected()
        self.assert_error(TypeError, 'can only extend array with array (not "Reflected")', extend_subclass)

    def test_reduction_and_copies(self):
        subclassed = Sub('u', u'ab')
        subclassed.attribute = 1
        self.assertEqual(subclassed.__reduce__(), (Sub, ('u', [u'a', u'b']), {'attribute': 1}))
        self.assertEqual(A('c', 'x').__reduce_ex__(2), (A, ('c', ['x']), None))
        self.assertIs(type(subclassed.__copy__()), A)
        self.assertIs(type(subclassed[:]), A)
        self.assertEqual(hash(subclassed), subclassed.__hash__())

    def test_large_arrays_round_trip(self):
        data = ''.join(chr(index % 251) for index in xrange(4096)) * 64
        words = A('I', data)
        self.assertEqual(len(words), len(data) // 4)
        self.assertEqual(words.tostring(), data)
        values = words.tolist()
        self.assertEqual(A('I', values), words)
        copied = A('I')
        copied.fromlist(values)
        copied.extend(words)
        self.assertEqual(len(copied), 2 * len(words))
        self.assertEqual(sum(1 for item in words), len(words))
        doubled = words * 2
        doubled[::2] = words
        self.assertTrue(doubled[::2] == words)
        self.assertTrue(doubled[1::2].tolist() == (values + values)[1::2])
