# Operator dispatch, coercion, true division, long-to-float conversion and
# numeric error messages follow CPython 2.7.
import sys


def error_text(callable_object, *args):
    try:
        callable_object(*args)
    except Exception as error:
        return type(error).__name__ + ": " + str(error)
    raise AssertionError("no exception raised")


class LessThanOnly(object):
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return self.value < other.value


assert LessThanOnly(2) > LessThanOnly(1)
assert not (LessThanOnly(1) > LessThanOnly(2))
assert max([LessThanOnly(1), LessThanOnly(3), LessThanOnly(2)]).value == 3


class Adder(object):
    def __add__(self, other):
        return "Adder.add"

    def __radd__(self, other):
        return "Adder.radd"


class InheritedAdder(Adder):
    pass


class OverridingAdder(Adder):
    def __radd__(self, other):
        return "OverridingAdder.radd"


assert (Adder() + InheritedAdder(), Adder() + OverridingAdder(), InheritedAdder() + Adder()) == ("Adder.add", "OverridingAdder.radd", "Adder.add")


class Equal(object):
    def __eq__(self, other):
        return "Equal.eq"


class SubEqual(Equal):
    def __eq__(self, other):
        return "SubEqual.eq"


assert (Equal() == SubEqual(), SubEqual() == Equal(), Equal() == Equal()) == ("SubEqual.eq", "SubEqual.eq", "Equal.eq")


class NotImplementedAdder(object):
    def __add__(self, other):
        return NotImplemented

    def __radd__(self, other):
        return "radd"


class NotImplementedChild(NotImplementedAdder):
    pass


assert error_text(lambda: NotImplementedAdder() + NotImplementedAdder()) == "TypeError: unsupported operand type(s) for +: 'NotImplementedAdder' and 'NotImplementedAdder'"
assert NotImplementedAdder() + NotImplementedChild() == "radd"


class Coercing:
    def __init__(self, value):
        self.value = value

    def __coerce__(self, other):
        return (self.value, other)


assert (Coercing(7) % 3, 7 % Coercing(3)) == (1, 1)
assert (Coercing(2) ** 3, 2 ** Coercing(3)) == (8, 8)
assert (Coercing(5) >> 1, Coercing(6) & 3, 1 << Coercing(4), Coercing(6) | 1, Coercing(6) ^ 2) == (2, 2, 16, 7, 4)
assert (divmod(Coercing(7), 2), divmod(7, Coercing(2))) == ((3, 1), (3, 1))
assert (Coercing(7) // 2, 7 // Coercing(2)) == (3, 3)

namespace = {}
exec "from __future__ import division\nresult = [(2**53+1)/3, (10**400)/(10**399), 1/3, -7/2, 2**1100/2**1000, 1/(2**1100), (2**60+1)/(2**60), 7/7, -1/(2**1074), 3/(2**1075)]\n" in namespace
assert namespace["result"] == [3002399751580331.0, 10.0, 0.3333333333333333, -3.5, 1.2676506002282294e+30, 0.0, 1.0, 1.0, -5e-324, 1e-323]


def true_division_overflow():
    exec "from __future__ import division\nresult = (2**1024)/1\n" in {}


def true_division_zero():
    exec "from __future__ import division\nresult = 1/0\n" in {}


assert error_text(true_division_overflow) == "OverflowError: integer division result too large for a float"
assert error_text(true_division_zero) == "ZeroDivisionError: division by zero"

assert (divmod(7, 2), divmod(7.5, 2), divmod(-7, 2), divmod(2 ** 70, 3), divmod(-7.5, 2)) == ((3, 1), (3.0, 1.5), (-4, 1), (393530540239137101141L, 1L), (-4.0, 0.5))
assert error_text(divmod, "a", 2) == "TypeError: unsupported operand type(s) for divmod(): 'str' and 'int'"


class FloorOnly(object):
    def __floordiv__(self, other):
        return 1

    def __mod__(self, other):
        return 2


assert error_text(divmod, FloorOnly(), 2) == "TypeError: unsupported operand type(s) for divmod(): 'FloorOnly' and 'int'"
assert (0 ** (2 ** 100), 1 ** (2 ** 100), (-1) ** (2 ** 100), (-1) ** (2 ** 100 + 1), 0 ** 0, 0L ** (2 ** 100), (-1L) ** (2 ** 100 + 1)) == (0L, 1L, 1L, -1L, 1, 0L, -1L)


class IntSubclass(int):
    pass


class LongSubclass(long):
    pass


class FloatSubclass(float):
    pass


assert [type(value).__name__ for value in (+IntSubclass(5), abs(IntSubclass(-5)), -IntSubclass(5), +LongSubclass(5), abs(FloatSubclass(-1.5)), abs(IntSubclass(5)))] == ["int", "int", "int", "long", "float", "int"]
assert error_text(lambda: +"a") == "TypeError: bad operand type for unary +: 'str'"
assert error_text(lambda: -"a") == "TypeError: bad operand type for unary -: 'str'"
assert error_text(lambda: ~1.5) == "TypeError: bad operand type for unary ~: 'float'"
assert error_text(abs, "a") == "TypeError: bad operand type for abs(): 'str'"
assert abs(complex(1e308, 1e308)) == 1.4142135623730951e+308
assert abs(complex(3, 4)) == 5.0
assert (int.__add__(1, 2L), long.__add__(2L, 1), float.__add__(1.0, 2), int.__add__(1, 2.0), int.__add__(1, True)) == (NotImplemented, 3L, 3.0, NotImplemented, 2)

assert (1 < 2.5, 2 < 2.0, 2 <= 2.0, -3 < -2.5, 2 ** 63 < 1e19, 2 ** 64 > 1e19, 3 == 3.0, 2 ** 53 + 1 == 2.0 ** 53, -2 ** 63 < -9.3e18, 5 > float("inf"), -5 > float("-inf")) == (True, False, True, True, True, True, True, False, False, False, True)
assert error_text(lambda: "\xe9" < u"a") == "UnicodeDecodeError: 'ascii' codec can't decode byte 0xe9 in position 0: ordinal not in range(128)"
assert ("a" < u"b", u"a" < "b", "b" > u"a") == (True, True, True)

assert error_text(lambda: 1 + "a") == "TypeError: unsupported operand type(s) for +: 'int' and 'str'"
assert error_text(lambda: "a" - 1) == "TypeError: unsupported operand type(s) for -: 'str' and 'int'"
assert error_text(lambda: [] * "a") == "TypeError: can't multiply sequence by non-int of type 'str'"
assert error_text(lambda: 1 / "a") == "TypeError: unsupported operand type(s) for /: 'int' and 'str'"
assert error_text(lambda: 1 // "a") == "TypeError: unsupported operand type(s) for //: 'int' and 'str'"
assert error_text(lambda: 1 % []) == "TypeError: unsupported operand type(s) for %: 'int' and 'list'"
assert error_text(lambda: 1 ** "a") == "TypeError: unsupported operand type(s) for ** or pow(): 'int' and 'str'"
assert error_text(lambda: 1 << "a") == "TypeError: unsupported operand type(s) for <<: 'int' and 'str'"
assert error_text(lambda: 1 & "a") == "TypeError: unsupported operand type(s) for &: 'int' and 'str'"
assert error_text(lambda: set([1]) | [2]) == "TypeError: unsupported operand type(s) for |: 'set' and 'list'"

assert float(2 ** 53 + 1) == 2.0 ** 53
assert float(2 ** 54 + 2) == 2.0 ** 54
assert float(2 ** 54 + 6) == 2.0 ** 54 + 8
assert float(2 ** 60 + 2 ** 7) == 2.0 ** 60
assert float(2 ** 60 + 2 ** 7 + 1) == 2.0 ** 60 + 256
assert float(-(2 ** 64 - 1)) == -2.0 ** 64
assert float(2 ** 1023 + 2 ** 970) == 2.0 ** 1023
assert float(2 ** 1024 - 2 ** 971) == sys.float_info.max
assert error_text(float, 2 ** 1024) == "OverflowError: long int too large to convert to float"
assert error_text(float, 2 ** 1024 - 2 ** 970) == "OverflowError: long int too large to convert to float"
