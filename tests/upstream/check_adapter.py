"""Check the assertion adapter independently of the vendored corpus."""

import assertions


def expect_failure(function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except AssertionError:
        return
    raise RuntimeError("assertion adapter accepted an invalid result")


def throw_value_error():
    raise ValueError("sentinel")


case = assertions.TestCase()
for method, good, bad in [
    (case.assertTrue, (True,), (False,)),
    (case.assertFalse, (False,), (True,)),
    (case.assertEqual, (1, 1), (1, 2)),
    (case.assertNotEqual, (1, 2), (1, 1)),
    (case.assertIs, (None, None), (None, False)),
    (case.assertIsNot, (None, False), (None, None)),
    (case.assertIn, (1, [1]), (2, [1])),
    (case.assertNotIn, (2, [1]), (1, [1])),
    (case.assertIsInstance, (1, int), ("a", int)),
    (case.assertNotIsInstance, ("a", int), (1, int)),
    (case.assertAlmostEqual, (1.0, 1.0 + 1e-9), (1.0, 2.0)),
]:
    method(*good)
    expect_failure(method, *bad)

case.assertAlmostEqual(1.0, 1.05, delta=0.1)
expect_failure(case.assertAlmostEqual, 1.0, 1.2, delta=0.1)
context = case.assertRaises(ValueError, throw_value_error)
assert str(context.exception) == "sentinel"
with case.assertRaises(ValueError) as context:
    throw_value_error()
assert str(context.exception) == "sentinel"
expect_failure(case.assertRaises, ValueError, lambda: None)
try:
    case.assertRaises(TypeError, throw_value_error)
except ValueError:
    pass
else:
    raise RuntimeError("assertion adapter swallowed an unexpected exception")


def check_string_locals():
    value = 0
    with_context = assertions.raises(ZeroDivisionError, "1 // value")
    assert isinstance(with_context.value, ZeroDivisionError)


check_string_locals()
function = lambda: None
assert assertions.skipIf(False, "unused")(function) is function
assert assertions.skipUnless(True, "unused")(function) is function
assert assertions.skip("reason")(function).__unittest_skip_why__ == "reason"
try:
    case.skipTest("reason")
except assertions.SkipTest as error:
    assert str(error) == "reason"
else:
    raise RuntimeError("skipTest did not raise SkipTest")

# Equal and not-equal operators are intentionally independent in Python 2.
class DifferentOperators(object):
    def __eq__(self, other):
        return True

    def __ne__(self, other):
        return True


case.assertEqual(DifferentOperators(), None)
case.assertNotEqual(DifferentOperators(), None)

with case.assertRaisesRegexp(ValueError, "sentinel"):
    throw_value_error()


def check_literal_exception_pattern(pattern):
    with case.assertRaisesRegexp(ValueError, pattern):
        throw_value_error()


expect_failure(check_literal_exception_pattern, "missing")
case.assertRaises(NotImplementedError, case.assertRaisesRegexp, ValueError, "sent.*")
try:
    with case.assertRaisesRegexp(TypeError, "sentinel"):
        throw_value_error()
except ValueError:
    pass
else:
    raise RuntimeError("exception-pattern adapter swallowed the wrong exception")
print "upstream assertion adapter: OK"
