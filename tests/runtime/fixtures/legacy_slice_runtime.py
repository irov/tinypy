# Two-argument slice syntax uses the Python 2 sequence slice protocol when the
# type overrides it: bounds arrive as plain offsets, a missing upper bound
# becomes the largest index and negative bounds are resolved through __len__.
MAXIMUM = 2 ** 63 - 1


class ClassicSlice:
    def __getslice__(self, start, stop):
        return ("getslice", start, stop)

    def __getitem__(self, key):
        return ("getitem", key)


class ModernSlice(object):
    def __getslice__(self, start, stop):
        return ("getslice", start, stop)

    def __getitem__(self, key):
        return ("getitem", key)


class SizedSlice(object):
    def __len__(self):
        return 3

    def __getslice__(self, start, stop):
        return ("getslice", start, stop)

    def __getitem__(self, key):
        return ("getitem", key)


class ItemOnly(object):
    def __getitem__(self, key):
        return ("getitem", key)


assert ClassicSlice()[1:2] == ("getslice", 1, 2)
assert ClassicSlice()[1:] == ("getslice", 1, MAXIMUM)
assert ClassicSlice()[:2] == ("getslice", 0, 2)
assert ClassicSlice()[:] == ("getslice", 0, MAXIMUM)
assert ModernSlice()[1:2] == ("getslice", 1, 2)
assert ModernSlice()[-1:] == ("getslice", -1, MAXIMUM)
assert SizedSlice()[-1:] == ("getslice", 2, MAXIMUM)
assert SizedSlice()[:-1] == ("getslice", 0, 2)

# An extended slice, an explicit slice object and a plain index keep using the
# item protocol.
assert ClassicSlice()[1:2:3] == ("getitem", slice(1, 2, 3))
assert ClassicSlice()[::2] == ("getitem", slice(None, None, 2))
assert ClassicSlice()[slice(1, 2)] == ("getitem", slice(1, 2))
assert ClassicSlice()[1] == ("getitem", 1)
assert ItemOnly()[1:2] == ("getitem", slice(1, 2, None))

recorded = []


class RecordingSlice(object):
    def __setslice__(self, start, stop, value):
        recorded.append(("setslice", start, stop, value))

    def __setitem__(self, key, value):
        recorded.append(("setitem", key, value))

    def __delslice__(self, start, stop):
        recorded.append(("delslice", start, stop))

    def __delitem__(self, key):
        recorded.append(("delitem", key))


recording = RecordingSlice()
recording[1:2] = [9]
recording[1:2:3] = [8]
del recording[1:2]
del recording[::2]
assert recorded == [
    ("setslice", 1, 2, [9]),
    ("setitem", slice(1, 2, 3), [8]),
    ("delslice", 1, 2),
    ("delitem", slice(None, None, 2)),
]

# Built-in sequences keep their own slicing, including subclasses that do not
# override the protocol.
assert [1, 2, 3, 4][1:3] == [2, 3]
assert [1, 2, 3, 4][-2:] == [3, 4]
assert [1, 2, 3, 4][::2] == [1, 3]
assert "abcd"[1:3] == "bc"
assert (1, 2, 3)[1:] == (2, 3)
assert u"abc"[1:] == u"bc"
assert bytearray("abcd")[1:3] == bytearray("bc")


class ListSubclass(list):
    def __getslice__(self, start, stop):
        return ("getslice", start, stop)


assert ListSubclass([1, 2, 3])[1:2] == ("getslice", 1, 2)


class PlainListSubclass(list):
    pass


assert PlainListSubclass([1, 2, 3])[1:2] == [2]

values = [1, 2, 3, 4]
values[1:3] = [7]
del values[0:1]
assert values == [7, 4]

# An xrange has no slice support and must report it instead of recursing.
try:
    xrange(10)[2:4]
except TypeError:
    pass
else:
    raise AssertionError("xrange accepted a slice")

try:
    xrange(10)[slice(2, 4)]
except TypeError:
    pass
else:
    raise AssertionError("xrange accepted a slice object")

assert xrange(10)[2] == 2
assert xrange(10)[-1] == 9
