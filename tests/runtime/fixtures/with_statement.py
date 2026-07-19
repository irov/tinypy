class Context(object):
    def __init__(self, suppress):
        self.suppress = suppress
        self.entered = 0
        self.exited = 0
        self.exception_seen = False

    def __enter__(self):
        self.entered = 1
        return 40

    def __exit__(self, exception_type, exception_value, traceback):
        self.exited = 1
        self.exception_seen = exception_type is ValueError and exception_value.args[0] == "bad" and traceback is not None
        return self.suppress


normal_context = Context(False)
with normal_context as value:
    normal_result = value + 2

suppress_context = Context(True)
with suppress_context:
    raise ValueError("bad")
suppress_result = 42

nonsuppress_context = Context(False)
try:
    with nonsuppress_context:
        raise ValueError("bad")
except ValueError:
    nonsuppress_result = 42

return_context = Context(False)


def return_from_with():
    with return_context:
        return 42


return_result = return_from_with()

if normal_result != 42 or normal_context.entered != 1 or normal_context.exited != 1:
    raise AssertionError("normal context manager failed")
if suppress_result != 42 or suppress_context.exited != 1 or not suppress_context.exception_seen:
    raise AssertionError("exception suppression failed")
if nonsuppress_result != 42 or nonsuppress_context.exited != 1 or not nonsuppress_context.exception_seen:
    raise AssertionError("exception propagation failed")
if return_result != 42 or return_context.exited != 1:
    raise AssertionError("return cleanup failed")
