"""Project-authored CPython 2.7 parity checks for the fields, text and
state of built-in exceptions.

Expectations are checked on CPython 2.7.18.
"""

import _weakref
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


def _deepest_frame(depth):
    try:
        return _deepest_frame(depth + 1)
    except RuntimeError:
        return depth


# Each handler below catches the error that the runtime raises in it, and
# makes no call, which would fail in the deepest frame.
def _key_error_frame(depth):
    try:
        return _key_error_frame(depth + 1)
    except RuntimeError:
        try:
            {}[depth]
        except KeyError:
            return depth


def _index_error_frame(depth):
    try:
        return _index_error_frame(depth + 1)
    except RuntimeError:
        try:
            [][depth]
        except IndexError:
            return depth


def _zero_division_frame(depth):
    try:
        return _zero_division_frame(depth + 1)
    except RuntimeError:
        try:
            depth / 0
        except ZeroDivisionError:
            return depth


def _name_error_frame(depth):
    try:
        return _name_error_frame(depth + 1)
    except RuntimeError:
        try:
            _undefined_name
        except NameError:
            return depth


def _attribute_error_frame(depth):
    try:
        return _attribute_error_frame(depth + 1)
    except RuntimeError:
        try:
            depth.missing
        except AttributeError:
            return depth


_BASE = 2.0


def _overflow_error_frame(depth):
    try:
        return _overflow_error_frame(depth + 1)
    except RuntimeError:
        try:
            _BASE ** 5000
        except OverflowError:
            return depth


def _format_error_frame(depth):
    try:
        return _format_error_frame(depth + 1)
    except RuntimeError:
        try:
            '%y' % depth
        except ValueError:
            return depth


class _Referent(object):
    pass


_DEAD_PROXY = _weakref.proxy(_Referent())


def _reference_error_frame(depth):
    try:
        return _reference_error_frame(depth + 1)
    except RuntimeError:
        try:
            _DEAD_PROXY.missing
        except ReferenceError:
            return depth


class _Root:
    pass


class _Derived(_Root):
    pass


class _Other:
    pass


_PENDING = ValueError('pending')
_CLASSIC = _Derived()
_SLICE = slice(1, 2)


# The except clause of a tuple takes no recursion level, so in the deepest
# frame it still matches, or lets the exception pass, unchanged. Classic
# classes match there without the __subclasscheck__ call after which CPython
# leaves its recursion error pending.
def _tuple_clause_frame(depth, raised, candidates):
    try:
        return _tuple_clause_frame(depth + 1, raised, candidates)
    except RuntimeError:
        try:
            raise raised
        except candidates:
            return depth


def _escaped(operation, *args):
    try:
        operation(*args)
    except:
        return sys.exc_info()[1]


# __class__ and the members of slice are descriptors that take no level.
def _attributes_frame(depth, instance):
    try:
        return _attributes_frame(depth + 1, instance)
    except RuntimeError as error:
        return depth, error.__class__, instance.__class__, _SLICE.start


def _classic_checks(instance, derived, root, other):
    results = [isinstance(instance, root), issubclass(derived, root)]
    try:
        raise instance
    except (other, root):
        results.append('caught')
    return results


def _class_released_at_the_limit(released):
    try:
        _class_released_at_the_limit(released)
    except RuntimeError:
        transient = type('Transient', (object,), {})
        del transient
        released.append(True)


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

    def test_classic_lookup_compares_keys_at_the_depth_of_its_caller(self):
        class Name(object):
            def __hash__(self):
                return hash('target')

            def __eq__(self, other):
                return other == 'target'

        class Base:
            pass

        Base.__dict__[Name()] = 'found'
        derived = Base
        for index in range(40):
            derived = type(Base)('Derived%d' % index, (derived,), {})
        self.assertEqual(_limited(lambda: getattr(derived, 'target', 'missing'), 20), 'found')

    def test_classic_subclass_checks_take_no_recursion_level(self):
        class Root:
            pass

        class Other:
            pass

        derived = Root
        for index in range(40):
            derived = type(Root)('Derived%d' % index, (derived, Other), {})
        results = _limited(_classic_checks, 12, derived(), derived, Root, Other)
        sys.exc_clear()
        self.assertEqual((results, len([1, 2])), ([True, True, 'caught'], 2))

    def test_runtime_error_raised_in_the_deepest_frame_is_caught_one_frame_up(self):
        # The runtime raises an error as its class and arguments, which the
        # handler normalizes with a call to the class; in the deepest frame
        # that call fails, so the handler sees RuntimeError instead.
        deepest = _limited(_deepest_frame, 20, 0)
        frames = (_key_error_frame, _index_error_frame, _zero_division_frame, _name_error_frame, _attribute_error_frame,
                  _overflow_error_frame, _format_error_frame, _reference_error_frame)
        self.assertEqual([_limited(frame, 20, 0) for frame in frames], [deepest - 1] * len(frames))
        sys.exc_clear()

    def test_tuple_clause_in_the_deepest_frame_keeps_the_exception(self):
        deepest = _limited(_deepest_frame, 20, 0)
        matching = [(_Other, _Root), (_Other, (_Other, (_Root,)))]
        self.assertEqual([_limited(_tuple_clause_frame, 20, 0, _CLASSIC, candidates) for candidates in matching], [deepest] * 2)
        escaping = [(_PENDING, (KeyError, IndexError)), (_CLASSIC, (LookupError, KeyError))]
        self.assertEqual([_escaped(_limited, _tuple_clause_frame, 20, 0, raised, candidates) for raised, candidates in escaping], [_PENDING, _CLASSIC])
        sys.exc_clear()

    def test_class_and_members_read_in_the_deepest_frame(self):
        class Instance(object):
            pass

        deepest = _limited(_deepest_frame, 20, 0)
        self.assertEqual(_limited(_attributes_frame, 20, 0, Instance()), (deepest, RuntimeError, Instance, 1))
        sys.exc_clear()

    def test_class_released_in_the_deepest_frame_reports_nothing(self):
        # The bases list a class through weak references without a callback,
        # whose call would fail there and report the error on stderr, which
        # the case compares.
        released = []
        _limited(_class_released_at_the_limit, 20, released)
        sys.exc_clear()
        self.assertEqual(released, [True])

if __name__ == '__main__':
    unittest.main()
