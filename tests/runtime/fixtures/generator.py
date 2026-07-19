def values():
    yield 1
    yield 2
    yield 3


generator = values()
first = next(generator)
second = next(generator)
third = next(generator)
try:
    next(generator)
except StopIteration:
    exhausted = True

comprehension = [value * 2 for value in values()]

finally_marker = 0


def with_finally():
    global finally_marker
    try:
        yield 40
    finally:
        finally_marker = 42


finally_generator = with_finally()
finally_value = next(finally_generator)
try:
    next(finally_generator)
except StopIteration:
    pass


def inside_exception():
    try:
        raise ValueError("bad")
    except ValueError as error:
        yield 41 + (error.args[0] == "bad")


exception_generator = inside_exception()
exception_value = next(exception_generator)
try:
    next(exception_generator)
except StopIteration:
    pass


def echo():
    incoming = yield 1
    yield incoming


send_generator = echo()
send_first = send_generator.next()
send_result = send_generator.send(42)


def catches_throw():
    try:
        yield "ready"
    except ValueError as error:
        yield error.args[0]


throw_generator = catches_throw()
throw_ready = next(throw_generator)
throw_result = throw_generator.throw(ValueError, "caught")

close_marker = 0


def closes_with_finally():
    global close_marker
    try:
        yield 1
    finally:
        close_marker = 42


close_generator = closes_with_finally()
next(close_generator)
close_result = close_generator.close()

if first != 1 or second != 2 or third != 3 or not exhausted:
    raise AssertionError("basic generator failed")
if comprehension != [2, 4, 6]:
    raise AssertionError("generator iteration failed")
if finally_value != 40 or finally_marker != 42:
    raise AssertionError("generator finally failed")
if exception_value != 42:
    raise AssertionError("generator exception state failed")
if send_first != 1 or send_result != 42:
    raise AssertionError("generator send failed")
if throw_ready != "ready" or throw_result != "caught":
    raise AssertionError("generator throw failed")
if close_result is not None or close_marker != 42:
    raise AssertionError("generator close failed")
