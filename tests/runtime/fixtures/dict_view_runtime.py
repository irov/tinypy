mapping = {"alpha": 1}
keys = mapping.viewkeys()
values = mapping.viewvalues()
items = mapping.viewitems()

assert type(keys) is not type(values)
assert type(values) is not type(items)
assert len(keys) == 1
assert "alpha" in keys
assert 1 in values
assert ("alpha", 1) in items

mapping["beta"] = 2
assert len(keys) == 2
assert sorted(keys) == ["alpha", "beta"]
assert sorted(values) == [1, 2]
assert sorted(items) == [("alpha", 1), ("beta", 2)]

del mapping["alpha"]
assert "alpha" not in keys
assert 1 not in values
assert ("alpha", 1) not in items

mapping = {1: "one", 2: "two"}
keys = mapping.viewkeys()
values = mapping.viewvalues()
items = mapping.viewitems()
assert repr(keys).startswith("dict_keys([")
assert repr(values).startswith("dict_values([")
assert repr(items).startswith("dict_items([")
assert keys == set([1, 2])
assert set([1, 2]) == keys
assert keys <= set([1, 2, 3])
assert keys < set([1, 2, 3])
assert keys >= set([1])
assert keys > set([1])
assert keys & set([2, 3]) == set([2])
assert set([2, 3]) & keys == set([2])
assert keys | set([3]) == set([1, 2, 3])
assert keys - set([2]) == set([1])
assert set([1, 3]) - keys == set([3])
assert keys ^ set([2, 3]) == set([1, 3])
assert items & set([(1, "one")]) == set([(1, "one")])
assert values != mapping.viewvalues()
for unhashable_view in (keys, items):
    try:
        hash(unhashable_view)
    except TypeError:
        pass
    else:
        raise AssertionError("set-like dict view unexpectedly hashable")
assert isinstance(hash(values), int)
