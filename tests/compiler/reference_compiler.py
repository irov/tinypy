import marshal
import struct
import sys
import types


CODE_FIELDS = (
    "co_argcount",
    "co_nlocals",
    "co_stacksize",
    "co_flags",
    "co_code",
    "co_names",
    "co_varnames",
    "co_freevars",
    "co_cellvars",
    "co_filename",
    "co_name",
    "co_firstlineno",
    "co_lnotab",
)


def read_bytes(path):
    stream = open(path, "rb")
    try:
        return stream.read()
    finally:
        stream.close()


def write_bytes(path, data):
    stream = open(path, "wb")
    try:
        stream.write(data)
    finally:
        stream.close()


def describe(value):
    if isinstance(value, float):
        return "float:%s" % struct.pack("<d", value).encode("hex")
    if isinstance(value, complex):
        return "complex:%s:%s" % (
            struct.pack("<d", value.real).encode("hex"),
            struct.pack("<d", value.imag).encode("hex"),
        )
    return "%s:%r" % (type(value).__name__, value)


def compare_value(expected, actual, path):
    if type(expected) is not type(actual):
        return "%s type: %s != %s" % (
            path,
            type(expected).__name__,
            type(actual).__name__,
        )
    if isinstance(expected, types.CodeType):
        return compare_code(expected, actual, path)
    if isinstance(expected, tuple):
        if len(expected) != len(actual):
            return "%s length: %d != %d" % (path, len(expected), len(actual))
        for index, expected_item in enumerate(expected):
            difference = compare_value(
                expected_item,
                actual[index],
                "%s[%d]" % (path, index),
            )
            if difference is not None:
                return difference
        return None
    if isinstance(expected, float):
        expected_bits = struct.pack("<d", expected)
        actual_bits = struct.pack("<d", actual)
        if expected_bits != actual_bits:
            return "%s: %s != %s" % (
                path,
                describe(expected),
                describe(actual),
            )
        return None
    if isinstance(expected, complex):
        expected_bits = struct.pack("<dd", expected.real, expected.imag)
        actual_bits = struct.pack("<dd", actual.real, actual.imag)
        if expected_bits != actual_bits:
            return "%s: %s != %s" % (
                path,
                describe(expected),
                describe(actual),
            )
        return None
    if expected != actual:
        return "%s: %s != %s" % (
            path,
            describe(expected),
            describe(actual),
        )
    return None


def compare_code(expected, actual, path):
    for field in CODE_FIELDS:
        expected_value = getattr(expected, field)
        actual_value = getattr(actual, field)
        difference = compare_value(
            expected_value,
            actual_value,
            "%s.%s" % (path, field),
        )
        if difference is not None:
            return difference
    return compare_value(expected.co_consts, actual.co_consts, path + ".co_consts")


def compile_source(source_path, output_path, logical_filename, mode):
    source = read_bytes(source_path)
    code = compile(source, logical_filename, mode, 0, 1)
    write_bytes(output_path, marshal.dumps(code, 2))


def compare_marshaled(expected_path, actual_path):
    expected = marshal.loads(read_bytes(expected_path))
    actual = marshal.loads(read_bytes(actual_path))
    difference = compare_value(expected, actual, "code")
    if difference is None:
        difference = "marshal stream differs while code objects are structurally equal"
    sys.stdout.write(difference + "\n")


def main():
    if len(sys.argv) == 6 and sys.argv[1] == "compile":
        compile_source(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
        return 0
    if len(sys.argv) == 4 and sys.argv[1] == "compare":
        compare_marshaled(sys.argv[2], sys.argv[3])
        return 0
    sys.stderr.write(
        "usage: reference_compiler.py compile SOURCE OUTPUT FILENAME MODE\n"
        "       reference_compiler.py compare EXPECTED ACTUAL\n"
    )
    return 2


if __name__ == "__main__":
    sys.exit(main())
