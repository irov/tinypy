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


class DirBase:
    inherited_marker = 1


class DirChild(DirBase):
    local_marker = 2

    def method(self):
        return 3


dir_instance = DirChild()
dir_instance.instance_marker = 4
class_names = dir(DirChild)
instance_names = dir(dir_instance)
for expected_name in ("inherited_marker", "local_marker", "method"):
    assert expected_name in class_names
for unexpected_name in ("__dict__", "__bases__", "__name__"):
    assert unexpected_name not in class_names
for expected_name in ("inherited_marker", "local_marker", "method", "instance_marker"):
    assert expected_name in instance_names
for unexpected_name in ("__class__", "__dict__"):
    assert unexpected_name not in instance_names


class ClassicDescriptor(object):
    def __get__(self, instance, owner):
        return instance is None, owner.__name__


class ClassicDescriptorOwner:
    value = ClassicDescriptor()


assert ClassicDescriptorOwner.value == (True, "ClassicDescriptorOwner")
assert ClassicDescriptorOwner().value == (False, "ClassicDescriptorOwner")


class ClassicPropertyOwner:
    value = property(lambda self: 9)


assert ClassicPropertyOwner.value is ClassicPropertyOwner.__dict__["value"]
assert ClassicPropertyOwner().value == 9


# Classic classes and their instances report the defining module.
class ClassicRepresentation:
    pass


class ClassicCustomRepresentation:
    def __repr__(self):
        return "custom repr"

    def __str__(self):
        return "custom str"


assert repr(ClassicRepresentation).startswith("<class %s.ClassicRepresentation at 0x" % __name__)
assert repr(ClassicRepresentation).endswith(">")
assert str(ClassicRepresentation) == repr(ClassicRepresentation)
assert repr(ClassicRepresentation()).startswith("<%s.ClassicRepresentation instance at 0x" % __name__)
assert repr(ClassicCustomRepresentation).startswith("<class %s.ClassicCustomRepresentation at 0x" % __name__)
assert repr(ClassicCustomRepresentation()) == "custom repr"
assert str(ClassicCustomRepresentation()) == "custom str"
assert repr(type(ClassicRepresentation)) == "<type 'classobj'>"
assert repr(type(ClassicRepresentation())) == "<type 'instance'>"


# Truth testing of a classic instance consults __nonzero__ and then __len__,
# both of which live in the class rather than in the shared instance type.
class ClassicLength:
    def __init__(self, length):
        self.length = length

    def __len__(self):
        return self.length


class ClassicNonzero:
    def __nonzero__(self):
        return False


class ClassicNonzeroWins:
    def __len__(self):
        return 0

    def __nonzero__(self):
        return True


assert bool(ClassicLength(0)) is False
assert bool(ClassicLength(2)) is True
assert (not ClassicLength(0)) is True
assert bool(ClassicNonzero()) is False
assert bool(ClassicNonzeroWins()) is True
assert bool(ClassicRepresentation()) is True
assert (ClassicLength(0) or "empty") == "empty"
assert (ClassicLength(1) and "full") == "full"

# A classic class never derives from a new-style type.
assert issubclass(ClassicLength, ClassicLength) is True
assert issubclass(ClassicLength, object) is False
assert issubclass(ClassicLength, dict) is False
assert issubclass(object, ClassicLength) is False
assert isinstance(ClassicLength(0), object) is True

# __setattr__ and __delattr__ hooks are honoured, while __class__ and __dict__
# are assigned directly.
attribute_log = []


class ClassicHooks:
    def __setattr__(self, name, value):
        attribute_log.append(("set", name, value))
        self.__dict__[name] = value

    def __delattr__(self, name):
        attribute_log.append(("del", name))
        del self.__dict__[name]


hooked = ClassicHooks()
hooked.value = 1
assert hooked.value == 1
del hooked.value
assert attribute_log == [("set", "value", 1), ("del", "value")]
assert hooked.__dict__ == {}


class ClassicFirst:
    def which(self):
        return "first"


class ClassicSecond:
    def which(self):
        return "second"


rebound = ClassicFirst()
rebound.__class__ = ClassicSecond
assert rebound.which() == "second"
rebound.__dict__ = {"stored": 7}
assert rebound.stored == 7

for invalid in (5, None):
    try:
        rebound.__class__ = invalid
    except TypeError:
        pass
    else:
        raise AssertionError("__class__ accepted a non-class")
    try:
        rebound.__dict__ = invalid
    except TypeError:
        pass
    else:
        raise AssertionError("__dict__ accepted a non-dictionary")

# Missing attributes name the class itself.
try:
    ClassicFirst().missing
except AttributeError, missing_error:
    assert str(missing_error) == "ClassicFirst instance has no attribute 'missing'"
else:
    raise AssertionError("missing classic attribute did not raise")

try:
    ClassicFirst.missing
except AttributeError, missing_error:
    assert str(missing_error) == "class ClassicFirst has no attribute 'missing'"
else:
    raise AssertionError("missing classic class attribute did not raise")

try:
    del ClassicFirst().missing
except AttributeError, missing_error:
    assert str(missing_error) == "ClassicFirst instance has no attribute 'missing'"
else:
    raise AssertionError("deleting a missing classic attribute did not raise")


# Methods report the owning class and, when bound, the instance.
class ClassicMethods:
    def method(self):
        return 1


assert repr(ClassicMethods.method) == "<unbound method ClassicMethods.method>"
assert repr(ClassicMethods().method).startswith("<bound method ClassicMethods.method of <%s.ClassicMethods instance at 0x" % __name__)


# str() of a classic instance falls back to __repr__ when __str__ is absent.
class ClassicReprOnly:
    def __repr__(self):
        return "classic repr"


class ClassicBothText:
    def __repr__(self):
        return "classic repr"

    def __str__(self):
        return "classic str"


assert str(ClassicReprOnly()) == "classic repr"
assert "%s" % ClassicReprOnly() == "classic repr"
assert repr(ClassicReprOnly()) == "classic repr"
assert str(ClassicBothText()) == "classic str"
assert repr(ClassicBothText()) == "classic repr"
