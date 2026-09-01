import _struct as struct


assert struct.calcsize(">d") == 8
assert struct.calcsize(">2d") == 16
assert struct.calcsize("d \t\r\n\v\fd") == 16
assert struct.calcsize("12d3") == 96
assert struct.calcsize("66") == 0
assert struct.unpack(">d", struct.pack(">d", 1.5)) == (1.5,)
assert struct.unpack("<2d", struct.pack("<2d", -2.0, 3.25)) == (-2.0, 3.25)

nan, = struct.unpack(">d", b"\x7f\xf8\x00\x00\x00\x00\x00\x00")
inf, = struct.unpack(">d", b"\x7f\xf0\x00\x00\x00\x00\x00\x00")
assert nan != nan
assert inf > 1e300

for invalid_format in ("1 2d", "1\t2d", "1\v2d", "1\f2d"):
    try:
        struct.calcsize(invalid_format)
    except struct.error:
        pass
    else:
        raise AssertionError("struct accepted whitespace inside a repeat count")

struct.calcsize("d" * 32)
struct._clearcache()

packed = struct.pack(">2d", 1.0, 2.0)
destination = bytearray(18)
assert struct.pack_into(">2d", destination, 1, 1.0, 2.0) is None
assert struct.unpack_from(">2d", destination, 1) == (1.0, 2.0)
assert struct.unpack_from(">2d", destination, -17) == (1.0, 2.0)

compiled_struct = struct.Struct(">2d")
assert compiled_struct.format == ">2d"
assert compiled_struct.size == 16
assert compiled_struct.pack(1.0, 2.0) == packed
assert compiled_struct.unpack(packed) == (1.0, 2.0)
compiled_destination = bytearray(16)
assert compiled_struct.pack_into(compiled_destination, 0, 3.0, 4.0) is None
assert compiled_struct.unpack_from(compiled_destination) == (3.0, 4.0)
