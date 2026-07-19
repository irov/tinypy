empty = bytearray()
assert len(empty) == 0, "empty len"
assert str(empty) == "", "empty str"
assert repr(empty) == "bytearray(b'')", "empty repr"

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

joined = bytearray("ab") + "cd"
assert repr(joined) == "bytearray(b'abcd')", "joined repr"
joined += bytearray("ef")
assert str(joined) == "abcdef", "inplace add"
joined.append(103)
joined.extend([104, 105])
assert str(joined) == "abcdefghi", "append extend"
assert joined.find("cde") == 2, "find present"
assert joined.find("z") == -1, "find absent"
assert joined.find("a", 1) == -1, "find bound"

view = buffer(joined, 2, 3)
assert str(view) == "cde", "buffer view"
assert bytearray(view) == bytearray("cde"), "bytearray equality"
