def baseslots(*slots):
    def meta(cls, name, bases, classdict):
        new_classdict = {}
        new_classdict.update(classdict)
        new_classdict.update(__slots__=())
        new_classdict.update(__multislots__=slots)
        return type(name, bases, new_classdict)

    return type("meta_baseslots", (type,), dict(__new__=meta))


def finalslots(*slots):
    def meta(cls, name, bases, classdict):
        mro = type(name, bases, {}).mro()
        final_slots = []
        for base in mro[1:]:
            if hasattr(base, "__multislots__") is False:
                continue
            final_slots.extend(base.__multislots__)
        final_slots.extend(slots)
        new_classdict = {}
        new_classdict.update(classdict)
        new_classdict.update(__slots__=final_slots)
        new_classdict.update(__multislots__=slots)
        return type(name, bases, new_classdict)

    return type("meta_finalslots", (type,), dict(__new__=meta))


class Base(object):
    __metaclass__ = baseslots("base_value")


class Final(Base):
    __metaclass__ = finalslots("final_value")


value = Final()
value.base_value = 10
value.final_value = 20
assert value.base_value == 10
assert value.final_value == 20
assert not hasattr(value, "__dict__")
assert Final.__multislots__ == ("final_value",)
assert Base.__multislots__ == ("base_value",)


class EmptyMixin(object):
    __slots__ = ()


class LaterSlottedBase(object):
    __slots__ = ("later_value",)


class EmptyMixinFirst(EmptyMixin, LaterSlottedBase):
    __slots__ = ()


combined = EmptyMixinFirst()
combined.later_value = 42
assert combined.later_value == 42
