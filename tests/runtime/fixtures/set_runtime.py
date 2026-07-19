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
