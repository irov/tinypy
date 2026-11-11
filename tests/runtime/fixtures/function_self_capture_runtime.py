# The compiler rejects the source before any recursive closure is allocated.
from _weakref import ref

sources = (
    ("def outer():\n    def walk(): return walk\n    return walk()\n", "walk"),
    ("def outer(flag):\n    walk, = (lambda: walk,) if flag else (None,)\n", "lambda"),
    ("def outer(flag):\n    [walk] = flag and [lambda: walk]\n", "lambda"),
    ("def outer(flag):\n    walk, = flag or (lambda: walk,)\n", "lambda"),
)

for source, function_name in sources:
    message = "local function '%s' captures its own binding 'walk'; use a method or pass the function explicitly" % function_name
    for unused in xrange(64):
        try:
            compile(source, "capture.py", "exec")
        except SyntaxError as error:
            assert error.msg == message
            assert error.filename == "capture.py"
            assert error.lineno == 2
        else:
            raise AssertionError("self capture compiled")

    try:
        exec source
    except SyntaxError as error:
        assert error.msg == message
    else:
        raise AssertionError("exec self capture compiled")

# Explicit recursion and ordinary closures retain normal ownership.
def factorial(n):
    return n * factorial(n - 1) if n else 1

class Owner(object):
    def factorial(self, n):
        return n * self.factorial(n - 1) if n else 1

    @staticmethod
    def static_factorial(n):
        return n * Owner.static_factorial(n - 1) if n else 1

def make_explicit():
    def walk(callback, n):
        return n * callback(callback, n - 1) if n else 1
    return walk(walk, 5)

def make_callback(value):
    def callback(extra):
        return value + extra
    return callback

assert factorial(5) == 120
assert Owner().factorial(5) == 120
assert Owner.static_factorial(5) == 120
assert make_explicit() == 120
callback = make_callback(40)
reference = ref(callback)
assert callback(2) == 42
del callback
assert reference() is None
print "function_self_capture_runtime_ok"
