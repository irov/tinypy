"""Project-authored generator normalization and namespace regressions."""

import sys
import unittest
import _weakref as weakref


class ControlDeep(unittest.TestCase):
    def test_generator_explicit_stop_iteration_preserves_type_value_and_payload(self):
        class Stopped(StopIteration):
            pass
        marker = Stopped('retained', 17)
        def sequence():
            yield 19
            raise marker
        for mode in ('method', 'send', 'builtin'):
            value = sequence()
            self.assertEqual(value.next(), 19)
            with self.assertRaises(Stopped) as caught:
                if mode == 'method':
                    value.next()
                elif mode == 'send':
                    value.send(None)
                else:
                    next(value)
            self.assertIs(caught.exception, marker)
            self.assertEqual(caught.exception.args, ('retained', 17))
            self.assertIs(value.gi_frame, None)
            with self.assertRaises(StopIteration) as finished:
                value.next()
            self.assertIs(type(finished.exception), StopIteration)
            self.assertEqual(finished.exception.args, ())

    def test_builtin_next_preserves_custom_iterator_stop_iteration(self):
        class Stopped(StopIteration):
            pass
        marker = Stopped('iterator payload')
        class Iterator(object):
            def __iter__(self):
                return self
            def next(self):
                raise marker
        with self.assertRaises(Stopped) as caught:
            next(Iterator())
        self.assertIs(caught.exception, marker)
        handled_type, handled_value, unused_traceback = sys.exc_info()
        del unused_traceback
        sentinel = object()
        self.assertIs(next(Iterator(), sentinel), sentinel)
        self.assertIs(sys.exc_info()[0], handled_type)
        self.assertIs(sys.exc_info()[1], handled_value)
        sys.exc_clear()

    def test_generator_unusual_stop_iteration_retains_requested_type(self):
        payload = ValueError('stop payload')
        class Stopped(StopIteration):
            def __new__(cls, *args):
                return payload
        def sequence():
            yield 17
            raise Stopped
        for mode in ('method', 'send', 'builtin'):
            value = sequence()
            value.next()
            try:
                if mode == 'method':
                    value.next()
                elif mode == 'send':
                    value.send(None)
                else:
                    next(value)
            except Stopped as error:
                self.assertIs(sys.exc_info()[0], Stopped)
                self.assertIs(error, payload)
            else:
                self.fail('requested StopIteration subtype was lost')
            self.assertIs(value.gi_frame, None)
        value = sequence()
        value.next()
        self.assertEqual(next(value, 23), 23)

    def test_generator_close_matches_requested_exception_type(self):
        for base in (GeneratorExit, StopIteration):
            class Exiting(base):
                def __new__(cls, *args):
                    return ValueError('alternate payload')
            def sequence():
                try:
                    yield 17
                finally:
                    raise Exiting
            value = sequence()
            value.next()
            self.assertIs(value.close(), None)
            self.assertIs(value.gi_frame, None)

    def test_throw_injects_exception_constructor_failure(self):
        for stage in ('new', 'init', 'classic'):
            events = []
            if stage == 'new':
                class Problem(ValueError):
                    def __new__(cls, *args):
                        events.append(args)
                        raise TypeError('constructor payload')
            elif stage == 'init':
                class Problem(ValueError):
                    def __init__(self, *args):
                        events.append(args)
                        raise TypeError('constructor payload')
            else:
                class Problem:
                    def __init__(self, *args):
                        events.append(args)
                        raise TypeError('constructor payload')
            def sequence():
                try:
                    yield 17
                except TypeError as error:
                    yield error.args
                finally:
                    events.append('cleanup')
            value = sequence()
            value.next()
            self.assertEqual(value.throw(Problem, ('argument', 19)), ('constructor payload',))
            self.assertEqual(events, [('argument', 19)])
            self.assertIs(value.close(), None)
            self.assertEqual(events, [('argument', 19), 'cleanup'])

    def test_throw_constructor_failure_injected_before_first_instruction(self):
        events = []
        class Problem(ValueError):
            def __init__(self):
                raise LookupError('unstarted')
        def sequence():
            try:
                events.append('entered')
                yield 17
            finally:
                events.append('cleanup')
        value = sequence()
        with self.assertRaisesRegexp(LookupError, 'unstarted'):
            value.throw(Problem)
        self.assertEqual(events, [])
        self.assertIs(value.gi_frame, None)

    def test_throw_constructor_failure_runs_suppression_and_finally(self):
        events = []
        class Problem(ValueError):
            def __init__(self):
                raise TypeError('injected')
        class Context(object):
            def __enter__(self):
                events.append('enter')
            def __exit__(self, kind, value, traceback):
                events.append(('exit', kind.__name__, value.args))
                return True
        def sequence():
            try:
                with Context():
                    yield 17
                events.append('suppressed')
            finally:
                events.append('finally')
        value = sequence()
        value.next()
        with self.assertRaises(StopIteration):
            value.throw(Problem)
        self.assertEqual(events, ['enter', ('exit', 'TypeError', ('injected',)), 'suppressed', 'finally'])
        self.assertIs(value.gi_frame, None)

    def test_throw_constructor_can_finish_generator_before_injection(self):
        events = []
        def sequence():
            try:
                yield 17
            finally:
                events.append('cleanup')
        value = sequence()
        value.next()
        class Problem(ValueError):
            def __init__(self):
                value.close()
                raise TypeError('after close')
        with self.assertRaisesRegexp(TypeError, 'after close'):
            value.throw(Problem)
        self.assertEqual(events, ['cleanup'])
        self.assertIs(value.gi_frame, None)

    def test_generator_running_callbacks_reject_reentry_without_finishing(self):
        holder = [None]
        events = []
        def sequence():
            for method in ('send', 'throw', 'close'):
                try:
                    if method == 'send':
                        holder[0].send(None)
                    elif method == 'throw':
                        holder[0].throw(ValueError)
                    else:
                        holder[0].close()
                except ValueError as error:
                    events.append((method, str(error), holder[0].gi_running))
            yield 17
        value = holder[0] = sequence()
        try:
            self.assertEqual(value.next(), 17)
            self.assertEqual(events, [(method, 'generator already executing', True)
                                      for method in ('send', 'throw', 'close')])
            self.assertFalse(value.gi_running)
        finally:
            value.close()
            holder[0] = None

    def test_throw_constructor_failure_releases_generator_local(self):
        class Carrier(object):
            pass
        class Problem(ValueError):
            def __init__(self):
                raise TypeError('release')
        def sequence(carrier):
            yield 17
        carrier = Carrier()
        reference = weakref.ref(carrier)
        value = sequence(carrier)
        del carrier
        value.next()
        with self.assertRaises(TypeError):
            value.throw(Problem)
        sys.exc_clear()
        self.assertIs(reference(), None)
        self.assertIs(value.gi_frame, None)

    def test_eval_and_exec_accept_bytearray_and_memoryview_namespaces(self):
        for namespace in (bytearray(), memoryview('')):
            scope = {}
            self.assertEqual(eval('17', scope, namespace), 17)
            self.assertIn('__builtins__', scope)
            scope = {}
            exec '17' in scope, namespace
            self.assertIn('__builtins__', scope)

    def test_classic_namespace_requires_getitem_before_adding_builtins(self):
        class Empty:
            pass
        for mode in ('eval', 'exec'):
            scope = {}
            with self.assertRaises(TypeError):
                if mode == 'eval':
                    eval('17', scope, Empty())
                else:
                    exec '17' in scope, Empty()
            self.assertNotIn('__builtins__', scope)

    def test_classic_namespace_getitem_lookup_preserves_callbacks_and_errors(self):
        events = []
        class Dynamic:
            def __getattr__(self, name):
                events.append(name)
                if name == '__getitem__':
                    return lambda key: 23
                raise AttributeError(name)
        class Failed:
            def __getattr__(self, name):
                events.append(name)
                raise ValueError('hidden by mapping check')
        self.assertEqual(eval('17', {}, Dynamic()), 17)
        self.assertEqual(events, ['__getitem__'])
        events[:] = []
        with self.assertRaises(TypeError):
            eval('17', {}, Failed())
        self.assertEqual(events, ['__getitem__'])
