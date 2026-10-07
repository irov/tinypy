"""Project-authored sys conversion, size protocol and displayhook regressions."""

import sys
import __builtin__
import unittest


def _error(callback, *args, **kwargs):
    try:
        callback(*args, **kwargs)
    except BaseException as error:
        return type(error).__name__, error.args
    raise AssertionError('operation must raise')


class SysStateAudit(unittest.TestCase):
    def test_negative_frame_depth_returns_current_frame(self):
        current = sys._getframe()
        for depth in (-1, -2L, -(2 ** 31)):
            self.assertIs(sys._getframe(depth), current)

    def test_frame_uses_int_protocol_and_ignores_index(self):
        events = []
        class Integer(object):
            def __int__(self):
                events.append('int')
                return 0
        class Index(object):
            def __index__(self):
                events.append('index')
                return 0
        self.assertIs(sys._getframe(Integer()), sys._getframe())
        self.assertEqual(events, ['int'])
        self.assertEqual(_error(sys._getframe, Index()), ('TypeError', ('an integer is required',)))
        self.assertEqual(events, ['int'])

    def test_frame_int_subtype_uses_payload_and_long_subtype_calls_int(self):
        events = []
        class Integer(int):
            def __int__(self):
                events.append('int')
                return 1000
        class Long(long):
            def __int__(self):
                events.append('long')
                return 0
        self.assertIs(sys._getframe(Integer(0)), sys._getframe())
        self.assertIs(sys._getframe(Long(1000)), sys._getframe())
        self.assertEqual(events, ['long'])

    def test_frame_integer_parser_rejects_float_and_c_int_overflow(self):
        self.assertEqual(_error(sys._getframe, 1.0),
                         ('TypeError', ('integer argument expected, got float',)))
        self.assertEqual(_error(sys._getframe, 2 ** 40),
                         ('OverflowError', ('signed integer is greater than maximum',)))
        self.assertEqual(_error(sys._getframe, -(2 ** 40)),
                         ('OverflowError', ('signed integer is less than minimum',)))

    def test_recursion_limit_accepts_long_and_int_protocol(self):
        events = []
        class Integer(object):
            def __int__(self):
                events.append('int')
                return 1001
        saved = sys.getrecursionlimit()
        try:
            for value in (1000L, Integer()):
                self.assertIs(sys.setrecursionlimit(value), None)
            self.assertEqual(sys.getrecursionlimit(), 1001)
            self.assertEqual(events, ['int'])
        finally:
            sys.setrecursionlimit(saved)

    def test_recursion_limit_error_preserves_previous_value(self):
        saved = sys.getrecursionlimit()
        try:
            for value, error in ((0, 'ValueError'), (-1, 'ValueError'),
                                 (1.5, 'TypeError'), (2 ** 40, 'OverflowError')):
                self.assertEqual(_error(sys.setrecursionlimit, value)[0], error)
                self.assertEqual(sys.getrecursionlimit(), saved)
        finally:
            sys.setrecursionlimit(saved)

    def test_sys_noargs_methods_report_native_arity(self):
        for method in (sys.exc_info, sys.exc_clear, sys.getrecursionlimit, sys.getdefaultencoding):
            self.assertEqual(_error(method, 1), ('TypeError',
                ('%s() takes no arguments (1 given)' % method.__name__,)))

    def test_sys_methods_reject_keywords_before_positional_conversion(self):
        for method in (sys.exc_info, sys.exc_clear, sys.getrecursionlimit,
                       sys.getdefaultencoding, sys._getframe, sys.setrecursionlimit,
                       sys.displayhook, sys.exit):
            self.assertEqual(_error(method, None, unknown=1), ('TypeError',
                ('%s() takes no keyword arguments' % method.__name__,)))

    def test_sizeof_bypasses_instance_attribute_and_getattribute(self):
        events = []
        class Sized(object):
            def __getattribute__(self, name):
                if name == '__sizeof__':
                    events.append('lookup')
                    return lambda: 999
                return object.__getattribute__(self, name)
            def __sizeof__(self):
                events.append('size')
                return 37
        class Zero(object):
            def __sizeof__(self):
                return 0
        value = Sized()
        value.__sizeof__ = lambda: 777
        self.assertEqual(sys.getsizeof(value) - sys.getsizeof(Zero()), 37)
        self.assertEqual(events, ['size'])

    def test_sizeof_binds_type_descriptor(self):
        events = []
        class Descriptor(object):
            def __get__(self, instance, owner):
                events.append(instance is not None)
                return lambda: 0
        class Sized(object):
            __sizeof__ = Descriptor()
        self.assertIs(type(sys.getsizeof(Sized())), int)
        self.assertEqual(events, [True])

    def test_sizeof_converts_numeric_results_to_base_int(self):
        events = []
        class Converted(object):
            def __int__(self):
                events.append('int')
                return 37
        class Number(int):
            def __int__(self):
                raise AssertionError('integer payload must win')
        class Sized(object):
            def __sizeof__(self):
                return returned
        class Zero(object):
            def __sizeof__(self):
                return 0
        for returned in (37, 37L, 37.5, Converted(), Number(37)):
            result = sys.getsizeof(Sized())
            self.assertIs(type(result), int)
            self.assertEqual(result - sys.getsizeof(Zero()), 37)
        self.assertEqual(events, ['int'])

    def test_sizeof_rejects_negative_result_and_overflow(self):
        class Sized(object):
            def __sizeof__(self):
                return returned
        returned = -1
        self.assertEqual(_error(sys.getsizeof, Sized()),
                         ('ValueError', ('__sizeof__() should return >= 0',)))
        returned = 2 ** 70
        self.assertEqual(_error(sys.getsizeof, Sized()),
                         ('OverflowError', ('long int too large to convert to int',)))

    def test_sizeof_default_consumes_only_type_error(self):
        class Sized(object):
            def __sizeof__(self):
                raise marker
        sentinel = object()
        for error_type in (TypeError, ValueError, KeyboardInterrupt):
            marker = error_type('size failure')
            if error_type is TypeError:
                self.assertIs(sys.getsizeof(Sized(), sentinel), sentinel)
            else:
                try:
                    sys.getsizeof(Sized(), sentinel)
                except BaseException as error:
                    self.assertIs(error, marker)
                else:
                    self.fail('size callback must raise')

    def test_sizeof_classic_instance_ignores_size_method(self):
        events = []
        class Classic:
            def __sizeof__(self):
                events.append('size')
                raise ValueError('ignored')
        result = sys.getsizeof(Classic())
        self.assertIs(type(result), int)
        self.assertTrue(result >= 0)
        self.assertEqual(events, [])

    def test_sizeof_keyword_arguments_and_required_parameter_priority(self):
        value = object()
        self.assertEqual(sys.getsizeof(object=value), sys.getsizeof(value))
        self.assertEqual(sys.getsizeof(default=17, object=value), sys.getsizeof(value))
        for kwargs in ({}, {'default': 17}, {'extra': 17}):
            self.assertEqual(_error(sys.getsizeof, **kwargs),
                ('TypeError', ("Required argument 'object' (pos 1) not found",)))

    def test_displayhook_flushes_initial_softspace_before_repr(self):
        saved = sys.stdout
        had = '_' in __builtin__.__dict__
        previous = __builtin__.__dict__.get('_')
        events = []
        class Sink(object):
            softspace = 1
            def write(self, text):
                events.append(('write', text))
        class Shown(object):
            def __repr__(self):
                events.append(('repr', __builtin__.__dict__.get('_') is None))
                return 'payload'
        sink = Sink()
        value = Shown()
        try:
            sys.stdout = sink
            self.assertIs(sys.displayhook(value), None)
            self.assertIs(__builtin__._, value)
            self.assertEqual(sink.softspace, 0)
        finally:
            sys.stdout = saved
            if had:
                __builtin__._ = previous
            else:
                __builtin__.__dict__.pop('_', None)
        self.assertEqual(events, [('write', '\n'), ('repr', True),
                                  ('write', 'payload'), ('write', '\n')])

    def test_displayhook_none_preserves_last_result(self):
        had = '_' in __builtin__.__dict__
        previous = __builtin__.__dict__.get('_')
        marker = object()
        try:
            __builtin__._ = marker
            self.assertIs(sys.displayhook(None), None)
            self.assertIs(__builtin__._, marker)
        finally:
            if had:
                __builtin__._ = previous
            else:
                __builtin__.__dict__.pop('_', None)

    def test_displayhook_missing_builtin_module_checked_even_for_none(self):
        module = sys.modules.pop('__builtin__')
        try:
            self.assertEqual(_error(sys.displayhook, None), ('RuntimeError', ('lost __builtin__',)))
        finally:
            sys.modules['__builtin__'] = module

    def test_displayhook_missing_stdout_leaves_underscore_none(self):
        saved = sys.stdout
        had = '_' in __builtin__.__dict__
        previous = __builtin__.__dict__.get('_')
        try:
            del sys.stdout
            self.assertEqual(_error(sys.displayhook, 17), ('RuntimeError', ('lost sys.stdout',)))
            self.assertIs(__builtin__._, None)
        finally:
            sys.stdout = saved
            if had:
                __builtin__._ = previous
            else:
                __builtin__.__dict__.pop('_', None)


if __name__ == '__main__':
    unittest.main()
