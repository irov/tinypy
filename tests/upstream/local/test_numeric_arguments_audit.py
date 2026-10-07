"""Project-authored numeric descriptor arguments and immutable snapshots."""

import sys
import unittest


def failure(callback, *args, **kwargs):
    try:
        callback(*args, **kwargs)
    except BaseException as error:
        return error
    raise AssertionError('operation must raise')


class Integer(int):
    pass


class Long(long):
    pass


class Float(float):
    pass


class Complex(complex):
    pass


OWNERS = (int, long, float, complex)
SUBTYPES = (Integer, Long, Float, Complex)
NOARGS = ('conjugate', '__getnewargs__', '__trunc__', 'bit_length',
          'is_integer', 'as_integer_ratio', 'hex')


class NumericArgumentsAudit(unittest.TestCase):
    def test_numeric_noargs_methods_report_user_argument_count(self):
        for owner, subtype in zip(OWNERS, SUBTYPES):
            for name in NOARGS:
                if not hasattr(owner, name):
                    continue
                for value in (owner(3), subtype(3)):
                    for count in (1, 2):
                        args = (None,) * count
                        message = '%s() takes no arguments (%d given)' % (name, count)
                        self.assertEqual(failure(getattr(value, name), *args).args, (message,))
                        self.assertEqual(failure(getattr(owner, name), value, *args).args, (message,))
        sys.exc_clear()

    def test_numeric_noargs_keyword_error_precedes_count(self):
        for owner in OWNERS:
            for name in NOARGS:
                if hasattr(owner, name):
                    self.assertEqual(failure(getattr(owner(3), name), None, unexpected=1).args,
                                     ('%s() takes no keyword arguments' % name,))
        sys.exc_clear()

    def test_numeric_unbound_descriptors_require_receiver(self):
        for owner in OWNERS:
            for name in NOARGS:
                if hasattr(owner, name):
                    message = "descriptor '%s' of '%s' object needs an argument" % (name, owner.__name__)
                    self.assertEqual(failure(getattr(owner, name)).args, (message,))
        sys.exc_clear()

    def test_numeric_slot_wrappers_report_count(self):
        for owner in OWNERS:
            names = ('__coerce__', '__cmp__', '__hex__', '__oct__')
            for name in names:
                if not hasattr(owner, name):
                    continue
                expected = 1 if name in ('__coerce__', '__cmp__') else 0
                for count in (0, 2, 3):
                    if count != expected:
                        self.assertEqual(failure(getattr(owner(3), name), *([None] * count)).args,
                                         ('expected %d arguments, got %d' % (expected, count),))
        sys.exc_clear()

    def test_numeric_slot_wrappers_keyword_errors_match_bound_and_unbound(self):
        for owner in OWNERS:
            for name in ('__coerce__', '__cmp__', '__hex__', '__oct__'):
                if hasattr(owner, name):
                    value = owner(3)
                    expected = ("wrapper %s doesn't take keyword arguments" % name,)
                    self.assertEqual(failure(getattr(value, name), unexpected=1).args, expected)
                    self.assertEqual(failure(getattr(owner, name), value, unexpected=1).args, expected)
        sys.exc_clear()

    def test_cmp_requires_same_builtin_family_for_other_operand(self):
        for owner, other in ((int, 3L), (long, 3), (long, True), (int, None)):
            message = "%s.__cmp__(x,y) requires y to be a '%s', not a '%s'" % (
                owner.__name__, owner.__name__, type(other).__name__)
            self.assertEqual(failure(owner(3).__cmp__, other).args, (message,))
        self.assertEqual(int.__cmp__(Integer(3), True), 1)
        self.assertEqual(long.__cmp__(Long(3), 3L), 0)
        sys.exc_clear()

    def test_getnewargs_copies_exact_and_subtype_long_payload(self):
        for owner in (long, Long):
            for payload in (0L, -3L, 2 ** 80):
                value = owner(payload)
                result = value.__getnewargs__()
                self.assertIs(type(result), tuple)
                self.assertIs(type(result[0]), long)
                self.assertEqual(result, (payload,))
                self.assertIsNot(result[0], value)

    def test_getnewargs_float_zero_is_a_fresh_base_float(self):
        for owner in (float, Float):
            for payload in (0.0, -0.0, 3.5):
                value = owner(payload)
                first, second = value.__getnewargs__(), value.__getnewargs__()
                self.assertIs(type(first[0]), float)
                self.assertIsNot(first[0], value)
                self.assertIsNot(first[0], second[0])
                self.assertEqual(first[0].hex(), payload.hex())

    def test_complex_getnewargs_components_are_distinct_base_floats(self):
        for owner in (complex, Complex):
            value = owner(0.0, 0.0)
            result = value.__getnewargs__()
            self.assertEqual(result, (0.0, 0.0))
            self.assertEqual([type(item) for item in result], [float, float])
            self.assertIsNot(result[0], result[1])

    def test_getnewargs_ignores_numeric_conversion_overrides(self):
        class Value(Long):
            def __long__(self):
                raise AssertionError('payload snapshot must ignore __long__')
            def __int__(self):
                raise AssertionError('payload snapshot must ignore __int__')
        self.assertEqual(Value(2 ** 80).__getnewargs__(), (2 ** 80,))

    def test_getnewargs_bool_and_integer_subtype_return_base_int(self):
        for value in (True, False, Integer(1000)):
            result = value.__getnewargs__()
            self.assertIs(type(result[0]), int)
            self.assertEqual(result[0], int(value))

    def test_float_class_methods_use_their_native_arity_styles(self):
        for name in ('fromhex', '__getformat__'):
            self.assertEqual(failure(getattr(float, name)).args,
                             ('%s() takes exactly one argument (0 given)' % name,))
            self.assertEqual(failure(getattr(Float, name), None, None).args,
                             ('%s() takes exactly one argument (2 given)' % name,))
        self.assertEqual(failure(float.__setformat__, 'float').args,
                         ('__setformat__() takes exactly 2 arguments (1 given)',))
        sys.exc_clear()

    def test_float_class_methods_reject_keywords_before_conversion(self):
        for owner in (float, Float):
            for name in ('fromhex', '__getformat__', '__setformat__'):
                self.assertEqual(failure(getattr(owner, name), None, unknown=1).args,
                                 ('%s() takes no keyword arguments' % name,))
        sys.exc_clear()

    def test_fromhex_rejects_nontext_without_conversion_callbacks(self):
        class Text(object):
            def __str__(self):
                raise AssertionError('fromhex must not call __str__')
        for value in (None, 3, Text()):
            message = 'expected string or Unicode object, %s found' % type(value).__name__
            self.assertEqual(failure(float.fromhex, value).args, (message,))
        self.assertEqual(Float.fromhex(u'0x1.8p1'), 3.0)
        sys.exc_clear()

    def test_fromhex_unicode_error_and_recovery(self):
        self.assertIs(type(failure(float.fromhex, u'\xe9')), UnicodeEncodeError)
        self.assertEqual(float.fromhex('0x1p2'), 4.0)
        sys.exc_clear()

    def test_getformat_requires_byte_string(self):
        for value in (None, 3, u'float'):
            self.assertEqual(failure(float.__getformat__, value).args,
                ('__getformat__() argument must be string, not %s' % type(value).__name__,))
        sys.exc_clear()

    def test_getformat_uses_c_string_prefix_and_rejects_unknown_kind(self):
        self.assertEqual(float.__getformat__('float\x00ignored'), float.__getformat__('float'))
        self.assertEqual(failure(float.__getformat__, 'unknown').args,
                         ("__getformat__() argument 1 must be 'double' or 'float'",))
        sys.exc_clear()

    def test_setformat_text_parser_validates_both_arguments_before_kind(self):
        self.assertEqual(failure(float.__setformat__, 'bad', None).args,
                         ('__setformat__() argument 2 must be string, not None',))
        self.assertEqual(failure(float.__setformat__, 'float', 'unknown\x00tail').args,
                         ('__setformat__() argument 2 must be string without null bytes, not str',))
        self.assertIs(type(failure(float.__setformat__, u'\xe9', None)), UnicodeEncodeError)
        sys.exc_clear()

    def test_setformat_invalid_kind_and_format_have_distinct_errors(self):
        self.assertEqual(failure(float.__setformat__, 'bad', 'unknown').args,
                         ("__setformat__() argument 1 must be 'double' or 'float'",))
        self.assertEqual(failure(float.__setformat__, 'float', 'bad').args,
            ("__setformat__() argument 2 must be 'unknown', 'IEEE, little-endian' or 'IEEE, big-endian'",))
        sys.exc_clear()

    def test_setformat_unknown_roundtrip_restores_native_state(self):
        saved = float.__getformat__('float')
        try:
            self.assertIs(float.__setformat__(u'float', u'unknown'), None)
            self.assertEqual(Float.__getformat__('float'), 'unknown')
        finally:
            float.__setformat__('float', saved)
        self.assertEqual(float.__getformat__('float'), saved)

    def test_numeric_format_direct_unicode_spec_returns_bytes(self):
        for owner in OWNERS + SUBTYPES:
            value = owner(3)
            self.assertIs(type(value.__format__(u'')), str)
            self.assertEqual(value.__format__(u''), value.__format__(''))
            self.assertIs(type(format(value, u'')), unicode)

    def test_numeric_format_unicode_subtype_calls_str_and_preserves_outer_kind(self):
        events = []
        class Spec(unicode):
            def __str__(self):
                events.append('str')
                return '+08.1f'
        for owner in OWNERS + SUBTYPES:
            value = owner(3)
            # Complex formatting does not permit zero padding.
            spec = Spec('not the rendered spec')
            if issubclass(owner, complex):
                self.assertIs(type(failure(value.__format__, spec)), ValueError)
                self.assertIs(type(failure(format, value, spec)), ValueError)
            else:
                self.assertEqual(value.__format__(spec), '+00003.0')
                self.assertEqual(format(value, spec), u'+00003.0')
        self.assertEqual(events, ['str'] * 16)
        sys.exc_clear()

    def test_numeric_format_str_callback_error_identity_and_recovery(self):
        marker = LookupError('format spec')
        events = []
        class Spec(unicode):
            def __str__(self):
                events.append('str')
                raise marker
        for owner in OWNERS + SUBTYPES:
            value = owner(3)
            self.assertIs(failure(value.__format__, Spec('d')), marker)
            self.assertIs(failure(format, value, Spec('d')), marker)
            self.assertEqual(value.__format__(''), str(value))
        self.assertEqual(events, ['str'] * 16)
        sys.exc_clear()

    def test_numeric_format_reports_exact_parser_and_type_errors(self):
        for owner in OWNERS:
            value = owner(3)
            self.assertEqual(failure(value.__format__).args,
                             ('__format__() takes exactly 1 argument (0 given)',))
            self.assertEqual(failure(value.__format__, None).args,
                             ('__format__ requires str or unicode',))
            self.assertEqual(failure(value.__format__, None, unknown=1).args,
                             ('__format__() takes no keyword arguments',))
        sys.exc_clear()

    def test_numeric_format_unknown_code_reports_actual_subtype(self):
        for value in (3.0, Float(3), 3j, Complex(3j), 3L, Long(3)):
            code = 'd' if isinstance(value, (float, complex)) else 's'
            message = "Unknown format code '%s' for object of type '%s'" % (code, type(value).__name__)
            self.assertEqual(failure(value.__format__, code).args, (message,))
        sys.exc_clear()


if __name__ == '__main__':
    unittest.main()
