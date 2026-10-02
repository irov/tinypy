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
