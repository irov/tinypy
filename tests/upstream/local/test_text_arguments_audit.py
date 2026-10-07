"""Project-authored Python 2 text argument and character-buffer regressions."""

import _codecs
import sys
import unittest


def _error(operation, *args, **kwargs):
    try:
        operation(*args, **kwargs)
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError('operation must raise')


class ByteText(str):
    pass


class UnicodeText(unicode):
    pass


class TextArgumentsAudit(unittest.TestCase):
    def test_keywords_precede_positional_count(self):
        for text in ('a', u'a'):
            for name in ('center', 'replace', 'split', 'join', 'partition',
                         'lower', 'isalpha', 'translate', '_formatter_parser'):
                self.assertEqual(_error(getattr(text, name), *([None] * 5), other=1),
                                 ('TypeError', '%s() takes no keyword arguments' % name))

    def test_alignment_parsed_arity(self):
        for text in ('a', u'a'):
            for name in ('center', 'ljust', 'rjust'):
                self.assertEqual(_error(getattr(text, name)),
                                 ('TypeError', '%s() takes at least 1 argument (0 given)' % name))
                self.assertEqual(_error(getattr(text, name), 1, 'x', None),
                                 ('TypeError', '%s() takes at most 2 arguments (3 given)' % name))

    def test_byte_search_common_parser_name(self):
        for name in ('find', 'rfind', 'index', 'rindex'):
            self.assertEqual(_error(getattr('a', name)), ('TypeError',
                'find/rfind/index/rindex() takes at least 1 argument (0 given)'))
            self.assertEqual(_error(getattr(u'a', name)), ('TypeError',
                '%s() takes at least 1 argument (0 given)' % name))

    def test_single_argument_methods(self):
        for text in ('a', u'a'):
            for name in ('join', 'partition', 'rpartition'):
                self.assertEqual(_error(getattr(text, name)), ('TypeError',
                    '%s() takes exactly one argument (0 given)' % name))
                self.assertEqual(_error(getattr(text, name), 'a', 'b'), ('TypeError',
                    '%s() takes exactly one argument (2 given)' % name))

    def test_translate_parser_styles(self):
        self.assertEqual(_error('a'.translate), ('TypeError',
                         'translate expected at least 1 arguments, got 0'))
        self.assertEqual(_error('a'.translate, None, '', ''), ('TypeError',
                         'translate expected at most 2 arguments, got 3'))
        self.assertEqual(_error(u'a'.translate), ('TypeError',
                         'translate() takes exactly one argument (0 given)'))

    def test_no_argument_methods(self):
        for text in ('a', u'a'):
            for name in ('lower', 'upper', 'title', 'capitalize', 'swapcase',
                         'isalpha', 'isdigit', '_formatter_parser',
                         '_formatter_field_name_split'):
                self.assertEqual(_error(getattr(text, name), 1), ('TypeError',
                    '%s() takes no arguments (1 given)' % name))

    def test_optional_argument_methods(self):
        for text in ('a', u'a'):
            for name, maximum in (('strip', 1), ('split', 2), ('rsplit', 2),
                                  ('splitlines', 1), ('expandtabs', 1)):
                self.assertEqual(_error(getattr(text, name), *([None] * (maximum + 1))),
                    ('TypeError', '%s() takes at most %d argument%s (%d given)' %
                     (name, maximum, '' if maximum == 1 else 's', maximum + 1)))

    def test_method_descriptors_and_bound_forms_share_parser(self):
        for kind, text in ((str, 'a'), (unicode, u'a')):
            descriptor = kind.center
            for method in (getattr(text, 'center'), descriptor.__get__(text, kind)):
                self.assertEqual(_error(method), ('TypeError',
                    'center() takes at least 1 argument (0 given)'))
            self.assertEqual(_error(descriptor, text), _error(text.center))

    def test_private_formatter_returns_fresh_nonempty_spans(self):
        for text in ('a b', u'a b', ByteText('a b'), UnicodeText(u'a b')):
            literal = list(text._formatter_parser())[0][0]
            head, tail = text._formatter_field_name_split()
            self.assertEqual(literal, text)
            self.assertEqual(head, text)
            self.assertIsNot(literal, text)
            self.assertIsNot(head, text)
            self.assertEqual(list(tail), [])

    def test_encoding_unknown_unicode_keyword_is_rejected(self):
        for text in ('a', u'a'):
            for name in ('encode', 'decode'):
                self.assertEqual(_error(getattr(text, name), **{u'unknown': 1}),
                                 ('TypeError', 'keywords must be strings'))
                self.assertEqual(getattr(text, name)(**{u'encoding': 'ascii'}),
                                 getattr(text, name)(encoding='ascii'))

    def test_byte_fill_accepts_only_single_byte_string(self):
        self.assertEqual('a'.center(3, ByteText('x')), 'xax')
        for fill, name in ((u'x', 'unicode'), (None, 'None'),
                           (buffer('x'), 'buffer'), (bytearray('x'), 'bytearray')):
            self.assertEqual(_error('a'.center, 3, fill), ('TypeError',
                'center() argument 2 must be char, not %s' % name))
        self.assertEqual('a'.center(3, '\xff'), '\xffa\xff')

    def test_unicode_fill_character_buffer_conversion(self):
        owner = u'x'
        fill = buffer(owner)
        self.assertEqual(u'a'.center(3, fill), u'xax')
        self.assertEqual(u'a'.ljust(3, fill), u'axx')
        self.assertEqual(u'a'.rjust(3, fill), u'xxa')

    def test_unicode_fill_replaces_conversion_error(self):
        for fill in ('\xff', bytearray('x'), None, object(), buffer(u'\xe9')):
            self.assertEqual(_error(u'a'.center, 3, fill), ('TypeError',
                'The fill character cannot be converted to Unicode'))
        self.assertEqual(u'a'.center(3, 'x'), u'xax')

    def test_unicode_fill_requires_one_character(self):
        for fill in ('', 'xy', u'', u'xy', buffer(u'xy')):
            self.assertEqual(_error(u'a'.center, 1, fill), ('TypeError',
                'The fill character must be exactly one character long'))
        self.assertEqual(u'a'.center(3, u'\U0001f600'), u'\U0001f600a\U0001f600')
        self.assertEqual(u'a'.center(3, u'\0'), u'\0a\0')

    def test_width_conversion_precedes_fill_validation(self):
        events = []
        marker = ValueError('width')
        class Width(object):
            def __int__(self):
                events.append('int')
                raise marker
        for text in ('a', u'a'):
            try:
                text.center(Width(), object())
            except ValueError as error:
                self.assertIs(error, marker)
            else:
                self.fail('width conversion must fail')
        self.assertEqual(events, ['int', 'int'])

    def test_width_callback_observes_current_fill_buffer(self):
        owner = bytearray('x')
        fill = buffer(owner)
        class Width(object):
            def __int__(self):
                owner[0] = ord('y')
                return 3
        self.assertEqual(u'a'.center(Width(), fill), u'yay')

    def test_width_uses_int_protocol(self):
        class Width(object):
            def __index__(self):
                return 3
        self.assertEqual(_error(u'a'.center, Width(), 'x'), ('TypeError',
                         'an integer is required'))

    def test_fill_conversion_ignores_unicode_hooks(self):
        events = []
        class Fill(object):
            def __unicode__(self):
                events.append('unicode')
                return u'x'
        self.assertEqual(_error(u'a'.center, 3, Fill()), ('TypeError',
                         'The fill character cannot be converted to Unicode'))
        self.assertEqual(events, [])

    def test_subtype_center_clamps_signed_margins_separately(self):
        for factory in (ByteText, UnicodeText):
            for size in (0, 2, 4):
                self.assertEqual(factory('a' * size).center(size - 1, 'x'),
                                 'x' + 'a' * size)
            self.assertEqual(factory('aaa').center(2, 'x'), 'aaa')
        text = u'aa'
        self.assertIs(text.center(1, 'x'), text)

    def test_join_masks_iterator_acquisition_typeerror(self):
        marker = TypeError('custom iterator')
        class Source(object):
            def __iter__(self):
                raise marker
        for text in ('-', u'-'):
            self.assertEqual(_error(text.join, Source()), ('TypeError',
                             'can only join an iterable'))
            self.assertEqual(_error(text.join, 1), ('TypeError',
                             'can only join an iterable'))

    def test_join_preserves_other_acquisition_and_iteration_errors(self):
        marker = ValueError('custom iterator')
        iteration_marker = TypeError('iteration')
        class Source(object):
            def __iter__(self):
                raise marker
        class Items(object):
            def __iter__(self):
                return self
            def next(self):
                raise iteration_marker
        for source, expected in ((Source(), marker), (Items(), iteration_marker)):
            try:
                '-'.join(source)
            except Exception as error:
                self.assertIs(error, expected)
            else:
                self.fail('iteration must fail')
        self.assertEqual('-'.join(['a', 'b']), 'a-b')

    def test_join_error_recovery_restores_handled_exception(self):
        marker = ValueError('handled')
        try:
            raise marker
        except ValueError:
            self.assertEqual(_error(u'-'.join, 1), ('TypeError',
                             'can only join an iterable'))
            self.assertIs(sys.exc_info()[1], marker)
            self.assertEqual(u'-'.join(['a', 'b']), u'a-b')

    def test_unicode_buffers_use_character_view_for_search_and_split(self):
        owner = u'b'
        needle = buffer(owner)
        for text in ('aba', u'aba'):
            self.assertEqual(text.find(needle), 1)
            self.assertEqual(text.count(needle), 1)
            self.assertEqual(text.split(needle), ['a', 'a'])
            self.assertEqual(text.replace(needle, 'x'), 'axa')
        self.assertIs('aba'.partition(needle)[1], needle)

    def test_character_buffer_offsets_apply_after_default_encoding(self):
        owner = u'abc'
        self.assertEqual('abc'.find(buffer(owner, 1, 1)), 1)
        self.assertEqual(u'abc'.find(buffer(buffer(owner, 1), 1, 1)), 2)
        self.assertEqual(_error('abc'.find, buffer(u'\xe9')), ('UnicodeEncodeError',
            "'ascii' codec can't encode character u'\\xe9' in position 0: ordinal not in range(128)"))

    def test_translate_uses_character_view_for_both_buffers(self):
        table_owner = u'x' * 256
        delete_owner = u'a'
        self.assertEqual('abc'.translate(buffer(table_owner), buffer(delete_owner)), 'xx')
        self.assertEqual('abc'.translate(None, buffer(delete_owner)), 'bc')
        self.assertEqual(_error('abc'.translate, buffer(u'x')), ('ValueError',
                         'translation table must be 256 characters long'))

    def test_core_encoders_use_character_buffers_and_decoders_raw_bytes(self):
        owner = u'a'
        view = buffer(owner)
        for encoder in (_codecs.ascii_encode, _codecs.latin_1_encode, _codecs.utf_8_encode):
            self.assertEqual(encoder(view), ('a', 1))
        for decoder in (_codecs.ascii_decode, _codecs.latin_1_decode, _codecs.utf_8_decode):
            self.assertEqual(decoder(view), (u'a\0\0\0', 4))

    def test_codecs_report_actual_name_and_parser_style(self):
        self.assertEqual(_error(_codecs.register), ('TypeError',
                         'register() takes exactly one argument (0 given)'))
        for function in (_codecs.lookup, _codecs.lookup_error):
            self.assertEqual(_error(function), ('TypeError',
                '%s() takes exactly 1 argument (0 given)' % function.__name__))
            self.assertEqual(_error(function, *([None] * 3), other=1), ('TypeError',
                '%s() takes no keyword arguments' % function.__name__))

    def test_codec_name_parser_reports_position_and_null(self):
        for function in (_codecs.lookup, _codecs.lookup_error):
            for value, label in ((None, 'None'), (1, 'int'), (buffer('ascii'), 'buffer')):
                self.assertEqual(_error(function, value), ('TypeError',
                    '%s() argument 1 must be string, not %s' % (function.__name__, label)))
            self.assertEqual(_error(function, 'a\0'), ('TypeError',
                '%s() argument 1 must be string without null bytes, not str' % function.__name__))
        self.assertEqual(_error(_codecs.lookup_error, 'missing_text_audit'),
                         ('LookupError', "unknown error handler name 'missing_text_audit'"))
        for decoder in (_codecs.ascii_decode, _codecs.utf_8_decode):
            self.assertEqual(_error(decoder, buffer(u'\xe9'), ''),
                             ('LookupError', "unknown error handler name ''"))

    def test_core_codec_errors_argument_is_nullable(self):
        for function in (_codecs.ascii_encode, _codecs.ascii_decode,
                         _codecs.latin_1_encode, _codecs.latin_1_decode,
                         _codecs.utf_8_encode, _codecs.utf_8_decode):
            self.assertEqual(function('a', None), function('a'))
            self.assertEqual(_error(function, 'a', 1), ('TypeError',
                '%s() argument 2 must be string or None, not int' % function.__name__))
            self.assertEqual(_error(function, 'a', 'a\0'), ('TypeError',
                '%s() argument 2 must be string without null bytes or None, not str' % function.__name__))

    def test_builtin_codec_handler_metadata_and_arity(self):
        for name in ('strict', 'ignore', 'replace', 'xmlcharrefreplace', 'backslashreplace'):
            function = _codecs.lookup_error(name)
            self.assertEqual(function.__name__, name + '_errors')
            self.assertIs(function.__module__, None)
            self.assertEqual(_error(function), ('TypeError',
                '%s_errors() takes exactly one argument (0 given)' % name))
            self.assertEqual(_error(function, None, other=1), ('TypeError',
                '%s_errors() takes no keyword arguments' % name))

    def test_encoder_parses_errors_before_converting_source(self):
        for function in (_codecs.ascii_encode, _codecs.latin_1_encode, _codecs.utf_8_encode):
            for source in (buffer(u'\xe9'), None, 1, bytearray('a'), memoryview('a')):
                self.assertEqual(_error(function, source, 1), ('TypeError',
                    '%s() argument 2 must be string or None, not int' % function.__name__))
                self.assertEqual(_error(function, source, 'a\0'), ('TypeError',
                    '%s() argument 2 must be string without null bytes or None, not str' % function.__name__))

    def test_encoder_source_conversion_uses_unicode_from_object(self):
        events = []
        class Source(object):
            def __unicode__(self):
                events.append('unicode')
                return u'a'
        for function in (_codecs.ascii_encode, _codecs.latin_1_encode, _codecs.utf_8_encode):
            self.assertEqual(_error(function, Source()), ('TypeError',
                'coercing to Unicode: need string or buffer, Source found'))
            self.assertEqual(_error(function, bytearray('a')), ('TypeError',
                             'decoding bytearray is not supported'))
        self.assertEqual(events, [])

    def test_codec_registry_names_bypass_subtype_hash_and_equality(self):
        events = []
        class Name(str):
            def __hash__(self):
                events.append('hash')
                raise ValueError('name hash')
            def __eq__(self, other):
                events.append('equal')
                return False
        def handler(error):
            return u'?', error.end
        _codecs.register_error(Name('text_arguments_named_handler'), handler)
        self.assertIs(_codecs.lookup_error('text_arguments_named_handler'), handler)
        self.assertIs(_codecs.lookup_error(Name('text_arguments_named_handler')), handler)
        self.assertEqual(u'\xe9'.encode('ascii', Name('text_arguments_named_handler')), '?')
        self.assertEqual(events, [])

    def test_unicode_codec_registry_names_and_handled_state(self):
        events = []
        marker = ValueError('handled registry')
        class Name(unicode):
            def __hash__(self):
                events.append('hash')
                raise marker
        try:
            raise marker
        except ValueError:
            self.assertIs(_codecs.lookup_error(Name(u'strict')), _codecs.lookup_error('strict'))
            self.assertIs(sys.exc_info()[1], marker)
        self.assertEqual(events, [])


if __name__ == '__main__':
    unittest.main()
