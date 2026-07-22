class Pair(object):
    def __init__(self, value):
        self.value = value

    def get(self):
        return self.value


result = Pair(42).get()


def outer(value):
    def inner():
        return value

    return inner


closure_result = outer(42)()
arithmetic_result = (7 + 5) * 3 - 4
loop_result = [value * 2 for value in (1, 2, 3)]
subscript_result = loop_result[-1]
loop_result[1] = 9
mapping_result = {"key": 7}["key"]
delete_result = [1, 2, 3]
del delete_result[0]
slice_result = (1, 2, 3, 4)[1:3]
extended_slice_result = "abcdef"[1:6:2]
slice_assign_result = [0, 1, 2, 3]
slice_assign_result[1:3] = [7, 8, 9]
slice_delete_result = [0, 1, 2, 3, 4]
del slice_delete_result[1:4:2]
power_result = 2 ** 10
bitwise_result = ((5 << 3) | 3) ^ 1
right_shift_result = -7 >> 1
long_shift_result = (1L << 70) >> 69
long_bitwise_result = ((1L << 70) | 5) & 7
invert_long_result = ~1L
negative_long_shift_result = -7L >> 1
negative_bitwise_result = -8L | 3L
long_power_result = 2 ** 3L
comparison_result = 3 < 4 and 4 >= 4 and 2 in (1, 2, 3) and "bc" in "abcd" and 5 not in [1, 2] and "key" in {"key": 7}
sequence_order_result = (1, 2) < (1, 3)


def collect(a, b=0, c=0):
    return a + b + c


call_var_result = collect(1, *(2,))
call_kw_result = collect(1, **{"c": 4})
call_var_kw_result = collect(1, *(2,), **{"c": 3})
builtin_len_result = len([1, 2, 3])
builtin_range_result = range(1, 6, 2)
builtin_isinstance_result = isinstance(Pair(1), Pair)
builtin_callable_result = callable(collect)
builtin_getattr_result = getattr(Pair(7), "value")
builtin_hasattr_result = hasattr(Pair(7), "value")


class OptionalLookup(object):
    plain = 11

    @property
    def broken_property(self):
        raise ValueError("property failure")

    def __getattr__(self, name):
        if name == "dynamic":
            return 12
        if name == "runtime_failure":
            raise RuntimeError("runtime failure")
        raise AttributeError(name)


optional_lookup = OptionalLookup()
assert hasattr(optional_lookup, "plain")
assert hasattr(optional_lookup, "dynamic")
assert not hasattr(optional_lookup, "missing")
assert not hasattr(optional_lookup, "broken_property")
assert not hasattr(optional_lookup, "runtime_failure")
assert getattr(optional_lookup, "dynamic", 17) == 12
assert getattr(optional_lookup, "missing", 17) == 17

try:
    getattr(optional_lookup, "broken_property", 17)
except ValueError:
    pass
else:
    raise AssertionError("getattr default swallowed ValueError")

try:
    getattr(optional_lookup, "runtime_failure", 17)
except RuntimeError:
    pass
else:
    raise AssertionError("getattr default swallowed RuntimeError")


builtin_abs_result = abs(-7)
builtin_ord_result = ord("A")
builtin_id_result = id(Pair(1)) > 0
builtin_iterator = iter((9,))
builtin_next_result = next(builtin_iterator)
builtin_setattr_object = Pair(1)
setattr(builtin_setattr_object, "value", 8)
builtin_setattr_result = builtin_setattr_object.value


class DescriptorBase(object):
    def __init__(self, value):
        self._value = value

    @staticmethod
    def increment(value):
        return value + 1

    @classmethod
    def is_descriptor_child(cls):
        return cls is DescriptorChild

    @property
    def descriptor_value(self):
        return self._value

    @descriptor_value.setter
    def descriptor_value(self, value):
        self._value = value

    def inherited_value(self):
        return self._value + 1


class DescriptorChild(DescriptorBase):
    def inherited_value(self):
        return super(DescriptorChild, self).inherited_value() + 1


class ObjectInitializerChild(object):
    def __init__(self):
        super(ObjectInitializerChild, self).__init__()
        self.initialized = True


descriptor_object = DescriptorChild(40)
object_initializer = ObjectInitializerChild()
staticmethod_result = DescriptorChild.increment(41)
classmethod_result = DescriptorChild.is_descriptor_child()
property_read_result = descriptor_object.descriptor_value
descriptor_object.descriptor_value = 50
property_write_result = descriptor_object.descriptor_value
super_result = descriptor_object.inherited_value()
object_initializer_result = object_initializer.initialized
property_fields_result = DescriptorBase.descriptor_value.fget is not None and DescriptorBase.descriptor_value.fset is not None


class CopiedMethodBase(object):
    def copied_method(self, value):
        return value


class CopiedMethodChild(CopiedMethodBase):
    pass


CopiedMethodChild.copied_method = CopiedMethodBase.copied_method
copied_method_result = CopiedMethodChild().copied_method(42)
assert copied_method_result == 42


def catch_builtin_exception(value):
    try:
        if value:
            raise ValueError("bad")
        return 0
    except ValueError as error:
        return 41 + (error.args[0] == "bad")


exception_result = catch_builtin_exception(True)
finally_marker = 0


def return_through_finally():
    global finally_marker
    try:
        return 42
    finally:
        finally_marker = 1


finally_return_result = return_through_finally()


def raise_nested():
    raise KeyError("key")


def catch_nested():
    try:
        raise_nested()
    except LookupError:
        return 42


nested_exception_result = catch_nested()


class CustomFailure(Exception):
    def __init__(self, value):
        self.value = value


def catch_custom_exception():
    try:
        raise CustomFailure(42)
    except CustomFailure as error:
        return error.value


custom_exception_result = catch_custom_exception()


def reraiser():
    try:
        raise IndexError("index")
    except IndexError:
        raise


def catch_reraised():
    try:
        reraiser()
    except LookupError:
        return 42


reraised_exception_result = catch_reraised()


def finally_override():
    try:
        raise ValueError("value")
    finally:
        raise KeyError("key")


def catch_finally_override():
    try:
        finally_override()
    except KeyError:
        return 42


finally_override_result = catch_finally_override()
