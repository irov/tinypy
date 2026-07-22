import sys

original_limit = sys.getrecursionlimit()
assert original_limit == 1000

try:
    sys.setrecursionlimit(0)
    assert False, "zero recursion limit"
except ValueError:
    pass

try:
    sys.setrecursionlimit("20")
    assert False, "non-integer recursion limit"
except TypeError:
    pass

sys.setrecursionlimit(20)
assert sys.getrecursionlimit() == 20

sys.setrecursionlimit(original_limit)
assert sys.getrecursionlimit() == original_limit
