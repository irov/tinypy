"""Project-authored Python 2 brace formatting and conversion regressions."""

import unittest


def failure(operation, exception):
    try:
        operation()
    except exception as error:
        return error
    raise AssertionError('operation must raise ' + exception.__name__)


def collect(iterator, count):
    records = []
    for step in range(count):
        try:
            records.append(('value', next(iterator)))
        except StopIteration:
            records.append(('stop',))
        except ValueError as error:
            records.append(('error', str(error)))
    return records


class FormatFields(unittest.TestCase):
    def test_lookup_and_format_callback_order(self):
        events = []
        class Value(object):
            def __getitem__(self, key):
                events.append(('item', type(key), key))
                return self
            def __getattr__(self, name):
                events.append(('attr', type(name), name))
                return self
            def __format__(self, spec):
                events.append(('format', type(spec), spec))
                return 'v'
        value = Value()
        self.assertEqual('{0[2].name:abc}'.format(value), 'v')
        self.assertEqual(events, [('item', int, 2), ('attr', str, 'name'),
                                  ('format', str, 'abc')])

    def test_mapping_numeric_keys_are_long(self):
        events = []
        class Mapping(dict):
            def __getitem__(self, key):
                events.append((type(key), key))
                return 'v'
        for pattern in ('{0[2]}', u'{0[02]}', u'{0[\u0662]}'):
            self.assertEqual(pattern.format(Mapping()), 'v')
        self.assertEqual(events, [(long, 2L)] * 3)

    def test_unicode_numeric_fields_and_item_keys(self):
        events = []
        class Sequence(object):
            def __getitem__(self, key):
                events.append((type(key), key))
                return 'v'
        self.assertEqual(u'{\u0660[\u0662]}'.format(Sequence()), u'v')
        self.assertEqual(events, [(int, 2)])
        self.assertEqual(u'{caf\xe9}'.format(**{u'caf\xe9': 'v'}), u'v')
        error = failure(lambda: u'{missing}'.format(), KeyError)
        self.assertEqual(error.args, (u'missing',))
        self.assertIs(type(error.args[0]), unicode)

    def test_item_punctuation_obeys_field_grammar(self):
        events = []
        class Source(object):
            def __getitem__(self, key):
                events.append(key)
                return 'v'
        source = Source()
        for key in ('foo', 'a.b', '@'):
            self.assertEqual(('{0[' + key + ']}').format(source), 'v')
        for key, message in (('!', "Missing ']' in format string"),
                              (':', "Missing ']' in format string"),
                              ('a:b', "Missing ']' in format string"),
                              ('a!b', "expected ':' after format specifier")):
            error = failure(lambda: ('{0[' + key + ']}').format(source), ValueError)
            self.assertEqual(str(error), message)
        self.assertEqual(events, ['foo', 'a.b', '@'])

    def test_unicode_attribute_encoding_precedes_lookup(self):
        events = []
        class Source(object):
            def __getattr__(self, name):
                events.append(name)
                return 'v'
        error = failure(lambda: u'{0.\xe9}'.format(Source()), UnicodeEncodeError)
        self.assertEqual((error.encoding, error.object, error.start, error.end),
                         ('ascii', u'\xe9', 0, 1))
        self.assertEqual(error.reason, 'ordinal not in range(128)')
        self.assertEqual(events, [])

    def test_path_validation_occurs_after_previous_lookup(self):
        events = []
        class Source(object):
            def __getitem__(self, key):
                events.append(key)
                return 'v'
        error = failure(lambda: '{0[2]tail}'.format(Source()), ValueError)
        self.assertEqual(str(error), "Only '.' or '[' may follow ']' in format field specifier")
        self.assertEqual(events, [2])

    def test_lexical_errors_precede_field_callbacks(self):
        events = []
        class Source(object):
            def __getitem__(self, key):
                events.append(key)
                return 'v'
        for pattern, message in (('{0[2]!sr}', "expected ':' after format specifier"),
                                  ('{0[2]', "unmatched '{' in format"),
                                  ('{', "Single '{' encountered in format string")):
            error = failure(lambda: pattern.format(Source()), ValueError)
            self.assertEqual(str(error), message)
        self.assertEqual(events, [])

    def test_conversion_precedes_nested_spec_callbacks(self):
        events = []
        class Source(object):
            def __str__(self):
                events.append('str')
                return 's'
            def __repr__(self):
                events.append('repr')
                return 'r'
            def __format__(self, spec):
                events.append('outer-format')
                return 'v'
        class Spec(object):
            def __format__(self, spec):
                events.append('spec')
                raise ValueError('nested callback')
        for conversion, first in (('s', 'str'), ('r', 'repr'), ('', None)):
            events[:] = []
            pattern = '{0' + ('!' + conversion if conversion else '') + ':{1}}'
            error = failure(lambda: pattern.format(Source(), Spec()), ValueError)
            self.assertEqual(str(error), 'nested callback')
            self.assertEqual(events, ([first] if first else []) + ['spec'])

    def test_conversion_result_subtype_format_is_called(self):
        events = []
        class String(str):
            def __format__(self, spec):
                events.append(('format', type(spec), spec))
                return 'custom'
        class Source(object):
            def __str__(self):
                events.append('str')
                return String('original')
            def __repr__(self):
                events.append('repr')
                return String('original')
        for pattern, prefix, spec_type in (('{0!s:>9}', 'str', str),
                                           (u'{0!r:>9}', 'repr', unicode)):
            events[:] = []
            self.assertEqual(pattern.format(Source()), 'custom')
            self.assertEqual(events, [prefix, ('format', spec_type, '>9')])

    def test_formatted_result_subtype_receiver_conversion(self):
        events = []
        class String(str):
            def __str__(self):
                events.append('str')
                return 'byte-result'
            def __unicode__(self):
                events.append('unicode')
                return u'unicode-result'
        class Source(object):
            def __format__(self, spec):
                return String('original')
        self.assertEqual('{0}'.format(Source()), 'byte-result')
        self.assertEqual(u'{0}'.format(Source()), u'unicode-result')
        self.assertEqual(events, ['str', 'unicode'])

    def test_invalid_format_result_diagnostics(self):
        answer = None
        class Source(object):
            def __format__(self, spec):
                return answer
        for answer in (None, 1, bytearray('a'), buffer('a')):
            error = failure(lambda: '{0}'.format(Source()), TypeError)
            self.assertEqual(str(error), 'Source.__format__ must return string or unicode, not ' + type(answer).__name__)

    def test_unicode_format_result_ascii_encoding_error(self):
        class Source(object):
            def __format__(self, spec):
                return u'\xe9'
        self.assertEqual(u'{0}'.format(Source()), u'\xe9')
        error = failure(lambda: '{0}'.format(Source()), UnicodeEncodeError)
        self.assertEqual((error.encoding, error.object, error.start, error.end),
                         ('ascii', u'\xe9', 0, 1))
        self.assertEqual(error.reason, 'ordinal not in range(128)')

    def test_unknown_conversion_and_null_conversion(self):
        events = []
        class Source(object):
            def __format__(self, spec):
                events.append(spec)
                return 'v'
        for pattern, message in (('{0!a}', 'Unknown conversion specifier a'),
                                  (u'{0!\xe9}', 'Unknown conversion specifier \\xe9'),
                                  ('{0!\x01}', 'Unknown conversion specifier \\x1')):
            self.assertEqual(str(failure(lambda: pattern.format(Source()), ValueError)), message)
        self.assertEqual('{0!\0}'.format(Source()), 'v')
        self.assertEqual(events, [''])

    def test_nested_fields_share_numbering_and_recursion_limit(self):
        self.assertEqual('{:{}.{}f}'.format(1.25, 6, 1), '   1.2')
        error = failure(lambda: '{0:{1:{2}}}'.format(1, 2, 3), ValueError)
        self.assertEqual(str(error), 'Max string recursion exceeded')
        for pattern in ('{0}{}', '{}{0}'):
            self.assertRaises(ValueError, pattern.format, 1, 2)

    def test_builtin_format_preserves_spec_subtype_identity(self):
        events = []
        class String(str):
            pass
        class Unicode(unicode):
            pass
        class Source(object):
            def __format__(self, received):
                events.append((type(received), received is spec))
                return 'v'
        for spec in (String(''), String('>3'), Unicode(u''), Unicode(u'>3')):
            self.assertEqual(format(Source(), spec), 'v')
        self.assertEqual(events, [(String, True), (String, True),
                                  (Unicode, True), (Unicode, True)])
        self.assertEqual(str(failure(lambda: format(Source(), None), TypeError)),
                         'format expects arg 2 to be string or unicode, not NoneType')

    def test_object_format_calls_converted_subtype_format(self):
        events = []
        class String(str):
            def __format__(self, spec):
                events.append(('format', spec))
                return 'custom'
        class Source(object):
            def __str__(self):
                events.append('str')
                return String('original')
        for spec in ('', '>9'):
            self.assertEqual(object.__format__(Source(), spec), 'custom')
        self.assertEqual(events, ['str', ('format', ''), 'str', ('format', '>9')])

    def test_str_and_repr_preserve_custom_string_identity(self):
        class String(str):
            pass
        answer = String('original')
        class Source(object):
            def __str__(self):
                return answer
            def __repr__(self):
                return answer
        self.assertIs(str(Source()), answer)
        self.assertIs(repr(Source()), answer)
        self.assertIs(type(str(answer)), str)
        self.assertIsNot(str(answer), answer)

    def test_str_and_repr_unicode_return_encoding(self):
        answer = u'ascii'
        class Source(object):
            def __str__(self):
                return answer
            def __repr__(self):
                return answer
        for convert in (str, repr):
            self.assertEqual(convert(Source()), 'ascii')
            self.assertIs(type(convert(Source())), str)
        answer = u'\xe9'
        for convert in (str, repr):
            error = failure(lambda: convert(Source()), UnicodeEncodeError)
            self.assertEqual((error.encoding, error.object, error.start, error.end),
                             ('ascii', answer, 0, 1))
            self.assertEqual(error.reason, 'ordinal not in range(128)')

    def test_format_receiver_subtype_uses_payload(self):
        class String(str):
            def __str__(self):
                raise AssertionError('receiver must not be converted')
            def __unicode__(self):
                raise AssertionError('receiver must not be converted')
        class Unicode(unicode):
            __str__ = String.__dict__['__str__']
            __unicode__ = String.__dict__['__unicode__']
        for receiver, result_type in ((String, str), (Unicode, unicode)):
            answer = receiver('a{0}b').format('v')
            self.assertEqual(answer, 'avb')
            self.assertIs(type(answer), result_type)

    def test_formatter_parser_recovers_after_error(self):
        for convert in (str, unicode):
            rows = collect(convert('a{0!sr}tail')._formatter_parser(), 3)
            self.assertEqual(rows, [('error', "expected ':' after format specifier"),
                                    ('value', ('tail', None, None, None)), ('stop',)])
            rows = collect(convert('a}tail{0}')._formatter_parser(), 3)
            self.assertEqual(rows, [('error', "Single '}' encountered in format string"),
                                    ('value', ('tail', '0', '', None)), ('stop',)])

    def test_field_split_iterator_yields_before_suffix_error(self):
        for convert in (str, unicode):
            head, iterator = convert('0[2]x.foo')._formatter_field_name_split()
            self.assertEqual(head, 0L)
            self.assertIs(type(head), long)
            rows = collect(iterator, 4)
            self.assertEqual(rows, [('value', (False, 2L)),
                                    ('error', "Only '.' or '[' may follow ']' in format field specifier"),
                                    ('value', (True, 'foo')), ('stop',)])
            self.assertIs(type(rows[0][1][1]), long)

    def test_field_split_unicode_decimals_and_overflow(self):
        head, iterator = u'\u0660[\u0662]'._formatter_field_name_split()
        self.assertEqual((head, list(iterator)), (0L, [(False, 2L)]))
        self.assertIs(type(head), long)
        for convert in (str, unicode):
            error = failure(lambda: convert('99999999999999999999')._formatter_field_name_split(), ValueError)
            self.assertEqual(str(error), 'Too many decimal digits in format string')
            head, iterator = convert('0[]x.foo')._formatter_field_name_split()
            self.assertEqual(collect(iterator, 4),
                             [('error', 'Empty attribute in format string'),
                              ('error', "Only '.' or '[' may follow ']' in format field specifier"),
                              ('value', (True, 'foo')), ('stop',)])

    def test_legacy_character_buffers_use_string_format_semantics(self):
        for source in (buffer('abc'), bytearray('abc')):
            self.assertEqual('{0}'.format(source), 'abc')
            self.assertEqual('{0!s}'.format(source), 'abc')
            self.assertEqual('{0:>4}'.format(source), ' abc')
            self.assertEqual(u'{0:>4}'.format(source), u' abc')
        for source in (buffer('\xff'), bytearray('\xff')):
            self.assertEqual('{0}'.format(source), '\xff')
            self.assertRaises(UnicodeDecodeError, u'{0}'.format, source)

    def test_classic_format_method_and_instance_override(self):
        events = []
        class Source:
            def __format__(self, spec):
                events.append((type(spec), spec))
                return 'classic'
        value = Source()
        self.assertEqual('{0:>4}'.format(value), 'classic')
        self.assertEqual(u'{0:>4}'.format(value), u'classic')
        self.assertEqual(events, [(str, '>4'), (unicode, u'>4')])
        value.__format__ = lambda spec: 'instance:' + spec
        self.assertEqual('{0:abc}'.format(value), 'instance:abc')
