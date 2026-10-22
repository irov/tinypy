import _struct as struct
import array
import sys


A = array.array


def expect(error_type, message, operation, *args):
    try:
        operation(*args)
    except error_type as error:
        assert error.args == ((message,) if message is not None else ()), error.args
    else:
        raise AssertionError("expected " + error_type.__name__)


# Item sizes of the LP64 UCS-4 build of CPython 2.7 on every target.
assert [A(code).itemsize for code in "cbBuhHiIlLfd"] == [1, 1, 1, 4, 2, 2, 4, 4, 8, 8, 4, 8]
assert A("l", [2 ** 63 - 1]).tolist() == [2 ** 63 - 1]
assert A("L", [2 ** 64 - 1]).tolist() == [2 ** 64 - 1]
assert "array" in sys.builtin_module_names


# A 'u' item beyond U+10FFFF, which byteswap or fromstring can store, has no
# unicode: reading it raises ValueError, while its bytes stay readable.
wide = A("u", u"ab")
wide.byteswap()
expect(ValueError, "character U+61000000 is not in range [U+0000; U+10ffff]", wide.tolist)
expect(ValueError, "character U+61000000 is not in range [U+0000; U+10ffff]", repr, wide)
expect(ValueError, "character U+62000000 is not in range [U+0000; U+10ffff]", wide.__getitem__, 1)
expect(ValueError, "character U+61000000 is not in range [U+0000; U+10ffff]", wide.tounicode)
assert len(wide) == 2 and wide.tostring()[0] == "\x00"
wide.byteswap()
assert wide.tounicode() == u"ab"
assert A("u", unichr(0x10ffff) + unichr(0xd800)).tolist() == [unichr(0x10ffff), unichr(0xd800)]


# A conversion that resizes the array: the item is stored where the array
# still has it, never past its items.
class Clear(object):
    def __init__(self, target):
        self.target = target

    def __int__(self):
        del self.target[:]
        return 7

    def __index__(self):
        del self.target[:]
        return 0


class Grow(object):
    def __init__(self, target):
        self.target = target

    def __int__(self):
        self.target.extend([1] * 40)
        return 7


cleared = A("i", [1, 2, 3])
expect(IndexError, "array assignment index out of range", cleared.__setitem__, 2, Clear(cleared))
assert cleared.tolist() == []
cleared = A("i", [1, 2, 3])
cleared.append(Clear(cleared))
assert cleared.tolist() == []
cleared = A("i", [1, 2, 3])
cleared.insert(1, Clear(cleared))
assert cleared.tolist() == []
cleared = A("i", [1, 2, 3])
cleared.fromlist([4, Clear(cleared), 5])
assert cleared.tolist() == [4, 7, 5]
cleared = A("i", [1, 2, 3])
expect(IndexError, "array index out of range", cleared.__getitem__, Clear(cleared))
grown = A("i", [1, 2, 3])
grown.append(Grow(grown))
assert len(grown) == 84 and grown[3] == 7
grown = A("i", [1, 2, 3])
grown[1] = Grow(grown)
assert grown[:3].tolist() == [1, 7, 3] and len(grown) == 43
grown = A("i", [1, 2, 3])
grown.fromlist([4, Grow(grown)])
assert grown[:3].tolist() == [1, 2, 3] and grown[-2:].tolist() == [4, 7]


# fromstring of a view of the array's own items reads them after the array
# grew.
source = A("c", "xyzw")
source.fromstring(buffer(source, 1, 2))
assert source.tostring() == "xyzwyz"


# A struct call that holds the items exports them: a callback cannot resize
# the array until the call returns.
doubles = A("d", [0.0, 0.0])


class Resizing(object):
    def __index__(self):
        doubles.append(1.0)
        return 0

    __int__ = __index__

    def __float__(self):
        doubles.append(1.0)
        return 1.0


expect(BufferError, "cannot resize an array that is exporting buffers", struct.unpack_from, "d", doubles, Resizing())
expect(struct.error, "required argument is not a float", struct.pack_into, "d", doubles, 0, Resizing())
assert len(doubles) == 2
doubles.append(2.0)
assert struct.pack_into("d", doubles, 16, 3.0) is None
assert doubles.tolist() == [0.0, 0.0, 3.0]


# Computed sizes past the address space fail before any allocation, with the
# MemoryError of PyErr_NoMemory.
expect(MemoryError, None, lambda: A("d", [0.0]) * (2 ** 62))
expect(MemoryError, None, lambda: A("d", [0.0]).__imul__(2 ** 62))
big = A("i", [1, 2])
try:
    big *= 2 ** 61
except MemoryError:
    pass
else:
    raise AssertionError("expected MemoryError")
assert big.tolist() == [1, 2]
