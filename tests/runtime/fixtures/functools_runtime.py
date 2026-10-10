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


# Calling a partial holds its callable: resolving __call__ on the callable's
# type compares a colliding key, which replaces the partial state.
class CallCollision(object):
    armed = False

    def __hash__(self):
        return hash("__call__")

    def __eq__(self, other):
        if CallCollision.armed:
            CallCollision.armed = False
            replaced.__setstate__((len, (), None, None))
        return False


def called_with(self, *args):
    return ("called", type(self).__name__)


Callable = type("Callable", (object,), {CallCollision(): 1})
Callable.__call__ = called_with
replaced = partial(Callable())
Callable.unrelated = 1
CallCollision.armed = True
assert replaced() == ("called", "Callable")
assert replaced.func is len
