"""Project-authored Python 2 statement semantics: exec, the name opcodes and code objects."""

import sys
import unittest


def exec_then_snapshot():
    exec ''
    snapshot = locals()
    later = 1
    first = sorted(snapshot)
    len
    second = sorted(snapshot)
    return first, second


def exec_then_refresh():
    exec ''
    names = sorted(locals())
    later = 1
    return names, sorted(locals())


def exec_assignment():
    exec 'bound = 10'
    return sorted(locals())


def exec_writes_back():
    value = 1
    exec 'value = 2'
    return value


def returns_locals():
    inner = 1
    return locals()


def generator_locals():
    inner = 5
    yield locals()


class StatementSemantics(unittest.TestCase):
    def failure(self, function, *args):
        try:
            function(*args)
        except Exception as error:
            return type(error).__name__ + ': ' + str(error)
        self.fail('expected an exception')

    def test_name_lookup_leaves_locals_snapshot_alone(self):
        self.assertEqual(exec_then_snapshot(), ([], []))
        self.assertEqual(exec_then_refresh(), ([], ['later', 'names']))

    def test_exec_binds_through_locals(self):
        self.assertEqual(exec_assignment(), ['bound'])
        self.assertEqual(exec_writes_back(), 2)

    def test_exec_of_code_with_parameters(self):
        code = (lambda a, b: a).func_code
        namespace = {}

        def run_exec():
            exec code in namespace

        def defaulted(x=1):
            return x

        self.assertEqual(self.failure(run_exec), 'TypeError: <lambda>() takes exactly 2 arguments (0 given)')
        self.assertEqual(self.failure(eval, code, {}), 'TypeError: <lambda>() takes exactly 2 arguments (0 given)')
        self.assertEqual(self.failure(eval, defaulted.func_code, {}), 'TypeError: defaulted() takes exactly 1 argument (0 given)')
        self.assertEqual(self.failure(eval, (lambda x, *rest: x).func_code, {}), 'TypeError: <lambda>() takes at least 1 argument (0 given)')
        self.assertEqual(self.failure(eval, (lambda a: (yield a)).func_code, {}), 'TypeError: <lambda>() takes exactly 1 argument (0 given)')
        self.assertEqual(eval((lambda *rest, **extra: (rest, extra)).func_code, {}), ((), {}))

    def test_exec_locals_must_be_a_mapping(self):
        class Mapping(object):
            def __getitem__(self, key):
                raise KeyError(key)

            def __setitem__(self, key, value):
                pass

        class Sliceable(Mapping):
            def __getslice__(self, start, stop):
                return []

        for value in (xrange(3), [], 'abc', set(), Sliceable()):
            def run_exec():
                exec 'x = 1' in {}, value
            self.assertEqual(self.failure(run_exec), 'TypeError: exec: arg 3 must be a mapping or None')
            self.assertEqual(self.failure(eval, '1', {}, value), 'TypeError: locals must be a mapping')
        for value in (Mapping(), {}, None):
            exec 'x = 1' in {}, value
            self.assertEqual(eval('1', {}, value), 1)

    def test_function_code_runs_in_its_own_locals(self):
        mapping = {}
        self.assertEqual(eval(returns_locals.func_code, {}, mapping), {'inner': 1})
        self.assertEqual(mapping, {})
        self.assertEqual(list(eval(generator_locals.func_code, {}, mapping)), [{'inner': 5}])
        self.assertEqual(mapping, {})

    def test_name_opcodes_without_locals(self):
        code_type = type(returns_locals.func_code)
        function_type = type(returns_locals)

        def crafted(bytecode):
            code = code_type(0, 0, 1, 0x43, bytecode, (None,), ('target',), (), '<crafted>', 'crafted', 1, '')
            return function_type(code, {'target': 1})

        self.assertEqual(self.failure(crafted('e\x00\x00S')), 'SystemError: no locals when loading target')
        self.assertEqual(self.failure(crafted('d\x00\x00Z\x00\x00d\x00\x00S')), "SystemError: no locals found when storing 'target'")
        self.assertEqual(self.failure(crafted('[\x00\x00d\x00\x00S')), 'SystemError: no locals when deleting target')

    def test_eval_argument_count_messages(self):
        self.assertEqual(self.failure(eval), 'TypeError: eval expected at least 1 arguments, got 0')
        self.assertEqual(self.failure(eval, '1', {}, {}, {}), 'TypeError: eval expected at most 3 arguments, got 4')

    def test_call_messages_truncate_names(self):
        code = (lambda a: a).func_code
        renamed = type(code)(code.co_argcount, code.co_nlocals, code.co_stacksize, code.co_flags, code.co_code, code.co_consts, code.co_names, code.co_varnames, code.co_filename, 'f' * 300, code.co_firstlineno, code.co_lnotab)
        function = type(returns_locals)(renamed, {})
        name = 'f' * 200
        self.assertEqual(self.failure(function), 'TypeError: ' + name + '() takes exactly 1 argument (0 given)')
        self.assertEqual(self.failure(lambda: function(**{'k' * 500: 1})), 'TypeError: ' + name + "() got an unexpected keyword argument '" + 'k' * 400 + "'")
        self.assertEqual(self.failure(lambda: function(**{'a\x00b': 1})), 'TypeError: ' + name + "() got an unexpected keyword argument 'a'")
        self.assertEqual(self.failure(lambda: function(1, **{'a\x00': 2})), 'TypeError: ' + name + "() got an unexpected keyword argument 'a'")
        self.assertEqual(self.failure(lambda: function(1, a=2)), 'TypeError: ' + name + "() got multiple values for keyword argument 'a'")
        self.assertEqual(self.failure(lambda: function(**{u'\xe9': 1})), 'TypeError: ' + name + "() got an unexpected keyword argument '?'")

    def test_code_constructor_messages(self):
        code_type = type(returns_locals.func_code)
        valid = (0, 0, 1, 0x43, 'd\x00\x00S', (None,), (), (), '<crafted>', 'crafted', 1, '')

        def replaced(index, value):
            arguments = list(valid)
            arguments[index] = value
            return tuple(arguments)

        self.assertEqual(self.failure(code_type, 1), 'TypeError: code() takes at least 12 arguments (1 given)')
        self.assertEqual(self.failure(code_type, *(valid + ((), (), ()))), 'TypeError: code() takes at most 14 arguments (15 given)')
        self.assertEqual(code_type(*valid, **{'ignored': 1}).co_name, 'crafted')
        self.assertEqual(self.failure(code_type, *replaced(0, -1)), 'ValueError: code: argcount must not be negative')
        self.assertEqual(self.failure(code_type, *replaced(1, -1)), 'ValueError: code: nlocals must not be negative')
        self.assertEqual(self.failure(code_type, *replaced(0, 1.5)), 'TypeError: integer argument expected, got float')
        self.assertEqual(self.failure(code_type, *replaced(10, 'x')), 'TypeError: an integer is required')
        self.assertEqual(self.failure(code_type, *replaced(3, 2 ** 40)), 'OverflowError: signed integer is greater than maximum')
        self.assertEqual(self.failure(code_type, *replaced(4, u'd\x00\x00S')), 'TypeError: code() argument 5 must be string, not unicode')
        self.assertEqual(self.failure(code_type, *replaced(5, [None])), 'TypeError: code() argument 6 must be tuple, not list')
        self.assertEqual(self.failure(code_type, *replaced(9, None)), 'TypeError: code() argument 10 must be string, not None')
        self.assertEqual(self.failure(code_type, *(valid + ((), []))), 'TypeError: code() argument 14 must be tuple, not list')
        self.assertEqual(self.failure(code_type, *replaced(6, (1,))), "TypeError: name tuples must contain only strings, not 'int'")

    def test_attribute_cache_entry_moved_by_nested_lookup(self):
        class Slotted(object):
            __slots__ = ('x',)

        class Plain(object):
            pass

        def first(value):
            return value.x

        def second(value):
            return value.x

        armed = []
        slotted = Slotted()
        slotted.x = 'slot'

        class Key(object):
            def __hash__(self):
                return hash('x')

            def __eq__(self, other):
                if armed:
                    armed.pop()(slotted)
                return False

        plain = Plain()
        for index in range(6):
            plain.__dict__['f%d' % index] = index
        plain.__dict__[Key()] = 0
        plain.__dict__['x'] = 'dict'
        other = Plain()
        other.x = 'other'
        self.assertEqual(second(other), 'other')
        for get in (first, second):
            armed.append(get)
            self.assertEqual(get(plain), 'dict')
            self.assertEqual(armed, [])
            self.assertEqual(get(slotted), 'slot')
            self.assertEqual(get(slotted), 'slot')

    def test_attribute_lookup_keeps_replaced_instance_dict(self):
        armed = [False]
        others = []

        class Key(object):
            def __hash__(self):
                return hash('x')

            def __eq__(self, other):
                if armed[0]:
                    armed[0] = False
                    holder.__dict__ = {}
                    others.extend({'x': index} for index in range(4))
                return False

        class Holder(object):
            pass

        def get(value):
            return value.x

        holder = Holder()
        holder.__dict__[Key()] = 0
        armed[0] = True
        self.assertEqual(self.failure(get, holder), "AttributeError: 'Holder' object has no attribute 'x'")

    def test_attribute_store_keeps_replaced_dict(self):
        armed = []

        class Key(object):
            def __hash__(self):
                return hash('x')

            def __eq__(self, other):
                if armed:
                    armed.pop().__dict__ = {}
                return False

        class Holder(object):
            pass

        def put(value, item):
            value.x = item

        def put_generic(value, item):
            setattr(value, 'x', item)

        def delete(value):
            del value.x

        def function():
            pass

        put(Holder(), 0)
        for target, store in ((Holder(), put), (Holder(), put_generic), (function, put)):
            target.__dict__[Key()] = 0
            armed.append(target)
            store(target, 1)
            self.assertEqual(target.__dict__, {})
        for target in (Holder(), function):
            target.__dict__[Key()] = 0
            target.__dict__['x'] = 1
            armed.append(target)
            delete(target)
            self.assertEqual(target.__dict__, {})

    def test_class_attribute_cache_keeps_found_attribute(self):
        armed = [False]

        class Key(object):
            def __hash__(self):
                return hash('__get__')

            def __eq__(self, other):
                if armed[0]:
                    armed[0] = False
                    del Owner.x
                return False

        Value = type('Value', (object,), {Key(): 1})

        class Owner(object):
            pass

        def get():
            return Owner.x

        Owner.x = Value()
        armed[0] = True
        self.assertEqual(type(get()).__name__, 'Value')

    def test_container_opcodes_reject_other_kinds(self):
        code_type = type(returns_locals.func_code)

        def run(bytecode):
            code = code_type(0, 0, 4, 0x40, bytecode, (0, 'value', 'key', None), (), (), '<crafted>', '<module>', 1, '')
            return eval(code)

        pushes = 'd\x00\x00d\x01\x00'
        rest = '\x01d\x03\x00S'
        for operation in ('^\x01\x00', '\x92\x01\x00', 'd\x02\x00\x93\x01\x00', 'd\x02\x006'):
            message = self.failure(run, pushes + operation + rest)
            self.assertTrue(message.startswith('SystemError: '), message)
            self.assertTrue(message.endswith('bad argument to internal function'), message)

    def test_instance_dict_lookup_ignores_key_comparison_errors(self):
        calls = []

        class Key(object):
            def __init__(self, name):
                self.name = name

            def __hash__(self):
                return hash(self.name)

            def __eq__(self, other):
                calls.append(other)
                raise ValueError('comparison')

        class Holder(object):
            x = 'class'

        def get(value):
            return value.x

        def get_missing(value):
            return value.y

        holder = Holder()
        holder.__dict__[Key('x')] = 1
        holder.__dict__[Key('y')] = 2
        for repeat in range(2):
            del calls[:]
            self.assertEqual(get(holder), 'class')

            class Unaffected(object):
                pass

            self.assertEqual(self.failure(get_missing, holder), "AttributeError: 'Holder' object has no attribute 'y'")
            self.assertEqual(calls, ['x', 'y'])
            self.assertFalse(hasattr(holder, 'y'))

    def test_class_statement_finds_its_metaclass(self):
        class Key(object):
            def __hash__(self):
                return hash('__metaclass__')

            def __eq__(self, other):
                raise RuntimeError('comparison')

        class Body(object):
            locals()[Key()] = 1

        namespace = {Key(): 1}
        exec 'class Classic: pass\nclassic_kind = type(Classic).__name__' in namespace

        class Old:
            pass

        class FromClassic(Old):
            pass

        class Meta(type):
            @property
            def __class__(cls):
                raise RuntimeError('no class')

        Base = Meta('Base', (object,), {})

        class Derived(Base):
            pass

        self.assertTrue(type(Body) is type)
        self.assertEqual(namespace['classic_kind'], 'classobj')
        self.assertTrue(type(FromClassic) is type(Old))
        self.assertTrue(type(Derived) is Meta)

    def test_bare_raise_checks_the_handled_traceback(self):
        def replace(traceback):
            try:
                raise ValueError('inner')
            except ValueError:
                pass
            sys._getframe().f_exc_traceback = traceback

        def reraise(traceback):
            try:
                raise KeyError('outer')
            except KeyError:
                replace(traceback)
                raise

        for traceback in (12345, ['not', 'a', 'traceback']):
            self.assertEqual(self.failure(reraise, traceback), 'TypeError: raise: arg 3 must be a traceback or None')

    def test_sys_exit_raises_system_exit_with_a_subclass_instance(self):
        class Code(SystemExit):
            pass

        class Capture(object):
            def __init__(self):
                self.parts = []

            def write(self, text):
                self.parts.append(text)

        class Exiting(object):
            def __del__(self):
                sys.exit(Code(7))

        capture = Capture()
        saved = sys.stderr
        sys.stderr = capture
        try:
            Exiting()
        finally:
            sys.stderr = saved
        self.assertTrue(''.join(capture.parts).startswith('Exception SystemExit: Code(7,) in '))
        try:
            sys.exit(Code(8))
        except SystemExit:
            self.assertTrue(sys.exc_info()[0] is Code)

    def test_frame_exception_fields_outlive_exit_and_yield(self):
        def keep():
            frame = sys._getframe()
            frame.f_exc_value = KeyError('value')
            frame.f_exc_traceback = 'traceback'
            return frame

        def generator():
            frame = sys._getframe()
            frame.f_exc_value = KeyError('stray')
            yield 1
            yield repr(frame.f_exc_value)

        frame = keep()
        self.assertEqual((repr(frame.f_exc_value), frame.f_exc_traceback), ("KeyError('value',)", 'traceback'))
        self.assertEqual(list(generator()), [1, "KeyError('stray',)"])
        self.assertEqual(sys.exc_info(), (None, None, None))

    def test_finalizer_assigns_exception_fields_of_a_returning_frame(self):
        class Assigner(object):
            def __init__(self, holder):
                self.holder = holder

            def __del__(self):
                frame = self.holder.pop()
                frame.f_exc_type = KeyError
                frame.f_exc_value = KeyError('assigned while the frame returns')

        def run():
            holder = [sys._getframe()]
            assigner = Assigner(holder)
            del holder

        for repeat in range(3):
            run()
        self.assertEqual(sys.exc_info(), (None, None, None))

    def test_inplace_concatenation_keeps_other_references(self):
        def build(count, piece):
            text = ''
            snapshots = []
            for index in xrange(count):
                text += piece
                if index % 97 == 0:
                    snapshots.append(text)
            return text, snapshots

        class Text(str):
            pass

        for count in (1, 9, 1000, 20000):
            text, snapshots = build(count, 'ab')
            self.assertEqual(text, 'ab' * count)
            self.assertEqual(snapshots, ['ab' * (index + 1) for index in range(0, count, 97)])
            self.assertEqual({text: count}['ab' * count], count)
            self.assertEqual(Text(text), text)
        namespace = {}
        exec "text = ''\nfor index in range(300):\n    text += 'ab'" in namespace
        self.assertEqual(namespace['text'], 'ab' * 300)

    def test_concatenation_keeps_other_references(self):
        def build(count, piece):
            text = ''
            cell = ''
            snapshots = []

            def peek():
                return cell
            for index in xrange(count):
                text = text + piece
                cell += piece
                if index % 97 == 0:
                    snapshots.append((text, peek()))
            return text, peek(), snapshots

        for count in (1, 9, 1000, 20000):
            text, cell, snapshots = build(count, 'ab')
            self.assertEqual(text, 'ab' * count)
            self.assertEqual(cell, 'ab' * count)
            self.assertEqual(snapshots, [('ab' * (index + 1),) * 2 for index in range(0, count, 97)])
        prefix = ''.join(['ab'] * 20)
        alias = prefix
        prefix = prefix + 'cd' + 'ef'
        joined = alias + 'gh' + 'ij'
        self.assertEqual((alias, prefix, joined), ('ab' * 20, 'ab' * 20 + 'cdef', 'ab' * 20 + 'ghij'))
        namespace = {}
        exec "text = ''\nfor index in range(300):\n    text = text + 'ab'\nclass Holder(object):\n    text = ''\n    for index in range(300):\n        text += 'cd'\n" in namespace
        self.assertEqual(namespace['text'], 'ab' * 300)
        self.assertEqual(namespace['Holder'].text, 'cd' * 300)

    def test_name_store_concatenation_with_colliding_key(self):
        # A key that collides with the name runs __eq__ while `s += t` or
        # `s = s + t` unbinds s to grow it in place: a string that __eq__
        # takes keeps its value, and the comparison's errors stay out of the
        # statement.
        state = {'calls': 0, 'fail_at': 0, 'take_at': 0, 'busy': False}
        held = []
        namespace = {}

        class Key(str):
            def __hash__(self):
                return hash('s')

            def __eq__(self, other):
                if state['busy']:
                    return False
                state['calls'] += 1
                if state['calls'] == state['fail_at']:
                    raise ZeroDivisionError('probe')
                if state['calls'] == state['take_at']:
                    state['busy'] = True
                    value = namespace.get('s')
                    held.append((value, None if value is None else bytearray(value)))
                    state['busy'] = False
                return False

        cases = [(call, 0) for call in range(1, 7)] + [(0, call) for call in range(1, 7)]
        for statement in ('s += t', 's = s + t'):
            for length in (20, 64, 500):
                original = 'ab' * length + 'c'
                for suffix in ('d', 'cd' * 300):
                    for fail_at, take_at in cases:
                        namespace.clear()
                        namespace[Key('k')] = 0
                        namespace['t'] = suffix
                        exec "s = ''.join(['ab'] * %d)\ns += 'c'\n" % length in namespace
                        del held[:]
                        state.update(calls=0, fail_at=fail_at, take_at=take_at)
                        try:
                            exec statement + "\nsize = len(s)\n" in namespace
                        except ZeroDivisionError:
                            self.assertNotIn('size', namespace)
                            self.assertEqual(namespace.get('s', original), original)
                        else:
                            self.assertEqual(namespace['s'], original + suffix)
                            self.assertEqual(namespace['size'], len(original) + len(suffix))
                        state.update(fail_at=0, take_at=0)
                        for value, snapshot in held:
                            if value is not None:
                                self.assertEqual(value, str(snapshot))
                                self.assertEqual(hash(value), hash(str(snapshot)))
                                self.assertIn(str(snapshot), {value: 1})
