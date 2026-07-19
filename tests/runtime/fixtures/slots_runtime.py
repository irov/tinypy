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
