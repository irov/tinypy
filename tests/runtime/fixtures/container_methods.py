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
assert mapping.pop("d") == 4
assert mapping.pop("missing", 8) == 8
copy = mapping.copy()
assert copy == mapping
item = copy.popitem()
assert item[0] not in copy
copy.clear()
assert copy == {}


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
assert filter(lambda value: value % 2, [1, 2, 3]) == [1, 3]
assert filter(None, (0, 1, 2)) == (1, 2)
assert filter(lambda value: value != "b", "abc") == "ac"
assert map(lambda value: value + 1, [1, 2]) == [2, 3]
assert map(None, [1, 2], [3]) == [(1, 3), (2, None)]
assert zip([1, 2], [3, 4, 5]) == [(1, 3), (2, 4)]
assert sum([1, 2, 3]) == 6
assert min(3, 1, 2) == 1
assert max([1, 4, 2]) == 4
assert max(["a", "bbb", "cc"], key=len) == "bbb"
assert list(reversed([1, 2, 3])) == [3, 2, 1]
assert chr(65) == "A"
assert unichr(0x20ac) == u"\u20ac"
assert cmp(1, 2) == -1 and cmp(2, 2) == 0 and cmp(3, 2) == 1
assert hash("value") == hash("value")
assert pow(2, 5) == 32
assert round(1.25, 1) == 1.3
assert globals() is locals()
assert list(xrange(1, 5, 2)) == [1, 3]
range_value = xrange(2, 10, 2)
assert type(range_value).__name__ == "xrange"
assert len(range_value) == 4 and bool(range_value)
assert range_value[-1] == 8
assert repr(range_value) == "xrange(2, 10, 2)"
assert list(range_value) == [2, 4, 6, 8]
assert list(range_value) == [2, 4, 6, 8]
enumerated = enumerate(["a", "b"], 3)
assert type(enumerated).__name__ == "enumerate"
assert next(enumerated) == (3, "a")
assert list(enumerated) == [(4, "b")]
reverse_iterator = reversed([1, 2, 3])
assert next(reverse_iterator) == 3
assert list(reverse_iterator) == [2, 1]


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
assert "value" in dir(instance)
assert "method" in dir(Introspection)
assert "Introspection" in dir()


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
