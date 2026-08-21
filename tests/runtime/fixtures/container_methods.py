values = [3, 1]
append = values.append
assert type(append) is type(len)
assert not hasattr(append, "func_code")
assert values.append(2) is None
values.extend((5, 4))
values.insert(-100, 0)
assert values == [0, 3, 1, 2, 5, 4]
assert values.count(3) == 1
assert values.index(2) == 3
values.remove(5)
assert values.pop(0) == 0
assert values.pop() == 4
values.reverse()
assert values == [2, 1, 3]
values.sort()
assert values == [1, 2, 3]
values_iterator = values.__iter__()
assert next(values_iterator) == 1
assert values.__len__() == 3
assert values.__getitem__(1) == 2
assert values.__repr__() == "[1, 2, 3]"

tuple_values = (3, 1, 3, 2)
assert tuple_values.count(3) == 2
assert tuple_values.index(3) == 0
assert tuple_values.index(3, 1) == 2
assert tuple_values.index(3, -2, 99) == 2
assert tuple_values.index(3, -10L ** 100, 10L ** 100) == 0
assert tuple_values.__len__() == 4
assert tuple_values.__getitem__(1) == 1
assert tuple_values.__repr__() == "(3, 1, 3, 2)"
assert tuple.__contains__(tuple_values, 2)
assert tuple.__add__((1,), (2,)) == (1, 2)
assert tuple.__mul__((1, 2), 2) == (1, 2, 1, 2)
assert tuple.__hash__((1, 2)) == hash((1, 2))
tuple_iterator = tuple_values.__iter__()
assert next(tuple_iterator) == 3

direct_list = [1, 2]
list.__setitem__(direct_list, 0, 3)
assert direct_list == [3, 2]
list.__delitem__(direct_list, 1)
assert direct_list == [3]
assert list.__contains__(direct_list, 3)
assert list.__add__(direct_list, [4]) == [3, 4]
assert list.__mul__(direct_list, 2) == [3, 3]
assert list.__eq__([1], [1]) and list.__lt__([1], [2])
assert list.__hash__ is None
direct_list_alias = direct_list
assert list.__iadd__(direct_list, (4, 5)) is direct_list
assert direct_list_alias == [3, 4, 5]
assert list.__imul__(direct_list, 2) is direct_list
assert direct_list_alias == [3, 4, 5, 3, 4, 5]
inplace_list = [1]
inplace_list_alias = inplace_list
inplace_list += (2, 3)
assert inplace_list is inplace_list_alias and inplace_list == [1, 2, 3]
inplace_list *= 2
assert inplace_list is inplace_list_alias and inplace_list == [1, 2, 3, 1, 2, 3]

direct_dict = {"a": 1}
dict.__setitem__(direct_dict, "b", 2)
assert dict.__contains__(direct_dict, "a")
dict.__delitem__(direct_dict, "a")
assert direct_dict == {"b": 2}
assert dict.__hash__ is None
assert {}.__cmp__({}) == 0
assert {"a": 1}.__cmp__({"a": 2}) == -1
assert {"a": 2}.__cmp__({"a": 1}) == 1
try:
    {}.__cmp__([])
except TypeError:
    pass
else:
    raise AssertionError("dict.__cmp__ accepted a non-dict argument")
for set_value in (set(), frozenset()):
    try:
        set_value.__cmp__(set())
    except TypeError:
        pass
    else:
        raise AssertionError("set.__cmp__ returned an ordering")

assert str.__getitem__("abc", 1) == "b"
assert str.__contains__("abc", "b")
assert str.__add__("a", "b") == "ab"
assert str.__mul__("ab", 2) == "abab"
assert str.__mod__("%s", "x") == "x"
assert str.__rmod__("x", "%s") == "x"
assert str.__hash__("abc") == hash("abc")
assert str.__format__("x", ">3") == "  x"
assert unicode.__hash__(u"abc") == hash(u"abc")
assert unicode.__format__(u"x", u">3") == u"  x"
assert list("a{{b}}c{0!r:>10}d"._formatter_parser()) == [
    ("a{", None, None, None),
    ("b}", None, None, None),
    ("c", "0", ">10", "r"),
    ("d", None, None, None),
]
assert list(u"{0:{1}}"._formatter_parser()) == [(u"", u"0", u"{1}", None)]
formatter_head, formatter_path = "name[2][key].value"._formatter_field_name_split()
assert formatter_head == "name"
assert list(formatter_path) == [(False, 2L), (False, "key"), (True, "value")]
formatter_head, formatter_path = u"01.name"._formatter_field_name_split()
assert formatter_head == 1L
assert list(formatter_path) == [(True, u"name")]
formatter_head, invalid_formatter_path = "name[]"._formatter_field_name_split()
assert formatter_head == "name"
try:
    next(invalid_formatter_path)
except ValueError:
    pass
else:
    raise AssertionError("formatter field splitter accepted an empty item")
try:
    next(invalid_formatter_path)
except StopIteration:
    pass
else:
    raise AssertionError("formatter field splitter resumed after an error")
invalid_formatter_parser = "{"._formatter_parser()
try:
    next(invalid_formatter_parser)
except ValueError:
    pass
else:
    raise AssertionError("formatter parser accepted an unmatched brace")
try:
    next(invalid_formatter_parser)
except StopIteration:
    pass
else:
    raise AssertionError("formatter parser resumed after an error")
assert int.__add__(2, 3) == 5
assert int.__rsub__(2, 10) == 8
assert long.__lshift__(1L, 5) == 32L
assert float.__div__(3.0, 2) == 1.5
assert complex.__abs__(3 + 4j) == 5.0
assert int.__hash__(42) == hash(42)
assert long.__hash__(42L) == hash(42L)
assert float.__hash__(1.5) == hash(1.5)
assert complex.__hash__(1 + 2j) == hash(1 + 2j)
assert int.__format__(15, "04x") == "000f"
assert float.__format__(1.5, ".1f") == "1.5"
assert "__eq__" not in int.__dict__
assert "__lt__" not in long.__dict__
assert "__eq__" in float.__dict__
assert "__eq__" in complex.__dict__
assert "__iter__" not in str.__dict__
assert "__iter__" not in unicode.__dict__
assert (2).__cmp__(3) == -1
assert (3L).__cmp__(2L) == 1
try:
    (2).__cmp__(3L)
except TypeError:
    pass
else:
    raise AssertionError("int.__cmp__ accepted a long argument")
assert (2).__coerce__(3) == (2, 3)
assert (2).__coerce__(3L) is NotImplemented
assert (3L).__coerce__(2) == (3L, 2L)
assert (2.5).__coerce__(3) == (2.5, 3.0)
assert (1 + 2j).__coerce__(3.0) == (1 + 2j, 3 + 0j)
assert (2).__getnewargs__() == (2,)
assert (3L).__getnewargs__() == (3L,)
assert (2.5).__getnewargs__() == (2.5,)
assert (1 + 2j).__getnewargs__() == (1.0, 2.0)
assert object().__class__ is object
assert "__class__" in dir(object())
assert object().__sizeof__() > 0
assert (1L).__sizeof__() > 0
assert isinstance(str.__doc__, str)
assert isinstance(dict.__doc__, str)


class DescriptorSample(object):
    def method(self):
        return 42

    @property
    def answer(self):
        return 42


descriptor_sample = DescriptorSample()
bound_method = descriptor_sample.method
assert bound_method.im_func is DescriptorSample.method.im_func
assert bound_method.__func__ is DescriptorSample.method.im_func
assert bound_method.im_self is descriptor_sample
assert bound_method.__self__ is descriptor_sample
assert bound_method.im_class is DescriptorSample
assert bound_method.__call__() == 42
assert bound_method.__repr__() == repr(bound_method)
assert bound_method.__hash__() == hash(bound_method)
assert bound_method.__cmp__(bound_method) == 0


class EqualMethodOwner(object):
    def __eq__(self, other):
        return isinstance(other, EqualMethodOwner)

    def __hash__(self):
        return 123

    def method(self):
        return 42


equal_method_left = EqualMethodOwner().method
equal_method_right = EqualMethodOwner().method
assert equal_method_left == equal_method_right
assert equal_method_left.__cmp__(equal_method_right) == 0
assert hash(equal_method_left) == hash(equal_method_right)


class UnhashableMethodOwner(object):
    __hash__ = None

    def method(self):
        return 42


try:
    hash(UnhashableMethodOwner().method)
except TypeError:
    pass
else:
    raise AssertionError("bound method must inherit unhashability from self")
for method_attribute in ("im_func", "__func__", "im_self", "__self__", "im_class"):
    assert method_attribute in dir(bound_method)
assert DescriptorSample.__dict__["method"].__get__(descriptor_sample, DescriptorSample)() == 42
assert DescriptorSample.__dict__["answer"].__get__(descriptor_sample, DescriptorSample) == 42
try:
    DescriptorSample.__dict__["answer"].__set__(descriptor_sample, 7)
except AttributeError:
    pass
else:
    raise AssertionError("read-only property accepted __set__")

descriptor_code = DescriptorSample.method.func_code
for code_attribute in (
    "co_argcount", "co_cellvars", "co_code", "co_consts", "co_filename",
    "co_firstlineno", "co_flags", "co_freevars", "co_lnotab", "co_name",
    "co_names", "co_nlocals", "co_stacksize", "co_varnames",
):
    assert code_attribute in dir(descriptor_code)

assert "__dict__" in DescriptorSample.__dict__
assert "__weakref__" in DescriptorSample.__dict__
replacement_dict = {"replacement": 43}
descriptor_sample.__dict__ = replacement_dict
assert descriptor_sample.__dict__ is replacement_dict


class InplaceProtocol(object):
    def __ipow__(self, other):
        return "ipow"

    def __imod__(self, other):
        return "imod"

    def __ifloordiv__(self, other):
        return "ifloordiv"

    def __ilshift__(self, other):
        return "ilshift"

    def __irshift__(self, other):
        return "irshift"

    def __iand__(self, other):
        return "iand"

    def __ixor__(self, other):
        return "ixor"

    def __ior__(self, other):
        return "ior"


inplace_protocol = InplaceProtocol()
inplace_protocol **= 2
assert inplace_protocol == "ipow"
inplace_protocol = InplaceProtocol()
inplace_protocol %= 2
assert inplace_protocol == "imod"
inplace_protocol = InplaceProtocol()
inplace_protocol //= 2
assert inplace_protocol == "ifloordiv"
inplace_protocol = InplaceProtocol()
inplace_protocol <<= 2
assert inplace_protocol == "ilshift"
inplace_protocol = InplaceProtocol()
inplace_protocol >>= 2
assert inplace_protocol == "irshift"
inplace_protocol = InplaceProtocol()
inplace_protocol &= 2
assert inplace_protocol == "iand"
inplace_protocol = InplaceProtocol()
inplace_protocol ^= 2
assert inplace_protocol == "ixor"
inplace_protocol = InplaceProtocol()
inplace_protocol |= 2
assert inplace_protocol == "ior"


class TupleIndex(object):
    def __index__(self):
        return 1


assert tuple_values.index(3, TupleIndex()) == 2
assert values.index(2, TupleIndex()) == 1


def drag_drop_item_index(*values):
    items_queue = values[1::2]
    return items_queue.index("ItemB")


assert drag_drop_item_index("SocketA", "ItemA", "SocketB", "ItemB") == 1

try:
    tuple_values.index(99)
except ValueError as tuple_index_error:
    assert str(tuple_index_error) == "tuple.index(x): x not in tuple"
else:
    raise AssertionError("tuple.index did not raise ValueError")

iterated_values = [1, 2, 3]
iterated_result = []
for iterated_value in iterated_values:
    iterated_result.append(iterated_value)
    if iterated_value == 1:
        iterated_values.append(4)
assert iterated_result == [1, 2, 3, 4]

iterated_values = [1, 2, 3]
iterated_result = []
for iterated_value in iterated_values:
    iterated_result.append(iterated_value)
    if iterated_value == 1:
        iterated_values.pop()
assert iterated_result == [1, 2]

unpacked_left, unpacked_right = "ab"
assert unpacked_left == "a" and unpacked_right == "b"


def unpack_values():
    yield 3
    yield 4


unpacked_left, unpacked_right = unpack_values()
assert unpacked_left == 3 and unpacked_right == 4

try:
    unpacked_left, unpacked_right = [1]
except ValueError:
    pass
else:
    raise AssertionError("short unpack did not raise ValueError")

try:
    unpacked_left, unpacked_right = [1, 2, 3]
except ValueError:
    pass
else:
    raise AssertionError("long unpack did not raise ValueError")

keyed = [(2, "b"), (1, "c"), (1, "a")]
keyed.sort(key=lambda pair: pair[0])
assert keyed == [(1, "c"), (1, "a"), (2, "b")]
keyed.sort(key=lambda pair: pair[1], reverse=True)
assert keyed == [(1, "c"), (2, "b"), (1, "a")]

singleton_key_calls = []

def singleton_key(pair):
    singleton_key_calls.append(pair)
    return pair[0]

singleton = [(1, "only")]
singleton.sort(key=singleton_key)
assert singleton == [(1, "only")]
assert singleton_key_calls == [(1, "only")]
assert sorted(singleton, key=lambda pair: pair[1]) == [(1, "only")]

empty_key_calls = []
empty_keyed = []
empty_keyed.sort(key=lambda value: empty_key_calls.append(value))
assert empty_keyed == [] and empty_key_calls == []

try:
    singleton.sort(None, singleton_key, key=singleton_key)
except TypeError:
    pass
else:
    raise AssertionError("duplicate sort key argument was accepted")

try:
    singleton.sort(None, None, False, reverse=True)
except TypeError:
    pass
else:
    raise AssertionError("duplicate sort reverse argument was accepted")


def long_comparison(left, right):
    return 0L


try:
    [2, 1].sort(long_comparison)
except TypeError:
    pass
else:
    raise AssertionError("sort comparison accepted a long result")

key_release_events = []


class SortReleaseKey(object):
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return self.value < other.value

    def __del__(self):
        key_release_events.append(self.value)


release_order_values = [3, 1, 2]
release_order_values.sort(key=lambda value: SortReleaseKey(value))
assert release_order_values == [1, 2, 3]
assert key_release_events == [1, 2, 3]


class CallableSortKey(object):
    def __call__(self, value):
        return -value


callable_key_values = [1, 3, 2]
callable_key_values.sort(key=CallableSortKey())
assert callable_key_values == [3, 2, 1]

key_mutated = [3, 2, 1]


def mutating_key(value):
    key_mutated.append(99)
    return value


try:
    key_mutated.sort(key=mutating_key)
except ValueError:
    pass
else:
    raise AssertionError("key mutation during sort was not detected")
assert key_mutated == [1, 2, 3]

cmp_mutated = [3, 2, 1]


def mutating_cmp(left, right):
    cmp_mutated.append(99)
    return cmp(left, right)


try:
    cmp_mutated.sort(cmp=mutating_cmp)
except ValueError:
    pass
else:
    raise AssertionError("comparison mutation during sort was not detected")
assert cmp_mutated == [1, 2, 3]

iterated_during_sort = [3, 2, 1]
sort_iterator = iter(iterated_during_sort)
assert next(sort_iterator) == 3


def iterating_key(value):
    try:
        next(sort_iterator)
    except StopIteration:
        pass
    return value


iterated_during_sort.sort(key=iterating_key)
assert iterated_during_sort == [1, 2, 3]

already_sorted = range(2048)
already_sorted.sort()
assert already_sorted[0] == 0 and already_sorted[-1] == 2047

mapping = {"a": 1, "b": 2}
assert mapping.__len__() == 2
assert mapping.__getitem__("a") == 1
assert sorted(list(mapping.__iter__())) == ["a", "b"]
assert mapping.__repr__() == repr(mapping)
assert mapping.get("a") == 1
assert mapping.get("missing") is None
assert mapping.get("missing", 7) == 7
assert mapping.has_key("b") is True
assert sorted(mapping.keys()) == ["a", "b"]
assert sorted(mapping.values()) == [1, 2]
assert sorted(mapping.items()) == [("a", 1), ("b", 2)]
assert sorted(list(mapping.iterkeys())) == ["a", "b"]
assert sorted(list(mapping.itervalues())) == [1, 2]
assert sorted(list(mapping.iteritems())) == [("a", 1), ("b", 2)]
assert mapping.setdefault("a", 9) == 1
assert mapping.setdefault("c", 3) == 3
mapping.update({"d": 4}, e=5)
mapping.update((("f", 6),))
mapping.update(["g7"])
assert mapping["g"] == "7"


def update_pair():
    yield "h"
    yield 8


mapping.update([update_pair()])
assert mapping["h"] == 8


class UpdateMapping(object):
    def keys(self):
        return ["i"]

    def __getitem__(self, key):
        return 9


mapping.update(UpdateMapping())
assert mapping["i"] == 9


def partially_invalid_update():
    yield ("partial", 10)
    yield ("invalid",)


try:
    mapping.update(partially_invalid_update())
except ValueError:
    pass
else:
    raise AssertionError("invalid update pair was accepted")
assert mapping["partial"] == 10
assert mapping.pop("d") == 4
assert mapping.pop("missing", 8) == 8
copy = mapping.copy()
assert copy == mapping
item = copy.popitem()
assert item[0] not in copy
copy.clear()
assert copy == {}
assert dict.fromkeys(["a", "b"]) == {"a": None, "b": None}
assert dict.fromkeys(("a", "b"), 7) == {"a": 7, "b": 7}

# Python 2 dict ordering compares size, then the smallest differing key/value.
assert cmp({1: 2}, {1: 3}) == -1
assert cmp({1: 2}, {2: 0}) == -1
assert cmp({1: 2}, {1: 2}) == 0
assert cmp({1: 2}, {1: 2, 2: 3}) == -1
assert {1: 2} < {1: 3}


class CountingHashKey(object):
    calls = 0

    def __hash__(self):
        CountingHashKey.calls += 1
        return 1

    def __eq__(self, other):
        return True


counting_key = CountingHashKey()
counting_mapping = {counting_key: 42}
CountingHashKey.calls = 0
assert counting_mapping.pop(CountingHashKey()) == 42
assert len(counting_mapping) == 0 and CountingHashKey.calls == 1
counting_mapping = {counting_key: 42}
CountingHashKey.calls = 0
assert counting_mapping.popitem()[1] == 42
assert len(counting_mapping) == 0 and CountingHashKey.calls == 0
counting_set = set([counting_key])
CountingHashKey.calls = 0
counting_set.discard(CountingHashKey())
assert len(counting_set) == 0 and CountingHashKey.calls == 1
counting_set = set([counting_key])
CountingHashKey.calls = 0
assert counting_set.pop() is counting_key
assert len(counting_set) == 0 and CountingHashKey.calls == 0

clear_mapping = {}


class ClearValue(object):
    def __init__(self, number):
        self.number = number

    def __del__(self):
        if self.number == 0:
            for clear_insert_index in xrange(64):
                clear_mapping["new" + str(clear_insert_index)] = clear_insert_index


for clear_index in xrange(32):
    clear_mapping[clear_index] = ClearValue(clear_index)
clear_mapping.clear()
assert len(clear_mapping) == 64
assert clear_mapping.get("new63") == 63
assert len(clear_mapping.items()) == 64

assert list(iter(u"a\u20ac")) == [u"a", u"\u20ac"]
unicode_iteration = list(u"\u20ac" * 4096)
assert len(unicode_iteration) == 4096
assert unicode_iteration[0] == u"\u20ac" and unicode_iteration[-1] == u"\u20ac"
assert type(u"Contract" + "_Cooldown") is unicode
assert u"Contract" + "_Cooldown" == u"Contract_Cooldown"
assert "Contract" + u"_Cooldown" == u"Contract_Cooldown"

# Python 2 orders None before every other value and numbers before
# non-numeric values when the types do not otherwise define an ordering.
assert None < 0
assert 0 < {}
assert not ({} < 0)

assert all([True, 1]) is True
assert all([True, 0]) is False
assert any([0, 2]) is True
assert any([0, False]) is False
assert list(enumerate(["a", "b"], 3)) == [(3, "a"), (4, "b")]
assert list(enumerate(sequence=["a", "b"], start=9223372036854775807L)) == [
    (9223372036854775807L, "a"),
    (9223372036854775808L, "b"),
]
assert filter(lambda value: value % 2, [1, 2, 3]) == [1, 3]
assert filter(None, (0, 1, 2)) == (1, 2)
assert filter(lambda value: value != "b", "abc") == "ac"
assert filter(None, u"abc") == u"abc"
assert filter(None, "\x00") == "\x00"
assert filter(lambda value: True, ["", ""]) == ["", ""]
assert dict.fromkeys(xrange(1000), 1)[999] == 1


class NonBooleanComparison(object):
    def __eq__(self, other):
        return 7

    def __lt__(self, other):
        return 8

    def __divmod__(self, other):
        return "custom-divmod"


assert (NonBooleanComparison() == 1) == 7
assert (NonBooleanComparison() < 1) == 8
assert divmod(NonBooleanComparison(), 1) == "custom-divmod"
minimum_integer = -9223372036854775807 - 1
minimum_quotient, minimum_remainder = divmod(minimum_integer, -1)
assert minimum_quotient == 9223372036854775808L and type(minimum_quotient) is long
assert minimum_remainder == 0L and type(minimum_remainder) is long
assert type(minimum_integer % -1) is long
assert 1 // 1.5 == 0.0
assert repr(0.0 % -1.5) == "-0.0"
assert (1 + 2j) // 1 == 1 + 0j
assert (1 + 2j) % 1 == 2j
assert divmod(1 + 2j, 1) == (1 + 0j, 2j)
assert +(True) == 1 and type(+(True)) is int
assert +(False) == 0 and type(+(False)) is int
assert (1 + 2j) ** 10 == 237 - 3116j
assert (1 + 2j) ** -2 == (-0.12 - 0.16j)
indeterminate_complex_power = (1 + 2j) ** (1e300 + 1e300j)
assert indeterminate_complex_power != indeterminate_complex_power
assert repr(complex("-0-0j") ** 1) == "-0j"
try:
    complex("1e-300-0j") ** -2
except ZeroDivisionError:
    pass
else:
    raise AssertionError("underflowed negative complex power did not fail")
assert (1 + 0j) % 1e-300 == 1.1102230246251565e-16 + 0j
assert divmod(1 + 2j, 1e-20) == (1e20 + 0j, 2j)
assert repr((-0.0) % (-2j)) == "(-0+0j)"
nan_power = float("nan") ** 2.0
assert nan_power != nan_power
assert 0.0 ** -float("inf") == float("inf")
assert (-float("inf")) ** 0.5 == float("inf")
assert map(lambda value: value + 1, [1, 2]) == [2, 3]
assert map(None, [1, 2], [3]) == [(1, 3), (2, None)]
assert zip([1, 2], [3, 4, 5]) == [(1, 3), (2, 4)]
assert sum([1, 2, 3]) == 6
assert sum(xrange(10000)) == 49995000
assert type(sum([9223372036854775807, 1])) is long
assert sum([9223372036854775807, 1]) == 9223372036854775808L
assert sum([], True) is True


class ReflectedSumValue(object):
    def __radd__(self, other):
        return "reflected sum"


assert sum([ReflectedSumValue()]) == "reflected sum"
assert min(3, 1, 2) == 1
assert max([1, 4, 2]) == 4
assert max(["a", "bbb", "cc"], key=len) == "bbb"

builtin_events = []


def filter_source():
    for value in xrange(3):
        builtin_events.append("g" + str(value))
        yield value


def filter_predicate(value):
    builtin_events.append("p" + str(value))
    return True


assert filter(filter_predicate, filter_source()) == [0, 1, 2]
assert builtin_events == ["g0", "p0", "g1", "p1", "g2", "p2"]

builtin_events = []


def map_source(prefix, count):
    for value in xrange(count):
        builtin_events.append(prefix + str(value))
        yield value


assert map(None, map_source("a", 2), map_source("b", 3)) == [(0, 0), (1, 1), (None, 2)]
assert builtin_events == ["a0", "b0", "a1", "b1", "b2"]


class BoundBuiltinCallbacks(object):
    def increment(self, value):
        return value + 1

    def odd(self, value):
        return value % 2

    def negative(self, value):
        return -value


bound_builtin_callbacks = BoundBuiltinCallbacks()
assert map(bound_builtin_callbacks.increment, xrange(3)) == [1, 2, 3]
assert filter(bound_builtin_callbacks.odd, xrange(5)) == [1, 3]
assert min([1, 3, 2], key=bound_builtin_callbacks.negative) == 3

builtin_events = []


def minimum_source():
    for value in [3, 1, 2]:
        builtin_events.append("g" + str(value))
        yield value


def minimum_key(value):
    builtin_events.append("k" + str(value))
    return value


assert min(minimum_source(), key=minimum_key) == 1
assert builtin_events == ["g3", "k3", "g1", "k1", "g2", "k2"]

try:
    sum(["a"], "")
except TypeError:
    pass
else:
    raise AssertionError("sum accepted a string start value")

sum_events = []


class SumIteratedBeforeStartValidation(object):
    def __iter__(self):
        sum_events.append("iter")
        return iter(())


try:
    sum(SumIteratedBeforeStartValidation(), "")
except TypeError:
    pass
else:
    raise AssertionError("sum accepted a string start value")
assert sum_events == ["iter"]

assert list(reversed([1, 2, 3])) == [3, 2, 1]
assert type(enumerate) is type
assert isinstance(enumerate([]), enumerate)
assert type(reversed) is type


class EnumerateSubtype(enumerate):
    pass


enumerate_subtype = EnumerateSubtype(["value"])
enumerate_subtype.marker = 7
assert type(enumerate_subtype) is EnumerateSubtype
assert enumerate_subtype.next() == (0, "value")
assert enumerate_subtype.marker == 7


class ReversedSubtype(reversed):
    pass


class ReversedSubtypeSequence(object):
    def __len__(self):
        return 1

    def __getitem__(self, index):
        if index == 0:
            return "value"
        raise IndexError


reversed_subtype = ReversedSubtype(ReversedSubtypeSequence())
reversed_subtype.marker = 8
assert type(reversed_subtype) is ReversedSubtype
assert reversed_subtype.next() == "value"
assert reversed_subtype.marker == 8


class ReverseProtocol(object):
    def __reversed__(self):
        return iter((4, 3, 2))


assert list(reversed(ReverseProtocol())) == [4, 3, 2]


class ReverseSequence(object):
    def __init__(self):
        self.items = [1, 2, 3]

    def __len__(self):
        return len(self.items)

    def __getitem__(self, index):
        return self.items[index]


assert list(reversed(ReverseSequence())) == [3, 2, 1]


class StopIterationSequence(object):
    def __getitem__(self, index):
        if index == 0:
            return 0
        if index == 1:
            raise StopIteration
        return index


stop_iteration_sequence = iter(StopIterationSequence())
assert next(stop_iteration_sequence) == 0
for exhausted_attempt in xrange(2):
    try:
        next(stop_iteration_sequence)
    except StopIteration:
        pass
    else:
        raise AssertionError("sequence iterator resumed after StopIteration")


class StopIterationReverseSequence(object):
    def __len__(self):
        return 3

    def __getitem__(self, index):
        if index == 2:
            return 2
        if index == 1:
            raise StopIteration
        return index


assert list(reversed(StopIterationReverseSequence())) == [2]
shrinking_reverse_source = [1, 2, 3]
shrinking_reverse = reversed(shrinking_reverse_source)
shrinking_reverse_source.pop()
assert list(shrinking_reverse) == []
assert list(reversed(xrange(-9223372036854775808L, 9223372036854775807L, 9223372036854775807L))) == [
    9223372036854775806,
    -1,
    -9223372036854775808L,
]
extreme_range = xrange(-9223372036854775808L, 9223372036854775807L, 9223372036854775807L)
assert extreme_range[2] == 9223372036854775806L
assert repr(extreme_range) == "xrange(-9223372036854775808, 9223372036854775807, 9223372036854775807)"
try:
    xrange(-9223372036854775808L, 9223372036854775807L)
except OverflowError:
    pass
else:
    raise AssertionError("xrange accepted a length larger than Py_ssize_t")

call_values = [1, 2, 3, 4]


def next_call_value():
    return call_values.pop(0)


assert list(iter(next_call_value, 4)) == [1, 2, 3]
stop_call_count = [0]


def stop_call_value():
    stop_call_count[0] += 1
    if stop_call_count[0] == 1:
        raise StopIteration
    return 1


stop_call_iterator = iter(stop_call_value, 2)
for exhausted_attempt in xrange(2):
    try:
        next(stop_call_iterator)
    except StopIteration:
        pass
    else:
        raise AssertionError("call iterator resumed after StopIteration")
assert stop_call_count == [1]
assert chr(65) == "A"
assert unichr(0x20ac) == u"\u20ac"
surrogate_character = unichr(0xd800)
assert len(surrogate_character) == 1 and ord(surrogate_character) == 0xd800
assert repr(surrogate_character) == "u'\\ud800'"
assert surrogate_character.encode("utf-8") == "\xed\xa0\x80"
assert cmp(1, 2) == -1 and cmp(2, 2) == 0 and cmp(3, 2) == 1
assert hash("value") == hash("value")
assert pow(2, 5) == 32
assert pow(14, 5, 200) == 24
assert repr(pow(2L, 1000L, 97L)) == "36L"
promoted_modular_power = pow(3037000500, 2, 535)
assert promoted_modular_power == 355L and type(promoted_modular_power) is long
assert round(1.25, 1) == 1.3
assert round(-1.25, 1) == -1.3
assert round(2.675, 2) == 2.67
assert round(1250.0, -2) == 1300.0
assert round(1e308, 308) == 1e308
assert round(number=1.25, ndigits=1) == 1.3
assert round(1.25, 1L << 100) == 1.25
assert round(1.25, -(1L << 100)) == 0.0


class RoundFloat(object):
    def __float__(self):
        return 1.25


round_float = RoundFloat()
round_float.__float__ = lambda: 9.75
assert round(round_float, 1) == 1.3
assert apply(lambda: 7) == 7
assert apply(lambda *values: values, [1, 2]) == (1, 2)
assert apply(lambda value=0: value, (), {"value": 42}) == 42
try:
    apply(lambda *values: values, iter([1, 2]))
except TypeError:
    pass
else:
    raise AssertionError("apply accepted a non-sequence iterator")
try:
    apply(lambda: None, (), [])
except TypeError:
    pass
else:
    raise AssertionError("apply accepted non-dictionary keywords")
assert coerce(True, 2) == (True, 2)
assert coerce(1, 2L) == (1L, 2L)
assert coerce(1L, 2.5) == (1.0, 2.5)
assert coerce(1, 2j) == (1 + 0j, 2j)
assert coerce([], []) == ([], [])


class LeftCoercion(object):
    def __coerce__(self, other):
        return "left", other


class RightCoercion(object):
    def __coerce__(self, other):
        return "right", other


assert coerce(LeftCoercion(), 3) == ("left", 3)
assert coerce(3, RightCoercion()) == (3, "right")
try:
    coerce("a", "b")
except TypeError:
    pass
else:
    raise AssertionError("coerce accepted incompatible values")
assert globals() is locals()
assert list(xrange(1, 5, 2)) == [1, 3]
range_value = xrange(2, 10, 2)
assert type(range_value).__name__ == "xrange"
assert len(range_value) == 4 and bool(range_value)
assert range_value[-1] == 8
assert repr(range_value) == "xrange(2, 10, 2)"
assert range_value.__len__() == 4
assert range_value.__getitem__(-1) == 8
assert list(range_value.__iter__()) == [2, 4, 6, 8]
assert list(range_value.__reversed__()) == [8, 6, 4, 2]
assert range_value.__repr__() == repr(range_value)
assert range_value.__str__() == str(range_value)
try:
    xrange.__len__(1)
except TypeError:
    pass
else:
    raise AssertionError("xrange method accepted a non-xrange object")
assert list(range_value) == [2, 4, 6, 8]
assert list(range_value) == [2, 4, 6, 8]
enumerated = enumerate(["a", "b"], 3)
assert type(enumerated).__name__ == "enumerate"
assert enumerated.__repr__() == repr(enumerated)
assert enumerated.__str__() == str(enumerated)
assert enumerated.next() == (3, "a")
assert list(enumerated) == [(4, "b")]
reverse_iterator = reversed([1, 2, 3])
assert reverse_iterator.__repr__() == repr(reverse_iterator)
assert reverse_iterator.__str__() == str(reverse_iterator)
assert reverse_iterator.__length_hint__() == 3
assert reverse_iterator.next() == 3
assert reverse_iterator.__length_hint__() == 2
assert list(reverse_iterator) == [2, 1]
assert reverse_iterator.__length_hint__() == 0

forward_iterator = iter([1, 2, 3])
assert forward_iterator.__length_hint__() == 3
assert next(forward_iterator) == 1
assert forward_iterator.__length_hint__() == 2
assert list(forward_iterator) == [2, 3]
assert forward_iterator.__length_hint__() == 0

shrinking_length_source = [1, 2, 3]
shrinking_length_iterator = iter(shrinking_length_source)
assert next(shrinking_length_iterator) == 1
del shrinking_length_source[:]
assert shrinking_length_iterator.__length_hint__() == 0

from _weakref import ref


class IteratorLifetimeTarget(object):
    pass


held_value = IteratorLifetimeTarget()
held_value_reference = ref(held_value)
held_value_iterator = iter([held_value])
del held_value
list(held_value_iterator)
assert held_value_reference() is None


class FallbackIteratorSource(object):
    def __getitem__(self, index):
        if index == 0:
            return 1
        raise IndexError


fallback_source = FallbackIteratorSource()
fallback_source_reference = ref(fallback_source)
fallback_iterator = iter(fallback_source)
del fallback_source
assert list(fallback_iterator) == [1]
assert fallback_source_reference() is None

reversed_source = IteratorLifetimeTarget()
reversed_source.items = [1]
reversed_source.__class__.__len__ = lambda self: len(self.items)
reversed_source.__class__.__getitem__ = lambda self, index: self.items[index]
reversed_source_reference = ref(reversed_source)
reversed_iterator_lifetime = reversed(reversed_source)
del reversed_source
assert list(reversed_iterator_lifetime) == [1]
assert reversed_source_reference() is None


class CallableIteratorSource(object):
    def __init__(self, sentinel):
        self.sentinel = sentinel

    def __call__(self):
        return self.sentinel


callable_sentinel = IteratorLifetimeTarget()
callable_source = CallableIteratorSource(callable_sentinel)
callable_sentinel_reference = ref(callable_sentinel)
callable_source_reference = ref(callable_source)
callable_iterator_lifetime = iter(callable_source, callable_sentinel)
del callable_sentinel
del callable_source
assert list(callable_iterator_lifetime) == []
assert callable_source_reference() is None
assert callable_sentinel_reference() is None


class LongLength(object):
    def __len__(self):
        return 3L


class NegativeLength(object):
    def __len__(self):
        return -1


assert len(LongLength()) == 3
assert type(len(LongLength())) is int
try:
    len(NegativeLength())
except ValueError:
    pass
else:
    raise AssertionError("negative __len__ result was accepted")


class Introspection(object):
    marker = 9

    def method(self, value=3):
        return value


instance = Introspection()
assert instance.__class__ is Introspection
assert instance.__dict__ == {}
instance.value = 42
assert instance.__dict__["value"] == 42
assert Introspection.__name__ == "Introspection"
assert Introspection.__bases__ == (object,)
assert Introspection.__mro__[0] is Introspection
assert str.__mro__ == (str, basestring, object)
assert str.__bases__ == (basestring,)
assert object.__mro__ == (object,)
assert object.__bases__ == ()
assert Introspection.__dict__["marker"] == 9
assert Introspection.method.im_func.func_name == "method"
assert Introspection.method.im_func.func_defaults == (3,)
assert Introspection.method.im_func.func_code.co_name == "method"
assert instance.method.im_self is instance
assert instance.method.im_class is Introspection
assert instance.method.func_code.co_name == "method"
assert instance.method.func_defaults == (3,)
assert not isinstance(append, type(instance.method))


class DynamicAttribute(object):
    def __getattr__(self, name):
        return "missing:" + name


assert DynamicAttribute().answer == "missing:answer"
assert repr(NotImplemented) == "NotImplemented"
assert isinstance("text", basestring)
assert isinstance(u"text", basestring)
assert bytes is str
plain_object = object()
assert plain_object.__repr__() == repr(plain_object)
assert plain_object.__str__() == str(plain_object)
assert slice(None).__str__() == str(slice(None))
assert set([1]).__str__() == str(set([1]))
assert frozenset([1]).__str__() == str(frozenset([1]))
assert "value" in dir(instance)
assert "method" in dir(Introspection)
assert "bit_length" in dir(bool)
assert "Introspection" in dir()
for dir_object_index in xrange(1000):
    assert "__repr__" in dir(object())


class ProtocolObject(object):
    def __init__(self):
        self.data = {}

    def __call__(self, value):
        return value + 1

    def __getitem__(self, key):
        return self.data.get(key, -1)

    def __setitem__(self, key, value):
        self.data[key] = value

    def __delitem__(self, key):
        del self.data[key]


protocol = ProtocolObject()
assert callable(protocol)
assert protocol(41) == 42
protocol["answer"] = 42
assert protocol["answer"] == 42
del protocol["answer"]
assert protocol["answer"] == -1
assert [1, 2] + [3] == [1, 2, 3]
assert (1,) + (2,) == (1, 2)
assert "ab" * 2 == "abab"
assert 2 * [1, 2] == [1, 2, 1, 2]


class NumberProtocol(object):
    def __init__(self, value):
        self.value = value

    def __add__(self, other):
        return self.value + other

    def __radd__(self, other):
        return other + self.value

    def __sub__(self, other):
        return self.value - other

    def __mul__(self, other):
        return self.value * other

    def __div__(self, other):
        return self.value / other

    def __floordiv__(self, other):
        return self.value // other

    def __mod__(self, other):
        return self.value % other

    def __pow__(self, other):
        return self.value ** other

    def __and__(self, other):
        return self.value & other

    def __or__(self, other):
        return self.value | other

    def __xor__(self, other):
        return self.value ^ other

    def __lshift__(self, other):
        return self.value << other


number_protocol = NumberProtocol(40)
assert number_protocol + 2 == 42
assert 2 + number_protocol == 42
assert number_protocol - 2 == 38
assert number_protocol * 2 == 80
assert number_protocol / 2 == 20
assert number_protocol // 3 == 13
assert number_protocol % 7 == 5
assert NumberProtocol(2) ** 5 == 32
assert NumberProtocol(6) & 3 == 2
assert NumberProtocol(6) | 1 == 7
assert NumberProtocol(6) ^ 3 == 5
assert NumberProtocol(3) << 2 == 12


class Comparable(object):
    def __init__(self, value):
        self.value = value

    def __eq__(self, other):
        return self.value == other.value

    def __lt__(self, other):
        return self.value < other.value


assert Comparable(1) == Comparable(1)
assert Comparable(1) != Comparable(2)
assert Comparable(1) < Comparable(2)
assert sorted([Comparable(2), Comparable(1)])[0].value == 1


class ContainsProtocol(object):
    def __contains__(self, value):
        return value == 42


class HashProtocol(object):
    def __hash__(self):
        return 12345


class HashKey(object):
    def __init__(self, value):
        self.value = value

    def __hash__(self):
        return 7

    def __eq__(self, other):
        return self.value == other.value


class HashNone(object):
    __hash__ = None


class HashableList(list):
    def __hash__(self):
        return 17


class HashableDict(dict):
    def __hash__(self):
        return 18


class HashableSet(set):
    def __hash__(self):
        return 19


class HashableTuple(tuple):
    def __hash__(self):
        return 23


class HashableInt(int):
    def __hash__(self):
        return 24


class InheritedHashableList(HashableList):
    pass


class UnhashableTuple(tuple):
    __hash__ = None


class UnhashableInt(int):
    __hash__ = None


class HashFailure(Exception):
    pass


class RaisingHash(object):
    calls = 0

    def __hash__(self):
        RaisingHash.calls += 1
        raise HashFailure("hash")


class RaisingEquality(object):
    def __hash__(self):
        return 1

    def __eq__(self, other):
        raise HashFailure("equality")


assert 42 in ContainsProtocol()
assert 7 not in ContainsProtocol()
assert hash(HashProtocol()) == 12345
assert [hash(value) for value in (HashableList(), HashableDict(), HashableSet(), HashableTuple(), HashableInt(), InheritedHashableList())] == [17, 18, 19, 23, 24, 17]
hash_mapping = {HashKey("a"): 1, HashKey("b"): 2}
assert hash_mapping[HashKey("a")] == 1
assert HashKey("b") in hash_mapping
assert HashKey("a") in set([HashKey("a")])
for unhashable in ([], {}, set(), bytearray(), ([],), HashNone(), UnhashableTuple(), UnhashableInt()):
    try:
        hash(unhashable)
    except TypeError:
        pass
    else:
        raise AssertionError("mutable value accepted as a hash key")
RaisingHash.calls = 0
assert {}.pop(RaisingHash(), 7) == 7
assert RaisingHash.calls == 0
try:
    {}[RaisingHash()] = 1
except HashFailure:
    pass
else:
    raise AssertionError("__hash__ exception was swallowed")
collision_mapping = {RaisingEquality(): 1}
try:
    RaisingEquality() in collision_mapping
except HashFailure:
    pass
else:
    raise AssertionError("__eq__ exception was swallowed")

for equality_pair in (([RaisingEquality()], [RaisingEquality()]), ((RaisingEquality(),), (RaisingEquality(),)), ({"value": RaisingEquality()}, {"value": RaisingEquality()})):
    try:
        equality_pair[0] == equality_pair[1]
    except HashFailure:
        pass
    else:
        raise AssertionError("nested __eq__ exception was swallowed")

try:
    RaisingEquality() in [RaisingEquality()]
except HashFailure:
    pass
else:
    raise AssertionError("list containment swallowed __eq__ exception")


class ListMutatingEquality(object):
    def __init__(self, values, result):
        self.values = values
        self.result = result

    def __eq__(self, other):
        del self.values[:]
        return self.result


mutating_remove_values = []
mutating_remove_values.append(ListMutatingEquality(mutating_remove_values, True))
mutating_remove_values.append(2)
assert mutating_remove_values.remove(1) is None
assert mutating_remove_values == []
mutating_count_values = []
mutating_count_values.append(ListMutatingEquality(mutating_count_values, False))
mutating_count_values.append(2)
assert mutating_count_values.count(1) == 0
assert mutating_count_values == []
mutating_index_values = []
mutating_index_values.append(ListMutatingEquality(mutating_index_values, False))
mutating_index_values.append(2)
try:
    mutating_index_values.index(1)
except ValueError:
    pass
else:
    raise AssertionError("list.index ignored reentrant clearing")
assert mutating_index_values == []
mutating_equality_left = []
mutating_equality_left.append(ListMutatingEquality(mutating_equality_left, True))
assert (mutating_equality_left == [1]) is False
assert mutating_equality_left == []
mutating_contains_values = []
mutating_contains_values.append(ListMutatingEquality(mutating_contains_values, False))
mutating_contains_values.append(2)
assert (1 in mutating_contains_values) is False
assert mutating_contains_values == []

raising_set_left = set([RaisingEquality()])
raising_set_right = set([RaisingEquality()])
for set_operation in xrange(4):
    try:
        if set_operation == 0:
            raising_set_left & raising_set_right
        elif set_operation == 1:
            raising_set_left.issubset(raising_set_right)
        elif set_operation == 2:
            raising_set_left.isdisjoint(raising_set_right)
        else:
            raising_set_copy = raising_set_left.copy()
            raising_set_copy.difference_update(raising_set_right)
    except HashFailure:
        pass
    else:
        raise AssertionError("set operation swallowed __eq__ exception")

reentrant_mapping = {}


class ReentrantKey(object):
    def __hash__(self):
        return 2

    def __eq__(self, other):
        reentrant_mapping.clear()
        return False


reentrant_mapping[ReentrantKey()] = 1
assert ReentrantKey() not in reentrant_mapping
assert reentrant_mapping == {}

value_mutation_mapping = {}


class ValueMutatingKey(object):
    def __init__(self, value):
        self.value = value

    def __hash__(self):
        return 3

    def __eq__(self, other):
        value_mutation_mapping["side"] += 1
        return self.value == other.value


stored_mutating_key = ValueMutatingKey("stored")
value_mutation_mapping[stored_mutating_key] = 42
value_mutation_mapping["side"] = 0
assert value_mutation_mapping[ValueMutatingKey("stored")] == 42
assert value_mutation_mapping["side"] == 1

structural_mutation_mapping = {}


class StructuralMutatingKey(object):
    def __init__(self, mutate):
        self.mutate = mutate

    def __hash__(self):
        return 4

    def __eq__(self, other):
        if self.mutate:
            structural_mutation_mapping["side" + str(len(structural_mutation_mapping))] = 1
        return True


structural_mutation_mapping[StructuralMutatingKey(True)] = 42
assert structural_mutation_mapping[StructuralMutatingKey(False)] == 42
assert len(structural_mutation_mapping) == 2

assert "{} {}".format("a", 2) == "a 2"
assert "{name!r}".format(name="x") == "'x'"
assert "{0:04x}".format(15) == "000f"
assert "{{{}}}".format(1) == "{1}"
assert "x".center(5, "-") == "--x--"
assert "x".ljust(3, "-") == "x--"
assert "x".rjust(3, "-") == "--x"
assert ",".join(["a", "b"]) == "a,b"
assert "banana".find("na") == 2
assert "banana".rfind("na") == 4
assert "banana".index("na", 3) == 4
assert "banana".rindex("na", 0, 5) == 2
assert "banana".count("na") == 2
assert "prefix-value".startswith(("other", "prefix"))
assert "prefix-value".endswith("value", 1)
assert "  value\t".strip() == "value"
assert "xyvalueyx".strip("xy") == "value"
assert "abcabc".replace("ab", "X", 1) == "Xcabc"
assert "ab".replace("", "-", 2) == "-a-b"
assert "a,b,,c".split(",") == ["a", "b", "", "c"]
assert "a,b,c".split(",", 1) == ["a", "b,c"]
assert "a,b,c".rsplit(",", 1) == ["a,b", "c"]
assert "  a  b \t c  ".split() == ["a", "b", "c"]
assert "  a  b \t c  ".rsplit(None, 1) == ["  a  b", "c"]
assert "AbC".lower() == "abc"
assert "AbC".upper() == "ABC"
assert "AbC".swapcase() == "aBc"
assert "hELLO".capitalize() == "Hello"
assert "hello WORLD".title() == "Hello World"
assert "abc".isalpha() and not "ab1".isalpha()
assert "123".isdigit() and not "".isdigit()
assert "abc123".isalnum() and not "abc-123".isalnum()
assert " \t\r\n".isspace() and not " x ".isspace()
assert "abc1".islower() and not "Abc".islower()
assert "ABC1".isupper() and not "ABc".isupper()
assert "Hello World".istitle() and not "Hello world".istitle()
assert "-42".zfill(5) == "-0042"
assert u"a\u20acb".find(u"\u20ac") == 1


class OverriddenInt(int):
    def __add__(self, other):
        return "int-add"

    def __radd__(self, other):
        return "int-radd"

    def __mul__(self, other):
        return "int-mul"

    def __nonzero__(self):
        return False

    def __abs__(self):
        return "int-abs"

    def __neg__(self):
        return "int-neg"

    def __repr__(self):
        return "int-repr"


class OverriddenFloat(float):
    def __add__(self, other):
        return "float-add"


class OverriddenComplex(complex):
    def __add__(self, other):
        return "complex-add"


class OverriddenString(str):
    def __add__(self, other):
        return "string-add"

    def __mul__(self, other):
        return "string-mul"

    def __getitem__(self, key):
        return "string-item"

    def __len__(self):
        return 17


class OverriddenTuple(tuple):
    def __add__(self, other):
        return "tuple-add"

    def __getitem__(self, key):
        return "tuple-item"


class OverriddenList(list):
    def __add__(self, other):
        return "list-add"

    def __getitem__(self, key):
        return "list-item"

    def __setitem__(self, key, value):
        self.assigned = (key, value)

    def __delitem__(self, key):
        self.deleted = key

    def __len__(self):
        return 19

    def __iter__(self):
        return iter(("list-iter",))

    def __contains__(self, item):
        return item == "list-contains"

    def __repr__(self):
        return "list-repr"


class OverriddenDict(dict):
    def __getitem__(self, key):
        return "dict-item"

    def __len__(self):
        return 23

    def __iter__(self):
        return iter(("dict-iter",))

    def __contains__(self, item):
        return item == "dict-contains"


class NotImplementedList(list):
    def __init__(self):
        self.add_calls = 0
        self.radd_calls = 0

    def __add__(self, other):
        self.add_calls += 1
        return NotImplemented

    def __radd__(self, other):
        self.radd_calls += 1
        return NotImplemented


overridden_int = OverriddenInt(2)
assert overridden_int + 1 == "int-add"
assert 1 + overridden_int == "int-radd"
assert overridden_int * 2 == "int-mul"
assert bool(overridden_int) is False
assert abs(overridden_int) == "int-abs"
assert OverriddenFloat(1.5) + 1 == "float-add"
assert OverriddenComplex(1 + 2j) + 1 == "complex-add"
overridden_string = OverriddenString("abc")
assert overridden_string + "x" == "string-add"
assert overridden_string * 2 == "string-mul"
assert overridden_string[0] == "string-item"
assert len(overridden_string) == 17
overridden_tuple = OverriddenTuple((1, 2))
assert overridden_tuple + (3,) == "tuple-add"
assert overridden_tuple[0] == "tuple-item"
overridden_list = OverriddenList((1, 2))
assert overridden_list + [3] == "list-add"
assert overridden_list[0] == "list-item"
overridden_list[0] = 7
assert overridden_list.assigned == (0, 7)
del overridden_list[0]
assert overridden_list.deleted == 0
assert len(overridden_list) == 19
assert list(iter(overridden_list)) == ["list-iter"]
assert "list-contains" in overridden_list
overridden_dict = OverriddenDict(answer=42)
assert overridden_dict["answer"] == "dict-item"
assert len(overridden_dict) == 23
assert list(iter(overridden_dict)) == ["dict-iter"]
assert "dict-contains" in overridden_dict
not_implemented_list = NotImplementedList()
try:
    not_implemented_list + 1
    assert False
except TypeError:
    pass
assert not_implemented_list.add_calls == 1
try:
    1 + not_implemented_list
    assert False
except TypeError:
    pass
assert not_implemented_list.radd_calls == 1

# Explicit builtin descriptors must operate on the builtin payload without
# dispatching back into overrides on a subclass.
assert type(list.append).__name__ == "method_descriptor"
assert list.append.__name__ == "append"
assert list.append.__objclass__ is list
assert repr(list.append) == "<method 'append' of 'list' objects>"
assert type(list.__len__).__name__ == "wrapper_descriptor"
assert list.__len__.__objclass__ is list
assert repr(list.__len__) == "<slot wrapper '__len__' of 'list' objects>"
assert type(set.add).__name__ == "method_descriptor"
assert set.add.__objclass__ is set
assert type(str.upper).__name__ == "method_descriptor"
assert str.upper.__objclass__ is str
assert type(bytearray.append).__name__ == "method_descriptor"
assert bytearray.append.__objclass__ is bytearray
descriptor_target = []
descriptor_append = list.append.__get__(descriptor_target, list)
assert type(descriptor_append).__name__ == "builtin_function_or_method"
assert descriptor_append(3) is None
assert descriptor_target == [3]
assert list.append.__get__(None, list) is list.append
try:
    list.append.__get__(1, list)
    assert False
except TypeError:
    pass
assert int.__add__(overridden_int, 1) == 3
assert int.__radd__(overridden_int, 1) == 3
assert int.__neg__(overridden_int) == -2
assert int.__nonzero__(overridden_int) is True
assert int.__repr__(overridden_int) == "2"
base_dispatch_list = OverriddenList((1, 2))
assert list.__getitem__(base_dispatch_list, 0) == 1
assert list(list.__iter__(base_dispatch_list)) == [1, 2]
assert list.__contains__(base_dispatch_list, 1) is True
assert list.__add__(base_dispatch_list, [3]) == [1, 2, 3]
assert list.__repr__(base_dispatch_list) == "[1, 2]"
assert list.__setitem__(base_dispatch_list, 0, 7) is None
assert list.__getitem__(base_dispatch_list, 0) == 7
assert not hasattr(base_dispatch_list, "assigned")
assert list.__delitem__(base_dispatch_list, 0) is None
assert list(list.__iter__(base_dispatch_list)) == [2]
assert list(base_dispatch_list) == ["list-iter"]
assert tuple(base_dispatch_list) == ("list-iter",)
assert not hasattr(base_dispatch_list, "deleted")
assert dict.__getitem__(overridden_dict, "answer") == 42
assert dict.__contains__(overridden_dict, "answer") is True
assert list(dict.__iter__(overridden_dict)) == ["answer"]


class IteratingTuple(tuple):
    def __iter__(self):
        return iter(("tuple-iter",))


iterating_tuple = IteratingTuple((1, 2))
assert list(iterating_tuple) == ["tuple-iter"]
assert tuple(iterating_tuple) == ("tuple-iter",)


class MissingDict(dict):
    def __missing__(self, key):
        return "missing:" + key


missing_dict = MissingDict()
assert missing_dict["first"] == "missing:first"
assert dict.__getitem__(missing_dict, "second") == "missing:second"
assert missing_dict.get("third") is None


class RecordingFromKeysDict(dict):
    def __init__(self):
        dict.__init__(self)
        self.assignments = []

    def __setitem__(self, key, value):
        self.assignments.append((key, value))
        dict.__setitem__(self, key, value)


recording_fromkeys = RecordingFromKeysDict.fromkeys(("a", "b"), 3)
assert type(recording_fromkeys) is RecordingFromKeysDict
assert recording_fromkeys.assignments == [("a", 3), ("b", 3)]
assert dict(recording_fromkeys) == {"a": 3, "b": 3}

assert list.__getslice__([0, 1, 2, 3], 1, 3) == [1, 2]
legacy_slice_list = [0, 1, 2, 3]
assert list.__setslice__(legacy_slice_list, 1, 3, [8, 9]) is None
assert legacy_slice_list == [0, 8, 9, 3]
assert list.__delslice__(legacy_slice_list, 1, 3) is None
assert legacy_slice_list == [0, 3]
assert list(list.__reversed__([1, 2, 3])) == [3, 2, 1]
assert tuple.__getslice__((0, 1, 2), 1, 3) == (1, 2)
assert str.__getslice__("abc", 1, 3) == "bc"
assert unicode.__getslice__(u"abc", 1, 3) == u"bc"
assert tuple.__getnewargs__((1, 2)) == ((1, 2),)
assert str.__getnewargs__("abc") == ("abc",)
assert unicode.__getnewargs__(u"abc") == (u"abc",)


class CustomDir(object):
    def __dir__(self):
        return ["z", "a"]


class InvalidDir(object):
    def __dir__(self):
        return ("not", "a", "list")


assert dir(CustomDir()) == ["a", "z"]
try:
    dir(InvalidDir())
    assert False, "dir() accepted a non-list result"
except TypeError:
    pass
assert "mro" not in dir(str)
assert "__call__" not in dir(str)


class ReprList(list):
    def __repr__(self):
        return "CUSTOM_LIST"


class ReprTuple(tuple):
    def __repr__(self):
        return "CUSTOM_TUPLE"


class ReprDict(dict):
    def __repr__(self):
        return "CUSTOM_DICT"


assert str(ReprList()) == "CUSTOM_LIST"
assert str(ReprTuple()) == "CUSTOM_TUPLE"
assert str(ReprDict()) == "CUSTOM_DICT"
assert "__str__" not in list.__dict__
assert "__str__" not in tuple.__dict__
assert "__str__" not in dict.__dict__
assert "aa"[0] is "aa"[1]
assert next(iter("bb")) is "bb"[0]
