from _functools import partial, reduce
from _weakref import ref


def combine(a, b, c=0):
    return a + b + c


bound = partial(combine, 10, c=3)
assert isinstance(bound, partial)
assert bound.func is combine
assert bound.args == (10,)
assert bound.keywords == {"c": 3}
assert bound(4) == 17
assert bound(4, c=8) == 22
bound.label = "sum"
assert bound.__dict__ == {"label": "sum"}
assert ref(bound)() is bound

assert reduce(lambda left, right: left + right, [1, 2, 3, 4]) == 10
assert reduce(lambda left, right: left + right, [], 7) == 7
try:
    reduce(combine, [])
except TypeError:
    pass
else:
    raise AssertionError("empty reduce must fail without an initializer")
