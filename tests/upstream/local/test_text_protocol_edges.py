"""Project-authored Python 2 text constructor and conversion regressions."""

import unittest
import sys


class TextProtocolEdges(unittest.TestCase):
    def test_string_subtype_numeric_protocols(self):
        events = []
        class String(str):
            def __int__(self):
                events.append('int')
                return 7
            def __long__(self):
                events.append('long')
                return 8L
            def __float__(self):
                events.append('float')
                return 9.5
            def __complex__(self):
                raise AssertionError('complex parses strings directly')
        class Unicode(unicode):
            __int__ = String.__dict__['__int__']
            __long__ = String.__dict__['__long__']
            __float__ = String.__dict__['__float__']
            __complex__ = String.__dict__['__complex__']
        for source in (String('2'), Unicode(u'2')):
            self.assertEqual(int(source), 7)
            self.assertEqual(long(source), 8L)
            self.assertEqual(float(source), 9.5)
            self.assertEqual(complex(source), 2+0j)
            self.assertEqual(int(source, 10), 2)
            self.assertEqual(long(source, 10), 2L)
        self.assertEqual(events, ['int', 'long', 'float'] * 2)

    def test_numeric_legacy_character_buffers(self):
        for source in (bytearray('12'), buffer('12')):
            self.assertEqual(int(source), 12)
            self.assertEqual(long(source), 12L)
            self.assertEqual(float(source), 12.0)
            self.assertRaises(TypeError, complex, source)
            self.assertRaises(TypeError, int, source, 10)
            self.assertRaises(TypeError, long, source, 10)
        source = memoryview('12')
        for convert in (int, long, float, complex):
            self.assertRaises(TypeError, convert, source)

    def test_numeric_invalid_protocol_diagnostics(self):
        class Source(object):
            def __int__(self):
                return None
            def __long__(self):
                return 1.5
            def __float__(self):
                return 1
        for convert, message in ((int, '__int__ returned non-int (type NoneType)'),
                                 (long, '__long__ returned non-long (type float)'),
                                 (float, '__float__ returned non-float (type int)')):
            try:
                convert(Source())
            except TypeError as error:
                self.assertEqual(str(error), message)
            else:
                self.fail('invalid conversion must fail')

    def test_truncation_invalid_integral_diagnostics(self):
        class Integral(object):
            def __int__(self):
                return 1.5
        class Source(object):
            def __trunc__(self):
                return Integral()
        for convert in (int, long):
            try:
                convert(Source())
            except TypeError as error:
                self.assertEqual(str(error), '__trunc__ returned non-Integral (type float)')
            else:
                self.fail('invalid integral must fail')

    def test_decimal_unicode_error_metadata(self):
        for source, start, end in ((u'\u1234', 0, 1),
                                   (u'12\u1234\u1235x\u1236', 2, 4),
                                   (u'1\0x', 1, 2)):
            for convert in (int, long, float, complex):
                try:
                    convert(source)
                except UnicodeEncodeError as error:
                    self.assertEqual(error.encoding, 'decimal')
                    self.assertEqual(error.object, source)
                    self.assertIs(type(error.object), unicode)
                    self.assertIsNot(error.object, source)
                    self.assertEqual((error.start, error.end), (start, end))
                    self.assertEqual(error.reason, 'invalid decimal Unicode string')
                else:
                    self.fail('unencodable decimal text must fail')

    def test_decimal_unicode_digits_and_latin1_literal(self):
        for convert in (int, long, float, complex):
            self.assertEqual(convert(u'\u0661\u0662'), 12)
            self.assertEqual(convert(u'\u00a012\u00a0'), 12)
        for convert, name in ((int, 'int'), (long, 'long')):
            try:
                convert(u'\xe9')
            except ValueError as error:
                self.assertEqual(str(error), "invalid literal for %s() with base 10: '\\xe9'" % name)
            else:
                self.fail('Latin1 non-digit must fail')

    def test_integer_literal_and_null_diagnostics(self):
        for convert, name in ((int, 'int'), (long, 'long')):
            for source in ('1\0x', bytearray('1\0x'), buffer('1\0x')):
                try:
                    convert(source)
                except ValueError as error:
                    self.assertEqual(str(error), 'null byte in argument for %s()' % name)
                else:
                    self.fail('null byte must fail')
            try:
                convert('1\0x', 10)
            except ValueError as error:
                self.assertEqual(str(error), "invalid literal for %s() with base 10: '1\\x00x'" % name)
            else:
                self.fail('explicit base must reject null byte')
        for convert, literal in ((int, '1.2  '), (long, '  1.2  ')):
            try:
                convert('  1.2  ')
            except ValueError as error:
                self.assertTrue(str(error).endswith(repr(literal)))
            else:
                self.fail('non-integer literal must fail')

    def test_float_literal_diagnostics(self):
        for source, message in (('+ 1', 'could not convert string to float: + 1'),
                                 ('1\0x', 'invalid literal for float(): 1'),
                                 (u'\xe9', 'could not convert string to float: \xe9')):
            try:
                float(source)
            except ValueError as error:
                self.assertEqual(str(error), message)
            else:
                self.fail('invalid float literal must fail')

    def test_base_uses_int_protocol_once(self):
        events = []
        class Base(object):
            def __int__(self):
                events.append('int')
                return 16
            def __index__(self):
                raise AssertionError('base uses int protocol')
        class LongBase(long):
            def __int__(self):
                events.append('long')
                return 2
        for convert in (int, long):
            self.assertEqual(convert('10', Base()), 16)
            self.assertEqual(convert('10', LongBase(16)), 2)
        self.assertEqual(events, ['int', 'long'] * 2)

    def test_base_conversion_overflow_and_error_order(self):
        events = []
        class Base(object):
            def __int__(self):
                events.append('base')
                raise ValueError('base callback')
        for convert in (int, long):
            self.assertRaises(OverflowError, convert, '10', 2**32)
            self.assertRaises(OverflowError, convert, '10', 2**65)
            self.assertRaises(TypeError, convert, '10', 2.0)
            try:
                convert('10', base=Base(), extra=1)
            except TypeError as error:
                self.assertTrue('takes at most' in str(error))
            else:
                self.fail('total arity must fail first')
            try:
                convert(base=Base(), extra=1)
            except ValueError as error:
                self.assertEqual(str(error), 'base callback')
            else:
                self.fail('base conversion precedes unknown keyword')
        self.assertEqual(events, ['base'] * 2)

        for convert, name in ((int, 'int'), (long, 'long')):
            self.assertRaises(UnicodeEncodeError, convert, u'1\0x', 1)
            self.assertRaises(TypeError, convert, bytearray('10'), 1)
            try:
                convert('1\0x', 1)
            except ValueError as error:
                self.assertEqual(str(error), "invalid literal for %s() with base 1: '1\\x00x'" % name)
            else:
                self.fail('null validation precedes base range')

    def test_numeric_constructor_keywords(self):
        self.assertEqual(int(x='12', base=10), 12)
        self.assertEqual(long(x='12', base=10), 12L)
        self.assertEqual(float(x='1.25'), 1.25)
        self.assertEqual(complex(real=1, imag=2), 1+2j)
        self.assertEqual(complex(1, imag=2), 1+2j)
        self.assertEqual(complex(imag=2), 0+2j)
        self.assertRaises(TypeError, complex, 1, real=2)
        self.assertRaises(TypeError, float, 1, x=2)

    def test_complex_second_string_before_callback(self):
        events = []
        class Source(object):
            def __complex__(self):
                events.append('complex')
                return 2+3j
        for second in ('a', u'a'):
            try:
                complex(Source(), second)
            except TypeError as error:
                self.assertEqual(str(error), "complex() second arg can't be a string")
            else:
                self.fail('second string must fail')
        self.assertEqual(events, [])
        self.assertRaises(TypeError, complex, Source(), None)
        self.assertEqual(events, ['complex'])

    def test_complex_second_numeric_subtype_float(self):
        events = []
        class Source(int):
            def __float__(self):
                events.append('float')
                return 2.5
        self.assertEqual(complex(1+3j, Source(1)), 1+5.5j)
        self.assertEqual(events, ['float'])

    def test_unicode_subtype_unicode_protocol(self):
        events = []
        class Source(unicode):
            def __unicode__(self):
                events.append('unicode')
                return u'converted'
        class String(str):
            __unicode__ = Source.__dict__['__unicode__']
        self.assertEqual(unicode(Source(u'original')), u'converted')
        self.assertEqual(unicode(String('original')), u'converted')
        self.assertEqual(events, ['unicode'] * 2)

    def test_unicode_constructor_keywords_and_builtin_decoder(self):
        class Source(str):
            def decode(self, *args):
                raise AssertionError('builtin constructor bypasses decode override')
            def __unicode__(self):
                raise AssertionError('explicit decoding bypasses unicode override')
        self.assertEqual(unicode(string='abc', encoding='ascii'), u'abc')
        self.assertEqual(unicode(Source('abc'), errors='ignore'), u'abc')
        self.assertEqual(unicode(buffer('abc'), encoding='ascii'), u'abc')
        self.assertEqual(unicode(encoding='unknown-but-unused'), u'')
        self.assertEqual(unicode(errors='unknown-but-unused'), u'')
        self.assertRaises(TypeError, unicode, u'abc', encoding='ascii')
        self.assertRaises(TypeError, unicode, bytearray('abc'), encoding='ascii')
        self.assertRaises(TypeError, unicode, memoryview('abc'), encoding='ascii')

    def test_string_constructor_keywords_and_bytearray_str(self):
        events = []
        class Source(bytearray):
            def __str__(self):
                events.append('str')
                return 'converted'
        self.assertEqual(str(object='abc'), 'abc')
        self.assertEqual(str(Source('abc')), 'converted')
        self.assertEqual(events, ['str'])
        self.assertRaises(TypeError, str, encoding='ascii')
        self.assertRaises(TypeError, str, 'abc', encoding='ascii')

    def test_text_constructor_string_argument_validation_order(self):
        for convert, name in ((unicode, 'unicode'), (bytearray, 'bytearray')):
            for keywords, message in (({'encoding':None}, '%s() argument 2 must be string, not None' % name),
                                      ({'errors':1}, '%s() argument 3 must be string, not int' % name),
                                      ({'encoding':'a\0b'}, '%s() argument 2 must be string without null bytes, not str' % name)):
                try:
                    convert(**keywords)
                except TypeError as error:
                    self.assertEqual(str(error), message)
                else:
                    self.fail('argument validation must precede missing source')
            self.assertRaises(UnicodeEncodeError, convert, encoding=u'\xe9')
            try:
                convert('abc', encoding=1, extra=2)
            except TypeError as error:
                self.assertEqual(str(error), '%s() argument 2 must be string, not int' % name)
            else:
                self.fail('known argument conversion precedes unknown name')

    def test_text_constructor_duplicate_keyword_diagnostics(self):
        for convert, keyword, source in ((unicode, 'string', 'abc'),
                                          (bytearray, 'source', 'abc'),
                                          (complex, 'real', 1),
                                          (int, 'x', 1), (long, 'x', 1)):
            try:
                convert(source, **{keyword:source})
            except TypeError as error:
                self.assertEqual(str(error), "Argument given by name ('%s') and position (1)" % keyword)
            else:
                self.fail('duplicate keyword must fail')

    def test_keyword_subtype_equality_controls_value_lookup(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append((str.__str__(self), other))
                return accepts[0]
            def __hash__(self):
                return str.__hash__(self)
        accepts = [False]
        rows = ((str, 'object', '', '12'), (int, 'x', 0, 12),
                (long, 'x', 0L, 12L), (float, 'x', 0.0, 12.0),
                (complex, 'real', 0j, 12+0j),
                (unicode, 'string', u'', u'12'),
                (bytearray, 'source', bytearray(), bytearray('12')))
        for convert, keyword, default, converted in rows:
            keywords = {Keyword(keyword):'12'}
            for accepted, expected in ((False, default), (True, converted)):
                accepts[0] = accepted
                events[:] = []
                self.assertEqual(convert(**keywords), expected)
                self.assertEqual(events, [(keyword, keyword)] * (2 if convert is str and not accepted else 1))

    def test_keyword_lookup_suppresses_errors_and_preserves_handled_state(self):
        class Keyword(str):
            def __eq__(self, other):
                raise ValueError('lookup callback')
            def __hash__(self):
                return str.__hash__(self)
        sentinel = KeyError('handled')
        try:
            raise sentinel
        except KeyError:
            for convert, keyword, expected in ((str, 'object', ''),
                                                (int, 'x', 0), (long, 'x', 0L),
                                                (float, 'x', 0.0), (complex, 'real', 0j),
                                                (unicode, 'string', u''),
                                                (bytearray, 'source', bytearray())):
                self.assertEqual(convert(**{Keyword(keyword):'12'}), expected)
                self.assertIs(sys.exc_info()[1], sentinel)

    def test_bytes_translate_unicode_table_promotes(self):
        table = u'\0' * 97 + u'z'
        result = 'a!'.translate(table)
        self.assertIs(type(result), unicode)
        self.assertEqual(result, u'z\0')
        self.assertEqual('abc'.translate(u''), u'abc')
        self.assertRaises(UnicodeDecodeError, '\xff'.translate, table)
        self.assertRaises(TypeError, 'a'.translate, table, '')

    def test_translate_legacy_character_buffers(self):
        table = ''.join(chr(i) for i in range(256))
        for source in (table, bytearray(table), buffer(table)):
            self.assertEqual('abc'.translate(source, buffer('a')), 'bc')
        self.assertRaises(TypeError, 'abc'.translate, memoryview(table))
        self.assertRaises(TypeError, 'abc'.translate, table, memoryview('a'))
        self.assertRaises(TypeError, 'abc'.translate, table, u'')
        try:
            'abc'.translate('short', u'')
        except ValueError as error:
            self.assertEqual(str(error), 'translation table must be 256 characters long')
        else:
            self.fail('table length validated before deletions')

    def test_translate_noop_identity_and_subtype_result(self):
        source = ''.join(['abc', 'def'])
        self.assertIs(source.translate(None), source)
        self.assertIs(source.translate(None, ''), source)
        table = ''.join(chr(i) for i in range(256))
        self.assertIs(source.translate(table), source)
        class Source(str):
            pass
        subtype = Source(source)
        result = subtype.translate(None)
        self.assertIs(type(result), str)
        self.assertIsNot(result, subtype)
        self.assertEqual(result, source)

    def test_unicode_translate_lookup_order_and_errors(self):
        events = []
        class Table(object):
            def __getitem__(self, ordinal):
                events.append(ordinal)
                raise LookupError('missing')
        self.assertEqual(u'aaaa'.translate(Table()), u'aaaa')
        self.assertEqual(events, [97] * 4)
        self.assertEqual(u''.translate(None), u'')
        try:
            u'a'.translate(None)
        except TypeError as error:
            self.assertEqual(str(error), "'NoneType' object has no attribute '__getitem__'")
        else:
            self.fail('missing mapping method must fail')
        try:
            u'a'.translate({97:0x110000})
        except TypeError as error:
            self.assertEqual(str(error), 'character mapping must be in range(0x%lx)')
        else:
            self.fail('mapping outside Unicode range must fail')
