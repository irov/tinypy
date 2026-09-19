empty = bytearray()
assert len(empty) == 0, "empty len"
assert str(empty) == "", "empty str"
assert repr(empty) == "bytearray(b'')", "empty repr"
assert repr(bytearray("J'")) == 'bytearray(b"J\\\'")', "single quote repr"

zeros = bytearray(4)
assert list(zeros) == [0, 0, 0, 0], "zeros list"
zeros[1] = 255
zeros[-1] = 7
assert zeros[1] == 255, "index set"
assert zeros[-1] == 7, "negative index set"

value = bytearray("abcdef")
assert list(value) == [97, 98, 99, 100, 101, 102], "value list"
assert str(value[1:5:2]) == "bd", "step slice"
assert str(value[::-1]) == "fedcba", "reverse slice"
value[1:3] = [88, 89, 90]
assert str(value) == "aXYZdef", "grow slice"
value[0:0] = [48, 49]
assert str(value) == "01aXYZdef", "insert slice"
value[::2] = [65, 66, 67, 68, 69]
assert str(value) == "A1BXCZDeE", "extended slice"
del value[1:3]
assert str(value) == "AXCZDeE", "delete slice"
deleted_by_empty_assignment = bytearray("abc")
deleted_by_empty_assignment[::2] = ""
assert deleted_by_empty_assignment == bytearray("b"), "empty extended slice assignment"

joined = bytearray("ab") + "cd"
assert repr(joined) == "bytearray(b'abcd')", "joined repr"
joined_alias = joined
joined += bytearray("ef")
assert str(joined) == "abcdef", "inplace add"
assert joined is joined_alias, "inplace add identity"
joined *= 2
assert joined is joined_alias and str(joined) == "abcdefabcdef", "inplace multiply identity"
joined = joined[:6]
joined_alias = joined
try:
    joined += joined
except BufferError:
    pass
else:
    raise AssertionError("non-empty self inplace add did not preserve its export")
assert joined is joined_alias and str(joined) == "abcdef", "failed self inplace add mutation"
empty_alias = empty
empty += empty
assert empty is empty_alias and len(empty) == 0, "empty self inplace add"
joined = joined[:6]
joined.append(103)
joined.extend([104, 105])
assert str(joined) == "abcdefghi", "append extend"
assert joined.find("cde") == 2, "find present"
assert joined.find("z") == -1, "find absent"
assert joined.find("a", 1) == -1, "find bound"
assert joined.find("", 100) == -1, "empty find high bound"
assert str(bytearray.fromhex("41 42 43")) == "ABC", "fromhex"
for invalid_hex_space in ("\t", "\n", "\r", "\v", "\f"):
    try:
        bytearray.fromhex("41" + invalid_hex_space + "42")
    except ValueError:
        pass
    else:
        raise AssertionError("fromhex accepted non-space ASCII whitespace")
assert "a" in bytearray("abc"), "string containment"
assert "ab" in bytearray("abc"), "substring containment"
assert "" in bytearray("abc"), "empty substring containment"
assert bytearray("ab") in bytearray("abc"), "bytearray containment"
assert buffer("ab") in bytearray("abc"), "buffer containment"
try:
    300 in bytearray("abc")
except ValueError:
    pass
else:
    raise AssertionError("out-of-range byte containment did not fail")


class BytearraySubtype(bytearray):
    pass


assert type(BytearraySubtype.fromhex("41")) is bytearray
assert bytearray("abc").__alloc__() >= 4
assert str(bytearray(u"caf\xe9", "latin-1")) == "caf\xe9", "unicode encoding"
assert str(bytearray("ab") * 3) == "ababab", "multiply"
assert str(3 * bytearray("ab")) == "ababab", "reflected multiply"
assert str(bytearray("abc").upper()) == "ABC", "upper"
assert [str(item) for item in bytearray("a,b").split(",")] == ["a", "b"], "split"
assert str(bytearray("abc").translate(None, "b")) == "ac", "translate delete"


class ByteIndex(object):
    def __index__(self):
        return 1


indexed = bytearray(ByteIndex())
indexed.append("A")
indexed[0] = "Z"
assert str(indexed) == "ZA", "index protocol and one-byte strings"

view = buffer(joined, 2, 3)
assert str(view) == "cde", "buffer view"
assert bytearray(view) == bytearray("cde"), "bytearray equality"
# Python 2 keeps bytearray's identity hash visible to subclasses while the
# exact mutable builtin remains unhashable.
try:
    hash(bytearray("abc"))
except TypeError:
    pass
else:
    raise AssertionError("bytearray was hashable")


class HashableBytearray(bytearray):
    pass


hashable_bytearray = HashableBytearray("abc")
assert bytearray.__hash__(hashable_bytearray) == object.__hash__(hashable_bytearray)
assert hash(hashable_bytearray) == object.__hash__(hashable_bytearray)


# A byte string concatenated with a bytearray keeps the mutable type.
assert "a" + bytearray("b") == bytearray("ab")
assert type("a" + bytearray("b")) is bytearray
assert bytearray("a") + "b" == bytearray("ab")
assert "" + bytearray("") == bytearray()
assert type("" + bytearray("")) is bytearray

try:
    "a" + buffer("b")
except TypeError:
    pass
else:
    raise AssertionError("str accepted a buffer operand")
