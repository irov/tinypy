# Keyword argument handling, class creation, descriptors, finalizers,
# __class__ assignment and constructor diagnostics follow CPython 2.7.
import _weakref as weakref
import sys


def error_text(callable_object, *args, **kwargs):
    try:
        callable_object(*args, **kwargs)
    except Exception as error:
        return type(error).__name__ + ": " + str(error)
    raise AssertionError("no exception raised")


class Old:
    pass


def keywords(a, b=2, **rest):
    return (a, b, sorted(rest.items()))


class KeyMapping(object):
    def keys(self):
        return ["a"]

    def __getitem__(self, key):
        return 42


assert keywords(**{u"a": 1, u"b": 3}) == (1, 3, [])
assert keywords(1, **{u"zz": 9}) == (1, 2, [(u"zz", 9)])
assert error_text(lambda: keywords(**{1: 2})) == "TypeError: keywords() keywords must be strings"
assert error_text(lambda: keywords(1, **{"a": 2})) == "TypeError: keywords() got multiple values for keyword argument 'a'"
assert error_text(lambda: keywords(**3)) == "TypeError: keywords() argument after ** must be a mapping, not int"
assert keywords(**KeyMapping()) == (42, 2, [])
assert error_text(lambda: len(**3)) == "TypeError: len() argument after ** must be a mapping, not int"


class Mixed(Old, object):
    pass


assert type(Mixed).__name__ == "type"
assert [cls.__name__ for cls in Mixed.__mro__] == ["Mixed", "Old", "object"]


def metaclass_function(name, bases, namespace):
    return (name, len(bases), sorted(namespace))


__metaclass__ = metaclass_function


class WithoutBases:
    x = 1


class WithClassicBase(Old):
    pass


del __metaclass__
assert WithoutBases == ("WithoutBases", 0, ["__module__", "x"])
assert type(WithClassicBase).__name__ == "classobj"


def integer_base():
    class Broken(1):
        pass


def duplicate_new_style_base():
    class Base(object):
        pass

    class Broken(Base, Base):
        pass


def duplicate_classic_base():
    class Both(Old, Old):
        pass
    return Both.__bases__


def mro_conflict():
    class Base(object):
        pass

    class Derived(Base):
        pass

    class Broken(Base, Derived):
        pass


class InstanceBase:
    def __init__(self, *args):
        self.args = args


def instance_base():
    class Built(InstanceBase()):
        pass
    return type(Built).__name__, Built.args[0], len(Built.args)


assert error_text(integer_base) == "TypeError: Error when calling the metaclass bases\n    int() takes at most 2 arguments (3 given)"
assert error_text(duplicate_new_style_base) == "TypeError: Error when calling the metaclass bases\n    duplicate base class Base"
assert duplicate_classic_base() == (Old, Old)
assert error_text(mro_conflict) == "TypeError: Error when calling the metaclass bases\n    Cannot create a consistent method resolution\norder (MRO) for bases Base, Derived"
assert instance_base() == ("instance", "Built", 3)

finalizer_log = []


class Finalized(object):
    pass


def finalizer(self):
    finalizer_log.append("fin")


Finalized.__del__ = finalizer
instance = Finalized()
del instance
del Finalized.__del__
instance = Finalized()
del instance
assert finalizer_log == ["fin"]


class BuiltinAttribute(object):
    pass


BuiltinAttribute.f = len
assert BuiltinAttribute.f is len
assert BuiltinAttribute().f([1, 2, 3]) == 3
assert type(BuiltinAttribute().f).__name__ == "builtin_function_or_method"


class SuperBase(object):
    def f(self):
        return "f"


class SuperDerived(SuperBase):
    pass


assert super(SuperDerived, SuperDerived).f.im_self is None
assert type(super(SuperDerived, SuperDerived).f).__name__ == "instancemethod"
assert super(SuperDerived, SuperDerived()).f() == "f"


class SetOnlyDescriptor(object):
    def __set__(self, instance, value):
        instance.__dict__["d"] = value * 2


class Described(object):
    d = SetOnlyDescriptor()


described = Described()
assert type(described.d).__name__ == "SetOnlyDescriptor"
described.d = 5
assert described.d == 10

weakref_log = []


class Referenced(object):
    def __del__(self):
        weakref_log.append(reference() is None)


referenced = Referenced()
reference = weakref.ref(referenced, lambda ref: weakref_log.append("callback"))
del referenced
assert weakref_log == ["callback", True]

init_log = []


class NonTypeMetaclass(type):
    def __new__(meta, name, bases, namespace):
        return 5

    def __init__(cls, *args):
        init_log.append("init")


class Replaced(object):
    __metaclass__ = NonTypeMetaclass


assert Replaced == 5 and init_log == []


class ClassAttribute(object):
    __class__ = int


class PlainClass(object):
    pass


assert ClassAttribute().__class__ is int
assert PlainClass().__class__ is PlainClass


def dict_slot_twice():
    class First(object):
        __slots__ = ("__dict__",)

    class Second(First):
        __slots__ = ("__dict__",)


def weakref_slot_twice():
    class First(object):
        __slots__ = ("__weakref__",)

    class Second(First):
        __slots__ = ("__weakref__",)


assert error_text(dict_slot_twice) == "TypeError: Error when calling the metaclass bases\n    __dict__ slot disallowed: we already got one"
assert error_text(weakref_slot_twice) == "TypeError: Error when calling the metaclass bases\n    __weakref__ slot disallowed: either we already got one, or __itemsize__ != 0"


class Movable(object):
    pass


class Target(object):
    pass


class Slotted(object):
    __slots__ = ("a",)


class MovableChild(Movable):
    pass


def assign_class(target):
    movable = Movable()
    movable.__class__ = target
    return type(movable).__name__


assert assign_class(Target) == "Target"
assert assign_class(MovableChild) == "MovableChild"
assert error_text(assign_class, Slotted) == "TypeError: __class__ assignment: 'Movable' object layout differs from 'Slotted'"
assert error_text(assign_class, int) == "TypeError: __class__ assignment: only for heap types"

assert type(object.__new__(object)).__name__ == "object"
assert error_text(object.__new__, int) == "TypeError: object.__new__(int) is not safe, use int.__new__()"
assert error_text(object.__new__, dict) == "TypeError: object.__new__(dict) is not safe, use dict.__new__()"


class LongSubclass(long):
    pass


assert type(int(2 ** 62 + 0L)).__name__ == "int"
assert type(int(2 ** 63 + 0L)).__name__ == "long"
assert type(int(-2 ** 63 + 0L)).__name__ == "int"
assert type(int(LongSubclass(2 ** 70))).__name__ == "long"
assert type(long(LongSubclass(5))).__name__ == "long"
assert type(int(LongSubclass(5))).__name__ == "int"


class NoParameters(object):
    pass


class ReturningInit(object):
    def __init__(self):
        return 1


class OneArgument(object):
    def __init__(self, x):
        self.x = x


assert error_text(NoParameters, 1) == "TypeError: object() takes no parameters"
assert error_text(ReturningInit) == "TypeError: __init__() should return None, not 'int'"
assert OneArgument(1).x == 1
assert error_text(OneArgument) == "TypeError: __init__() takes exactly 2 arguments (1 given)"
assert error_text(OneArgument, 1, 2) == "TypeError: __init__() takes exactly 2 arguments (3 given)"
assert error_text(OneArgument, y=1) == "TypeError: __init__() got an unexpected keyword argument 'y'"
assert error_text(int, 1, 2, 3) == "TypeError: int() takes at most 2 arguments (3 given)"
assert error_text(type, 1, 2) == "TypeError: type() takes 1 or 3 arguments"
assert error_text(dict, 1, 2) == "TypeError: dict expected at most 1 arguments, got 2"
assert error_text(len) == "TypeError: len() takes exactly one argument (0 given)"
assert error_text(getattr, 1) == "TypeError: getattr expected at least 2 arguments, got 1"
assert error_text(object, 1) == "TypeError: object() takes no parameters"
assert error_text(bool, 1, 2) == "TypeError: bool() takes at most 1 argument (2 given)"
