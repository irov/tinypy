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
