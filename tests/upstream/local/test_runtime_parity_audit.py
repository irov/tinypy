"""Project-authored CPython 2.7 parity checks for the order in which built-in
values release what they own, argument errors, codec inputs, membership
fallbacks and recursion errors.

Expectations are checked on CPython 2.7.18.
"""

import _codecs
import _functools
import sys
import unittest


_released = []


class _Tracked(object):
    def __init__(self, name):
        self.name = name

    def __del__(self):
        _released.append(self.name)


def _release_order(make):
    del _released[:]
    value = make()
    value = None
    order = list(_released)
    del _released[:]
    return order


def _error(operation, *args, **kwargs):
    try:
        operation(*args, **kwargs)
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError('operation must fail')


class _UnicodeResult(unicode):
    pass


def _decode_kind(data, errors='strict'):
    return _UnicodeResult(u'%s:%s:%s' % (type(data).__name__, str(data), errors)), len(data)


def _decode_number(data, errors='strict'):
    return 5, len(data)


def _encode_kind(data, errors='strict'):
    return type(data).__name__, len(data)


def _search_codec(name):
    if name == 'runtime_parity_kind':
        return _encode_kind, _decode_kind, None, None
    if name == 'runtime_parity_number':
        return _encode_kind, _decode_number, None, None
    return None


_codecs.register(_search_codec)


def _recurse_raising(depth):
    try:
        raise ValueError(depth)
    except ValueError:
        return _recurse_raising(depth + 1)


class RuntimeParityAudit(unittest.TestCase):
    def test_slice_releases_its_step_first(self):
        order = _release_order(lambda: slice(_Tracked('start'), _Tracked('stop'), _Tracked('step')))
        self.assertEqual(order, ['step', 'start', 'stop'])

    def test_exceptions_release_their_fields_before_their_arguments(self):
        self.assertEqual(_release_order(lambda: EnvironmentError(_Tracked('a'), _Tracked('b'))), ['b', 'a'])
        self.assertEqual(_release_order(lambda: IOError(_Tracked('errno'), _Tracked('strerror'), _Tracked('filename'))), ['filename', 'strerror', 'errno'])
        self.assertEqual(_release_order(lambda: SyntaxError(_Tracked('m'), (_Tracked('f'), _Tracked('l'), _Tracked('o'), _Tracked('t')))), ['t', 'o', 'l', 'f', 'm'])

        def environment():
            error = EnvironmentError(_Tracked('a'), _Tracked('b'))
            error.filename = _Tracked('f')
            error.errno = _Tracked('e')
            return error

        def system_exit():
            error = SystemExit(_Tracked('a'))
            error.code = _Tracked('c')
            return error

        def unicode_error():
            error = UnicodeDecodeError('a', 'b', 0, 1, 'c')
            error.encoding = _Tracked('encoding')
            error.object = _Tracked('object')
            error.reason = _Tracked('reason')
            error.extra = _Tracked('dict')
            return error

        class Slotted(IOError):
            __slots__ = ('slot',)

        def slotted():
            error = Slotted(_Tracked('errno'), _Tracked('strerror'), _Tracked('filename'))
            error.slot = _Tracked('slot')
            error.extra = _Tracked('dict')
            return error

        self.assertEqual(_release_order(environment), ['e', 'f', 'b', 'a'])
        self.assertEqual(_release_order(system_exit), ['c', 'a'])
        self.assertEqual(_release_order(unicode_error), ['encoding', 'object', 'reason', 'dict'])
        self.assertEqual(_release_order(slotted), ['slot', 'filename', 'dict', 'strerror', 'errno'])

    def test_missing_required_argument_is_reported_before_an_unknown_keyword(self):
        message = "Required argument 'sequence' (pos 1) not found"
        for structure in (type(sys.long_info), type(sys.float_info)):
            self.assertEqual(_error(structure, seq=(1, 2)), ('TypeError', message))
            self.assertEqual(_error(structure, dict={}), ('TypeError', message))
            self.assertEqual(_error(structure, sequence=(1, 2), bogus=1), ('TypeError', "'bogus' is an invalid keyword argument for this function"))
            self.assertEqual(_error(structure, seq=1, dict={}, other=2), ('TypeError', 'structseq() takes at most 2 arguments (3 given)'))
        message = "Required argument 'object' (pos 1) not found"
        self.assertEqual(_error(sys.getsizeof), ('TypeError', message))
        self.assertEqual(_error(sys.getsizeof, obj=1), ('TypeError', message))
        self.assertEqual(_error(sys.getsizeof, default=1), ('TypeError', message))
        self.assertEqual(_error(sys.getsizeof, 1, object=2), ('TypeError', "Argument given by name ('object') and position (1)"))
        self.assertEqual(sys.getsizeof(object=1, default=None) > 0, True)

    def test_exception_methods_word_arity_errors_as_their_wrappers(self):
        error = Exception(1, 2)
        self.assertEqual(_error(Exception.__getitem__, error), ('TypeError', 'expected 1 arguments, got 0'))
        self.assertEqual(_error(error.__getitem__, x=1), ('TypeError', "wrapper __getitem__ doesn't take keyword arguments"))
        self.assertEqual(_error(error.__getslice__, 1), ('TypeError', 'function takes exactly 2 arguments (1 given)'))
        self.assertEqual(_error(error.__str__, 1), ('TypeError', 'expected 0 arguments, got 1'))
        self.assertEqual(_error(error.__repr__, 1, 2), ('TypeError', 'expected 0 arguments, got 2'))
        self.assertEqual(_error(error.__unicode__, 1), ('TypeError', '__unicode__() takes no arguments (1 given)'))
        self.assertEqual(_error(error.__reduce__, x=1), ('TypeError', '__reduce__() takes no keyword arguments'))
        self.assertEqual(_error(error.__setstate__), ('TypeError', '__setstate__() takes exactly one argument (0 given)'))
        self.assertEqual(_error(EnvironmentError(1, 2).__reduce__, 1), ('TypeError', '__reduce__() takes no arguments (1 given)'))
        self.assertEqual(_error(KeyError.__str__, KeyError(1), 1), ('TypeError', 'expected 0 arguments, got 1'))

        class Swapped(Exception):
            __str__ = Exception.__getitem__

        self.assertEqual(_error(str, Swapped(1)), ('TypeError', 'expected 1 arguments, got 0'))

    def test_generator_repr_words_arity_errors_as_its_wrapper(self):
        def generator():
            yield 1

        iterator = generator()
        self.assertEqual(_error(type(iterator).__repr__, iterator, 1), ('TypeError', 'expected 0 arguments, got 1'))
        self.assertEqual(_error(iterator.__repr__, x=1), ('TypeError', "wrapper __repr__ doesn't take keyword arguments"))

    def test_partial_names_its_type_and_words_method_arity_errors(self):
        class Partial(_functools.partial):
            pass

        message = "type 'partial' takes at least one argument"
        self.assertEqual(_error(_functools.partial), ('TypeError', message))
        self.assertEqual(_error(_functools.partial, func=len), ('TypeError', message))
        self.assertEqual(_error(Partial), ('TypeError', message))
        value = _functools.partial(len)
        self.assertEqual(_error(value.__reduce__, 1), ('TypeError', '__reduce__() takes no arguments (1 given)'))
        self.assertEqual(_error(value.__setstate__), ('TypeError', '__setstate__() takes exactly one argument (0 given)'))
        self.assertEqual(_error(value.__setstate__, x=1), ('TypeError', '__setstate__() takes no keyword arguments'))

    def test_type_checks_run_the_default_rules(self):
        class Classic:
            pass

        class Based(object):
            __bases__ = (int,)

        class Proxy(object):
            __class__ = property(lambda self: int)

        self.assertEqual(type.__subclasscheck__(int, bool), True)
        self.assertEqual(type.__subclasscheck__(int, Classic), False)
        self.assertEqual(type.__subclasscheck__(int, Based()), True)
        self.assertEqual(_error(type.__subclasscheck__, int, 1), ('TypeError', 'issubclass() arg 1 must be a class'))
        self.assertEqual(_error(type.__subclasscheck__, int, (bool,)), ('TypeError', 'issubclass() arg 1 must be a class'))
        self.assertEqual(type.__instancecheck__(int, Proxy()), True)
        self.assertEqual(type.__instancecheck__(int, Classic()), False)

        class Meta(type):
            def __subclasscheck__(cls, subclass):
                return type.__subclasscheck__(cls, subclass)

        Checked = Meta('Checked', (object,), {})
        self.assertEqual(issubclass(Meta('Derived', (Checked,), {}), Checked), True)
        self.assertEqual(_error(issubclass, 1, Checked), ('TypeError', 'issubclass() arg 1 must be a class'))

    def test_frame_line_number_refuses_deletion_and_values_other_than_int(self):
        frame = sys._getframe()

        def delete():
            del frame.f_lineno

        def assign(value):
            frame.f_lineno = value

        self.assertEqual(_error(delete), ('AttributeError', 'cannot delete attribute'))
        self.assertEqual(_error(assign, 'x'), ('ValueError', 'lineno must be an integer'))
        self.assertEqual(_error(assign, 1L), ('ValueError', 'lineno must be an integer'))

    def test_recursion_error_normalized_at_the_limit_is_the_shared_instance_message(self):
        # A limit a few frames above the current depth is reached before the
        # native stack guard, which a sanitized build trips within a few
        # dozen frames.
        depth = 0
        frame = sys._getframe()
        while frame is not None:
            depth += 1
            frame = frame.f_back
        limit = sys.getrecursionlimit()
        sys.setrecursionlimit(depth + 10)
        try:
            message = _error(_recurse_raising, 0)
        finally:
            sys.setrecursionlimit(limit)
        self.assertEqual(message, ('RuntimeError', 'maximum recursion depth exceeded'))

    def test_registered_decoders_receive_what_cpython_passes_them(self):
        decoded = unicode('ab', 'runtime_parity_kind')
        self.assertEqual((type(decoded), decoded), (_UnicodeResult, u'buffer:ab:strict'))
        decoded = unicode(buffer('xab', 1), 'runtime_parity_kind', 'ignore')
        self.assertEqual((type(decoded), decoded), (_UnicodeResult, u'buffer:ab:ignore'))
        self.assertEqual(unicode('', 'runtime_parity_number'), u'')
        self.assertEqual(_error(unicode, 'ab', 'runtime_parity_number'), ('TypeError', 'decoder did not return an unicode object (type=int)'))

        class Text(unicode):
            pass

        decoded = Text('ab', 'runtime_parity_kind')
        self.assertEqual((type(decoded), decoded), (Text, u'buffer:ab:strict'))
        decoded = 'ab'.decode('runtime_parity_kind')
        self.assertEqual((type(decoded), decoded), (_UnicodeResult, u'str:ab:strict'))
        data = bytearray('ab')
        decoded = data.decode('runtime_parity_kind', 'replace')
        self.assertEqual((type(decoded), decoded), (_UnicodeResult, u'bytearray:ab:replace'))
        self.assertEqual(data.decode(encoding='runtime_parity_number'), 5)
        self.assertEqual(data.decode('utf-8'), u'ab')
        self.assertEqual(bytearray('a\xffb').decode('utf-8', 'replace'), u'a\ufffdb')
        self.assertEqual(_error(data.decode, 'a', 'b', 'c'), ('TypeError', 'decode() takes at most 2 arguments (3 given)'))
        self.assertEqual(_error(data.decode, 1), ('TypeError', 'decode() argument 1 must be string, not int'))

    def test_classic_contains_falls_back_to_iteration_on_attribute_error(self):
        class Missing(object):
            def __get__(self, instance, owner):
                raise AttributeError('missing')

        class Failing(object):
            def __get__(self, instance, owner):
                raise ValueError('failing')

        class Iterated:
            __contains__ = Missing()

            def __iter__(self):
                return iter([1, 2])

        class Indexed:
            __contains__ = Missing()

            def __getitem__(self, index):
                if index < 3:
                    return index
                raise IndexError(index)

        class Raising:
            __contains__ = Failing()

            def __iter__(self):
                return iter([1, 2])

        class Plain:
            __contains__ = Missing()

        self.assertEqual((1 in Iterated(), 3 in Iterated(), 1 not in Iterated()), (True, False, False))
        self.assertEqual(2 in Indexed(), True)
        self.assertEqual(_error(lambda: 1 in Raising()), ('ValueError', 'failing'))
        self.assertEqual(_error(lambda: 1 in Plain()), ('TypeError', "argument of type 'instance' is not iterable"))


if __name__ == '__main__':
    unittest.main()
