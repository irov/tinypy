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


def keyword_property_getter(self):
    return 42


keyword_property = property(fget=keyword_property_getter, doc="keyword property")


class KeywordPropertyOwner(object):
    value = keyword_property


assert KeywordPropertyOwner().value == 42
assert keyword_property.fget is keyword_property_getter
assert keyword_property.__doc__ == "keyword property"


class CallableDescriptor(object):
    def __call__(self, value):
        return value + 1


callable_descriptor = CallableDescriptor()


class CallableStaticOwner(object):
    increment = staticmethod(callable_descriptor)


assert CallableStaticOwner.increment(41) == 42


class CallableClassMethod(object):
    def __call__(self, owner, value):
        return owner.__name__, value


class CallableClassOwner(object):
    identify = classmethod(CallableClassMethod())


assert CallableClassOwner.identify(42) == ("CallableClassOwner", 42)


class CallablePropertyGetter(object):
    "callable getter doc"

    def __call__(self, instance):
        return 42


class CallablePropertyOwner(object):
    value = property().getter(CallablePropertyGetter())


assert CallablePropertyOwner().value == 42
assert CallablePropertyOwner.value.__doc__ == "callable getter doc"


def first_documented_getter(self):
    "first getter doc"


def second_documented_getter(self):
    "second getter doc"


assert property(first_documented_getter).getter(second_documented_getter).__doc__ == "second getter doc"
assert property(first_documented_getter, doc="explicit doc").getter(second_documented_getter).__doc__ == "explicit doc"
assert type(staticmethod(42)) is staticmethod
assert type(classmethod(42)) is classmethod


def direct_class_function(owner, value):
    return owner.__name__, value


def direct_static_function(value):
    return value + 1


direct_classmethod = classmethod(direct_class_function)
direct_staticmethod = staticmethod(direct_static_function)
assert direct_classmethod.__func__ is direct_class_function
assert direct_staticmethod.__func__ is direct_static_function
assert direct_classmethod.__get__(None, DescriptorChild)(42) == ("DescriptorChild", 42)
assert direct_staticmethod.__get__(None, DescriptorChild) is direct_static_function
for descriptor, attribute in (
    (direct_classmethod, "__func__"),
    (direct_staticmethod, "__func__"),
):
    try:
        setattr(descriptor, attribute, 42)
    except TypeError:
        pass
    else:
        raise AssertionError("descriptor __func__ was writable")

def direct_method_function(self, value):
    return value + 1


method_type = type(direct_method_function.__get__(descriptor_object, DescriptorChild))
constructed_method = method_type(direct_method_function, descriptor_object)
assert constructed_method.__func__ is direct_method_function
assert constructed_method.__self__ is descriptor_object
assert constructed_method(42) == 43
direct_constructed_method = method_type.__new__(method_type, direct_method_function, descriptor_object, DescriptorChild)
assert direct_constructed_method.__func__ is direct_method_function
assert direct_constructed_method.__self__ is descriptor_object
assert direct_constructed_method.im_class is DescriptorChild
assert direct_constructed_method(42) == 43
noncallable_getter = property().getter(42)
noncallable_setter = property().setter(42)
noncallable_deleter = property().deleter(42)
assert noncallable_getter.fget == 42
assert noncallable_setter.fset == 42
assert noncallable_deleter.fdel == 42


class IncompleteProperties(object):
    unreadable = property()
    readonly = property(lambda self: 1)


incomplete_properties = IncompleteProperties()
try:
    incomplete_properties.unreadable
except AttributeError:
    pass
else:
    raise AssertionError("property without getter did not raise AttributeError")

try:
    incomplete_properties.readonly = 2
except AttributeError:
    pass
else:
    raise AssertionError("property without setter did not raise AttributeError")

try:
    del incomplete_properties.readonly
except AttributeError:
    pass
else:
    raise AssertionError("property without deleter did not raise AttributeError")

try:
    super(DescriptorChild, descriptor_object).missing
except AttributeError:
    pass
else:
    raise AssertionError("missing super attribute did not raise AttributeError")

try:
    super(DescriptorChild).missing
except AttributeError:
    pass
else:
    raise AssertionError("unbound super attribute did not raise AttributeError")

try:
    property(keyword_property_getter, fget=keyword_property_getter)
except TypeError:
    pass
else:
    raise AssertionError("duplicate property argument was accepted")


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
