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
assert record.count(10) == 1, "count"
assert record.index(20) == 1, "index"
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

# hash() of values nested through bound methods recurses natively, as
# instancemethod_hash does; the native stack budget stops that recursion
# with RuntimeError where CPython would exhaust the stack.
class Holder(object):
    def method(self):
        pass


unbound = Holder.__dict__["method"]
nested = 1
for index in range(100):
    nested = unbound.__get__(Record(nested, index), Holder)
assert hash(nested) == hash(unbound.__get__(Record(nested.im_self[0], 99), Holder)), "nested method hash"
for index in range(100000):
    nested = unbound.__get__(Record(nested, index), Holder)
try:
    hash(nested)
except RuntimeError as error:
    assert str(error) == "maximum recursion depth exceeded", error
else:
    raise AssertionError("hash of deeply nested methods")
