"""Project-authored Python 2 statement semantics: exec, the name opcodes and code objects."""

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
