"""Project-authored Python 2 text constructor and conversion regressions."""

import unittest
import sys
import _sre


_ALIVE_AT_EXIT = []


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

    def test_memoryview_requires_object_argument(self):
        for create in (memoryview, lambda: memoryview.__new__(memoryview)):
            with self.assertRaises(TypeError) as caught:
                create()
            self.assertEqual(str(caught.exception), "Required argument 'object' (pos 1) not found")

    def test_percent_long_subtype_conversion_methods(self):
        class Long(long):
            def __hex__(self):
                return '0xHEX'
            def __oct__(self):
                return '-017L'
            def __str__(self):
                return 'STR'
        self.assertEqual('%x|%#X|%d|%u|%o|%#o' % ((Long(255),) * 6), 'HEX|0XHEX|STR|STR|-17|-017')
        self.assertEqual(u'%5x|%+o' % (Long(1), Long(1)), u'  HEX|-17')
        self.assertEqual('%d|%x' % (True, 255), '1|ff')
        for result, conversion, message in ((1, 'd', 'expected string or Unicode object, int found'),
                                             ('abc', 'x', '%x format: invalid result of __hex__ (type=Bad)'),
                                             ('0x', 'X', '%X format: invalid result of __hex__ (type=Bad)'),
                                             ('L', 'o', '%o format: invalid result of __oct__ (type=Bad)')):
            class Bad(long):
                def __str__(self):
                    return result
                __hex__ = __oct__ = __str__
            error = ValueError if isinstance(result, str) else TypeError
            with self.assertRaises(error) as caught:
                ('%' + conversion) % Bad()
            self.assertEqual(str(caught.exception), message)
        class Truncated:
            def __trunc__(self):
                return 3
        self.assertEqual('%d %x' % (Truncated(), Truncated()), '3 3')
        class NotString(object):
            def __str__(self):
                return 5
        with self.assertRaises(TypeError) as caught:
            '%s' % NotString()
        self.assertEqual(str(caught.exception), '__str__ returned non-string (type int)')

    def test_percent_mapping_requires_mapping_subscript(self):
        with self.assertRaises(TypeError) as caught:
            '%(a)s' % Exception()
        self.assertEqual(str(caught.exception), 'format requires a mapping')
        for format, value in (('', Exception()), ('%%', xrange(3)), (u'x', xrange(3))):
            with self.assertRaises(TypeError) as caught:
                format % value
            self.assertEqual(str(caught.exception), 'not all arguments converted during string formatting')
        class Indexed(Exception):
            def __getitem__(self, key):
                return key.upper()
        self.assertEqual('%(a)s' % Indexed(), 'A')
        self.assertEqual('' % [1], '')

    def test_text_conversion_honours_subtype_overrides(self):
        class Integer(int):
            def __str__(self):
                return 'I!'
        class Complex(complex):
            def __str__(self):
                return 'C!'
        class String(str):
            def __str__(self):
                return 'S!'
        class Unicode(unicode):
            def __unicode__(self):
                return u'U!'
        class Plain(unicode):
            pass
        self.assertEqual('{0}'.format(Integer(3)), 'I!')
        self.assertEqual(format(Complex(1), ''), 'C!')
        self.assertEqual('{}'.format(String('ab')), 'S!')
        self.assertEqual('{}'.format(Unicode(u'ab')), 'U!')
        self.assertEqual(unicode(String('ab')), u'S!')
        self.assertEqual('%s' % Unicode(u'ab'), u'U!')
        self.assertEqual(u'%s' % Unicode(u'ab'), u'U!')
        self.assertIs(type('%s' % Plain(u'ab')), unicode)
        self.assertIs(type(unicode(Plain(u'ab'))), unicode)
        class Kept(unicode):
            def __unicode__(self):
                return self
        kept = Kept(u'ab')
        self.assertIs(unicode(kept), kept)
        class Representation(object):
            def __repr__(self):
                return u'\xe9'
        self.assertEqual(object.__str__(Representation()), u'\xe9')
        self.assertEqual(u'%s' % (Representation(),), u'\xe9')
        self.assertEqual(unicode(Representation()), u'\xe9')
        self.assertIs(type('%s' % (Representation(),)), unicode)

    def test_format_spec_types_digits_and_spec_kinds(self):
        for value, spec, message in ((u'x', '\x00', "Unknown format code '\\x0' for object of type 'unicode'"),
                                     ('x', '\x00', "Unknown format code '\x00' for object of type 'str'"),
                                     (1, ',\x00', "Unknown format code '\x00' for object of type 'int'"),
                                     ({'a': 1}, u'\x00', "Unknown format code '\\x0' for object of type 'unicode'")):
            with self.assertRaises(ValueError) as caught:
                format(value, spec)
            self.assertEqual(caught.exception.args[0], message)
        with self.assertRaises(ValueError) as caught:
            '{0:{1}}'.format({'a': 1}, u'\x00')
        self.assertEqual(caught.exception.args[0], "Unknown format code '\x00' for object of type 'str'")
        with self.assertRaises(ValueError) as caught:
            '{0!r:d}'.format(1)
        self.assertEqual(str(caught.exception), "Unknown format code 'd' for object of type 'str'")
        self.assertEqual(format(1.5, u'\x00'), u'1.5')
        self.assertEqual(format(u'ab', u'\u0665'), u'ab   ')
        self.assertEqual(format(u'ab', u'.\u0661'), u'a')
        self.assertEqual(u'{0:{1}}'.format(None, u'\u0665'), u'None ')
        result = 'ab'.__format__(u'^6')
        self.assertIs(type(result), str)
        self.assertEqual(result, '  ab  ')
        with self.assertRaises(UnicodeDecodeError) as caught:
            format('\xff\xfe', u'^7')
        self.assertEqual(caught.exception.start, 2)
        for value in ('a', u'a'):
            with self.assertRaises(TypeError) as caught:
                value.__format__(set())
            self.assertEqual(str(caught.exception), '__format__ arg must be str or unicode, not set')
        self.assertEqual(True.__format__(''), 'True')

    def test_float_presentation_non_float_results(self):
        class Long(long):
            def __float__(self):
                return None
        class Other(object):
            def __float__(self):
                return None
        for call, message in ((lambda: format(Long(1), '.0%'), '__float__ returned non-float (type NoneType)'),
                              (lambda: u'%f' % Other(), 'nb_float should return float object'),
                              (lambda: '%f' % Other(), 'float argument required, not Other')):
            with self.assertRaises(TypeError) as caught:
                call()
            self.assertEqual(str(caught.exception), message)

    def test_text_codec_error_ranges_and_decoder_results(self):
        for call in (lambda: u'\x80\xff'.decode('utf-8'), lambda: u'\u20ac\xff'.decode('ascii', 'ignore')):
            with self.assertRaises(UnicodeEncodeError) as caught:
                call()
            self.assertEqual((caught.exception.start, caught.exception.end), (0, 2))
        with self.assertRaises(TypeError) as caught:
            unicode('abcd', 'hex')
        self.assertEqual(str(caught.exception), 'decoder did not return an unicode object (type=str)')
        self.assertEqual(unicode('', 'hex'), u'')
        self.assertEqual(unicode('', 'bogus'), u'')
        self.assertEqual(unicode(buffer(''), 'bogus'), u'')

    def test_containment_operand_errors(self):
        for call, error, message in ((lambda: 1 in 'abc', TypeError, "'in <string>' requires string as left operand, not int"),
                                     (lambda: 1 in u'abc', TypeError, 'coercing to Unicode: need string or buffer, int found'),
                                     (lambda: 1.5 in bytearray('a'), TypeError, "Type float doesn't support the buffer API"),
                                     (lambda: 2**63 in bytearray('a'), TypeError, "Type long doesn't support the buffer API"),
                                     (lambda: None in bytearray('a'), TypeError, "Type NoneType doesn't support the buffer API"),
                                     (lambda: 256 in bytearray('a'), ValueError, 'byte must be in range(0, 256)'),
                                     (lambda: -1 in bytearray('a'), ValueError, 'byte must be in range(0, 256)')):
            with self.assertRaises(error) as caught:
                call()
            self.assertEqual(str(caught.exception), message)
        class Index(object):
            def __index__(self):
                return 97
        self.assertTrue(Index() in bytearray('a'))
        self.assertTrue(True in bytearray('\x01'))

    def test_bytearray_decode_argument_types(self):
        for args, message in ((('latin-1', bytearray()), 'decode() argument 2 must be string, not bytearray'),
                              ((bytearray(3),), 'decode() argument 1 must be string, not bytearray'),
                              ((buffer('ascii'),), 'decode() argument 1 must be string, not buffer')):
            with self.assertRaises(TypeError) as caught:
                bytearray('a').decode(*args)
            self.assertEqual(str(caught.exception), message)
        self.assertEqual(bytearray('a').decode(u'ascii', u'strict'), u'a')

    def test_formatter_iterator_type_names(self):
        parser = 'a{0}'._formatter_parser()
        fields = 'a.b'._formatter_field_name_split()[1]
        self.assertEqual(type(parser).__name__, 'formatteriterator')
        self.assertEqual(type(fields).__name__, 'fieldnameiterator')
        self.assertFalse(hasattr(parser, '__length_hint__'))
        self.assertEqual(list(parser), [('a', '0', '', None)])
        self.assertEqual(list(fields), [(True, 'b')])

    def test_size_overflow_diagnostics(self):
        for call, error, message in ((lambda: buffer('ab') * sys.maxint, MemoryError, 'result too large'),
                                     (lambda: buffer('a') * sys.maxint, OverflowError, 'string is too large'),
                                     (lambda: 'ab'.center(sys.maxint), OverflowError, 'string is too large'),
                                     (lambda: 'a'.zfill(sys.maxint), OverflowError, 'string is too large'),
                                     (lambda: '{0:{1}}'.format(1, sys.maxint), OverflowError, 'string is too large'),
                                     (lambda: u'{0:{1}}'.format(1, sys.maxint), OverflowError, 'string is too large'),
                                     (lambda: 'a\tb'.expandtabs(sys.maxint), OverflowError, 'signed integer is greater than maximum'),
                                     (lambda: u'a'.splitlines(-sys.maxint), OverflowError, 'signed integer is less than minimum'),
                                     (lambda: bytearray('a').expandtabs(sys.maxint), OverflowError, 'signed integer is greater than maximum')):
            with self.assertRaises(error) as caught:
                call()
            self.assertEqual(str(caught.exception), message)

    def test_unraisable_native_error_writes_message(self):
        class Capture(object):
            def __init__(self):
                self.parts = []
            def write(self, text):
                self.parts.append(text)
        def ignoring():
            try:
                yield 1
            except GeneratorExit:
                pass
            yield 2
        class Raising(object):
            def __del__(self):
                raise ValueError('in del')
        saved = sys.stderr
        sys.stderr = capture = Capture()
        try:
            generator = ignoring()
            next(generator)
            del generator
            value = Raising()
            del value
        finally:
            sys.stderr = saved
        text = ''.join(capture.parts)
        self.assertTrue(text.startswith("Exception RuntimeError: 'generator ignored GeneratorExit' in <generator object ignoring at "), text)
        self.assertIn("Exception ValueError: ValueError('in del',) in <bound method Raising.__del__ of ", text)

    def test_sre_scanner_pattern_and_code_overflow(self):
        for code in ([2**32], [-1], [2**128]):
            with self.assertRaises(OverflowError) as caught:
                _sre.compile('abc', 0, code)
            self.assertEqual(str(caught.exception), 'regular expression code size limit exceeded')
        pattern = _sre.compile('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1])
        self.assertIs(pattern.groupindex, None)
        self.assertEqual(pattern.match('a').groupdict(), {})
        scanner = pattern.scanner('aa')
        self.assertIs(scanner.pattern, pattern)
        self.assertEqual(scanner.search().span(), (0, 1))
        # A scanner alive at shutdown is the only owner of its pattern.
        _ALIVE_AT_EXIT.append(_sre.compile('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1], 0, {}, [None]).scanner('a'))

    def test_sre_search_keeps_marks_of_failed_candidates(self):
        unbounded = 4294967295
        lazy = _sre.compile(u'(?P<n>a)*?a?$', 0, [28, 9, 0, unbounded, 21, 0, 19, 97, 21, 1, 23, 29, 6, 0, 1, 19, 97, 1, 6, 5, 1],
                            1, {u'n': 1}, [None, u'n'])
        match = lazy.search(u'AaBb')
        self.assertEqual((match.groupdict(), match.regs, match.lastindex), ({u'n': u''}, ((4, 4), (3, 2)), 1))
        reference = _sre.compile('(?(1)a|b){,2}\xe9*?(a)*?\\1{2,}?', 16,
                                 [28, 12, 0, 2, 13, 0, 6, 19, 97, 18, 3, 19, 98, 22, 31, 6, 0, unbounded, 19, 233, 1,
                                  28, 9, 0, unbounded, 21, 0, 19, 97, 21, 1, 23, 28, 5, 2, unbounded, 12, 0, 23, 1],
                                 1, {}, [None, None])
        self.assertEqual(reference.search('a.b').regs, ((1, 1), (1, 1)))
        exists = _sre.compile(u'(?P<n>a)*?(?(1)a|b)+', 0,
                              [28, 9, 0, unbounded, 21, 0, 19, 97, 21, 1, 23, 28, 12, 1, unbounded, 13, 0, 6, 19, 97, 18, 3, 19, 98, 22, 1],
                              1, {u'n': 1}, [None, u'n'])
        self.assertEqual(list(exists.finditer('a.b')), [])

    def test_decimal_digit_value_outside_zero_run(self):
        # U+19DA NEW TAI LUE THAM DIGIT ONE carries decimal value 1 after the 0..9 run.
        digit = unichr(0x19da)
        self.assertTrue(digit.isdecimal())
        self.assertEqual((int(digit), long(digit), float(digit)), (1, 1L, 1.0))
        self.assertEqual(int(unichr(0x19d0) + unichr(0x19d9) + digit), 91)
        self.assertEqual(int(digit * 3), 111)

    def test_utf8_stateful_decoder_defers_truncated_tails(self):
        import _codecs
        replacement = unichr(0xfffd)
        for data in ('\xe2A', '\xf0A', '\xf0\x90A', '\xe2\x82'):
            for errors in ('strict', 'ignore', 'replace'):
                self.assertEqual(_codecs.utf_8_decode(data, errors, False), (u'', 0))
        self.assertEqual(_codecs.utf_8_decode('\xc2\xf3x', 'replace', False), (replacement, 1))
        self.assertEqual(_codecs.utf_8_decode('\xc2\xf3x', 'ignore', False), (u'', 1))
        self.assertEqual(_codecs.utf_8_decode('\xf0\xf2\xf0\xc0', 'replace', False), (replacement, 1))
        self.assertRaises(UnicodeDecodeError, _codecs.utf_8_decode, '\xe2A', 'strict', True)
        self.assertEqual(_codecs.utf_8_decode('\xe2A', 'replace', True), (replacement + u'A', 2))

    def test_unicode_constructor_reads_buffer_character_buffer(self):
        self.assertEqual(unicode(buffer(u'ab'), 'ascii'), u'ab')
        self.assertEqual(unicode(buffer(u'ab', 1), 'ascii'), u'b')
        self.assertEqual(unicode(buffer(u'abcd', 2, 1), 'ascii'), u'c')
        with self.assertRaises(UnicodeEncodeError) as caught:
            unicode(buffer(unichr(0xe9)), 'latin-1')
        self.assertEqual(str(caught.exception), "'ascii' codec can't encode character u'\\xe9' in position 0: ordinal not in range(128)")
        self.assertTrue(buffer(u'a') in u'ab')
        self.assertTrue(u'abc'.__contains__(buffer('ab')))
        self.assertTrue(buffer(u'ab') == u'ab')
        self.assertEqual(str(buffer(u'ab')), 'a\x00\x00\x00b\x00\x00\x00')

    def test_unicode_in_buffer_searches_items(self):
        source = buffer('ab')
        self.assertEqual((u'ab' in source, u'' in source, u'a' in source, 'ab' in source, 'a' in source), (False, False, True, False, True))
        with self.assertRaises(TypeError) as caught:
            bytearray('a') in u'ab'
        self.assertEqual(str(caught.exception), 'decoding bytearray is not supported')
        self.assertRaises(UnicodeDecodeError, lambda: buffer('\xe9') in u'ab')

    def test_hex_codec_buffer_arguments_and_keywords(self):
        import _codecs
        hex_encode, hex_decode = _codecs.lookup('hex')[:2]
        for value, message in ((1, 'b2a_hex() argument 1 must be string or buffer, not int'),
                               (None, 'b2a_hex() argument 1 must be string or buffer, not None')):
            with self.assertRaises(TypeError) as caught:
                hex_encode(value)
            self.assertEqual(str(caught.exception), message)
        with self.assertRaises(TypeError) as caught:
            hex_decode(1)
        self.assertEqual(str(caught.exception), 'a2b_hex() argument 1 must be string or buffer, not int')
        self.assertEqual(hex_encode(bytearray('ab')), ('6162', 2))
        self.assertEqual(hex_encode(buffer('ab')), ('6162', 2))
        self.assertEqual(hex_encode(u'ab'), ('6162', 2))
        self.assertEqual(hex_decode(bytearray('6162')), ('ab', 4))
        self.assertEqual(hex_encode(input='ab', errors='strict'), ('6162', 2))
        self.assertEqual(hex_decode('6162', errors='strict'), ('ab', 4))
        self.assertEqual(_codecs.encode(buffer(u'a'), 'hex'), '61000000')
        self.assertRaises(AssertionError, hex_encode, 1, 'ignore')
        for call, message in ((lambda: hex_encode(), 'hex_encode() takes at least 1 argument (0 given)'),
                              (lambda: hex_encode(errors='strict'), 'hex_encode() takes at least 1 argument (1 given)'),
                              (lambda: hex_encode('a', 'strict', 1), 'hex_encode() takes at most 2 arguments (3 given)'),
                              (lambda: hex_encode('a', input='b'), "hex_encode() got multiple values for keyword argument 'input'"),
                              (lambda: hex_decode('61', bad=1), "hex_decode() got an unexpected keyword argument 'bad'")):
            with self.assertRaises(TypeError) as caught:
                call()
            self.assertEqual(str(caught.exception), message)

    def test_unicode_error_position_member_messages(self):
        error = UnicodeEncodeError('ascii', u'x', 0, 1, 'bad')
        for name in ('start', 'end'):
            with self.assertRaises(TypeError) as caught:
                delattr(error, name)
            self.assertEqual(str(caught.exception), "can't delete numeric/char attribute")
            with self.assertRaises(OverflowError) as caught:
                setattr(error, name, 2 ** 70)
            self.assertEqual(str(caught.exception), 'long int too large to convert to int')
            self.assertEqual(getattr(error, name), -1)
            setattr(error, name, 3L)
            self.assertEqual(getattr(error, name), 3)

    def test_syntax_error_object_members(self):
        error = SyntaxError('x', ('f', 1, 2, 'txt'))
        for name in ('msg', 'filename', 'lineno', 'offset', 'text', 'print_file_and_line'):
            self.assertEqual(type(getattr(SyntaxError, name)).__name__, 'member_descriptor')
        self.assertEqual((error.lineno, error.offset, error.text, error.filename, error.msg, error.print_file_and_line), (1, 2, 'txt', 'f', 'x', None))
        del error.print_file_and_line
        self.assertIs(error.print_file_and_line, None)
        del error.lineno
        del error.lineno
        self.assertIs(error.lineno, None)
        self.assertEqual(str(error), 'x (f)')
        error.lineno = 7
        self.assertEqual(str(error), 'x (f, line 7)')
        self.assertEqual(repr(SyntaxError.lineno), "<member 'lineno' of 'exceptions.SyntaxError' objects>")
        class Derived(SyntaxError):
            pass
        derived = Derived('m', ('g', 5, 6, 'u'))
        self.assertEqual((derived.lineno, derived.filename), (5, 'g'))
        with self.assertRaises(TypeError):
            SyntaxError.lineno = 5

    def test_unexpected_unicode_keyword_name_message(self):
        def target(a=1):
            pass
        for name, shown in ((unichr(0x3c0) + unichr(0x3b9), '??'), (u'ab', 'ab'), (unichr(0xe9), '?'), ('abc', 'abc')):
            with self.assertRaises(TypeError) as caught:
                target(**{name: 1})
            self.assertEqual(str(caught.exception), "target() got an unexpected keyword argument '%s'" % shown)
        with self.assertRaises(TypeError) as caught:
            target(1, **{u'a': 2})
        self.assertEqual(str(caught.exception), "target() got multiple values for keyword argument 'a'")

    def test_next_classic_instance_without_next_method(self):
        class Iterable:
            def __iter__(self):
                return self
        with self.assertRaises(TypeError) as caught:
            next(iter(Iterable()))
        self.assertEqual(str(caught.exception), 'instance has no next() method')
        with self.assertRaises(TypeError) as caught:
            next(Iterable(), 5)
        self.assertEqual(str(caught.exception), 'instance has no next() method')

    def test_replace_argument_coercion_order(self):
        for call, message in ((lambda: 'a.b'.replace(3.3, u'x', 1), 'expected a string or other character buffer object'),
                              (lambda: 'a.b'.replace(bytearray('a'), u'x'), 'decoding bytearray is not supported'),
                              (lambda: 'a.b'.replace(u'x', 3.3), 'coercing to Unicode: need string or buffer, float found'),
                              (lambda: 'a.b'.replace('a', 3), 'expected a string or other character buffer object')):
            with self.assertRaises(TypeError) as caught:
                call()
            self.assertEqual(str(caught.exception), message)
        self.assertEqual('a.b'.replace(buffer('a'), u'x'), u'x.b')
        self.assertEqual('a.b'.replace(bytearray('a'), 'x'), 'x.b')

    def test_unicode_translate_table_subscript_messages(self):
        for table, message in ((set([1]), "'set' object does not support indexing"),
                               (1, "'int' object has no attribute '__getitem__'")):
            with self.assertRaises(TypeError) as caught:
                u'abc'.translate(table)
            self.assertEqual(str(caught.exception), message)
        self.assertEqual(u''.translate(set()), u'')
