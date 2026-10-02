# Raise forms, the with-statement protocol, exception matching, name and
# unpacking diagnostics, exec/eval argument handling and print softspace follow
# CPython 2.7.
import sys


class Old:
    pass


class E(Exception):
    pass


def error_text(callable_object, *args):
    try:
        callable_object(*args)
    except:
        error = sys.exc_info()[1]
        if isinstance(error, Old):
            return "Old"
        return type(error).__name__ + ": " + str(error)
    raise AssertionError("no exception raised")


def raise_with_traceback():
    try:
        raise E("x")
    except E:
        traceback = sys.exc_info()[2]
    raise E("y"), None, traceback


def raise_tuple():
    raise (E, "x"), "msg"


def raise_integer():
    raise 1


def raise_bad_traceback():
    raise E, 1, 2


def raise_instance_with_value():
    raise E("a"), "b"


def bare_raise():
    raise


def raise_classic():
    raise Old, None


def traceback_depth():
    def inner():
        try:
            raise E("deep")
        except E:
            return sys.exc_info()[2]
    traceback = inner()
    try:
        raise E, E("again"), traceback
    except E:
        current = sys.exc_info()[2]
        depth = 0
        while current is not None:
            depth += 1
            current = current.tb_next
        return depth


assert error_text(raise_with_traceback) == "E: y"
assert error_text(raise_tuple) == "E: msg"
assert error_text(raise_integer) == "TypeError: exceptions must be old-style classes or derived from BaseException, not int"
assert error_text(raise_bad_traceback) == "TypeError: raise: arg 3 must be a traceback or None"
assert error_text(raise_instance_with_value) == "TypeError: instance exception may not have a separate value"
assert error_text(bare_raise) == "TypeError: exceptions must be old-style classes or derived from BaseException, not NoneType"
assert error_text(raise_classic) == "Old"
assert traceback_depth() == 1


class NoExit(object):
    def __enter__(self):
        return 1


def with_integer():
    with 1:
        pass


def with_no_exit():
    with NoExit():
        pass


def with_classic():
    with Old():
        pass


assert error_text(with_integer) == "AttributeError: __exit__"
assert error_text(with_no_exit) == "AttributeError: __exit__"
assert error_text(with_classic) == "AttributeError: Old instance has no attribute '__exit__'"


def except_integer():
    try:
        raise E("q")
    except 5:
        return "matched"


def except_tuple():
    try:
        raise E("q")
    except (5, E):
        return "matched tuple"


assert error_text(except_integer) == "E: q"
assert except_tuple() == "matched tuple"


def unbound_local():
    print zz
    zz = 1


def delete_unbound():
    del zz


def free_variable():
    def inner():
        return fv
    result = inner()
    fv = 1
    return result


def unpack_list():
    a, b, c = [1, 2]


def unpack_tuple():
    a, b = (1, 2, 3)


def unpack_empty():
    a, = []


def unpack_iterator_few():
    a, b, c = iter([1, 2])


def unpack_iterator_many():
    a, b = iter([1, 2, 3])


def global_name():
    return undefined_global_name


assert error_text(unbound_local) == "UnboundLocalError: local variable 'zz' referenced before assignment"
assert error_text(delete_unbound) == "UnboundLocalError: local variable 'zz' referenced before assignment"
assert error_text(free_variable) == "NameError: free variable 'fv' referenced before assignment in enclosing scope"
assert error_text(lambda: (lambda a, b, c: 0)(*[1, 2])) == "TypeError: <lambda>() takes exactly 3 arguments (2 given)"
assert error_text(unpack_list) == "ValueError: need more than 2 values to unpack"
assert error_text(unpack_tuple) == "ValueError: too many values to unpack"
assert error_text(unpack_empty) == "ValueError: need more than 0 values to unpack"
assert error_text(unpack_iterator_few) == "ValueError: need more than 2 values to unpack"
assert error_text(unpack_iterator_many) == "ValueError: too many values to unpack"
assert error_text(global_name) == "NameError: global name 'undefined_global_name' is not defined"


class Mapping(object):
    def __init__(self):
        self.storage = {}

    def __getitem__(self, key):
        return self.storage[key]

    def __setitem__(self, key, value):
        self.storage[key] = value

    def __delitem__(self, key):
        del self.storage[key]


def exec_locals_to_fast():
    x = 0
    exec "x = 1"
    return x


def exec_tuple_form():
    exec ("y = 5", {})
    return "ok"


def exec_tuple_three():
    namespace = {}
    exec ("y = 6", namespace, namespace)
    return namespace["y"]


def exec_mapping_locals():
    mapping = Mapping()
    exec "q = 7; w = q + 1" in {}, mapping
    return sorted(mapping.storage.items())


def exec_integer():
    exec 1


def exec_bad_globals():
    exec "x" in 1


def exec_bad_locals():
    exec "x" in {}, 1


def make_closure():
    v = 1

    def inner():
        return v
    return inner


def exec_free_variables():
    exec make_closure().func_code in {}


def eval_free_variables():
    eval(make_closure().func_code)


def eval_mapping_locals():
    mapping = Mapping()
    mapping.storage["q"] = 10
    return eval("q + 1", {}, mapping)


def exec_mapping_import():
    mapping = Mapping()
    exec "import sys" in {}, mapping
    return "sys" in mapping.storage


def exec_mapping_delete():
    mapping = Mapping()
    mapping.storage["v"] = 3
    exec "del v" in {}, mapping
    return mapping.storage


def exec_mapping_delete_missing():
    exec "del nothere" in {}, Mapping()


assert exec_locals_to_fast() == 1
assert exec_tuple_form() == "ok"
assert exec_tuple_three() == 6
assert exec_mapping_locals() == [("q", 7), ("w", 8)]
assert error_text(exec_integer) == "TypeError: exec: arg 1 must be a string, file, or code object"
assert error_text(exec_bad_globals) == "TypeError: exec: arg 2 must be a dictionary or None"
assert error_text(exec_bad_locals) == "TypeError: exec: arg 3 must be a mapping or None"
assert error_text(exec_free_variables) == "TypeError: code object passed to exec may not contain free variables"
assert error_text(eval_free_variables) == "TypeError: code object passed to eval() may not contain free variables"
assert eval("q + 1", {}, {"q": 1}) == 2
assert eval_mapping_locals() == 11
assert error_text(eval, "1", {}, 1) == "TypeError: locals must be a mapping"
assert error_text(eval, "1", 1) == "TypeError: globals must be a dict"
assert error_text(eval, "1", Mapping()) == "TypeError: globals must be a real dict; try eval(expr, {}, mapping)"
assert error_text(eval, 1) == "TypeError: eval() arg 1 must be a string or code object"
assert exec_mapping_import() is True
assert exec_mapping_delete() == {}
assert error_text(exec_mapping_delete_missing) == "NameError: name 'nothere' is not defined"


class Capture(object):
    def __init__(self):
        self.chunks = []

    def write(self, text):
        self.chunks.append(text)


class Shouting(str):
    def __str__(self):
        return "S!"


class Newline:
    def __str__(self):
        return "x\n"


capture = Capture()
saved_stdout = sys.stdout
sys.stdout = capture
try:
    print "a", "\nb"
    print "a ",
    print "b"
    print Newline(), 1
    print Shouting("raw"), Shouting("raw2")
    print "tab\t",
    print "after"
    print "",
    print "empty"
finally:
    sys.stdout = saved_stdout
assert "".join(capture.chunks) == "a \nb\na  b\nx\n 1\nS! S!\ntab\tafter\n empty\n"
