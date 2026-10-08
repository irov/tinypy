"""Project-authored Python 2.7 execution and evaluation-order checks."""

import unittest


def compare_until_recursion_limit(left, right):
    left < right
    return compare_until_recursion_limit(left, right)


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

    def test_class_attribute_cache_follows_class_changes(self):
        class Base(object):
            value = 1
            @staticmethod
            def static(argument):
                return ('static', argument)
            @classmethod
            def bound(cls, argument):
                return (cls.__name__, argument)
        class Child(Base):
            pass
        def read(cls):
            return (cls.value, cls.static(2), cls.bound(3))
        self.assertEqual([read(Child) for _ in range(3)], [(1, ('static', 2), ('Child', 3))] * 3)
        self.assertEqual(read(Base), (1, ('static', 2), ('Base', 3)))
        Base.value = 4
        Base.static = staticmethod(lambda argument: ('replaced', argument))
        self.assertEqual(read(Child), (4, ('replaced', 2), ('Child', 3)))
        Child.value = 5
        self.assertEqual((read(Child)[0], read(Base)[0]), (5, 4))
        del Child.value
        self.assertEqual(read(Child)[0], 4)
        del Base.value
        with self.assertRaises(AttributeError):
            read(Child)

    def test_class_attribute_cache_follows_descriptor_changes(self):
        class Descriptor(object):
            pass
        class Owner(object):
            attribute = Descriptor()
        def read():
            return Owner.attribute
        self.assertIsInstance(read(), Descriptor)
        self.assertIsInstance(read(), Descriptor)
        Descriptor.__get__ = lambda self, instance, owner: ('get', instance, owner.__name__)
        self.assertEqual(read(), ('get', None, 'Owner'))
        del Descriptor.__get__
        self.assertIsInstance(read(), Descriptor)
        class Other(object):
            def __get__(self, instance, owner):
                return 'other'
        Owner.__dict__['attribute'].__class__ = Other
        self.assertEqual(read(), 'other')
        class Meta(type):
            @property
            def attribute(cls):
                return 'meta'
        Late = Meta('Late', (Owner,), {})
        self.assertEqual([getattr(Late, 'attribute'), Late.attribute], ['meta', 'meta'])

    def test_slot_attribute_cache_follows_class_changes(self):
        class Point(object):
            __slots__ = ('x',)
        class Other(object):
            __slots__ = ('x',)
        def read(point):
            return point.x
        def write(point, value):
            point.x = value
        point = Point()
        for value in range(3):
            write(point, value)
            self.assertEqual(read(point), value)
        del point.x
        self.assertRaises(AttributeError, read, point)
        write(point, 'again')
        self.assertEqual(read(point), 'again')
        point.__class__ = Other
        self.assertEqual(read(point), 'again')
        Other.x = property(lambda self: 'property')
        self.assertEqual(read(point), 'property')
        self.assertRaises(AttributeError, write, point, 1)
        class Foreign(object):
            __slots__ = ('y',)
        Foreign.x = Point.__dict__['x']
        self.assertRaises(TypeError, read, Foreign())
        self.assertRaises(TypeError, write, Foreign(), 1)
        del Point.x
        self.assertRaises(AttributeError, read, Point())
        self.assertRaises(AttributeError, write, Point(), 1)

    def test_bound_method_call_keeps_method_lifetime(self):
        import _weakref as weakref
        events = []
        class Receiver(object):
            def method(self, value):
                return value
        receiver = Receiver()
        for value in range(3):
            self.assertEqual(receiver.method(value), value)
        bound = receiver.method
        reference = weakref.ref(bound, lambda ref: events.append('collected'))
        self.assertEqual(bound(4), 4)
        self.assertEqual(reference()(5), 5)
        del bound
        self.assertEqual(events, ['collected'])
        self.assertIs(reference(), None)
        class Dying(object):
            def method(self):
                return 'alive'
            def __del__(self):
                events.append('receiver')
        self.assertEqual(Dying().method(), 'alive')
        self.assertEqual(events, ['collected', 'receiver'])

    def test_star_call_keyword_dictionary_is_not_shared(self):
        def collect(*args, **kwargs):
            kwargs['added'] = True
            return args, kwargs
        class Receiver(object):
            def method(self, *args, **kwargs):
                return collect(*args, **kwargs)
        arguments = (1, 2)
        keywords = {'key': 3}
        self.assertEqual(collect(*arguments, **keywords), ((1, 2), {'key': 3, 'added': True}))
        self.assertEqual(Receiver().method(0, *arguments, **keywords), ((0, 1, 2), {'key': 3, 'added': True}))
        self.assertEqual(keywords, {'key': 3})
        self.assertEqual(collect(*iter(arguments), extra=4), ((1, 2), {'extra': 4, 'added': True}))
        class Key(str):
            def __hash__(self):
                return str.__hash__(self)
            def __eq__(self, other):
                mutated.clear()
                return str.__eq__(self, other)
        def named(key=None, other=None):
            return key, other
        mutated = {Key('key'): 1, 'other': 2}
        self.assertEqual(named(**mutated), (1, 2))
        self.assertEqual(mutated, {})

    def test_module_attribute_cache_follows_dictionary_changes(self):
        import sys
        module = type(sys)('cached')
        def read(target):
            return target.value
        module.value = 1
        self.assertEqual([read(module) for _ in range(3)], [1, 1, 1])
        module.value = 2
        self.assertEqual(read(module), 2)
        module.__dict__['value'] = 3
        self.assertEqual(read(module), 3)
        del module.value
        self.assertRaises(AttributeError, read, module)
        other = type(sys)('other')
        other.value = 'other'
        module.value = 4
        self.assertEqual([read(other), read(module), read(other)], ['other', 4, 'other'])

    def test_builtin_type_attribute_cache_follows_receiver_type(self):
        class Recording(list):
            def append(self, value):
                list.append(self, ('recorded', value))
        def append(target, value):
            target.append(value)
        plain = []
        recording = Recording()
        for value in range(2):
            append(plain, value)
            append(recording, value)
        self.assertEqual(plain, [0, 1])
        self.assertEqual(recording, [('recorded', 0), ('recorded', 1)])
        def real(number):
            return number.real
        self.assertEqual([real(3), real(2.5), real(1j), real(True)], [3, 2.5, 0.0, 1])

    def test_comparisons_reach_the_recursion_limit_like_ceval(self):
        messages = []
        for left, right in ((1, 2), (1.5, 2.5), (1, 2.5), (1 << 60, 2.5), ('a', 'b'), ((1,), (2,))):
            try:
                compare_until_recursion_limit(left, right)
            except RuntimeError as error:
                messages.append(str(error))
        self.assertEqual(messages, ['maximum recursion depth exceeded'] + ['maximum recursion depth exceeded in cmp'] * 5)

    def test_special_method_overrides_follow_class_changes(self):
        class Value(object):
            pass
        class Child(Value):
            pass
        value = Child()
        self.assertEqual([bool(value), bool(value)], [True, True])
        self.assertRaises(TypeError, lambda: value + 1)
        self.assertEqual(getattr(value, 'missing', 'default'), 'default')
        Value.__nonzero__ = lambda self: False
        Value.__add__ = lambda self, other: ('added', other)
        Value.__getattr__ = lambda self, name: ('hook', name)
        self.assertEqual([bool(value), value + 1, getattr(value, 'missing', 'default')], [False, ('added', 1), ('hook', 'missing')])
        del Value.__nonzero__
        Child.__len__ = lambda self: 0
        self.assertFalse(value)
        del Child.__len__
        self.assertTrue(value)
        empty = []
        filled = {1: 2}
        self.assertEqual(['yes' if empty else 'no', 'yes' if filled else 'no', 'yes' if '' else 'no', 'yes' if (1,) else 'no'], ['no', 'yes', 'no', 'yes'])

    def test_attribute_call_and_representation_hooks_follow_class_changes(self):
        class Base(object):
            pass
        class Child(Base):
            pass
        def read(item):
            return item.missing
        def write(item, data):
            item.data = data
        def remove(item):
            del item.data
        value = Child()
        write(value, 1)
        remove(value)
        self.assertRaises(AttributeError, read, value)
        self.assertRaises(TypeError, value)
        self.assertTrue(str(value).startswith('<'))
        self.assertTrue(repr([value]).startswith('[<'))
        log = []
        Base.__getattr__ = lambda self, name: ('missing', name)
        Base.__setattr__ = lambda self, name, item: log.append(('set', name, item))
        Base.__delattr__ = lambda self, name: log.append(('del', name))
        Base.__call__ = lambda self, *args: ('called', args)
        Base.__str__ = lambda self: 'text'
        Base.__repr__ = lambda self: 'shown'
        write(value, 2)
        remove(value)
        self.assertEqual(log, [('set', 'data', 2), ('del', 'data')])
        self.assertEqual([read(value), value(3), str(value), repr([value]), '%s' % value], [('missing', 'missing'), ('called', (3,)), 'text', '[shown]', 'text'])
        Child.__getattribute__ = lambda self, name: ('get', name)
        self.assertEqual([read(value), getattr(value, 'other')], [('get', 'missing'), ('get', 'other')])
        del Child.__getattribute__
        del Base.__getattr__
        del Base.__setattr__
        del Base.__delattr__
        del Base.__call__
        del Base.__str__
        write(value, 4)
        self.assertEqual(value.data, 4)
        remove(value)
        self.assertRaises(AttributeError, read, value)
        self.assertRaises(TypeError, value)
        self.assertEqual([str(value), repr([value])], ['shown', '[shown]'])

    def test_class_call_follows_constructor_changes(self):
        class Base(object):
            def __init__(self, value):
                self.value = value
        class Child(Base):
            pass
        def make(cls, *args):
            return cls(*args).__dict__
        self.assertEqual([make(Child, index) for index in range(3)], [{'value': 0}, {'value': 1}, {'value': 2}])
        Base.__init__ = lambda self, value, extra=0: setattr(self, 'pair', (value, extra))
        self.assertEqual(make(Child, 1, 2), {'pair': (1, 2)})
        del Base.__init__
        self.assertEqual(make(Child), {})
        self.assertRaises(TypeError, make, Child, 1)
        Base.__new__ = staticmethod(lambda cls, *args: 'replaced')
        self.assertEqual(Child(1), 'replaced')
        del Base.__new__
        class Grandchild(Child):
            def __init__(self, *args):
                self.args = args
        Child.__new__ = staticmethod(lambda cls, *args: object.__new__(Grandchild))
        self.assertEqual(make(Child, 5), {'args': (5,)})

    def test_exception_call_follows_constructor_changes(self):
        class Failure(Exception):
            pass
        class Detail(Failure):
            pass
        def make(cls, *args):
            error = cls(*args)
            return (type(error).__name__, error.args, sorted(error.__dict__.items()))
        self.assertEqual([make(Detail, index) for index in range(2)], [('Detail', (0,), []), ('Detail', (1,), [])])
        Failure.__init__ = lambda self, code: setattr(self, 'code', code)
        self.assertEqual(make(Detail, 3), ('Detail', (), [('code', 3)]))
        del Failure.__init__
        self.assertEqual(make(Detail, 4, 5), ('Detail', (4, 5), []))
        class Other(Detail):
            def __init__(self, *args):
                self.seen = args
        Failure.__new__ = staticmethod(lambda cls, *args: Exception.__new__(Other))
        self.assertEqual(make(Detail, 6), ('Other', (), [('seen', (6,))]))
        del Failure.__new__
        self.assertEqual(make(Detail, 7), ('Detail', (7,), []))
