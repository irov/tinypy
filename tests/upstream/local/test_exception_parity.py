"""Project-authored CPython 2.7 parity checks for the fields, text and
state of built-in exceptions.

Expectations are checked on CPython 2.7.18.
"""

import sys
import unittest


def _nested(cls, depth):
    exception = cls('leaf')
    for index in range(depth):
        exception = cls(exception)
    return exception


def _recursion_message(operation, *args):
    try:
        operation(*args)
    except RuntimeError as error:
        return str(error)
    raise AssertionError('operation must exceed the recursion limit')


def _limited(operation, headroom, *args):
    """Runs operation with the recursion limit a few levels above the frames
    of the caller, so that the limit rather than the native stack stops it."""
    frame = sys._getframe(1)
    depth = 0
    while frame is not None:
        depth += 1
        frame = frame.f_back
    limit = sys.getrecursionlimit()
    sys.setrecursionlimit(depth + headroom)
    try:
        return operation(*args)
    finally:
        sys.setrecursionlimit(limit)


class ExceptionParity(unittest.TestCase):
    def test_located_syntax_error_has_an_empty_message(self):
        cases = [
            ('(', 'SyntaxError', 'unexpected EOF while parsing', ('f.py', 1, 2, '(\n')),
            ('a b', 'SyntaxError', 'invalid syntax', ('f.py', 1, 3, 'a b\n')),
            ('return 1', 'SyntaxError', "'return' outside function", ('f.py', 1, None, None)),
            ('x = yield', 'SyntaxError', "'yield' outside function", ('f.py', 1, None, None)),
            ('\tif 1:\n        pass', 'IndentationError', 'unexpected indent', ('f.py', 1, 1, '\tif 1:\n')),
            ('x = 1\nfrom __future__ import division\n', 'SyntaxError', 'from __future__ imports must occur at the beginning of the file', ('f.py', 2, None, None)),
        ]
        for source, name, message, location in cases:
            try:
                compile(source, 'f.py', 'exec')
            except SyntaxError as error:
                self.assertEqual(type(error).__name__, name)
                self.assertEqual(error.message, '')
                self.assertEqual(error.args, (message, location))
                self.assertEqual((error.msg, error.filename, error.lineno, error.offset, error.text), (message,) + location)
                self.assertEqual(str(error), '%s (f.py, line %d)' % (message, location[1]))
            else:
                self.fail('%r must not compile' % source)

    def test_syntax_error_located_after_raising_keeps_its_message(self):
        cases = [
            ('from __future__ import braces', 'not a chance', 1),
            ('def f(a, a): pass', "duplicate argument 'a' in function definition", 1),
            ('def f():\n    yield 1\n    return 2\n', "'return' with argument inside generator", 3),
        ]
        for source, message, line in cases:
            try:
                compile(source, 'f.py', 'exec')
            except SyntaxError as error:
                self.assertEqual(error.message, message)
                self.assertEqual(error.args, (message,))
                self.assertEqual((error.msg, error.filename, error.lineno, error.offset), (message, 'f.py', line, None))
            else:
                self.fail('%r must not compile' % source)

    def test_environment_error_str_reads_errno_after_the_filename_repr(self):
        class Name(object):
            def __init__(self, error):
                self.error = error

            def __repr__(self):
                error = self.error
                self.error = None
                error.errno = 2
                error.strerror = 'No such file or directory'
                return "'data.txt'"
        error = IOError(None, None, None)
        error.filename = Name(error)
        self.assertEqual(str(error), "[Errno 2] No such file or directory: 'data.txt'")

        class Failing(object):
            def __repr__(self):
                raise KeyError('repr')
        error = OSError(1, 'denied', Failing())
        self.assertRaises(KeyError, str, error)
        self.assertEqual(str(EnvironmentError(1, 'denied')), '[Errno 1] denied')
        self.assertEqual(str(EnvironmentError(1)), '1')

    def test_setstate_names_the_type_of_a_key_that_is_no_string(self):
        for state, name in (({1: 2}, 'int'), ({None: 1}, 'NoneType'), ({(1,): 2}, 'tuple')):
            error = ValueError()
            try:
                error.__setstate__(state)
            except TypeError as failure:
                self.assertEqual(str(failure), "attribute name must be string, not '%s'" % name)
            else:
                self.fail('a key that is no string must fail')
        error = ValueError()
        error.__setstate__({'note': 1, u'other': 2})
        self.assertEqual((error.note, error.other), (1, 2))

    def test_recursion_in_str_and_repr_names_the_conversion(self):
        deep = _nested(ValueError, 3000)
        self.assertEqual(_recursion_message(str, deep), 'maximum recursion depth exceeded while getting the str of an object')
        self.assertEqual(_recursion_message(lambda: '%s' % (deep,)), 'maximum recursion depth exceeded while getting the str of an object')
        self.assertEqual(_recursion_message(lambda: '%r' % (deep,)), 'maximum recursion depth exceeded while getting the repr of an object')
        self.assertEqual(_recursion_message(repr, deep), 'maximum recursion depth exceeded while getting the repr of an object')
        syntax = _nested(SyntaxError, 3000)
        self.assertEqual(_recursion_message(str, syntax), 'maximum recursion depth exceeded while getting the str of an object')
        own = ValueError()
        own.args = (own,)
        self.assertEqual(_recursion_message(str, own), 'maximum recursion depth exceeded while getting the str of an object')
        own.args = ()

    def test_builtin_method_under_another_slot_name_is_called(self):
        class Representation(object):
            __repr__ = object.__str__
        class Unicode(Exception):
            __str__ = Exception.__unicode__
        class Swapped(Exception):
            __repr__ = Exception.__unicode__
            __str__ = object.__str__
        class Classic:
            __repr__ = object.__str__
        class Inherited(Classic):
            pass
        cases = ((str, Representation()), (repr, Representation()), (unicode, Representation()),
                 (lambda value: '%s' % (value,), Representation()), (unicode, Unicode(1)), (str, Swapped(1)),
                 (repr, Classic()), (repr, Inherited()))
        for operation, value in cases:
            message = _limited(_recursion_message, 60, operation, value)
            self.assertEqual(message, 'maximum recursion depth exceeded while calling a Python object')
        sys.exc_clear()

    def test_classic_lookup_takes_no_recursion_level(self):
        class Base:
            value = 1
        derived = Base
        for index in range(100):
            derived = type(Base)('Derived%d' % index, (derived,), {})
        self.assertEqual(_limited(lambda: (derived.value, derived().value, hasattr(derived, 'missing')), 20), (1, 1, False))

if __name__ == '__main__':
    unittest.main()
