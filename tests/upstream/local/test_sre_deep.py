"""Project-authored valid SRE protocols and bounded callback regressions."""

import sys
import unittest
import _sre


def _letter():
    return _sre.compile('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1], 0, {}, [None])


def _optional():
    return _sre.compile('(?P<word>a)?', 0, [28, 9, 0, 1, 21, 0, 19, 97, 21, 1, 22, 1],
                        1, {'word': 1}, [None, 'word'])


class SREDeep(unittest.TestCase):
    def test_match_search_findall_accept_named_bounds_and_string_alias(self):
        pattern = _letter()
        for method in ('match', 'search', 'findall'):
            invoke = getattr(pattern, method)
            result = invoke(string='baa', pos=1, endpos=2)
            self.assertEqual(result if method == 'findall' else result.span(),
                             ['a'] if method == 'findall' else (1, 2))
            alias = 'source' if method == 'findall' else 'pattern'
            result = invoke(**{alias: 'a'})
            self.assertEqual(result if method == 'findall' else result.group(),
                             ['a'] if method == 'findall' else 'a')

    def test_pattern_keyword_duplicates_missing_and_extras_are_rejected(self):
        pattern = _letter()
        for method in ('match', 'search', 'findall'):
            invoke = getattr(pattern, method)
            self.assertRaises(TypeError, invoke)
            self.assertRaises(TypeError, invoke, 'a', string='a')
            self.assertRaises(TypeError, invoke, 'a', pos=0, unknown=17)
            self.assertRaises(TypeError, invoke, 'a', 0, 1, pos=0)
            self.assertRaises(TypeError, invoke, string='a', **{
                'source' if method == 'findall' else 'pattern': 'a'})

    def test_split_and_sub_accept_named_parameters(self):
        pattern = _letter()
        self.assertEqual(pattern.split(string='aba', maxsplit=1), ['', 'ba'])
        self.assertEqual(pattern.split(source='aba', maxsplit=1), ['', 'ba'])
        self.assertEqual(pattern.sub(repl='X', string='aba', count=1), 'Xba')
        self.assertEqual(pattern.subn(repl='X', string='aba', count=1), ('Xba', 1))
        for method in ('sub', 'subn'):
            invoke = getattr(pattern, method)
            self.assertRaises(TypeError, invoke, string='a')
            self.assertRaises(TypeError, invoke, 'X', 'a', repl='Y')
            self.assertRaises(TypeError, invoke, repl='X', string='a', invalid=0)
        self.assertRaises(TypeError, pattern.split, 'a', string='a')
        self.assertRaises(TypeError, pattern.split, string='a', source='a')

    def test_group_defaults_accept_keywords_and_preserve_identity(self):
        found = _optional().match('')
        marker = object()
        self.assertEqual(found.groups(default=marker), (marker,))
        self.assertIs(found.groups(default=marker)[0], marker)
        self.assertIs(found.groupdict(default=marker)['word'], marker)
        for method in ('groups', 'groupdict'):
            invoke = getattr(found, method)
            self.assertRaises(TypeError, invoke, marker, default=marker)
            self.assertRaises(TypeError, invoke, invalid=marker)

    def test_finditer_and_scanner_reject_keywords(self):
        pattern = _letter()
        self.assertRaises(TypeError, pattern.finditer, 'aba', invalid=object(), pos=2)
        self.assertRaises(TypeError, pattern.finditer, string='aba')
        self.assertRaises(TypeError, pattern.scanner, 'aba', pos=1)
        self.assertRaises(TypeError, pattern.scanner, string='aba')

    def test_integer_arguments_use_int_protocol_once_in_parser_order(self):
        events = []
        class Integer(object):
            def __init__(self, name, value):
                self.name, self.value = name, value
            def __int__(self):
                events.append(self.name)
                return self.value
        pattern = _letter()
        self.assertEqual(pattern.search(string='baa', pos=Integer('pos', 1),
                                        endpos=Integer('end', 2)).span(), (1, 2))
        self.assertEqual(events, ['pos', 'end'])
        events[:] = []
        self.assertEqual(pattern.subn(repl='X', string='aba', count=Integer('count', 1)), ('Xba', 1))
        self.assertEqual(events, ['count'])

    def test_integer_argument_errors_preserve_callbacks_and_overflow(self):
        class IndexOnly(object):
            def __index__(self):
                return 1
        class Failed(object):
            def __int__(self):
                raise ValueError('integer callback')
        pattern = _letter()
        for method in ('match', 'search', 'findall'):
            invoke = getattr(pattern, method)
            self.assertRaises(TypeError, invoke, 'a', IndexOnly())
            self.assertRaises(TypeError, invoke, 'a', 1.0)
            self.assertRaises(OverflowError, invoke, 'a', 1L << 100)
            with self.assertRaisesRegexp(ValueError, 'integer callback'):
                invoke('a', Failed())
        for invoke in (pattern.split, lambda string, count: pattern.sub('X', string, count)):
            self.assertRaises(TypeError, invoke, 'a', IndexOnly())
            self.assertRaises(TypeError, invoke, 'a', 1.0)
            with self.assertRaisesRegexp(OverflowError, 'Python int too large to convert to C long'):
                invoke('a', 1L << 100)
        self.assertEqual(pattern.search('a').group(), 'a')

    def test_sub_count_conversion_precedes_template_helper_and_subject_validation(self):
        original = sys.modules.get('re')
        events = []
        module = type(sys)('re')
        def helper(pattern, template):
            events.append('template')
            return 'X'
        module._subx = helper
        class Count(object):
            def __int__(self):
                events.append('count')
                return 1
        class Failed(object):
            def __int__(self):
                events.append('count')
                raise ValueError('bad count')
        sys.modules['re'] = module
        try:
            pattern = _letter()
            self.assertEqual(pattern.sub('\\1', 'a', Count()), 'X')
            self.assertEqual(events, ['count', 'template'])
            events[:] = []
            with self.assertRaisesRegexp(ValueError, 'bad count'):
                pattern.sub('\\1', object(), Failed())
            self.assertEqual(events, ['count'])
            events[:] = []
            self.assertRaises(TypeError, pattern.sub, '\\1', object(), Count())
            self.assertEqual(events, ['count', 'template'])
        finally:
            if original is None:
                del sys.modules['re']
            else:
                sys.modules['re'] = original

    def test_getlower_uses_integer_protocol_and_signed_c_int_limits(self):
        class Character(object):
            def __int__(self):
                return 65
        class Flags(object):
            def __int__(self):
                return 0
        class Failed(object):
            def __int__(self):
                raise ValueError('lower integer')
        self.assertEqual(_sre.getlower(Character(), Flags()), 97)
        self.assertEqual(_sre.getlower(-1, 0), -1)
        self.assertEqual(_sre.getlower(-(1 << 31), 0), -(1 << 31))
        self.assertRaises(TypeError, _sre.getlower, 65.0, 0)
        self.assertRaises(TypeError, _sre.getlower, 65, 0.0)
        for value in (1 << 31, -(1 << 31) - 1):
            self.assertRaises(OverflowError, _sre.getlower, value, 0)
            self.assertRaises(OverflowError, _sre.getlower, 65, value)
        with self.assertRaisesRegexp(ValueError, 'lower integer'):
            _sre.getlower(Failed(), 0)
        self.assertEqual(_sre.getlower(65, 0), 97)

    def test_group_lookup_failures_clear_hash_errors_and_recover(self):
        class Key(object):
            def __hash__(self):
                raise ValueError('group hash')
        found = _optional().match('a')
        for method in ('group', 'start', 'end', 'span'):
            invoke = getattr(found, method)
            for key in (1L << 100, -(1L << 100), Key(), [], 1.0):
                with self.assertRaisesRegexp(IndexError, 'no such group'):
                    invoke(key)
        self.assertEqual(found.group('word'), 'a')
        self.assertEqual(found.span(1), (0, 1))

    def test_buffer_subjects_return_sequence_slice_types(self):
        pattern = _letter()
        for subject in (bytearray('aba'), buffer('aba')):
            found = pattern.search(subject)
            self.assertIs(found.string, subject)
            self.assertEqual(found.span(), (0, 1))
            group = found.group()
            self.assertIs(type(group), bytearray if isinstance(subject, bytearray) else str)
            self.assertEqual(group, bytearray('a') if isinstance(subject, bytearray) else 'a')
        self.assertRaises(TypeError, pattern.search, memoryview('aba'))

    def test_match_group_uses_sequence_slice_override_and_callback_recovers(self):
        events = []
        class Text(str):
            def __getslice__(self, start, end):
                events.append((start, end))
                return 'overridden'
        class Failed(str):
            def __getslice__(self, start, end):
                raise ValueError('slice callback')
        pattern = _letter()
        self.assertEqual(pattern.match(Text('a')).group(), 'overridden')
        self.assertEqual(events, [(0, 1)])
        with self.assertRaisesRegexp(ValueError, 'slice callback'):
            pattern.match(Failed('a')).group()
        self.assertEqual(pattern.match('a').group(), 'a')

    def test_negative_update_counts_preserve_subject(self):
        pattern = _letter()
        for count in (-2, -1):
            self.assertEqual(pattern.sub('X', 'aba', count), 'aba')
            self.assertEqual(pattern.subn('X', 'aba', count), ('aba', 0))
            self.assertEqual(pattern.split('aba', count), ['aba'])

    def test_reversed_bounds_match_and_search_preserve_python2_difference(self):
        empty = _sre.compile('', 0, [17, 4, 0, 0, 0, 1], 0, {}, [None])
        self.assertEqual(empty.match('abc', 2, 1).span(), (2, 2))
        self.assertIs(empty.search('abc', 2, 1), None)
        self.assertEqual(empty.findall('abc', 2, 1), [])
        self.assertEqual(list(empty.finditer('abc', 2, 1)), [])
        self.assertEqual(_optional().match('abc', 2, 1).span(), (2, 2))
        for name, opcode in (('a*', 29), ('a*?', 31)):
            repeated = _sre.compile(name, 0, [opcode, 6, 0, 4294967295L, 19, 97, 1, 1], 0, {}, [None])
            for subject in ('a', u'a', bytearray('a'), buffer('a')):
                self.assertIs(repeated.match(subject, 1, 0), None)
                self.assertEqual(repeated.match(subject, 1, 1).span(), (1, 1))

    def test_scanner_finditer_and_sub_callback_matches_preserve_initial_pos(self):
        pattern = _letter()
        scanner = pattern.scanner('baaba', 1, 5)
        self.assertEqual([scanner.search().pos for unused in range(3)], [1, 1, 1])
        self.assertIs(scanner.search(), None)
        self.assertEqual([found.pos for found in pattern.finditer('baaba', 1, 5)], [1, 1, 1])
        events = []
        def replacement(found):
            events.append((found.pos, found.endpos, found.span()))
            return 'X'
        self.assertEqual(pattern.sub(replacement, 'aaba'), 'XXbX')
        self.assertEqual(events, [(0, 4, (0, 1)), (0, 4, (1, 2)), (0, 4, (3, 4))])
