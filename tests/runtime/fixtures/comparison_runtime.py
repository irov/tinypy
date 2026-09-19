# Python 2 comparison dispatch: built-in comparisons defer to a user operand,
# both directions of __cmp__ are tried, and recursion is bounded.
import sys


class RichEqual(object):
    def __eq__(self, other):
        return "rich"


class ClassicOrder:
    def __cmp__(self, other):
        return 0


class ModernOrder(object):
    def __cmp__(self, other):
        return 1


rich = RichEqual()
classic = ClassicOrder()
modern = ModernOrder()

for builtin_value in (1, 1.5, 2 ** 70, "text", u"text", [1], (1,), {1: 1}, set([1]), frozenset([1])):
    assert (builtin_value == rich) == "rich", builtin_value
    assert (builtin_value == classic) is True, builtin_value
    assert cmp(builtin_value, classic) == 0, builtin_value
    assert cmp(classic, builtin_value) == 0, builtin_value
    assert (builtin_value == modern) is False, builtin_value

# The __cmp__ slot wrappers still type-check their argument.
for wrapper_owner, wrapper_argument in (({}, []), ((1), "a")):
    try:
        wrapper_owner.__cmp__(wrapper_argument)
    except TypeError:
        pass
    else:
        raise AssertionError("a built-in __cmp__ wrapper accepted a foreign argument")

# Ordinary comparisons are untouched.
assert 1 == 1 and [1] == [1] and (1,) == (1,) and {1: 1} == {1: 1}
assert 1 < 2 and "a" < "b" and [1] < [2]
assert 1 == 1.0 and 1 < 1.5 and set([1]) == set([1])
assert cmp(1, 2) == -1 and cmp("b", "a") == 1
assert sorted([1, "a", None, (1,), [2]]) == [None, 1, [2], "a", (1,)]
assert ([1] == (1,)) is False and (1 == "a") is False


class SetSubclass(set):
    pass


class ListSubclass(list):
    pass


assert SetSubclass([1, 2]) == set([1, 2])
assert ListSubclass([1]) == [1]

# Self-referential and deeply nested structures raise instead of overflowing
# the C stack, the way Py_EnterRecursiveCall guards CPython's comparisons.
left_cycle = []
left_cycle.append(left_cycle)
right_cycle = []
right_cycle.append(right_cycle)
try:
    left_cycle == right_cycle
except RuntimeError:
    pass
else:
    raise AssertionError("a self-referential comparison did not stop")
assert left_cycle == left_cycle
# tinypy has no cyclic collector, so the fixture breaks its own cycles.
del left_cycle[:]
del right_cycle[:]

deep_left = []
deep_right = []
for _ in range(4000):
    deep_left = [deep_left]
    deep_right = [deep_right]
try:
    deep_left == deep_right
except RuntimeError:
    pass
else:
    raise AssertionError("a deeply nested comparison did not stop")

# type.mro() materialises the table for static built-in types too.
assert int.mro() == [int, object]
assert object.mro() == [object]
assert type.mro(bool) == [bool, int, object]
assert Exception.mro()[-1] is object


# Classic classes coerce before the operator hook, and __class__ may be
# reassigned between layout-compatible Python classes.
class CoercingOperand:
    def __coerce__(self, other):
        return (10, other)

    def __add__(self, other):
        return ("hook", other)


assert CoercingOperand() + 5 == 15
assert CoercingOperand() - 5 == 5
assert CoercingOperand() * 5 == 50


class SelfCoercing:
    def __coerce__(self, other):
        return (self, other)

    def __add__(self, other):
        return "hook"


class DecliningCoercion:
    def __coerce__(self, other):
        return None

    def __add__(self, other):
        return "hook"


assert SelfCoercing() + 1 == "hook"
assert DecliningCoercion() + 1 == "hook"
assert coerce(1, 2.0) == (1.0, 2.0)


class BeforeAssignment(object):
    def which(self):
        return "before"


class AfterAssignment(object):
    def which(self):
        return "after"


reassigned = BeforeAssignment()
reassigned.__class__ = AfterAssignment
assert reassigned.which() == "after"
assert reassigned.__class__ is AfterAssignment


class FirstSlots(object):
    __slots__ = ("stored",)


class SecondSlots(object):
    __slots__ = ("other",)


class MatchingSlots(object):
    __slots__ = ("stored",)


slotted = FirstSlots()
slotted.stored = 1
slotted.__class__ = MatchingSlots
assert slotted.stored == 1
for incompatible in (SecondSlots, BeforeAssignment, int):
    try:
        slotted.__class__ = incompatible
    except TypeError:
        pass
    else:
        raise AssertionError("__class__ accepted an incompatible layout")

try:
    reassigned.__class__ = 5
except TypeError:
    pass
else:
    raise AssertionError("__class__ accepted a non-class")
