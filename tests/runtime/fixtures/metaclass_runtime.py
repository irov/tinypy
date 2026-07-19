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
