source = "abcdef"
view = buffer(source, 1, 4)

assert type(view) is buffer
assert str(view) == "bcde"
assert len(view) == 4
assert bool(view) is True
assert view[0] == "b"
assert view[-1] == "e"
assert view[1:3] == "cd"
assert view[::-1] == "edcb"
assert list(view) == ["b", "c", "d", "e"]
assert hash(view) == hash("bcde")
assert str(buffer(view, 1, 2)) == "cd"
assert view.__len__() == 4
assert view.__getitem__(0) == "b"
assert view.__getslice__(1, 3) == "cd"
assert view.__add__("fg") == "bcdefg"
assert view.__mul__(2) == "bcdebcde"
assert view.__rmul__(2) == "bcdebcde"
assert buffer("abc").__cmp__(buffer("abc")) == 0
assert buffer("abc").__cmp__(buffer("abd")) == -1
assert buffer("abd").__cmp__(buffer("abc")) == 1
assert buffer("abc").__cmp__(buffer("abcd")) == -1
try:
    buffer("abc").__cmp__("abc")
except TypeError:
    pass
else:
    raise AssertionError("buffer.__cmp__ accepted a non-buffer argument")
assert view + "fg" == "bcdefg"
assert view * 2 == "bcdebcde"
assert 2 * view == "bcdebcde"

dynamic_owner = bytearray("ab")
dynamic_view = buffer(dynamic_owner)
dynamic_owner.extend("cd")
assert len(dynamic_view) == 4
assert str(dynamic_view) == "abcd"
dynamic_offset_view = buffer(dynamic_owner, 2, 2)
del dynamic_owner[:]
assert len(dynamic_view) == 0
assert str(dynamic_view) == ""
assert len(dynamic_offset_view) == 0
assert str(dynamic_offset_view) == ""
dynamic_owner.extend("uvwxyz")
assert str(dynamic_view) == "uvwxyz"
assert str(dynamic_offset_view) == "wx"

for readonly_method, readonly_arguments in (
    (view.__setitem__, (0, "x")),
    (view.__setslice__, (0, 1, "x")),
    (view.__delitem__, (0,)),
    (view.__delslice__, (0, 1)),
):
    try:
        readonly_method(*readonly_arguments)
    except TypeError:
        pass
    else:
        raise AssertionError("read-only buffer method accepted a mutation")
assert repr(Ellipsis) == "Ellipsis"
assert bool(Ellipsis) is True

readonly_memory = memoryview("abcd")
assert type(readonly_memory) is memoryview
assert len(readonly_memory) == 4
assert readonly_memory[0] == "a"
assert readonly_memory[-1] == "d"
assert readonly_memory[1:3].tobytes() == "bc"
assert readonly_memory.tobytes() == "abcd"
assert readonly_memory.tostring() == "abcd"
assert readonly_memory.tolist() == [97, 98, 99, 100]
assert readonly_memory.format == "B"
assert readonly_memory.itemsize == 1L
assert readonly_memory.ndim == 1L
assert readonly_memory.readonly is True
assert readonly_memory.shape == (4L,)
assert readonly_memory.strides == (1L,)
assert readonly_memory.suboffsets is None
assert readonly_memory == "abcd"
try:
    readonly_memory[0] = "z"
except TypeError:
    pass
else:
    raise AssertionError("read-only memoryview accepted a mutation")
try:
    readonly_memory[::2]
except NotImplementedError:
    pass
else:
    raise AssertionError("memoryview accepted a stepped slice")

writable_owner = bytearray("abcd")
writable_memory = memoryview(writable_owner)
assert writable_memory.readonly is False
writable_memory[1] = "Z"
writable_memory[2:4] = "XY"
assert str(writable_owner) == "aZXY"
child_memory = memoryview(writable_memory)[1:3]
child_memory[:] = "12"
assert str(writable_owner) == "a12Y"

def resize_delete():
    del writable_owner[0]

def resize_slice():
    writable_owner[0:1] = "xy"

for resize in (
    lambda: writable_owner.append(0),
    lambda: writable_owner.extend("x"),
    resize_delete,
    resize_slice,
    lambda: writable_owner.__init__("x"),
):
    try:
        resize()
    except BufferError:
        pass
    else:
        raise AssertionError("bytearray resized while exporting memory")
del child_memory
del writable_memory
writable_owner.append(33)
assert str(writable_owner) == "a12Y!"

# Keep a dynamically owned view alive through VM shutdown to cover graph
# traversal of the native payload owner.
retained_memory = memoryview(bytearray("retained"))
assert retained_memory.tobytes() == "retained"
