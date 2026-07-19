class CheckMeta(type):
    def __instancecheck__(cls, instance):
        return getattr(instance, "virtual_instance", False)

    def __subclasscheck__(cls, subclass):
        return getattr(subclass, "virtual_subclass", False)


class Contract(object):
    __metaclass__ = CheckMeta


class Concrete(object):
    virtual_subclass = True

    def __init__(self):
        self.virtual_instance = True


assert isinstance(Concrete(), Contract)
assert issubclass(Concrete, Contract)
assert not isinstance(object(), Contract)
assert not issubclass(object, Contract)


class Base(object):
    pass


class First(Base):
    pass


class Second(Base):
    pass


subclasses = Base.__subclasses__()
assert First in subclasses
assert Second in subclasses
assert Contract not in subclasses
