"""Project-authored valid SRE protocols and bounded callback regressions."""

import sys
import unittest
import _sre
import _weakref


def _letter():
    return _sre.compile('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1], 0, {}, [None])


def _optional():
    return _sre.compile('(?P<word>a)?', 0, [28, 9, 0, 1, 21, 0, 19, 97, 21, 1, 22, 1],
                        1, {'word': 1}, [None, 'word'])


def _pair(groups, *names):
    return _sre.compile('(a)(b)', 0, [17, 10, 1, 2, 2, 2, 0, 97, 98, 0, 0, 21, 0, 19, 97, 21, 1, 21, 2, 19, 98, 21, 3, 1],
                        groups, *names)


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

    def test_unset_group_spans_are_undefined_as_a_whole(self):
        for pattern, subject, code, regs in (
                ('(?!(a))b', 'b', [17, 4, 0, 1, 1, 5, 9, 0, 21, 0, 19, 97, 21, 1, 1, 19, 98, 1], ((0, 1), (-1, -1))),
                ('(?!x(a))', 'xb', [17, 4, 0, 0, 0, 5, 11, 0, 19, 120, 21, 0, 19, 97, 21, 1, 1, 1], ((0, 0), (-1, -1)))):
            found = _sre.compile(pattern, 0, code, 1, {}, [None, None]).match(subject)
            self.assertEqual(found.regs, regs)
            self.assertEqual((found.span(1), found.start(1), found.end(1)), ((-1, -1), -1, -1))
            self.assertIs(found.group(1), None)
            self.assertEqual(found.groups('-'), ('-',))

    def test_scanner_match_starts_past_endpos(self):
        empty = _sre.compile('', 0, [17, 4, 0, 0, 0, 1], 0, {}, [None])
        scanner = empty.scanner('abcdef', 4, 2)
        self.assertEqual([(found.span(), found.pos, found.endpos) for found in (scanner.match(), scanner.match())],
                         [((4, 4), 4, 2), ((5, 5), 4, 2)])
        self.assertIs(empty.scanner('abcdef', 4, 2).search(), None)
        scanner = empty.scanner('abc', 1, 3)
        self.assertEqual([found and found.span() for found in [scanner.match() for unused in range(4)]],
                         [(1, 1), (2, 2), (3, 3), None])
        scanner = _letter().scanner('xab')
        self.assertEqual([scanner.match(), scanner.match()], [None, None])

    def test_multiline_end_from_a_start_past_endpos(self):
        end_line = _sre.compile('$', 8, [17, 4, 0, 0, 0, 6, 6, 1], 0, {}, [None])
        for subject in (u'\xe9' * 3, 'abc', u'abc', bytearray('abc'), buffer('abc')):
            self.assertIs(end_line.match(subject, 3, 0), None)
            self.assertIs(end_line.match(subject, 3, 1), None)
            self.assertIs(end_line.search(subject, 3, 1), None)
        self.assertEqual(end_line.match(u'\xe9\n', 1, 0).span(), (1, 1))
        self.assertEqual(end_line.match('a\n', 1, 0).span(), (1, 1))
        self.assertEqual(end_line.search(u'\xe9\xe9\n').span(), (2, 2))

    def test_compile_rejects_programs_sre_compile_cannot_emit(self):
        unbounded = 4294967295
        letter = [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1]
        branch = [17, 9, 4, 2, 2, 19, 97, 19, 99, 0, 7, 7, 19, 97, 19, 98, 18, 9, 7, 19, 99, 19, 100, 18, 2, 0, 1]
        exists = [17, 8, 1, 1, 1, 1, 0, 97, 0, 21, 0, 19, 97, 21, 1, 13, 0, 6, 19, 98, 18, 3, 19, 99, 1]
        backtracking = [17, 4, 0, 1, 1, 21, 0, 21, 1, 28, 59, 0, unbounded, 13, 0, 43, 28, 37, unbounded - 1, 0, 21, 2, 7, 3,
                        18, 27, 3, 18, 24, 22, 15, 7, 26, 19, 95, 19, 97, 0, 29, 6, 1, unbounded, 19, 95, 1, 6, 0, 6, 5,
                        18, 2, 0, 21, 3, 22, 18, 13, 12, 0, 15, 7, 19, 32, 27, 48, 122, 0, 19, 65, 23, 19, 66, 1]
        for code, groups in ((letter, 1), (letter, 100), (branch, 0), (exists, 1), ([21, 3, 1], 1), ([12, 0, 1], 1),
                             ([6, 11, 1], 0), ([15, 4, 9, 17, 0, 1], 0), ([29, 6, 1, 2, 19, 97, 1, 1], 0),
                             ([28, 5, 0, 1, 19, 98, 22, 1], 0), ([4, 7, 2, 19, 97, 19, 98, 1, 1], 0)):
            _sre.compile('x', 0, code, groups)
        mismatched = list(branch)
        mismatched[17] = 8
        missing_group = list(exists)
        missing_group[16] = 1
        for code, groups in (([], 0), ([19, 97], 0), (letter, 101), (letter, -1), ([21, 4, 1], 1), ([12, 1, 1], 1),
                             ([6, 12, 1], 0), ([15, 4, 9, 18, 0, 1], 0), ([15, 3, 27, 97, 0, 1], 0),
                             ([29, 6, 2, 1, 19, 97, 1, 1], 0), ([29, 6, 1, 2, 19, 97, 0, 1], 0),
                             ([28, 5, 0, 1, 19, 98, 18, 1], 0), ([17, 4, 8, 0, 0, 1], 0), ([17, 4, 2, 0, 0, 1], 0),
                             ([4, 5, 2147483648, 19, 97, 1, 1], 0), ([8, 2, 1, 1], 0), ([18, 1, 1], 0),
                             ([31, 6, 0, 1, 9, 4, 1, 1], 0), (mismatched, 0), (missing_group, 1), (backtracking, 2)):
            with self.assertRaisesRegexp(RuntimeError, 'invalid SRE code'):
                _sre.compile('x', 0, code, groups)
        self.assertEqual(_sre.compile('x', 0, branch).search('xcd').span(), (1, 3))

    def test_groupdict_keeps_names_alive_while_hashing_changes_groupindex(self):
        index = {}
        class Name(str):
            armed = False
            def __hash__(self):
                if Name.armed:
                    Name.armed = False
                    index.clear()
                return str.__hash__(self)
        index[Name('w')] = 1
        found = _sre.compile('(?P<w>a)', 0, [17, 8, 1, 1, 1, 1, 0, 97, 0, 21, 0, 19, 97, 21, 1, 1], 1, index,
                             [None, 'w']).match('a')
        Name.armed = True
        with self.assertRaisesRegexp(IndexError, 'no such group'):
            found.groupdict()
        self.assertEqual(index, {})
        index[Name('v')] = 1
        index['w'] = 1
        self.assertEqual(sorted(found.groupdict().items()), [('v', 'a'), ('w', 'a')])

    def test_unicode_subclass_subject_dies_with_its_last_reference(self):
        pattern = _sre.compile(u'(.)', 0, [17, 4, 0, 1, 1, 21, 0, 2, 21, 1, 1], 1, {}, [None, None])
        found = []
        class Text(unicode):
            def __del__(self):
                found.append(pattern.match(self).group(1))
        subject = Text(u'\xe9t\xe9')
        reference = _weakref.ref(subject)
        self.assertEqual(pattern.match(subject).group(1), u'\xe9')
        del subject
        self.assertIs(reference(), None)
        self.assertEqual(found, [u'\xe9'])
        self.assertEqual(pattern.match(u'\xfcber').group(1), u'\xfc')
        self.assertEqual(pattern.match(u'\xfcber').group(1), u'\xfc')
        self.assertEqual(pattern.match(Text(u'\xe0x')).group(1), u'\xe0')
        self.assertEqual(found, [u'\xe9', u'\xe0'])

    def test_argument_errors_name_the_method(self):
        found = _optional().match('a')
        scanner = _letter().scanner('aa')
        for invoke, arguments, keywords, message in (
                (scanner.match, (1,), {}, 'match() takes no arguments (1 given)'),
                (scanner.search, (1,), {}, 'search() takes no arguments (1 given)'),
                (scanner.search, (), {'pos': 1}, 'search() takes no keyword arguments'),
                (found.group, (), {'default': 1}, 'group() takes no keyword arguments'),
                (found.start, (1, 2), {}, 'start expected at most 1 arguments, got 2'),
                (found.span, (), {'group': 1}, 'span() takes no keyword arguments'),
                (found.expand, (), {}, 'expand() takes exactly one argument (0 given)'),
                (found.expand, ('', ''), {}, 'expand() takes exactly one argument (2 given)'),
                (_sre.compile, ('a', 0), {}, 'function takes at least 3 arguments (2 given)'),
                (_sre.compile, ('a', 0, [1], 0, {}, [None], 1), {}, 'function takes at most 6 arguments (7 given)'),
                (_sre.compile, ('a', 0, [1]), {'groups': 0}, 'compile() takes no keyword arguments'),
                (_sre.getlower, (65,), {}, 'function takes exactly 2 arguments (1 given)'),
                (_sre.getlower, (65, 0), {'flags': 0}, 'getlower() takes no keyword arguments'),
                (_sre.getcodesize, (1,), {}, 'getcodesize() takes no arguments (1 given)')):
            with self.assertRaises(TypeError) as context:
                invoke(*arguments, **keywords)
            self.assertEqual(str(context.exception), message)
        self.assertEqual(scanner.match().span(), (0, 1))

    def test_short_str_subclass_length_bounds_empty_match_scans(self):
        class Text(str):
            def __len__(self):
                return 2
        repeated = _sre.compile('x*', 0, [29, 6, 0, 4294967295L, 19, 120, 1, 1], 0, {}, [None])
        subject = Text('abc')
        self.assertEqual(repeated.sub('-', subject), '-a-b-')
        self.assertEqual(repeated.subn('-', subject, 5), ('-a-b-', 3))
        self.assertEqual(repeated.findall(subject), ['', '', ''])
        self.assertEqual([found.span() for found in repeated.finditer(subject)], [(0, 0), (1, 1), (2, 2)])
        self.assertEqual(repeated.split(subject), ['ab'])
        self.assertEqual(repeated.search(subject, 3).span(), (2, 2))

    def test_failed_attempts_leave_the_marks_of_python27(self):
        # MIN_REPEAT_ONE counts one item past its maximum before it fails,
        # MIN_UNTIL keeps only the last mark around its tail, and BRANCH in a
        # repeat saves marks up to the last mark only when it is above 0.
        one = _sre.compile('(?!.??(a)c)\\w', 0, [17, 4, 0, 1, 1, 5, 17, 0, 31, 5, 0, 1, 2, 1, 21, 0, 19, 97, 21, 1, 19, 99,
                                                  1, 15, 4, 9, 4, 0, 1], 1, {}, [None, None])
        found = one.search('aab')
        self.assertEqual((found.span(), found.regs, found.lastindex, found.groups()),
                         ((0, 1), ((0, 1), (-1, -1)), None, (None,)))
        until = _sre.compile('(?!(?:y)*?(a))\\B', 0, [17, 4, 0, 0, 0, 5, 16, 0, 28, 5, 0, 4294967295L, 19, 121, 23, 21, 0,
                                                       19, 97, 21, 1, 1, 6, 4, 1], 1, {}, [None, None])
        found = until.search('a1')
        self.assertEqual((found.span(), found.regs, found.lastindex, found.groups()),
                         ((1, 1), ((1, 1), (1, 1)), 1, ('',)))
        branch = _sre.compile('(([abc]?)*?\\2|ab*)+?c', 0, [17, 4, 0, 1, 0, 28, 46, 1, 4294967295L, 21, 0, 7, 25, 28, 18, 0,
                                                            4294967295L, 21, 2, 29, 10, 0, 1, 15, 5, 27, 97, 99, 0, 1, 21, 3,
                                                            23, 12, 1, 18, 14, 12, 19, 97, 29, 6, 0, 4294967295L, 19, 98, 1,
                                                            18, 2, 0, 21, 1, 23, 19, 99, 1], 2, {}, [None, None, None])
        found = branch.search('ac')
        self.assertEqual((found.span(), found.regs, found.lastindex, found.groups()),
                         ((0, 2), ((0, 2), (2, 1), (-1, -1)), 1, ('', None)))

    def test_data_attributes_are_readonly_members_and_getsets(self):
        pattern = _optional()
        found = pattern.match('a')
        scanner = pattern.scanner('a')
        for instance, name, members, getsets, values in (
                (pattern, '_sre.SRE_Pattern', ('pattern', 'flags', 'groups', 'groupindex'), (),
                 ('(?P<word>a)?', 0, 1, {'word': 1})),
                (found, '_sre.SRE_Match', ('string', 're', 'pos', 'endpos'), ('lastindex', 'lastgroup', 'regs'),
                 ('a', pattern, 0, 1, 1, 'word', ((0, 1), (0, 1)))),
                (scanner, '_sre.SRE_Scanner', ('pattern',), (), (pattern,))):
            owner = type(instance)
            for attribute, value in zip(members + getsets, values):
                descriptor = owner.__dict__[attribute]
                member = attribute in members
                self.assertEqual(type(descriptor).__name__, 'member_descriptor' if member else 'getset_descriptor')
                self.assertEqual(repr(descriptor), "<%s '%s' of '%s' objects>" % ('member' if member else 'attribute',
                                                                                   attribute, name))
                self.assertIn(attribute, dir(instance))
                self.assertIs(descriptor.__get__(None, owner), descriptor)
                self.assertEqual(descriptor.__get__(instance, owner), value)
                self.assertEqual(getattr(instance, attribute), value)
                with self.assertRaises(TypeError) as context:
                    descriptor.__get__(1, int)
                self.assertEqual(str(context.exception), "descriptor '%s' for '%s' objects doesn't apply to 'int' object"
                                 % (attribute, name))
                message = 'readonly attribute' if member else "attribute '%s' of '%s' objects is not writable" % (attribute, name)
                for change in (lambda: setattr(instance, attribute, 1), lambda: delattr(instance, attribute)):
                    with self.assertRaises(TypeError if member else AttributeError) as context:
                        change()
                    self.assertEqual(str(context.exception), message)
                self.assertEqual(getattr(instance, attribute), value)

    def test_patterns_and_matches_refuse_copies(self):
        pattern = _letter()
        found = pattern.match('a')
        for value, kind in ((pattern, 'pattern'), (found, 'match')):
            for method, verb, arguments in (('__copy__', 'copy', ()), ('__deepcopy__', 'deepcopy', ({},))):
                with self.assertRaises(TypeError) as context:
                    getattr(value, method)(*arguments)
                self.assertEqual(str(context.exception), 'cannot %s this %s object' % (verb, kind))
            for call, message in ((lambda: value.__copy__(1), '__copy__() takes no arguments (1 given)'),
                                  (lambda: value.__deepcopy__(), '__deepcopy__() takes exactly one argument (0 given)'),
                                  (lambda: value.__deepcopy__(memo={}), '__deepcopy__() takes no keyword arguments')):
                with self.assertRaises(TypeError) as context:
                    call()
                self.assertEqual(str(context.exception), message)
        with self.assertRaises(TypeError) as context:
            type(pattern).__copy__(found)
        self.assertEqual(str(context.exception),
                         "descriptor '__copy__' requires a '_sre.SRE_Pattern' object but received a '_sre.SRE_Match'")

    def test_compile_parses_its_arguments_as_python27(self):
        code = [19, 97, 1]
        for arguments, kind, message in (
                (('a', 'x', 'x'), TypeError, 'an integer is required'),
                (('a', 1.5, code), TypeError, 'integer argument expected, got float'),
                (('a', 1 << 31, code), OverflowError, 'signed integer is greater than maximum'),
                (('a', -(1 << 31) - 1, code), OverflowError, 'signed integer is less than minimum'),
                (('a', 0, 'x'), TypeError, 'argument 3 must be list, not str'),
                (('a', 0, tuple(code)), TypeError, 'argument 3 must be list, not tuple'),
                (('a', 0, None), TypeError, 'argument 3 must be list, not None'),
                (('a', 0, code, 'x'), TypeError, 'an integer is required'),
                (('a', 0, code, 1.5), TypeError, 'integer argument expected, got float'),
                (('a', 0, code, 1 << 64), OverflowError, 'Python int too large to convert to C long')):
            with self.assertRaises(kind) as context:
                _sre.compile(*arguments)
            self.assertEqual(str(context.exception), message)
        class Integer(object):
            def __init__(self, value):
                self.value = value
            def __int__(self):
                return self.value
        class Code(list):
            pass
        pattern = _sre.compile(5, Integer(0), Code([21, 0, 19, 97, 21, 1, 1]), Integer(1), 7, (None, 'name'))
        self.assertEqual((pattern.pattern, pattern.flags, pattern.groups, pattern.groupindex), (5, 0, 1, 7))
        found = pattern.match('a')
        self.assertEqual((found.lastindex, found.lastgroup, found.group(1)), (1, 'name', 'a'))
        with self.assertRaisesRegexp(IndexError, 'no such group'):
            found.group('name')
        self.assertIs(_sre.compile('a', 0, [21, 0, 19, 97, 21, 1, 1], 1, None, 3).match('a').lastgroup, None)

    def test_negative_str_subclass_length_is_rejected_as_len_does(self):
        class Text(str):
            def __len__(self):
                return -1
        for call in (lambda: _letter().search(Text('abc')), lambda: len(Text('abc'))):
            with self.assertRaises(ValueError) as context:
                call()
            self.assertEqual(str(context.exception), '__len__() should return >= 0')

    def test_sre_types_are_final_and_immutable(self):
        pattern = _letter()
        for owner, name in ((type(pattern), '_sre.SRE_Pattern'), (type(pattern.match('a')), '_sre.SRE_Match'),
                            (type(pattern.scanner('a')), '_sre.SRE_Scanner')):
            with self.assertRaises(TypeError) as context:
                type('Derived', (owner,), {})
            self.assertEqual(str(context.exception), "type '%s' is not an acceptable base type" % name)
            with self.assertRaises(TypeError) as context:
                owner.extra = 1
            self.assertEqual(str(context.exception), "can't set attributes of built-in/extension type '%s'" % name)
            with self.assertRaises(TypeError) as context:
                owner()
            self.assertEqual(str(context.exception), "cannot create '%s' instances" % name)

    def test_programs_run_with_the_marks_validation_accepts(self):
        # _validate lets MARK reach 2 * groups + 1, of 100 groups for a
        # program compiled without groups, and a match keeps the groups' own.
        for groups, values, regs in ((0, (), ((0, 2),)), (1, ('a',), ((0, 2), (0, 1))),
                                     (2, ('a', 'b'), ((0, 2), (0, 1), (1, 2)))):
            found = _pair(groups).match('ab')
            self.assertEqual((found.span(), found.groups(), found.lastindex, found.regs), ((0, 2), values, 2, regs))
        for groups in (0, 1):
            pattern = _sre.compile('(a)\\1', 0, [21, 0, 19, 97, 21, 1, 12, 0, 1], groups)
            self.assertEqual((pattern.match('aa').span(), pattern.match('ab')), ((0, 2), None))

    def test_lastgroup_reads_indexgroup_as_a_sequence(self):
        # match_lastgroup_get uses PySequence_GetItem: a dict has no sq_item,
        # a class defined in Python has one with any __getitem__, and a
        # failure reads as no name.
        class Names(dict):
            pass
        class Lookup(object):
            def __getitem__(self, index):
                return 'item%d' % index
        class Classic:
            def __getitem__(self, index):
                return 'classic%d' % index
        class Failing(object):
            def __getitem__(self, index):
                raise KeyError(index)
        for indexgroup, name in (({2: 'second'}, None), (Names({2: 'second'}), 'second'),
                                 ([None, 'first', 'second'], 'second'), (('x', 'y', 'z'), 'z'), ('xyz', 'z'),
                                 (bytearray('xyz'), 122), (Lookup(), 'item2'), (Classic(), 'classic2'),
                                 (Failing(), None), (object(), None), ([None], None), (None, None)):
            self.assertEqual(_pair(2, {}, indexgroup).match('ab').lastgroup, name)

    def test_group_names_follow_the_mapping_protocol_of_groupindex(self):
        # match_getindex looks a name up with PyObject_GetItem and clears a
        # failure, and groupdict lists the names with PyMapping_Keys; only a
        # pattern compiled without a groupindex has no names to list.
        class Missing(dict):
            def __missing__(self, name):
                return 2
        class Names(object):
            def keys(self):
                return ['first', 'second']
            def __getitem__(self, name):
                return {'first': 1, 'second': 2}[name]
        class FailingKeys(dict):
            def keys(self):
                raise ValueError('keys failed')
        class FailingItem(dict):
            def __getitem__(self, name):
                raise ValueError('item failed')
        absent = _pair(2)
        self.assertIs(absent.groupindex, None)
        self.assertEqual(absent.match('ab').groupdict(), {})
        for groupindex in (None, [1]):
            found = _pair(2, groupindex).match('ab')
            with self.assertRaises(AttributeError) as context:
                found.groupdict()
            self.assertEqual(str(context.exception), "'%s' object has no attribute 'keys'" % type(groupindex).__name__)
            with self.assertRaisesRegexp(IndexError, 'no such group'):
                found.group('first')
        found = _pair(2, Missing(first=1)).match('ab')
        self.assertEqual((found.group('first'), found.span('other'), found.groupdict()), ('a', (1, 2), {'first': 'a'}))
        found = _pair(2, Names()).match('ab')
        self.assertEqual((found.group('second'), found.groupdict()), ('b', {'first': 'a', 'second': 'b'}))
        with self.assertRaisesRegexp(ValueError, 'keys failed'):
            _pair(2, FailingKeys(first=1)).match('ab').groupdict()
        found = _pair(2, FailingItem(first=1)).match('ab')
        for call in (lambda: found.group('first'), found.groupdict):
            with self.assertRaisesRegexp(IndexError, 'no such group'):
                call()
