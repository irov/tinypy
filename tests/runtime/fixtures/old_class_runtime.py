class Base:
    class_value = 7

    def __init__(self, value):
        self.value = value

    def add(self, amount):
        return self.value + amount

    @staticmethod
    def static_value(value):
        return value + 1

    @classmethod
    def class_name(cls):
        return cls.__name__

    def __getattr__(self, name):
        if name == "fallback":
            return 31
        raise AttributeError(name)

    def __call__(self, amount):
        return self.value + amount


class Child(Base):
    pass


assert type(Base).__name__ == "classobj"
assert type(Base(1)).__name__ == "instance"
assert type(Base.add).__name__ == "instancemethod"
assert Base.add.im_self is None
assert Base.add.im_class is Base

value = Child(10)
assert value.__class__ is Child
assert value.add(4) == 14
assert Child.add(value, 5) == 15
assert Base.static_value(8) == 9
assert value.static_value(9) == 10
assert Child.class_name() == "Child"
assert value.class_name() == "Child"
assert value.class_value == 7
assert value.fallback == 31
assert callable(value)
assert value(6) == 16
assert isinstance(value, Child)
assert isinstance(value, Base)
assert issubclass(Child, Base)
assert not issubclass(Base, Child)

Child.dynamic = 12
assert value.dynamic == 12
del Child.dynamic
assert not hasattr(value, "dynamic")

value.temporary = 18
assert value.temporary == 18
del value.temporary
assert not hasattr(value, "temporary")


class Protocol:
    def __init__(self):
        self.items = [2, 4]
        self.position = 0

    def __len__(self):
        return len(self.items)

    def __nonzero__(self):
        return True

    def __getitem__(self, index):
        return self.items[index]

    def __setitem__(self, index, value):
        self.items[index] = value

    def __delitem__(self, index):
        del self.items[index]

    def __contains__(self, value):
        return value in self.items

    def __iter__(self):
        self.position = 0
        return self

    def next(self):
        if self.position == len(self.items):
            raise StopIteration
        value = self.items[self.position]
        self.position += 1
        return value

    def __add__(self, value):
        return len(self.items) + value

    def __radd__(self, value):
        return value + len(self.items)

    def __cmp__(self, other):
        return cmp(len(self.items), len(other.items))

    def __repr__(self):
        return "Protocol(%d)" % len(self.items)

    def __str__(self):
        return "protocol"

    def __hash__(self):
        return 1234

    def __pos__(self):
        return 11

    def __neg__(self):
        return -11

    def __invert__(self):
        return 12

    def __abs__(self):
        return 13


protocol = Protocol()
assert len(protocol) == 2
assert bool(protocol)
assert protocol[1] == 4
protocol[1] = 6
assert protocol[1] == 6
del protocol[0]
assert 6 in protocol
assert list(protocol) == [6]
assert protocol + 3 == 4
assert 3 + protocol == 4
assert protocol == protocol
assert repr(protocol) == "Protocol(1)"
assert str(protocol) == "protocol"
assert hash(protocol) == 1234
assert +protocol == 11
assert -protocol == -11
assert ~protocol == 12
assert abs(protocol) == 13
