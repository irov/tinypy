"""Project-authored text, codec and valid SRE state regressions."""

import _codecs
import _sre
import sys
import unittest


def _error(operation, *args, **kwargs):
    try:
        operation(*args, **kwargs)
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError('operation must raise')


def _letter():
    return _sre.compile('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1],
                        0, {}, [None])


def _register(name, events):
    def transform(*args):
        events.append(('transform', len(args), tuple(type(value).__name__ for value in args)))
        return 'converted', object()
    def search(encoding):
        events.append(('search', encoding))
        if encoding == name:
            return transform, transform, None, None
    _codecs.register(search)


class TextStateAudit(unittest.TestCase):
    def test_byte_methods_report_character_buffer_errors(self):
        for method in ('find', 'rfind', 'count', 'index', 'rindex', 'partition',
                       'rpartition', 'split', 'rsplit', 'replace'):
            for value in (object(), memoryview('a'), 1):
                args = (value, 'x') if method == 'replace' else (value,)
                self.assertEqual(_error(getattr('aba', method), *args),
                                 ('TypeError', 'expected a string or other character buffer object'))

    def test_unicode_methods_report_coercion_and_bytearray_errors(self):
        for method in ('find', 'rfind', 'count', 'index', 'rindex', 'partition',
                       'rpartition', 'split', 'rsplit', 'replace'):
            for value in (object(), memoryview('a'), 1, bytearray('a')):
                args = (value, 'x') if method == 'replace' else (value,)
                expected = 'decoding bytearray is not supported' if isinstance(value, bytearray) else (
                    'coercing to Unicode: need string or buffer, %s found' % type(value).__name__)
                self.assertEqual(_error(getattr(u'aba', method), *args), ('TypeError', expected))

    def test_prefix_errors_distinguish_first_argument_and_tuple_item(self):
        for text in ('a', u'a'):
            for method in ('startswith', 'endswith'):
                invoke = getattr(text, method)
                self.assertEqual(_error(invoke, object()), ('TypeError',
                    '%s first arg must be str, unicode, or tuple, not object' % method))
                expected = ('coercing to Unicode: need string or buffer, object found'
                            if isinstance(text, unicode) else 'expected a string or other character buffer object')
                self.assertEqual(_error(invoke, ('z', object())), ('TypeError', expected))
                self.assertTrue(invoke(('a', object())))

    def test_strip_errors_name_method_and_receiver_text_kind(self):
        for text in ('a', u'a'):
            for method in ('strip', 'lstrip', 'rstrip'):
                order = 'unicode or str' if isinstance(text, unicode) else 'str or unicode'
                self.assertEqual(_error(getattr(text, method), buffer('a')),
                                 ('TypeError', '%s arg must be None, %s' % (method, order)))

    def test_search_bounds_callback_precedes_invalid_needle_and_preserves_error(self):
        events = []
        marker = ValueError('search bound')
        class Bound(object):
            def __index__(self):
                events.append('index')
                raise marker
        for text in ('aba', u'aba'):
            for method in ('find', 'count', 'startswith', 'endswith'):
                events[:] = []
                try:
                    getattr(text, method)(object(), Bound())
                except ValueError as error:
                    self.assertIs(error, marker)
                else:
                    self.fail('bound must raise')
                self.assertEqual(events, ['index'])

    def test_search_invalid_index_result_names_actual_returned_type(self):
        events = []
        class Bound(object):
            def __init__(self, result):
                self.result = result
            def __index__(self):
                events.append('index')
                return self.result
        for value in (1.5, 'a', u'a', None, object()):
            events[:] = []
            self.assertEqual(_error('aba'.find, 'a', Bound(value)), ('TypeError',
                '__index__ returned non-(int,long) (type %s)' % type(value).__name__))
            self.assertEqual(events, ['index'])
        self.assertEqual('aba'.find('a', Bound(1)), 2)

    def test_update_count_callback_precedes_invalid_text_argument(self):
        events = []
        marker = ValueError('update count')
        class Count(object):
            def __int__(self):
                events.append('int')
                raise marker
        for text in ('aba', u'aba'):
            for method in ('split', 'rsplit', 'replace'):
                events[:] = []
                args = (object(), 'x', Count()) if method == 'replace' else (object(), Count())
                try:
                    getattr(text, method)(*args)
                except ValueError as error:
                    self.assertIs(error, marker)
                else:
                    self.fail('count must raise')
                self.assertEqual(events, ['int'])

    def test_unicode_case_preserves_exact_unchanged_receiver(self):
        for text, methods in ((u'abc', ('lower',)), (u'ABC', ('upper',)),
                              (u'123 !', ('lower', 'upper', 'swapcase'))):
            for method in methods:
                self.assertIs(getattr(text, method)(), text)
        self.assertEqual(u'\xe9'.upper(), u'\xc9')
        self.assertEqual(u'\xc9'.lower(), u'\xe9')
        unchanged = u'123 !'
        self.assertIsNot(unchanged.capitalize(), unchanged)
        self.assertIsNot(unchanged.title(), unchanged)
        sharp_s = u'\xdf'
        self.assertIs(sharp_s.upper(), sharp_s)
        self.assertIsNot(sharp_s.swapcase(), sharp_s)

    def test_unicode_case_empty_and_subtype_result_identity(self):
        class Child(unicode):
            pass
        for method in ('lower', 'upper', 'capitalize', 'title', 'swapcase'):
            text = u''
            self.assertIs(getattr(text, method)(), text)
            result = getattr(Child(u''), method)()
            self.assertIs(type(result), unicode)
            self.assertIs(result, text)
        child = Child(u'abc')
        self.assertIs(type(child.lower()), unicode)
        self.assertIsNot(child.lower(), child)

    def test_byte_partition_miss_preserves_subtype_receiver(self):
        class Child(str):
            pass
        text = Child('abc')
        self.assertIs(text.partition('z')[0], text)
        self.assertIs(text.rpartition('z')[2], text)
        self.assertIs(type(text.partition(u'z')[0]), unicode)

    def test_partition_match_preserves_byte_separator_identity(self):
        class Separator(str):
            pass
        for separator in (Separator('b'), bytearray('b'), buffer('b')):
            self.assertIs('abc'.partition(separator)[1], separator)
            self.assertIs('abc'.rpartition(separator)[1], separator)
        class Child(unicode):
            pass
        self.assertIs(type(Child(u'abc').partition(u'z')[0]), unicode)

    def test_empty_expandtabs_reuses_empty_base_text(self):
        class Text(str):
            pass
        class Unicode(unicode):
            pass
        for text in ('', u''):
            for size in (-1, 0, 8):
                self.assertIs(text.expandtabs(size), text)
        self.assertIs(Text('').expandtabs(), '')
        self.assertIs(Unicode(u'').expandtabs(), u'')

    def test_generic_codec_default_encoding_and_argument_count(self):
        self.assertEqual(_codecs.encode('abc'), 'abc')
        self.assertEqual(_codecs.decode('abc'), u'abc')
        for operation in (_codecs.encode, _codecs.decode):
            self.assertRaises(UnicodeEncodeError, operation, u'\xe9')
            self.assertEqual(_error(operation), ('TypeError',
                '%s() takes at least 1 argument (0 given)' % operation.__name__))

    def test_registered_codec_omitted_errors_uses_single_argument(self):
        events = []
        name = 'tinypy_eighth_single_argument'
        _register(name, events)
        for text in ('abc', u'abc'):
            for method in ('encode', 'decode'):
                for operation in (getattr(_codecs, method),
                                  lambda value, codec: getattr(value, method)(codec)):
                    events[:] = []
                    self.assertEqual(operation(text, name), 'converted')
                    self.assertEqual(events[-1], ('transform', 1, (type(text).__name__,)))

    def test_registered_codec_explicit_errors_normalize_to_exact_byte_string(self):
        events = []
        name = 'tinypy_eighth_errors_string'
        _register(name, events)
        class ErrorName(str):
            pass
        for text in ('abc', u'abc'):
            for method in ('encode', 'decode'):
                for errors in (u'ignore', ErrorName('ignore')):
                    events[:] = []
                    self.assertEqual(getattr(_codecs, method)(text, name, errors), 'converted')
                    self.assertEqual(events[-1], ('transform', 2, (type(text).__name__, 'str')))
                    events[:] = []
                    self.assertEqual(getattr(text, method)(name, errors), 'converted')
                    self.assertEqual(events[-1], ('transform', 2, (type(text).__name__, 'str')))

    def test_generic_codec_invalid_errors_precede_lookup_callback(self):
        events = []
        _register('tinypy_eighth_invalid_errors', events)
        for method in ('encode', 'decode'):
            for value, name in ((None, 'None'), (object(), 'object')):
                events[:] = []
                self.assertEqual(_error(getattr(_codecs, method), 'abc', 'tinypy_eighth_invalid_errors', value),
                    ('TypeError', '%s() argument 3 must be string, not %s' % (method, name)))
                self.assertEqual(events, [])

    def test_codec_name_embedded_null_and_unicode_errors_are_parsed_in_order(self):
        for method in ('encode', 'decode'):
            invoke = getattr(_codecs, method)
            for value in ('ascii\0', u'ascii\0'):
                self.assertEqual(_error(invoke, 'a', value), ('TypeError',
                    '%s() argument 2 must be string without null bytes, not %s' % (method, type(value).__name__)))
                self.assertEqual(_error(invoke, 'a', 'ascii', value), ('TypeError',
                    '%s() argument 3 must be string without null bytes, not %s' % (method, type(value).__name__)))
            self.assertRaises(UnicodeEncodeError, invoke, 'a', 'ascii', u'\xe9')

    def test_text_codec_keyword_equality_failure_is_suppressed_and_state_restored(self):
        events = []
        class Key(str):
            def __eq__(self, other):
                events.append(other)
                raise KeyboardInterrupt('keyword equality')
            __hash__ = str.__hash__
        marker = ValueError('outer exception')
        try:
            raise marker
        except ValueError:
            self.assertEqual('abc'.encode(**{Key('encoding'): 'ascii'}), 'abc')
            self.assertIs(sys.exc_info()[1], marker)
        self.assertEqual(events, ['encoding'])

    def test_sre_keyword_errors_precede_missing_subject(self):
        pattern = _letter()
        for method in ('match', 'search', 'findall', 'split'):
            invoke = getattr(pattern, method)
            self.assertEqual(_error(invoke, other=1), ('TypeError',
                "'other' is an invalid keyword argument for this function"))
            self.assertEqual(_error(invoke), ('TypeError',
                "Required argument 'string' (pos 1) not found"))
        self.assertEqual(_error(pattern.findall, pattern='a'), ('TypeError',
            "'pattern' is an invalid keyword argument for this function"))

    def test_sre_duplicate_alias_and_bound_report_exact_position(self):
        pattern = _letter()
        for method in ('match', 'search', 'findall'):
            invoke = getattr(pattern, method)
            self.assertEqual(_error(invoke, 'a', 0, pos=1), ('TypeError',
                "Argument given by name ('pos') and position (2)"))
            alias = 'source' if method == 'findall' else 'pattern'
            self.assertEqual(_error(invoke, 'a', **{alias: 'a'}), ('TypeError',
                "Argument given by name ('%s') and position (1)" % alias))
        for method in ('finditer', 'scanner'):
            self.assertEqual(_error(getattr(pattern, method), string='a'),
                ('TypeError', '%s() takes no keyword arguments' % method))

    def test_sre_keyword_equality_errors_are_suppressed_without_losing_handled_exception(self):
        pattern = _letter()
        events = []
        class Key(str):
            def __eq__(self, other):
                events.append(other)
                raise ValueError('keyword equality')
            __hash__ = str.__hash__
        marker = ValueError('handled exception')
        for method in ('search', 'findall'):
            events[:] = []
            try:
                raise marker
            except ValueError:
                result = getattr(pattern, method)('aba', **{Key('pos'): 1})
                self.assertEqual(result if method == 'findall' else result.span(),
                                 ['a', 'a'] if method == 'findall' else (0, 1))
                self.assertIs(sys.exc_info()[1], marker)
            self.assertEqual(events, ['pos'])

    def test_sre_subject_length_callback_runs_once_after_bound_conversions(self):
        events = []
        class Subject(str):
            def __len__(self):
                events.append('length')
                return 3
        class Bound(object):
            def __int__(self):
                events.append('bound')
                return 0
        pattern = _letter()
        for method in ('match', 'search', 'findall', 'finditer', 'scanner'):
            events[:] = []
            result = getattr(pattern, method)(Subject('aba'), Bound())
            self.assertEqual(events, ['bound', 'length'])
            if method == 'finditer':
                self.assertEqual([found.span() for found in result], [(0, 1), (2, 3)])
                self.assertEqual(events, ['bound', 'length'])
        for method in ('split', 'sub', 'subn'):
            events[:] = []
            args = (Subject('aba'),) if method == 'split' else ('X', Subject('aba'))
            getattr(pattern, method)(*args)
            self.assertEqual(events, ['length'])

    def test_sre_unicode_length_override_is_ignored_and_bytearray_length_observed(self):
        events = []
        class Unicode(unicode):
            def __len__(self):
                raise AssertionError('Unicode SRE length must use stored scalar count')
        class Bytes(bytearray):
            def __len__(self):
                events.append('length')
                return 3
        pattern = _letter()
        self.assertEqual(pattern.findall(Unicode(u'aba')), [u'a', u'a'])
        self.assertEqual(pattern.findall(Bytes('aba')), [bytearray('a'), bytearray('a')])
        self.assertEqual(events, ['length'])

    def test_sre_short_valid_subject_length_limits_matching_and_tail_slice(self):
        events = []
        class Subject(str):
            def __len__(self):
                events.append('length')
                return 2
            def __getslice__(self, start, end):
                events.append((start, end))
                return str.__getslice__(self, start, end)
        subject = Subject('aba')
        pattern = _letter()
        self.assertEqual(pattern.findall(subject), ['a'])
        self.assertEqual(events, ['length', (0, 1)])
        events[:] = []
        self.assertEqual(pattern.split(subject), ['', 'b'])
        self.assertEqual(events, ['length', (0, 0), (1, 2)])

    def test_sre_callable_text_replacement_precedes_template_detection(self):
        events = []
        class Replacement(str):
            def __len__(self):
                raise AssertionError('callable must precede template buffer lookup')
            def __call__(self, match):
                events.append(match.span())
                return 'X'
        self.assertEqual(_letter().sub(Replacement('\\not-a-template'), 'aba'), 'XbX')
        self.assertEqual(events, [(0, 1), (2, 3)])

    def test_sre_literal_replacement_length_precedes_subject_length(self):
        events = []
        class Replacement(str):
            def __len__(self):
                events.append('replacement')
                return 1
        class Subject(str):
            def __len__(self):
                events.append('subject')
                return 3
        self.assertEqual(_letter().sub(Replacement('X'), Subject('aba')), 'XbX')
        self.assertEqual(events, ['replacement', 'subject'])

    def test_sre_group_slice_error_preserves_identity_and_recovers(self):
        marker = ValueError('slice callback')
        class Subject(str):
            def __getslice__(self, start, end):
                raise marker
        pattern = _letter()
        found = pattern.match(Subject('aba'))
        try:
            found.group()
        except ValueError as error:
            self.assertIs(error, marker)
        else:
            self.fail('group slice must raise')
        self.assertEqual(pattern.findall('aba'), ['a', 'a'])

    def test_sre_findall_captured_groups_only_slice_actual_result(self):
        events = []
        class Subject(str):
            def __getslice__(self, start, end):
                events.append((start, end))
                return str.__getslice__(self, start, end)
        optional = _sre.compile('(?P<word>a)?', 0,
            [28, 9, 0, 1, 21, 0, 19, 97, 21, 1, 22, 1],
            1, {'word': 1}, [None, 'word'])
        self.assertEqual(optional.findall(Subject('aba')), ['a', '', 'a', ''])
        self.assertEqual(events, [(0, 1), (0, 0), (2, 3), (0, 0)])
        events[:] = []
        pair = _sre.compile('(a)(b)?', 0,
            [17, 8, 1, 1, 2, 1, 0, 97, 0, 21, 0, 19, 97, 21, 1,
             28, 9, 0, 1, 21, 2, 19, 98, 21, 3, 22, 1],
            2, {}, [None, None, None])
        self.assertEqual(pair.findall(Subject('aba')), [('a', 'b'), ('a', '')])
        self.assertEqual(events, [(0, 1), (1, 2), (2, 3), (0, 0)])
