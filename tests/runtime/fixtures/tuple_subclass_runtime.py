class Record(tuple):
    def __new__(cls, first, second, name=None):
        self = tuple.__new__(cls, (first, second))
        self.name = name
        return self


record = Record(10, 20, "pair")
assert isinstance(record, tuple), "isinstance"
assert type(record) is Record, "type"
assert len(record) == 2, "length"
assert record[0] == 10, "first"
assert record[1] == 20, "second"
assert record == (10, 20), "equal"
assert hash(record) == hash((10, 20)), "hash"
assert record.name == "pair", "name"
record.extra = 30
assert record.__dict__ == {"name": "pair", "extra": 30}, "dict"

empty = Record.__new__(Record, 1, 2)
plain = tuple(empty)
assert plain == (1, 2), "plain equal"
assert type(plain) is tuple, "plain type"

try:
    class Invalid(tuple):
        __slots__ = ("value",)
except TypeError:
    pass
else:
    raise AssertionError("tuple subtype accepted nonempty slots")
