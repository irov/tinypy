"""Project-authored Python 2.7 execution and evaluation-order checks."""

import unittest


class EvaluationSemantics(unittest.TestCase):
    def test_call_evaluates_receiver_before_arguments(self):
        events = []
        class Receiver(object):
            def invoke(self, first, second):
                events.append(('invoke', first, second))
                return first + second
        def receiver():
            events.append('receiver')
            return Receiver()
        def argument(name, value):
            events.append(name)
            return value
        self.assertEqual(receiver().invoke(argument('first', 4), argument('second', 9)), 13)
        self.assertEqual(events, ['receiver', 'first', 'second', ('invoke', 4, 9)])

    def test_call_argument_failure_stops_evaluation(self):
        events = []
        def argument(name):
            events.append(name)
            if name == 'bad':
                raise LookupError(name)
            return name
        def invoke(*args):
            events.append('invoke')
        with self.assertRaises(LookupError):
            invoke(argument('first'), argument('bad'), argument('last'))
        self.assertEqual(events, ['first', 'bad'])

    def test_assignment_rhs_precedes_target(self):
        events = []
        target = [0]
        def value():
            events.append('value')
            return 19
        def receiver():
            events.append('receiver')
            return target
        def index():
            events.append('index')
            return 0
        receiver()[index()] = value()
        self.assertEqual(events, ['value', 'receiver', 'index'])
        self.assertEqual(target, [19])

    def test_augmented_subscript_evaluates_target_once(self):
        events = []
        class Target(object):
            def __getitem__(self, key):
                events.append(('get', key))
                return 7
            def __setitem__(self, key, value):
                events.append(('set', key, value))
        def receiver():
            events.append('receiver')
            return Target()
        def index():
            events.append('index')
            return 'slot'
        def value():
            events.append('value')
            return 5
        receiver()[index()] += value()
        self.assertEqual(events, ['receiver', 'index', ('get', 'slot'), 'value', ('set', 'slot', 12)])

    def test_chained_assignment_shares_rhs(self):
        events = []
        class Target(object):
            def __setattr__(self, name, value):
                events.append((name, value))
        shared = []
        first = second = Target()
        first.left = second.right = shared
        self.assertEqual([event[0] for event in events], ['left', 'right'])
        self.assertIs(events[0][1], shared)
        self.assertIs(events[1][1], shared)

    def test_unpacking_failure_keeps_earlier_nested_target(self):
        first, second, third = 'old-first', 'old-second', 'old-third'
        with self.assertRaises(ValueError):
            first, (second, third) = [8, [9]]
        self.assertEqual((first, second, third), (8, 'old-second', 'old-third'))
        with self.assertRaises(ValueError):
            first, second = iter([1, 2, 3])
        self.assertEqual((first, second), (8, 'old-second'))

    def test_comparison_chain_evaluates_middle_once(self):
        events = []
        def value(name, number):
            events.append(name)
            return number
        self.assertTrue(value('left', 2) < value('middle', 5) < value('right', 11))
        self.assertEqual(events, ['left', 'middle', 'right'])
        events[:] = []
        self.assertFalse(value('left', 8) < value('middle', 5) < value('right', 11))
        self.assertEqual(events, ['left', 'middle'])

    def test_comparison_chain_preserves_result_object(self):
        events = []
        class Result(object):
            def __init__(self, truth):
                self.truth = truth
            def __nonzero__(self):
                events.append(('truth', self.truth))
                return self.truth
        first_result, last_result = Result(True), Result(False)
        class Operand(object):
            def __init__(self, result):
                self.result = result
            def __lt__(self, other):
                events.append('compare')
                return self.result
        result = Operand(first_result) < Operand(last_result) < object()
        self.assertIs(result, last_result)
        self.assertEqual(events, ['compare', ('truth', True), 'compare'])

    def test_boolean_branch_does_not_repeat_truth_callback(self):
        events = []
        class Truth(object):
            def __init__(self, name, value):
                self.name, self.value = name, value
            def __nonzero__(self):
                events.append(self.name)
                if events.count(self.name) > 1:
                    raise AssertionError('repeated truth callback')
                return self.value
        if Truth('yes', True) or Truth('unused', False):
            events.append('body')
        self.assertEqual(events, ['yes', 'body'])
        events[:] = []
        while Truth('no', False) and Truth('unused', True):
            self.fail('unreachable body')
        self.assertEqual(events, ['no'])

    def test_conditional_expression_only_evaluates_selected_arm(self):
        events = []
        def value(name, result):
            events.append(name)
            return result
        self.assertEqual(value('yes', 17) if value('test', True) else value('no', 23), 17)
        self.assertEqual(events, ['test', 'yes'])
        events[:] = []
        self.assertEqual(value('yes', 17) if value('test', False) else value('no', 23), 23)
        self.assertEqual(events, ['test', 'no'])

    def test_dict_display_python2_value_before_key(self):
        events = []
        def value(name, result):
            events.append(name)
            return result
        result = {value('key-a', 'a'): value('value-a', 6), value('key-b', 'b'): value('value-b', 7)}
        self.assertEqual(result, {'a': 6, 'b': 7})
        self.assertEqual(events, ['value-a', 'key-a', 'value-b', 'key-b'])

    def test_list_comprehension_filters_before_result(self):
        events = []
        def keep(value):
            events.append(('keep', value))
            return value % 2
        def result(value):
            events.append(('result', value))
            return value * 10
        self.assertEqual([result(value) for value in [2, 3, 4] if keep(value)], [30])
        self.assertEqual(events, [('keep', 2), ('keep', 3), ('result', 3), ('keep', 4)])

    def test_python2_comprehension_scope_distinctions(self):
        item = 'outer'
        self.assertEqual([item for item in [4, 7]], [4, 7])
        self.assertEqual(item, 7)
        item = 'outer'
        self.assertEqual({item for item in [4, 7]}, {4, 7})
        self.assertEqual(item, 'outer')
        self.assertEqual({item: item * 2 for item in [4, 7]}, {4: 8, 7: 14})
        self.assertEqual(item, 'outer')
        self.assertEqual(list(item for item in [4, 7]), [4, 7])
        self.assertEqual(item, 'outer')

    def test_generator_expression_outer_iterator_is_eager(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([3, 8])
        def transform(value):
            events.append(('transform', value))
            return value + 1
        generated = (transform(value) for value in Source())
        self.assertEqual(events, ['iter'])
        self.assertEqual(next(generated), 4)
        self.assertEqual(events, ['iter', ('transform', 3)])
        generated.close()

    def test_decorator_factories_and_application_order(self):
        events = []
        def decorator(name):
            events.append(('factory', name))
            def apply(function):
                events.append(('apply', name, function.__name__))
                return function
            return apply
        @decorator('outer')
        @decorator('inner')
        def task(value=21):
            return value
        self.assertEqual(events, [('factory', 'outer'), ('factory', 'inner'), ('apply', 'inner', 'task'), ('apply', 'outer', 'task')])
        self.assertEqual(task(), 21)

    def test_class_decorator_can_replace_class(self):
        events = []
        replacement = object()
        def decorate(cls):
            events.append(('decorate', cls.value))
            return replacement
        @decorate
        class Subject(object):
            events.append('body')
            value = 32
        self.assertIs(Subject, replacement)
        self.assertEqual(events, ['body', ('decorate', 32)])

    def test_unbound_local_is_decided_for_whole_function(self):
        captured = 9
        def read():
            result = captured
            captured = 12
            return result
        self.assertRaises(UnboundLocalError, read)
        self.assertEqual(captured, 9)

    def test_finally_return_replaces_pending_exception(self):
        events = []
        def run():
            try:
                raise KeyError('pending')
            finally:
                events.append('finally')
                return 28
        self.assertEqual(run(), 28)
        self.assertEqual(events, ['finally'])

    def test_finally_scopes_after_early_return(self):
        events = []
        def run(values, early):
            try:
                if early:
                    return 'early'
                def nested():
                    return [value + 2 for value in values]
                return nested()
            finally:
                events.append(sorted(value * 3 for value in values))
                def nested():
                    return (lambda value: value + 1)(len(values))
                events.append(nested())
        self.assertEqual(run([5, 1], True), 'early')
        self.assertEqual(run([2, 4], False), [4, 6])
        self.assertEqual(events, [[3, 15], 3, [6, 12], 3])

    def test_break_and_continue_run_finally_once(self):
        events = []
        for value in range(5):
            try:
                if value == 1:
                    continue
                if value == 3:
                    break
                events.append(('body', value))
            finally:
                events.append(('finally', [item for item in [value]]))
        else:
            events.append('else')
        self.assertEqual(events, [('body', 0), ('finally', [0]), ('finally', [1]), ('body', 2), ('finally', [2]), ('finally', [3])])

    def test_except_target_survives_handler_in_python2(self):
        failure = ValueError('retained')
        try:
            raise failure
        except ValueError as caught:
            self.assertIs(caught, failure)
        self.assertIs(caught, failure)

    def test_multiple_context_managers_unwind_on_enter_failure(self):
        events = []
        class Manager(object):
            def __init__(self, name, fail=False):
                self.name, self.fail = name, fail
            def __enter__(self):
                events.append(('enter', self.name))
                if self.fail:
                    raise ValueError('enter failed')
                return self.name
            def __exit__(self, kind, value, traceback):
                events.append(('exit', self.name, kind))
        with self.assertRaises(ValueError):
            with Manager('outer') as outer, Manager('inner', True) as inner:
                self.fail('unreachable body')
        self.assertEqual(events, [('enter', 'outer'), ('enter', 'inner'), ('exit', 'outer', ValueError)])

    def test_context_manager_exit_sees_binding_failure(self):
        events = []
        class Manager(object):
            def __enter__(self):
                return [1]
            def __exit__(self, kind, value, traceback):
                events.append(kind)
                return True
        with Manager() as (first, second):
            self.fail('unreachable body')
        self.assertEqual(events, [ValueError])

    def test_generator_send_and_close_run_cleanup(self):
        events = []
        def generate():
            try:
                received = yield 'ready'
                yield ('received', received)
            finally:
                events.append('closed')
        generated = generate()
        self.assertRaises(TypeError, generated.send, 5)
        self.assertEqual(next(generated), 'ready')
        self.assertEqual(generated.send(13), ('received', 13))
        generated.close()
        self.assertEqual(events, ['closed'])
        self.assertRaises(StopIteration, next, generated)

    def test_generator_can_yield_after_catching_throw(self):
        events = []
        def generate():
            try:
                try:
                    yield 'ready'
                except LookupError as error:
                    events.append(error.args)
                    yield 'recovered'
                yield 'tail'
            finally:
                events.append('cleanup')
        generated = generate()
        self.assertEqual(next(generated), 'ready')
        self.assertEqual(generated.throw(KeyError('injected')), 'recovered')
        self.assertEqual(list(generated), ['tail'])
        self.assertEqual(events, [('injected',), 'cleanup'])
