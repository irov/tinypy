class Slotted(object):
    __slots__ = ("value", "__private")

    def __init__(self, value):
        self.value = value
        self.__private = value + 1

    def private(self):
        return self.__private


slotted = Slotted(40)
assert type(Slotted.value).__name__ == "member_descriptor"
assert slotted.value == 40
assert slotted.private() == 41
assert not hasattr(slotted, "__dict__")
value_descriptor = Slotted.__dict__["value"]
assert value_descriptor.__name__ == "value"
assert value_descriptor.__objclass__ is Slotted
for descriptor_method in ("__get__", "__set__", "__delete__", "__repr__"):
    assert descriptor_method in type(value_descriptor).__dict__
assert value_descriptor.__get__(slotted, Slotted) == 40
assert value_descriptor.__repr__() == repr(value_descriptor)
assert value_descriptor.__set__(slotted, 40) is None
assert value_descriptor.__delete__(slotted) is None
assert not hasattr(slotted, "value")
assert value_descriptor.__set__(slotted, 40) is None

try:
    slotted.other = 1
except AttributeError:
    pass
else:
    raise AssertionError("slotted instance unexpectedly has a dictionary")

del slotted.value
assert not hasattr(slotted, "value")
slotted.value = 42
assert slotted.value == 42


class SlottedChild(Slotted):
    pass


child = SlottedChild(5)
child.other = 6
assert child.other == 6
assert child.__dict__ == {"other": 6}


class TightChild(Slotted):
    __slots__ = ()


tight = TightChild(7)
assert not hasattr(tight, "__dict__")


# Protocol 2 pickling carries __slots__ values beside the instance dictionary.
class SlottedState(object):
    __slots__ = ("first", "second")


slotted_state = SlottedState()
slotted_state.first = 1
slotted_state.second = [2]
reduced = slotted_state.__reduce_ex__(2)
assert reduced[2] == (None, {"first": 1, "second": [2]})

slotted_state = SlottedState()
slotted_state.first = 3
assert slotted_state.__reduce_ex__(2)[2] == (None, {"first": 3})

assert SlottedState().__reduce_ex__(2)[2] is None


class MixedState(object):
    __slots__ = ("slotted", "__dict__")


mixed_state = MixedState()
mixed_state.slotted = 1
mixed_state.stored = 2
assert mixed_state.__reduce_ex__(2)[2] == ({"stored": 2}, {"slotted": 1})


class PlainState(object):
    pass


plain_state = PlainState()
plain_state.stored = 1
assert plain_state.__reduce_ex__(2)[2] == {"stored": 1}
