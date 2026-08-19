a = set([1, 2, 3])
b = frozenset([3, 4])
c = {2, 4, 6}

assert len(a) == 3
assert 2 in a
assert 9 not in a
assert a | c == set([1, 2, 3, 4, 6])
assert a & c == set([2])
assert a - c == set([1, 3])
assert a ^ c == set([1, 3, 4, 6])
assert set.__and__(a, c) == set([2])
assert set.__or__(a, c) == set([1, 2, 3, 4, 6])
assert set.__xor__(a, c) == set([1, 3, 4, 6])
assert set.__sub__(a, c) == set([1, 3])
assert set.__rsub__(a, c) == set([4, 6])
assert set.__and__(a, [1]) is NotImplemented
assert frozenset.__hash__(b) == hash(b)
assert set.__hash__ is None
assert set([1, 2]) < a
assert a <= set([1, 2, 3])
assert a.issuperset([1, 2])
assert a.isdisjoint([8, 9])

d = a.copy()
d.add(5)
d.discard(2)
d.remove(1)
assert d == set([3, 5])
d.update([6, 7])
d.intersection_update([3, 6, 8])
assert d == set([3, 6])
d.symmetric_difference_update([6, 9])
assert d == set([3, 9])
d.difference_update([3])
assert d == set([9])
assert d.pop() == 9
assert len(d) == 0

inplace_set = set([1, 2, 3])
inplace_set_alias = inplace_set
inplace_set &= set([2, 3, 4])
assert inplace_set is inplace_set_alias and inplace_set == set([2, 3])
inplace_set |= set([4])
assert inplace_set is inplace_set_alias and inplace_set == set([2, 3, 4])
inplace_set ^= set([3, 5])
assert inplace_set is inplace_set_alias and inplace_set == set([2, 4, 5])
inplace_set -= set([4])
assert inplace_set is inplace_set_alias and inplace_set == set([2, 5])

letters = set("abca")
assert letters == set(["a", "b", "c"])

keyed = {frozenset([1, 2]): "ok"}
assert keyed[frozenset([2, 1])] == "ok"

generated = set(x for x in [1, 1, 2, 3])
assert generated == set([1, 2, 3])
comprehended = set([x for x in [1, 2, 2, 3]])
assert comprehended == set([1, 2, 3])
set_comprehended = {x for x in [1, 2, 2, 3]}
assert set_comprehended == set([1, 2, 3])


class SetSubclass(set):
    pass


class FrozenSetSubclass(frozenset):
    pass


set_subclass = SetSubclass([1, 2])
assert type(set_subclass) is SetSubclass
assert repr(set_subclass) == "SetSubclass([1, 2])"
assert type(set_subclass.copy()) is SetSubclass
assert type(set_subclass.union([3])) is SetSubclass
assert type(set_subclass.intersection([2])) is SetSubclass

frozen_subclass = FrozenSetSubclass([1, 2])
assert type(frozen_subclass) is FrozenSetSubclass
assert list(frozen_subclass) == [1, 2]
assert repr(frozen_subclass) == "FrozenSetSubclass([1, 2])"
assert type(frozen_subclass.copy()) is FrozenSetSubclass
assert type(frozen_subclass.union([3])) is FrozenSetSubclass
assert type(frozen_subclass.intersection([2])) is FrozenSetSubclass

for set_subclass_type in (SetSubclass, FrozenSetSubclass):
    try:
        object.__new__(set_subclass_type)
    except TypeError:
        pass
    else:
        raise AssertionError("object.__new__ created an unsafe set subtype")
