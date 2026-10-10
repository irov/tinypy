# isinstance/issubclass, compile keywords, buffer bounds, hashing, set and
# dict view operators, container draining and range follow CPython 2.7.


def error_text(callable_object, *args, **kwargs):
    try:
        callable_object(*args, **kwargs)
    except Exception as error:
        return type(error).__name__ + ": " + str(error)
    raise AssertionError("no exception raised")


class Old:
    pass


class New(object):
    pass


class OldChild(Old):
    pass


class NewChild(New):
    pass


assert (isinstance(OldChild(), Old), isinstance(NewChild(), New), issubclass(OldChild, Old), issubclass(NewChild, (Old, New))) == (True, True, True, True)


class Pretender(object):
    __class__ = property(lambda self: New)


class AbstractDerived(object):
    __bases__ = (New,)


class InstanceChecker(type):
    def __instancecheck__(cls, instance):
        return instance == "accepted"


class Checked(object):
    __metaclass__ = InstanceChecker


assert isinstance(Pretender(), New)
assert issubclass(AbstractDerived(), New)
assert error_text(isinstance, 1, 2) == "TypeError: isinstance() arg 2 must be a class, type, or tuple of classes and types"
assert error_text(issubclass, 1, New) == "TypeError: issubclass() arg 1 must be a class"
assert error_text(issubclass, New, 2) == "TypeError: issubclass() arg 2 must be a class or tuple of classes"
assert isinstance(NewChild(), (int, (str, (New,))))
assert isinstance("accepted", Checked) and not isinstance("other", Checked)

assert eval(compile(source="1 + 1", filename="f", mode="eval")) == 2
assert eval(compile("2 + 2", "f", "eval", 0x10)) == 4
assert error_text(compile, "1", "f", "eval", 0x40000000) == "ValueError: compile(): unrecognised flags"
assert error_text(compile, "1", "f", "eval", bogus=1) == "TypeError: 'bogus' is an invalid keyword argument for this function"
assert error_text(compile, "1", "f", "eval", source="1") == "TypeError: Argument given by name ('source') and position (1)"
assert error_text(compile, "1", "f") == "TypeError: Required argument 'mode' (pos 3) not found"
assert type(classmethod(1)).__name__ == "classmethod"
assert type(staticmethod(1)).__name__ == "staticmethod"

assert (str(buffer("abc", 5)), str(buffer("abc", 1, 5)), str(buffer("abc", 1, 1))) == ("", "bc", "b")
assert error_text(buffer, "abc", -1) == "ValueError: offset must be zero or positive"


class NoHash(object):
    __hash__ = None


def unhashable_key():
    {}[[]] = 1


assert error_text(hash, []) == "TypeError: unhashable type: 'list'"
assert error_text(unhashable_key) == "TypeError: unhashable type: 'list'"
assert error_text(hash, NoHash()) == "TypeError: unhashable type: 'NoHash'"

values = set([1, 2])
alias = values
values |= set([3, 9])
values -= set([2])
values &= set([1, 3, 9, 10])
values ^= set([10, 11])
values ^= set([11])
assert alias is values and sorted(values) == [1, 3, 9, 10]
assert type(frozenset([1]) | set([2])).__name__ == "frozenset"

mapping = {1: "a", 2: "b", 3: "c"}
assert sorted(mapping.viewkeys() | [7]) == [1, 2, 3, 7]
assert sorted([7] | mapping.viewkeys()) == [1, 2, 3, 7]
assert sorted(mapping.viewkeys() & [1, 5]) == [1]
assert sorted(mapping.viewkeys() - [2, 3]) == [1]
assert sorted(mapping.viewitems()) == [(1, "a"), (2, "b"), (3, "c")]

draining_set = set(range(2000))
drained = 0
while draining_set:
    draining_set.pop()
    drained += 1
assert drained == 2000
draining_dict = dict.fromkeys(range(2000))
drained = 0
while draining_dict:
    draining_dict.popitem()
    drained += 1
assert drained == 2000

assert (range(3), range(1, 10, 4), range(5, 0, -2), range(0)) == ([0, 1, 2], [1, 5, 9], [5, 3, 1], [])
assert error_text(range) == "TypeError: range expected at least 1 arguments, got 0"
assert sorted([1, 2, 3], reverse=True) == [3, 2, 1]

# apply passes the caller's own keyword dict, which the callbacks of a builtin
# may clear; the builtin keeps the arguments it read alive. CPython 2.7 itself
# crashes or reads freed memory on several of these.
keywords = {}


def clearing_key(item):
    keywords.clear()
    return -item


keywords["key"] = clearing_key
del clearing_key
assert apply(sorted, ([3, 1, 2],), keywords) == [3, 2, 1]


class ClearingIterable(object):
    def __iter__(self):
        keywords.clear()
        return iter([3, 1, 2])

    def __len__(self):
        return 3


keywords["iterable"] = ClearingIterable()
assert apply(sorted, (), keywords) == [1, 2, 3]


class Sequence(object):
    def __iter__(self):
        return iter("ab")


class ClearingStart(object):
    def __index__(self):
        keywords.clear()
        return 5


keywords["sequence"] = Sequence()
keywords["start"] = ClearingStart()
assert list(apply(enumerate, (), keywords)) == [(5, "a"), (6, "b")]


# The key comparison of the start lookup clears the keywords, whose sequence
# enumerate holds from its own lookup.
class CollidingStart(object):
    def __hash__(self):
        return hash("start")

    def __eq__(self, other):
        keywords.clear()
        return False


keywords["sequence"] = [10, 20]
keywords[CollidingStart()] = 0
assert list(apply(enumerate, (), keywords)) == [(0, 10), (1, 20)]


class ClearingFlags(object):
    def __int__(self):
        keywords.clear()
        return 0


keywords["source"] = "".join(["compiled_value", " = 1"])
keywords["filename"] = "".join(["fix", "ture"])
keywords["mode"] = "".join(["ex", "ec"])
keywords["flags"] = ClearingFlags()
keywords["dont_inherit"] = int("1")
exec apply(compile, (), keywords)
assert compiled_value == 1


# A callable iterator that its own callable exhausts keeps the callable and
# the sentinel for the comparison in progress.
def counting():
    state = [0]

    def produce():
        state[0] += 1
        if state[0] == 1:
            for produced in reentered:
                pass
        return state[0] * 10

    return produce


reentered = iter(counting(), 20)
assert list(reentered) == []
assert list(reentered) == []
