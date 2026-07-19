import _struct as struct


assert struct.calcsize(">d") == 8
assert struct.calcsize(">2d") == 16
assert struct.unpack(">d", struct.pack(">d", 1.5)) == (1.5,)
assert struct.unpack("<2d", struct.pack("<2d", -2.0, 3.25)) == (-2.0, 3.25)

nan, = struct.unpack(">d", b"\x7f\xf8\x00\x00\x00\x00\x00\x00")
inf, = struct.unpack(">d", b"\x7f\xf0\x00\x00\x00\x00\x00\x00")
assert nan != nan
assert inf > 1e300
