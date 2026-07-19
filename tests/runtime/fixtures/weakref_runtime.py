from _weakref import getweakrefcount, getweakrefs, ref


events = []


class Target(object):
    pass


def removed(reference):
    events.append(reference() is None)


target = Target()
reference = ref(target, removed)
assert reference() is target
assert getweakrefcount(target) == 1
assert getweakrefs(target) == [reference]
assert hash(reference) == hash(target)
del target
assert reference() is None
assert events == [True]


class KeyedRef(ref):
    __slots__ = ("key",)

    def __new__(type, value, callback, key):
        self = ref.__new__(type, value, callback)
        self.key = key
        return self

    def __init__(self, value, callback, key):
        super(KeyedRef, self).__init__(value, callback)
second = Target()
keyed = KeyedRef(second, None, "second")
assert keyed() is second
assert keyed.key == "second"
assert isinstance(keyed, ref)

class Classic:
    pass


classic = Classic()
classic_reference = ref(classic)
assert classic_reference() is classic
del classic
assert classic_reference() is None

type_reference = ref(Target)
assert type_reference() is Target
