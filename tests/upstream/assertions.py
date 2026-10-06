"""Small Python 2 assertion adapter for upstream tests, without stdlib I/O."""

import sys


class SkipTest(Exception):
    pass


class Raises(object):
    def __init__(self, expected):
        self.expected = expected
        self.value = None
        self.exception = None

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        if kind is None:
            raise AssertionError("expected %r to be raised" % (self.expected,))
        if not issubclass(kind, self.expected):
            return False
        self.value = self.exception = value
        return True


def raises(expected, function=None, *args, **kwargs):
    context = Raises(expected)
    if function is None:
        return context
    if isinstance(function, basestring):
        frame = sys._getframe(1)
        with context:
            exec function in frame.f_globals, frame.f_locals
    else:
        with context:
            function(*args, **kwargs)
    return context


class RaisesLiteralPattern(Raises):
    """The literal-pattern subset needed by the portable syntax tests."""

    def __init__(self, expected, pattern):
        Raises.__init__(self, expected)
        if not isinstance(pattern, basestring) or any(char in pattern for char in ".^$*+?{}[]\\|()"):
            raise NotImplementedError("the adapter accepts literal exception patterns only")
        self.pattern = pattern

    def __exit__(self, kind, value, traceback):
        handled = Raises.__exit__(self, kind, value, traceback)
        if handled and self.pattern not in str(value):
            raise AssertionError("exception text does not contain %r: %s" % (self.pattern, value))
        return handled


def fail(message="test failed"):
    raise AssertionError(message)


def skip(reason):
    def decorate(function):
        function.__unittest_skip__ = True
        function.__unittest_skip_why__ = reason
        return function
    return decorate


def skipIf(condition, reason):
    return skip(reason) if condition else lambda function: function


def skipUnless(condition, reason):
    return skipIf(not condition, reason)


class TestCase(object):
    """Only assertions used by the imported corpus; unsupported APIs fail."""

    def __init__(self, methodName="runTest"):
        self._testMethodName = methodName

    def setUp(self):
        pass

    def tearDown(self):
        pass

    def fail(self, msg="test failed"):
        fail(msg)

    def skipTest(self, reason):
        raise SkipTest(reason)

    def assertTrue(self, value, msg=None):
        if not value:
            self.fail(msg or "%r is not true" % (value,))

    def assertFalse(self, value, msg=None):
        if value:
            self.fail(msg or "%r is not false" % (value,))

    def assertEqual(self, actual, expected, msg=None):
        if not actual == expected:
            self.fail(msg or "%r != %r" % (actual, expected))

    def assertNotEqual(self, actual, expected, msg=None):
        if not actual != expected:
            self.fail(msg or "%r == %r" % (actual, expected))

    def assertIs(self, actual, expected, msg=None):
        if actual is not expected:
            self.fail(msg or "%r is not %r" % (actual, expected))

    def assertIsNot(self, actual, expected, msg=None):
        if actual is expected:
            self.fail(msg or "%r is %r" % (actual, expected))

    def assertIn(self, member, container, msg=None):
        if member not in container:
            self.fail(msg or "%r not in %r" % (member, container))

    def assertNotIn(self, member, container, msg=None):
        if member in container:
            self.fail(msg or "%r in %r" % (member, container))

    def assertIsInstance(self, value, kind, msg=None):
        if not isinstance(value, kind):
            self.fail(msg or "%r is not an instance of %r" % (value, kind))

    def assertNotIsInstance(self, value, kind, msg=None):
        if isinstance(value, kind):
            self.fail(msg or "%r is an instance of %r" % (value, kind))

    def assertAlmostEqual(self, actual, expected, places=7, msg=None, delta=None):
        if actual == expected:
            return
        difference = abs(actual - expected)
        if delta is not None:
            matches = difference <= delta
        else:
            matches = round(difference, places) == 0
        if not matches:
            self.fail(msg or "%r != %r within tolerance" % (actual, expected))

    def assertRaises(self, expected, function=None, *args, **kwargs):
        return raises(expected, function, *args, **kwargs)

    def assertRaisesRegexp(self, expected, pattern):
        return RaisesLiteralPattern(expected, pattern)
