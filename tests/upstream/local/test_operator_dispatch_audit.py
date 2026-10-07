"""Project-authored Python 2 operator dispatch and coercion regressions."""

import sys
import unittest


def _failure(invoke):
    try:
        invoke()
    except BaseException as error:
        return type(error).__name__, str(error)
    raise AssertionError('operation must fail')


class OperatorDispatchAudit(unittest.TestCase):
    def test_classic_success_precedes_right_coercion(self):
        events = []
        class Left:
            def __coerce__(self, other):
                events.append('left-coerce')
                return None
            def __add__(self, other):
                events.append('left-add')
                return 17
        class Right:
            def __coerce__(self, other):
                events.append('right-coerce')
                return 3, 4
        self.assertEqual(Left() + Right(), 17)
        self.assertEqual(events, ['left-coerce', 'left-add'])

    def test_classic_coercion_uses_replacement_receiver(self):
        events = []
        class Replacement:
            def __add__(self, other):
                events.append(('replacement', other))
                return 23
        replacement = Replacement()
        class Source:
            def __coerce__(self, other):
                return replacement, 8
            def __add__(self, other):
                raise AssertionError('original receiver must be replaced')
        self.assertEqual(Source() + 2, 23)
        self.assertEqual(events, [('replacement', 8)])

    def test_classic_coercion_changed_type_uses_normal_dispatch(self):
        events = []
        class Replacement(object):
            def __add__(self, other):
                events.append(other)
                return 29
        replacement = Replacement()
        class Source:
            def __coerce__(self, other):
                return replacement, 8
        self.assertEqual(Source() + 2, 29)
        self.assertEqual(events, [8])

    def test_reflected_classic_coercion_preserves_operand_order(self):
        class Source:
            def __coerce__(self, other):
                return 7, 3
        self.assertEqual(11 - Source(), -4)
        self.assertEqual(11 / Source(), 0)

    def test_classic_inplace_coerces_before_hook(self):
        events = []
        class Source:
            def __coerce__(self, other):
                events.append('coerce')
                return self, 8
            def __iadd__(self, other):
                events.append(('inplace', other))
                return 31
        value = Source()
        value += 2
        self.assertEqual(value, 31)
        self.assertEqual(events, ['coerce', ('inplace', 8)])

    def test_classic_inplace_retries_normal_slot_after_notimplemented(self):
        events = []
        class Source:
            def __iadd__(self, other):
                events.append('inplace')
                return NotImplemented
            def __add__(self, other):
                events.append('left')
                return NotImplemented
        class Right(object):
            def __radd__(self, other):
                events.append('right')
                return 37
        value = Source()
        value += Right()
        self.assertEqual(value, 37)
        self.assertEqual(events, ['inplace', 'left', 'left', 'right'])

    def test_classic_inplace_normal_success_stops_retry(self):
        events = []
        class Source:
            def __iadd__(self, other):
                events.append('inplace')
                return NotImplemented
            def __add__(self, other):
                events.append('left')
                return 41
        value = Source()
        value += 2
        self.assertEqual(value, 41)
        self.assertEqual(events, ['inplace', 'left'])

    def test_classic_dynamic_missing_coerce_preserves_handled_error(self):
        events = []
        class Source:
            def __getattr__(self, name):
                events.append(name)
                raise AttributeError(name)
            def __add__(self, other):
                return 43
        retained = ValueError('outer')
        try:
            raise retained
        except ValueError:
            self.assertEqual(Source() + 2, 43)
            self.assertIs(sys.exc_info()[1], retained)
        self.assertEqual(events, ['__coerce__'])

    def test_classic_dynamic_coerce_hook_is_called(self):
        events = []
        class Source:
            def __getattr__(self, name):
                events.append(name)
                if name == '__coerce__':
                    return lambda other: (3, 2)
                raise AttributeError(name)
        self.assertEqual(Source() + 5, 5)
        self.assertEqual(events, ['__coerce__'])

    def test_classic_getter_error_and_called_attributeerror_propagate(self):
        retained = LookupError('getter')
        class Getter:
            def __getattr__(self, name):
                raise retained
        try:
            Getter() + 2
        except LookupError as error:
            self.assertIs(error, retained)
        else:
            self.fail('lookup error must propagate')
        retained = AttributeError('called hook')
        class Called:
            def __coerce__(self, other):
                raise retained
        try:
            Called() + 2
        except AttributeError as error:
            self.assertIs(error, retained)
        else:
            self.fail('callback AttributeError must propagate')

    def test_coercion_tuple_lives_through_replacement_callback(self):
        events = []
        class Pair(tuple):
            def __del__(self):
                events.append('pair-final')
        class Replacement:
            def __add__(self, other):
                events.append('replacement')
                return 47
        replacement = Replacement()
        class Source:
            def __coerce__(self, other):
                return Pair((replacement, 8))
        self.assertEqual(Source() + 2, 47)
        self.assertEqual(events, ['replacement', 'pair-final'])

    def test_same_numeric_subtype_notimplemented_rejects_raw_fallback(self):
        events = []
        class Number(int):
            def __add__(self, other):
                events.append('left')
                return NotImplemented
            def __radd__(self, other):
                raise AssertionError('same type must not reflect')
        self.assertEqual(_failure(lambda: Number(1) + Number(2)),
                         ('TypeError', "unsupported operand type(s) for +: 'Number' and 'Number'"))
        self.assertEqual(events, ['left'])

    def test_distinct_numeric_builtin_slot_remains_available(self):
        class Number(int):
            def __add__(self, other):
                return NotImplemented
        self.assertEqual(Number(1) + 2, 3)
        self.assertIs(type(Number(1) + 2L), long)
        self.assertIs(type(Number(1) + 2.0), float)

    def test_same_set_subtype_notimplemented_rejects_raw_fallback(self):
        class Values(set):
            def __sub__(self, other):
                return NotImplemented
        self.assertEqual(_failure(lambda: Values([1]) - Values([2])),
                         ('TypeError', "unsupported operand type(s) for -: 'Values' and 'Values'"))

    def test_reflected_descriptor_comparison_controls_subclass_priority(self):
        events = []
        class Reverse(object):
            def __init__(self, label):
                self.label = label
            def __get__(self, receiver, owner):
                events.append(('get', self.label, receiver is None))
                if receiver is None:
                    return self
                return lambda other: 53
            def __ne__(self, other):
                events.append(('different', self.label, other.label))
                return False
        class Left(object):
            __radd__ = Reverse('base')
            def __add__(self, other):
                events.append('left')
                return 59
        class Right(Left):
            __radd__ = Reverse('child')
        self.assertEqual(Left() + Right(), 59)
        self.assertEqual(events, [('get', 'child', True), ('get', 'base', True),
                                  ('different', 'base', 'child'), 'left'])

    def test_reflected_descriptor_error_is_quiet_and_preserves_handled_error(self):
        events = []
        class Reverse(object):
            def __get__(self, receiver, owner):
                if receiver is None:
                    return self
                return lambda other: 61
            def __ne__(self, other):
                events.append('different')
                raise KeyboardInterrupt('comparison')
        class Left(object):
            __radd__ = Reverse()
            def __add__(self, other):
                return 67
        class Right(Left):
            __radd__ = Reverse()
        retained = LookupError('outer')
        try:
            raise retained
        except LookupError:
            self.assertEqual(Left() + Right(), 67)
            self.assertIs(sys.exc_info()[1], retained)
        self.assertEqual(events, ['different'])

    def test_same_type_does_not_call_reflected_hook(self):
        events = []
        class Value(object):
            def __add__(self, other):
                events.append('left')
                return NotImplemented
            def __radd__(self, other):
                events.append('right')
                return 71
        self.assertEqual(_failure(lambda: Value() + Value()),
                         ('TypeError', "unsupported operand type(s) for +: 'Value' and 'Value'"))
        self.assertEqual(events, ['left'])

    def test_inplace_notimplemented_calls_hook_once(self):
        events = []
        class Value(object):
            def __iadd__(self, other):
                events.append('inplace')
                return NotImplemented
            def __add__(self, other):
                events.append('left')
                return 73
        value = Value()
        value += 2
        self.assertEqual(value, 73)
        self.assertEqual(events, ['inplace', 'left'])

    def test_inplace_unsupported_error_uses_augmented_symbol(self):
        class Value(object):
            def __iadd__(self, other):
                return NotImplemented
            def __add__(self, other):
                return NotImplemented
        def operation():
            value = Value()
            value += Value()
        self.assertEqual(_failure(operation),
                         ('TypeError', "unsupported operand type(s) for +=: 'Value' and 'Value'"))

    def test_inplace_preserves_callback_error_identity_and_text(self):
        retained = TypeError("unsupported operand type(s) for +: 'Value' and 'Value'")
        class Value(object):
            def __iadd__(self, other):
                return NotImplemented
            def __add__(self, other):
                raise retained
        value = Value()
        try:
            value += Value()
        except TypeError as error:
            self.assertIs(error, retained)
            self.assertEqual(str(error), "unsupported operand type(s) for +: 'Value' and 'Value'")
        else:
            self.fail('callback error must propagate')

    def test_newstyle_inplace_power_has_no_normal_retry(self):
        events = []
        class Value(object):
            def __ipow__(self, other):
                events.append('inplace')
                return NotImplemented
            def __pow__(self, other):
                events.append('normal')
                return 79
        def operation():
            value = Value()
            value **= 2
        self.assertEqual(_failure(operation),
                         ('TypeError', "unsupported operand type(s) for ** or pow(): 'Value' and 'int'"))
        self.assertEqual(events, ['inplace'])

    def test_classic_inplace_power_tries_normal_slot_once(self):
        events = []
        class Value:
            def __ipow__(self, other):
                events.append('inplace')
                return NotImplemented
            def __pow__(self, other):
                events.append('normal')
                return 83
        value = Value()
        value **= 2
        self.assertEqual(value, 83)
        self.assertEqual(events, ['inplace', 'normal'])

    def test_power_none_uses_binary_hook_arity_and_fallback(self):
        events = []
        class Number(int):
            def __pow__(self, *arguments):
                events.append(len(arguments))
                return NotImplemented
        self.assertEqual(pow(Number(2), 3, None), 8)
        self.assertEqual(events, [1])
        class Classic:
            def __coerce__(self, other):
                return 3, 2
        self.assertEqual(pow(Classic(), 7, None), 9)

    def test_ternary_power_ignores_reflected_python_hook(self):
        events = []
        class Exponent(int):
            def __rpow__(self, other):
                events.append('reflected')
                return 89
        self.assertEqual(pow(2, Exponent(3), 5), 3)
        self.assertEqual(events, [])

    def test_ternary_power_distinguishes_overridden_long_slot(self):
        events = []
        class Number(long):
            def __pow__(self, *arguments):
                events.append(len(arguments))
                return NotImplemented
        self.assertEqual(_failure(lambda: pow(Number(2), 3, 5)),
                         ('TypeError', "unsupported operand type(s) for pow(): 'Number', 'int', 'int'"))
        self.assertEqual(events, [2])
        self.assertEqual(pow(Number(2), 3L, 5), 3L)
        self.assertEqual(events, [2, 2])

    def test_ternary_power_builtin_slot_errors_and_priorities(self):
        self.assertEqual(_failure(lambda: pow(2, 3, 1.5)),
                         ('TypeError', 'pow() 3rd argument not allowed unless all arguments are integers'))
        self.assertEqual(_failure(lambda: pow(2, 3, 1j)),
                         ('ValueError', 'complex modulo'))
        self.assertEqual(_failure(lambda: pow(2, -3, object())),
                         ('TypeError', 'pow() 2nd argument cannot be negative when 3rd argument specified'))
        self.assertEqual(_failure(lambda: pow(2, 3L, object())),
                         ('TypeError', "unsupported operand type(s) for pow(): 'long', 'long', 'object'"))

    def test_ternary_classic_hook_receives_two_arguments_without_coercion(self):
        events = []
        class Classic:
            def __coerce__(self, other):
                raise AssertionError('ternary classic hook does not coerce')
            def __pow__(self, *arguments):
                events.append(arguments)
                return 97
        self.assertEqual(pow(Classic(), 3, 5), 97)
        self.assertEqual(events, [(3, 5)])

    def test_ternary_legacy_coercion_error_preserves_handled_state(self):
        events = []
        class Value(object):
            def __pow__(self, *arguments):
                events.append('power')
                return NotImplemented
            def __coerce__(self, other):
                events.append('coerce')
                raise LookupError('discarded coercion')
        retained = ValueError('outer')
        try:
            raise retained
        except ValueError:
            self.assertEqual(_failure(lambda: pow(Value(), 3, object())),
                             ('TypeError', "unsupported operand type(s) for pow(): 'Value', 'int', 'object'"))
            self.assertIs(sys.exc_info()[1], retained)
        self.assertEqual(events, ['power', 'coerce'])


if __name__ == '__main__':
    unittest.main()
