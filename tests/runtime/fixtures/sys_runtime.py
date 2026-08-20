import sys

original_limit = sys.getrecursionlimit()
assert original_limit == 1000
assert sys.maxunicode == 0x10ffff

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


def recursive_call():
    recursive_call()


try:
    recursive_call()
    assert False, "recursion limit was not enforced"
except RuntimeError:
    pass

sys.setrecursionlimit(original_limit)
assert sys.getrecursionlimit() == original_limit
