"""Independent Python 2.7 text and numeric protocol edge regressions."""

import unittest


class TextNumericEdges(unittest.TestCase):
    def test_decimal_conversion_halfway_edges(self):
        rows = (
            ('2.2250738585072011e-308', '0x0.fffffffffffffp-1022'),
            ('2.4703282292062327e-324', '0x0.0p+0'),
            ('2.4703282292062328e-324', '0x0.0000000000001p-1022'),
            ('1.7976931348623158e308', '0x1.fffffffffffffp+1023'),
        )
        for text, expected in rows:
            self.assertEqual(float(text).hex(), expected)
            self.assertEqual(float(unicode(text)).hex(), expected)

    def test_fromhex_halfway_edges_and_overflow(self):
        rows = (
            ('0x1.00000000000008p0', '0x1.0000000000000p+0'),
            ('0x1.000000000000080001p0', '0x1.0000000000001p+0'),
            ('0x0.fffffffffffff8p-1022', '0x1.0000000000000p-1022'),
            ('-0x0.00000000000008p-1022', '-0x0.0p+0'),
        )
        for text, expected in rows:
            self.assertEqual(float.fromhex(text).hex(), expected)
            self.assertEqual(float.fromhex(unicode(text)).hex(), expected)
        self.assertRaises(OverflowError, float.fromhex, '0x1.fffffffffffff8p1023')
        self.assertEqual(float.fromhex('0p9999999999999999999999'), 0.0)

    def test_fromhex_unicode_uses_default_ascii_codec(self):
        for text in (u'\u0661', u'\uff11', u'\u20030x1p1', u'\u00a00x1p1',
                     u'\ud800', u'\u00e9'):
            self.assertRaises(UnicodeEncodeError, float.fromhex, text)
        class Source(unicode):
            def encode(self, *args):
                raise AssertionError('builtin conversion bypasses override')
        self.assertEqual(float.fromhex(Source(u'0x1p1')), 2.0)
        self.assertRaises(UnicodeEncodeError, float.fromhex, Source(u'\u00e9'))

    def test_integer_parsing_sign_whitespace_and_suffix(self):
        for convert in (int, long):
            for text in ('+ 12', '- 12', '+\t12', '-\n12'):
                expected = -12 if text[0] == '-' else 12
                self.assertEqual(convert(text, 10), expected)
                self.assertEqual(convert(unicode(text), 10), expected)
            for text in (u'\u0661\u0662', u'\uff11\uff12', u'\u200312\u2003'):
                self.assertEqual(convert(text, 10), 12)
            for text in ('1_0', '1\x00', '0x ff', '0b 1'):
                self.assertRaises(ValueError, convert, text, 0)
        self.assertRaises(ValueError, int, '1L', 0)
        self.assertEqual(long('1L', 0), 1L)
        self.assertEqual(long('1L', 36), 57L)
        self.assertEqual(long('-0x10L', 0), -16L)

    def test_round_integer_subtype_float_protocol(self):
        events = []
        class Integer(int):
            def __float__(self):
                events.append('integer')
                return 1.25
        class Long(long):
            def __float__(self):
                events.append('long')
                return 1.25
        class Float(float):
            def __float__(self):
                raise AssertionError('stored float must be used')
        self.assertEqual(round(Integer(8), 1), 1.3)
        self.assertEqual(round(Long(8), 1), 1.3)
        self.assertEqual(round(Float(1.25), 1), 1.3)
        self.assertEqual(events, ['integer', 'long'])

    def test_round_conversion_order_and_failures(self):
        events = []
        class Value(object):
            def __float__(self):
                events.append('float')
                return 1.25
        class Digits(object):
            def __index__(self):
                events.append('index')
                return 1
        self.assertEqual(round(Value(), Digits()), 1.3)
        self.assertEqual(events, ['float', 'index'])
        events[:] = []
        class Failure(int):
            def __float__(self):
                events.append('float')
                raise KeyError('conversion')
        self.assertRaises(KeyError, round, Failure(1), Digits())
        self.assertEqual(events, ['float'])
        for invalid in (1, 1L, '1', None):
            class Invalid(long):
                def __float__(self):
                    return invalid
            self.assertRaises(TypeError, round, Invalid(8))

    def test_search_bounds_precede_operand_validation(self):
        for text in ('abc', u'abc', '\xff', u'\u00e9'):
            for candidate in (1, u'\u00e9', '\xff'):
                for name in ('find', 'rfind', 'index', 'rindex', 'count',
                             'startswith', 'endswith'):
                    events = []
                    class Bound(object):
                        def __index__(self):
                            events.append('index')
                            raise KeyError('bound')
                    self.assertRaises(KeyError, getattr(text, name), candidate, Bound())
                    self.assertEqual(events, ['index'])

    def test_search_converts_start_then_end(self):
        for text in ('ababa', u'ababa'):
            for name in ('find', 'rfind', 'count', 'startswith', 'endswith'):
                events = []
                class Bound(object):
                    def __init__(self, name, value):
                        self.name, self.value = name, value
                    def __index__(self):
                        events.append(self.name)
                        return self.value
                getattr(text, name)('a', Bound('start', 1), Bound('end', 4))
                self.assertEqual(events, ['start', 'end'])

    def test_split_and_replace_count_precedes_validation(self):
        for text in ('abc', u'abc', '\xff', u'\u00e9'):
            for candidate in (1, '', u'\u00e9', '\xff'):
                for name in ('split', 'rsplit', 'replace'):
                    events = []
                    class Count(object):
                        def __int__(self):
                            events.append('int')
                            raise KeyError('count')
                    args = (candidate, Count())
                    if name == 'replace':
                        args = (candidate, 'x', Count())
                    self.assertRaises(KeyError, getattr(text, name), *args)
                    self.assertEqual(events, ['int'])

    def test_count_callback_observes_mutable_operands(self):
        for operation in ('split', 'rsplit', 'replace'):
            separator = bytearray('z')
            class Count(object):
                def __int__(self):
                    separator[0] = 'a'
                    return 1
            if operation == 'replace':
                actual = 'aba'.replace(separator, 'x', Count())
                self.assertEqual(actual, 'xba')
            else:
                actual = getattr('aba', operation)(separator, Count())
                expected = ['', 'ba'] if operation == 'split' else ['ab', '']
                self.assertEqual(actual, expected)

    def test_byte_methods_accept_legacy_character_buffers(self):
        for candidate in (bytearray('a'), buffer('a')):
            self.assertEqual('aba'.find(candidate), 0)
            self.assertEqual('aba'.rfind(candidate), 2)
            self.assertEqual('aba'.count(candidate), 2)
            self.assertEqual('aba'.startswith(candidate), True)
            self.assertEqual('aba'.endswith(candidate), True)
            self.assertEqual('aba'.split(candidate), ['', 'b', ''])
            self.assertEqual('aba'.rsplit(candidate), ['', 'b', ''])
            self.assertEqual('aba'.replace(candidate, 'x'), 'xbx')
            self.assertEqual('aba'.replace('a', candidate), 'aba')
            self.assertRaises(TypeError, 'aba'.strip, candidate)
        for name in ('find', 'count', 'startswith', 'endswith', 'split',
                     'rsplit', 'partition', 'rpartition'):
            self.assertRaises(TypeError, getattr('aba', name), memoryview('a'))
        self.assertRaises(TypeError, 'aba'.replace, memoryview('a'), 'x')

    def test_unicode_methods_accept_only_legacy_character_buffers(self):
        candidate = buffer('a')
        self.assertEqual(u'aba'.find(candidate), 0)
        self.assertEqual(u'aba'.count(candidate), 2)
        self.assertEqual(u'aba'.startswith(candidate), True)
        self.assertEqual(u'aba'.endswith(candidate), True)
        self.assertEqual(u'aba'.split(candidate), [u'', u'b', u''])
        self.assertEqual(u'aba'.rsplit(candidate), [u'', u'b', u''])
        self.assertEqual(u'aba'.replace(candidate, 'x'), u'xbx')
        self.assertEqual('aba'.replace(candidate, u'x'), u'xbx')
        for invalid in (bytearray('a'), memoryview('a')):
            for name in ('find', 'count', 'startswith', 'endswith', 'split',
                         'rsplit', 'partition', 'rpartition'):
                self.assertRaises(TypeError, getattr(u'aba', name), invalid)
            self.assertRaises(TypeError, u'aba'.replace, invalid, 'x')
            self.assertRaises(TypeError, 'aba'.replace, invalid, u'x')
            self.assertRaises(TypeError, 'aba'.replace, u'a', invalid)

    def test_partition_retains_byte_separator_identity(self):
        class Separator(str):
            def __str__(self):
                raise AssertionError('stored bytes must be used')
        for separator in (Separator('a'), bytearray('a'), buffer('a')):
            for name in ('partition', 'rpartition'):
                result = getattr('aba', name)(separator)
                self.assertIs(result[1], separator)
                self.assertIs(type(result[0]), str)
                self.assertIs(type(result[2]), str)

    def test_unicode_partition_erases_separator_subtype(self):
        class UnicodeSeparator(unicode):
            pass
        class ByteSeparator(str):
            pass
        for separator in (UnicodeSeparator(u'a'), ByteSeparator('a'), buffer('a')):
            for name in ('partition', 'rpartition'):
                result = getattr(u'aba', name)(separator)
                self.assertEqual(result[1], u'a')
                self.assertIs(type(result[1]), unicode)
                self.assertIsNot(result[1], separator)
        separator = UnicodeSeparator(u'a')
        self.assertIs(type('aba'.partition(separator)[1]), unicode)

    def test_join_consumes_iterable_before_validation(self):
        for separator in ('', u''):
            events = []
            def values():
                for index, value in enumerate(('a', 1, 'b')):
                    events.append(index)
                    yield value
                events.append('end')
            self.assertRaises(TypeError, separator.join, values())
            self.assertEqual(events, [0, 1, 2, 'end'])
            events[:] = []
            def failure():
                events.append(0)
                yield 1
                events.append(1)
                raise KeyError('iteration')
            self.assertRaises(KeyError, separator.join, failure())
            self.assertEqual(events, [0, 1])

    def test_join_honors_sequence_subtype_iterators(self):
        class List(list):
            def __iter__(self):
                return iter(('replacement',))
        class Tuple(tuple):
            def __iter__(self):
                return iter(('replacement',))
        for sequence in (List(('a', 'b')), Tuple(('a', 'b'))):
            for separator in (',', u','):
                self.assertEqual(separator.join(sequence), 'replacement')

    def test_join_uses_iterator_length_hint(self):
        for separator in ('', u''):
            events = []
            class Iterator(object):
                def __init__(self):
                    self.position = 0
                def __iter__(self):
                    events.append('iterator.iter')
                    return self
                def __len__(self):
                    events.append('iterator.len')
                    return 2
                def next(self):
                    events.append('next')
                    if self.position == 2:
                        raise StopIteration
                    self.position += 1
                    return 'x'
            class Source(object):
                def __iter__(self):
                    events.append('source.iter')
                    return Iterator()
                def __len__(self):
                    raise AssertionError('original source length is not queried')
            self.assertEqual(separator.join(Source()), 'xx')
            self.assertEqual(events, ['source.iter', 'iterator.iter', 'iterator.len',
                                      'next', 'next', 'next'])

    def test_join_unicode_errors_precede_later_invalid_items(self):
        for sequence in (['\xff', u'x', 1], [u'x', '\xff', 1]):
            for separator in ('', u''):
                self.assertRaises(UnicodeDecodeError, separator.join, sequence)
                self.assertRaises(UnicodeDecodeError, separator.join, iter(sequence))
        self.assertRaises(TypeError, ''.join, [1, u'x', '\xff'])
        self.assertRaises(TypeError, u''.join, [1, '\xff'])

    def test_join_singleton_ignores_unused_separator(self):
        value = u'nonascii \u20ac'
        self.assertIs('\xff'.join([value]), value)
        class Unicode(unicode):
            pass
        result = '\xff'.join([Unicode(u'x')])
        self.assertEqual(result, u'x')
        self.assertIs(type(result), unicode)
        self.assertEqual('\xff'.join([]), '')

    def test_builtin_format_unicode_spec_uses_ascii(self):
        values = (0, 1L << 200, 1.25, complex(1.0, 2.0), 'abc')
        for value in values:
            for spec in (u'\u20ac^12', u'\U0001f600^12'):
                self.assertRaises(UnicodeEncodeError, format, value, spec)
            self.assertEqual(format(value, u'>12'), unicode(format(value, '>12')))
            self.assertRaises(UnicodeEncodeError, u'{0:\u20ac^12}'.format, value)
            self.assertRaises(UnicodeEncodeError, u'{0!r:\u20ac^12}'.format, value)
        self.assertEqual(format(u'abc', u'\u20ac^7'), u'\u20ac\u20acabc\u20ac\u20ac')
        self.assertEqual(u'{0!s:\u20ac^3}'.format(1), u'\u20ac1\u20ac')

    def test_custom_format_receives_unicode_spec(self):
        events = []
        class Value(object):
            def __format__(self, spec):
                events.append((type(spec), spec))
                return u'\u20ac'
        self.assertEqual(format(Value(), u'\u20ac'), u'\u20ac')
        self.assertEqual(u'{0:\u20ac}'.format(Value()), u'\u20ac')
        self.assertEqual(events, [(unicode, u'\u20ac'), (unicode, u'\u20ac')])


if __name__ == '__main__':
    unittest.main()
