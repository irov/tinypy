events = []


class Meta(type):
    def __new__(mcls, name, bases, namespace):
        events.append(("new", name))
        namespace["created_by_meta"] = name
        return super(Meta, mcls).__new__(mcls, name, bases, namespace)

    def __init__(cls, name, bases, namespace):
        events.append(("init", name))
        cls.initialized_by_meta = True
        super(Meta, cls).__init__(name, bases, namespace)


class MetaProduct(object):
    __metaclass__ = Meta


assert type(MetaProduct) is Meta
assert MetaProduct.created_by_meta == "MetaProduct"
assert MetaProduct.initialized_by_meta is True
assert events == [("new", "MetaProduct"), ("init", "MetaProduct")]


class Constructed(object):
    def __new__(cls, value):
        instance = super(Constructed, cls).__new__(cls)
        instance.from_new = value + 1
        return instance

    def __init__(self, value):
        self.from_init = value + 2


constructed = Constructed(40)
assert constructed.from_new == 41
assert constructed.from_init == 42

Dynamic = type("Dynamic", (object,), {"value": 42})
assert Dynamic.value == 42


class Descriptor(object):
    def __get__(self, instance, owner):
        if instance is None:
            return owner.__name__
        return instance.__dict__.get("descriptor_value", -1)

    def __set__(self, instance, value):
        instance.__dict__["descriptor_value"] = value

    def __delete__(self, instance):
        del instance.__dict__["descriptor_value"]


class DescriptorOwner(object):
    value = Descriptor()


descriptor_owner = DescriptorOwner()
assert DescriptorOwner.value == "DescriptorOwner"
assert descriptor_owner.value == -1
descriptor_owner.value = 42
assert descriptor_owner.value == 42
assert "value" not in descriptor_owner.__dict__
del descriptor_owner.value
assert descriptor_owner.value == -1
descriptor_owner.plain = 7
del descriptor_owner.plain
assert not hasattr(descriptor_owner, "plain")
descriptor_owner.other = 8
delattr(descriptor_owner, "other")
assert not hasattr(descriptor_owner, "other")


class ProtocolMeta(type):
    def __getattribute__(cls, name):
        if name == "intercepted":
            return 41
        return type.__getattribute__(cls, name)

    def __getattr__(cls, name):
        if name == "fallback":
            return 42
        raise AttributeError(name)

    def __call__(cls, *args, **kwargs):
        return args, kwargs

    def __str__(cls):
        return "protocol-str"

    def __repr__(cls):
        return "protocol-repr"


class ProtocolProduct(object):
    __metaclass__ = ProtocolMeta


assert ProtocolProduct.intercepted == 41
assert ProtocolProduct.fallback == 42
assert str(ProtocolProduct) == "protocol-str"
assert repr(ProtocolProduct) == "protocol-repr"
assert ProtocolProduct(1, value=2) == ((1,), {"value": 2})


class MutableMetadata(object):
    pass


MutableMetadata.__name__ = "RenamedMetadata"
MutableMetadata.__module__ = "renamed_module"
assert MutableMetadata.__name__ == "RenamedMetadata"
assert MutableMetadata.__module__ == "renamed_module"

try:
    MutableMetadata.__name__ = "invalid\x00name"
except ValueError:
    pass
else:
    raise AssertionError("type __name__ accepted an embedded null")
assert MutableMetadata.__name__ == "RenamedMetadata"

try:
    type("invalid\x00name", (object,), {})
except ValueError:
    pass
else:
    raise AssertionError("type() accepted an embedded null in the name")


class AbstractMetadata(object):
    pass


try:
    del AbstractMetadata.__abstractmethods__
except AttributeError:
    pass
else:
    raise AssertionError("missing __abstractmethods__ was deleted")

AbstractMetadata.__abstractmethods__ = frozenset(["required"])
assert AbstractMetadata.__flags__ & (1 << 20)
try:
    AbstractMetadata()
except TypeError:
    pass
else:
    raise AssertionError("abstract class was instantiated")
del AbstractMetadata.__abstractmethods__
assert AbstractMetadata.__flags__ & (1 << 20) == 0
assert isinstance(AbstractMetadata(), AbstractMetadata)
assert type(object.__flags__) is long
assert object.__flags__ & (1 << 10)
assert object.__flags__ & (1 << 12)
assert int.__flags__ & (1 << 23)
assert str.__flags__ & (1 << 27)
assert Exception.__flags__ & (1 << 30)
assert type.__flags__ & (1 << 31)


class AbstractCustomNew(object):
    def __new__(cls):
        return 42


AbstractCustomNew.__abstractmethods__ = frozenset(["required"])
assert AbstractCustomNew() == 42
del AbstractCustomNew.__abstractmethods__

assert sorted(bool.__dict__.keys()) == [
    "__and__", "__doc__", "__new__", "__or__", "__rand__",
    "__repr__", "__ror__", "__rxor__", "__str__", "__xor__",
]
for type_metadata_name in (
    "__abstractmethods__", "__base__", "__bases__", "__basicsize__",
    "__dict__", "__dictoffset__", "__doc__", "__flags__", "__itemsize__",
    "__module__", "__mro__", "__name__", "__weakrefoffset__",
):
    assert type_metadata_name in type.__dict__
assert type.__dict__["__name__"].__get__(MutableMetadata, type) == "RenamedMetadata"
assert type.__dict__["__bases__"].__get__(MutableMetadata, type) == (object,)


class RebaseFirst(object):
    marker = "first"


class RebaseSecond(object):
    marker = "second"


class Rebased(RebaseFirst):
    pass


class RebasedChild(Rebased):
    pass


rebased = Rebased()
rebased_child = RebasedChild()
assert Rebased in RebaseFirst.__subclasses__()
Rebased.__bases__ = (RebaseSecond,)
assert Rebased.__bases__ == (RebaseSecond,)
assert Rebased.__base__ is RebaseSecond
assert Rebased.__mro__ == (Rebased, RebaseSecond, object)
assert RebasedChild.__mro__ == (RebasedChild, Rebased, RebaseSecond, object)
assert rebased.marker == "second" and rebased_child.marker == "second"
assert Rebased not in RebaseFirst.__subclasses__()
assert Rebased in RebaseSecond.__subclasses__()


class CachedBase(object):
    cached_value = 1


class CachedChild(CachedBase):
    pass


cached_child = CachedChild()
for cache_iteration in range(20):
    assert cached_child.cached_value == 1
CachedBase.cached_value = 2
assert cached_child.cached_value == 2
del CachedBase.cached_value
try:
    cached_child.cached_value
except AttributeError:
    pass
else:
    raise AssertionError("descendant attribute cache survived a base mutation")


class DocumentedType(object):
    "documented type"


class UndocumentedType(DocumentedType):
    pass


assert DocumentedType.__doc__ == "documented type"
assert UndocumentedType.__doc__ is None
try:
    DocumentedType.__doc__ = "changed"
except AttributeError:
    pass
else:
    raise AssertionError("type.__doc__ was writable")

try:
    del DocumentedType.__doc__
except AttributeError:
    pass
else:
    raise AssertionError("type.__doc__ was deletable")

try:
    Rebased.__bases__ = (RebasedChild,)
except TypeError:
    pass
else:
    raise AssertionError("cyclic type bases were accepted")
assert Rebased.__bases__ == (RebaseSecond,)

for metadata_name, metadata_value in (
    ("__mro__", (MutableMetadata, object)),
    ("__flags__", 0),
    ("__basicsize__", 0),
):
    try:
        setattr(MutableMetadata, metadata_name, metadata_value)
    except TypeError:
        pass
    else:
        raise AssertionError("read-only type metadata was changed")
try:
    int.injected = 42
except TypeError:
    pass
else:
    raise AssertionError("builtin type was mutated")
try:
    del MutableMetadata.__module__
except TypeError:
    pass
else:
    raise AssertionError("required type metadata was deleted")


def populate_reclaimable_type_cache():
    class CachedValue(object):
        pass

    class CachedOwner(object):
        value = CachedValue()

    return CachedOwner().value


populate_reclaimable_type_cache()


def read_reused_type_cache():
    accesses = []

    class ReusedDescriptor(object):
        def __get__(self, instance, owner):
            accesses.append((instance is None, owner.__name__))
            return 7

    class ReusedOwner(object):
        value = ReusedDescriptor()

    return ReusedOwner.value, ReusedOwner().value, accesses


assert read_reused_type_cache() == (7, 7, [(True, "ReusedOwner"), (False, "ReusedOwner")])
