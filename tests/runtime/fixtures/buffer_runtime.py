source = "abcdef"
view = buffer(source, 1, 4)

assert type(view) is buffer
assert str(view) == "bcde"
assert len(view) == 4
assert bool(view) is True
assert view[0] == "b"
assert view[-1] == "e"
assert view[1:3] == "cd"
assert view[::-1] == "edcb"
assert list(view) == ["b", "c", "d", "e"]
assert hash(view) == hash("bcde")
assert str(buffer(view, 1, 2)) == "cd"
assert repr(Ellipsis) == "Ellipsis"
assert bool(Ellipsis) is True
