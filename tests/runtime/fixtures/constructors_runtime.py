assert bool() is False
assert bool([1]) is True
assert int() == 0
assert int(True) == 1
assert int("255") == 255
assert int("ff", 16) == 255
assert int("0b101", 0) == 5
assert long("12345678901234567890") == 12345678901234567890L
assert float() == 0.0
assert float("1.25") == 1.25
assert complex() == 0j
assert complex(2, 3) == 2 + 3j
assert str() == ""
assert str(42) == "42"
assert unicode() == u""
assert unicode("text") == u"text"
assert unicode("caf\xc3\xa9", "utf-8") == u"caf\xe9"
assert unicode("a\xff", "ascii", "ignore") == u"a"
assert list() == []
assert list((1, 2, 3)) == [1, 2, 3]
assert list("ab") == ["a", "b"]
assert tuple() == ()
assert tuple([1, 2]) == (1, 2)
assert dict() == {}
assert dict([("a", 1), ("b", 2)]) == {"a": 1, "b": 2}
assert dict(answer=42) == {"answer": 42}

value = object()
assert type(value) is object
Dynamic = type("Dynamic", (object,), {"answer": 42})
assert Dynamic.answer == 42


class FalseByNonzero(object):
    def __nonzero__(self):
        return False


class FalseByLength(object):
    def __len__(self):
        return 0


assert bool(FalseByNonzero()) is False
assert bool(FalseByLength()) is False
