"""Project-authored Python 2 compiler boundary and callback regressions."""

import unittest
import _codecs
import sys


class CompilerEdges(unittest.TestCase):
    def test_adjacent_literals_keep_default_codec_replacements(self):
        previous = _codecs.lookup_error('strict')
        events = []
        def handler(error):
            events.append(error.object)
            return u'\u1234Z', error.end
        cases = (("u'a' '\\xff'", u'a\u1234Z', ['\xff']),
                 ("'\\xff' u'a'", u'\u1234Za', ['\xff']),
                 ("'a' '\\xff' u'b'", u'a\u1234Zb', ['a\xff']),
                 ("u'a' '\\xff' '\\xfe'", u'a\u1234Z\u1234Z', ['\xff', '\xfe']))
        try:
            _codecs.register_error('strict', handler)
            for source, expected, trace in cases:
                for mode in ('eval', 'exec'):
                    events[:] = []
                    if mode == 'eval':
                        result = eval(compile(source, '<literal>', mode))
                    else:
                        namespace = {}
                        exec compile('value=' + source, '<literal>', mode) in namespace
                        result = namespace['value']
                    self.assertEqual(result, expected)
                    self.assertEqual(events, trace)
        finally:
            _codecs.register_error('strict', previous)

    def test_adjacent_literal_callback_failure_keeps_identity_and_handled_state(self):
        previous = _codecs.lookup_error('strict')
        outer = ValueError('outer')
        def handler(error):
            raise marker
        def check_failure():
            with self.assertRaises(type(marker)) as caught:
                compile("u'a' '\\xff'", '<literal>', 'eval')
            self.assertIs(caught.exception, marker)
        try:
            _codecs.register_error('strict', handler)
            for marker in (RuntimeError('literal'), TypeError('literal')):
                try:
                    raise outer
                except ValueError:
                    check_failure()
                    self.assertIs(sys.exc_info()[1], outer)
        finally:
            _codecs.register_error('strict', previous)

    def test_adjacent_literal_codec_callback_can_compile_and_replace_handler(self):
        previous = _codecs.lookup_error('strict')
        events = []
        def second(error):
            events.append(('second', error.object))
            return u'Y', error.end
        def first(error):
            events.append(('first', error.object))
            self.assertEqual(eval(compile("u'n' 'x'", '<nested>', 'eval')), u'nx')
            _codecs.register_error('strict', second)
            return u'Z', error.end
        try:
            _codecs.register_error('strict', first)
            self.assertEqual(eval("u'a' '\\xff' '\\xfe'"), u'aZY')
            self.assertEqual(events, [('first', '\xff'), ('second', '\xfe')])
        finally:
            _codecs.register_error('strict', previous)

    def test_unicode_escape_errors_take_strict_handler_replacements(self):
        # The escape decoders pass an escape they cannot decode to the strict
        # handler with one exception per literal and go on at the position
        # it returns; the raw decoder reports the whole backslash run.
        previous = _codecs.lookup_error('strict')
        events = []
        def handler(error):
            events.append((error, error.encoding, error.object, error.start, error.end, error.reason))
            return u'<%d>' % error.start, error.end
        cases = [("u'a\\N{nope}b'", u'a<1>b', [('unicodeescape', 'a\\N{nope}b', 1, 9, 'unknown Unicode character name')]),
                 ("u'\\x4g\\u12'", u'<0>g<4>', [('unicodeescape', '\\x4g\\u12', 0, 3, 'truncated \\xXX escape'),
                                                ('unicodeescape', '\\x4g\\u12', 4, 8, 'truncated \\uXXXX escape')]),
                 ("u'\\U00110000z\\N'", u'<0>z<11>', [('unicodeescape', '\\U00110000z\\N', 0, 10, 'illegal Unicode character'),
                                                       ('unicodeescape', '\\U00110000z\\N', 11, 13, 'malformed \\N character escape')]),
                 ("u'\\N{'", u'<0>', [('unicodeescape', '\\N{', 0, 3, 'malformed \\N character escape')]),
                 ("ur'\\\\\\u12z'", u'\\\\<0>z', [('rawunicodeescape', '\\\\\\u12z', 0, 6, 'truncated \\uXXXX')]),
                 ("ur'a\\U00110000'", u'a<1>', [('rawunicodeescape', 'a\\U00110000', 1, 11, '\\Uxxxxxxxx out of range')])]
        try:
            _codecs.register_error('strict', handler)
            for source, expected, trace in cases:
                events[:] = []
                self.assertEqual(eval(compile(source, '<literal>', 'eval')), expected)
                self.assertEqual([event[1:] for event in events], trace)
                self.assertTrue(all(event[0] is events[0][0] for event in events))
        finally:
            _codecs.register_error('strict', previous)

    def test_unicode_escape_handler_position_and_result_checks(self):
        previous = _codecs.lookup_error('strict')
        results = []
        def handler(error):
            return results.pop(0)
        cases = [([(u'', 4), (u'', 8)], u''),
                 ([(u'N', -6), (u'M', -1)], u'N4gMh'),
                 ([(u'', 9)], IndexError('position 9 from error handler out of bounds')),
                 ([('s', 3)], TypeError('decoding error handler must return (unicode, int) tuple')),
                 ([(u'', 3, 4)], TypeError('decoding error handler must return (unicode, int) tuple')),
                 ([(u'', 3.0)], TypeError('integer argument expected, got float'))]
        try:
            _codecs.register_error('strict', handler)
            for replies, expected in cases:
                results[:] = replies
                if isinstance(expected, Exception):
                    with self.assertRaises(type(expected)) as caught:
                        compile("u'\\x4g\\x5h'", '<literal>', 'eval')
                    self.assertEqual(caught.exception.args, expected.args)
                else:
                    self.assertEqual(eval(compile("u'\\x4g\\x5h'", '<literal>', 'eval')), expected)
                self.assertEqual(results, [])
        finally:
            _codecs.register_error('strict', previous)

    def test_unicode_escape_lone_final_backslash_goes_to_handler(self):
        # A handler that resumes a literal at its last byte, the second
        # backslash of a pair, leaves the escape decoder a lone backslash it
        # reports; the raw decoder keeps it.
        previous = _codecs.lookup_error('strict')
        events = []
        def handler(error):
            events.append((error.encoding, error.start, error.end, error.reason))
            if len(events) == 3:
                return u'', error.end
            return u'<%d>' % len(events), len(error.object) - 1
        cases = [("u'\\x\\\\'", u'<1><2>', [('unicodeescape', 0, 2, 'truncated \\xXX escape'),
                                                 ('unicodeescape', 3, 4, '\\ at end of string'),
                                                 ('unicodeescape', 3, 4, '\\ at end of string')]),
                 ("u'\xe9\\N\\\\'", u'\xe9<1><2>', [('unicodeescape', 1, 3, 'malformed \\N character escape'),
                                                         ('unicodeescape', 4, 5, '\\ at end of string'),
                                                         ('unicodeescape', 4, 5, '\\ at end of string')]),
                 ("ur'\\u\\\\'", u'<1>\\', [('rawunicodeescape', 0, 2, 'truncated \\uXXXX')])]
        try:
            _codecs.register_error('strict', handler)
            for source, expected, trace in cases:
                events[:] = []
                self.assertEqual(eval(compile(source, '<literal>', 'eval')), expected)
                self.assertEqual(events, trace)
        finally:
            _codecs.register_error('strict', previous)

    def test_unicode_literal_handler_failure_propagates_unless_unicode_error(self):
        previous = _codecs.lookup_error('strict')
        def handler(error):
            raise marker
        sources = ["u'\\x4g'", "ur'\\u12'", "# coding: utf-8\nvalue = u'ab\xffcd'\n"]
        try:
            _codecs.register_error('strict', handler)
            for source in sources:
                for marker in (KeyError('from handler'), MemoryError()):
                    with self.assertRaises(type(marker)) as caught:
                        compile(source, '<literal>', 'exec')
                    self.assertIs(caught.exception, marker)
                marker = UnicodeError('literal callback')
                try:
                    compile(source, '<literal>', 'exec')
                except SyntaxError as error:
                    self.assertEqual(error.msg, '(unicode error) literal callback')
                else:
                    self.fail('expected SyntaxError')
        finally:
            _codecs.register_error('strict', previous)

    def test_invalid_utf8_unicode_literal_takes_handler_replacement(self):
        # decode_utf8 decodes the run of non-ASCII bytes and decode_unicode
        # escapes each character of the result, a backslash included.
        previous = _codecs.lookup_error('strict')
        events = []
        replacement = [u'?']
        def handler(error):
            events.append((error.object, error.start, error.end))
            return replacement[0], error.end
        try:
            _codecs.register_error('strict', handler)
            for source, value, trace in [("u'ab\xffcd'", u'ab?cd', [('\xff', 0, 1)]),
                                         ("u'ab\xc3\xa9\xffcd \xfe'", u'ab\xe9?cd ?', [('\xc3\xa9\xff', 2, 3), ('\xfe', 0, 1)]),
                                         ("ur'ab\\\\\xffcd'", u'ab\\\\?cd', [('\xff', 0, 1)])]:
                events[:] = []
                namespace = {}
                exec compile('# coding: utf-8\nvalue = ' + source + '\n', '<literal>', 'exec') in namespace
                self.assertEqual((namespace['value'], events), (value, trace))
            replacement[0] = u'\\'
            self.assertEqual(eval(compile("# coding: utf-8\nu'a\xffn'\n", '<literal>', 'eval')), u'a\\n')
        finally:
            _codecs.register_error('strict', previous)

    def test_invalid_utf8_unicode_literal_handler_positions_and_replacements(self):
        # decode_utf8 calls the handler for each invalid sequence of a run
        # and decodes on from the position it returns; each character of a
        # replacement becomes one of the literal, then the escape decoder
        # calls the handler for an escape after the run. A source without a
        # cookie or with a Latin-1 one keeps its bytes.
        previous = _codecs.lookup_error('strict')
        events = []
        replies = []
        def handler(error):
            events.append((error.reason, error.start, error.end, error.object))
            replacement, position = replies.pop(0)
            return replacement, error.end if position is None else position
        pad = ' ' * 100
        invalid = 'invalid start byte'
        cases = [("u'\xffa\xfe\xfdb\xc3\xa9\xfc'", [(u'1', None), (u'2', None), (u'3', None), (u'4', None)], u'1a23b\xe94',
                  [(invalid, 0, 1, '\xff'), (invalid, 0, 1, '\xfe\xfd'), (invalid, 1, 2, '\xfe\xfd'), (invalid, 2, 3, '\xc3\xa9\xfc')]),
                 ("u'\xff" + pad + "'", [(u'L' * 40, None)], u'L' * 40 + pad, [(invalid, 0, 1, '\xff')]),
                 ("u'\xff" + pad + "'", [(u'', None)], unicode(pad), [(invalid, 0, 1, '\xff')]),
                 ("ur'\xff" + pad + "'", [(u'\U0001f600' * 30, None)], u'\U0001f600' * 30 + pad, [(invalid, 0, 1, '\xff')]),
                 ("u'\xff" + pad + "'", [(u'\ud83d\\x41\\', None)], u'\ud83d\\x41\\' + pad, [(invalid, 0, 1, '\xff')]),
                 ("ur'\\\xff" + pad + "'", [(u'\\u0041', None)], u'\\\\u0041' + pad, [(invalid, 0, 1, '\xff')]),
                 ("u'a\xff\xfe\xfdb'", [(u'F', 2), (u'?', None)], u'aF?b', [(invalid, 0, 1, '\xff\xfe\xfd'), (invalid, 2, 3, '\xff\xfe\xfd')]),
                 ("u'a\xff\xfe\xfdb'", [(u'B', 0), (u'C', 3)], u'aBCb', [(invalid, 0, 1, '\xff\xfe\xfd'), (invalid, 0, 1, '\xff\xfe\xfd')]),
                 ("u'a\xc3\xa9\xff\xc3\xa9b'", [(u'<>', 1), (u'!', 3)], u'a\xe9<>!\xe9b', [(invalid, 2, 3, '\xc3\xa9\xff\xc3\xa9'), (invalid, 1, 2, '\xc3\xa9\xff\xc3\xa9')]),
                 ("u'a\xc3\xa9\xff\xc3\xa9b'", [(u'<>', 5)], u'a\xe9<>b', [(invalid, 2, 3, '\xc3\xa9\xff\xc3\xa9')]),
                 ("u'a\xff\xfe'", [(u'x', -1), (u'?', None)], u'ax?', [(invalid, 0, 1, '\xff\xfe'), (invalid, 1, 2, '\xff\xfe')]),
                 ("u'\xff' u'\xfe' 'c' ur'\\\xfd'", [(u'1', None), (u'2', None), (u'3', None)], u'12c\\3', [(invalid, 0, 1, '\xff'), (invalid, 0, 1, '\xfe'), (invalid, 0, 1, '\xfd')]),
                 ("u'\xff\\x4g" + pad + "'", [(u'R' * 30, None), (u'E' * 300, None)], u'R' * 30 + u'E' * 300 + u'g' + pad,
                  [(invalid, 0, 1, '\xff'), ('truncated \\xXX escape', 300, 303, '\\U00000052' * 30 + '\\x4g' + pad)])]
        try:
            _codecs.register_error('strict', handler)
            for source, reply_list, value, trace in cases:
                events[:] = []
                replies[:] = reply_list
                namespace = {}
                exec compile('# coding: utf-8\nvalue = ' + source + '\n', '<literal>', 'exec') in namespace
                self.assertEqual((namespace['value'], events, replies), (value, trace, []))
            for run in (lambda source: eval(compile(source, '<literal>', 'eval')), eval):
                events[:] = []
                replies[:] = [(u'\U0001f600', None)]
                self.assertEqual(run("# coding: utf-8\nu'a\xffb'\n"), u'a\U0001f600b')
                self.assertEqual(events, [(invalid, 0, 1, '\xff')])
            for cookie in ('', '# coding: latin-1\n'):
                namespace = {}
                exec cookie + "value = u'a\xff\xfeb' ur'\\\xfd'\n" in namespace
                self.assertEqual(namespace['value'], u'a\xff\xfeb\\\xfd')
            self.assertEqual(events, [(invalid, 0, 1, '\xff')])
        finally:
            _codecs.register_error('strict', previous)

    def test_codec_cookie_source_tokenizes_decoded_text(self):
        # translate_into_utf8 decodes the whole string with the strict
        # handler; the tokenizer reads the text the replacements make.
        previous = _codecs.lookup_error('strict')
        events = []
        replacement = [u'?']
        def handler(error):
            events.append((error.encoding, error.start, error.end))
            return replacement[0], error.end
        try:
            _codecs.register_error('strict', handler)
            for cookie, encoding in [('utf8', 'utf8'), ('ascii', 'ascii'), ('us-ascii', 'ascii')]:
                for literal, value in [("'a\xffb'", 'a?b'), ("u'a\xffb'", u'a?b')]:
                    events[:] = []
                    namespace = {}
                    source = '# coding: %s\nvalue = %s # \xfe\n' % (cookie, literal)
                    exec compile(source, '<source>', 'exec') in namespace
                    self.assertEqual(namespace['value'], value)
                    start = source.index('\xff')
                    self.assertEqual(events, [(encoding, start, start + 1), (encoding, start + 6, start + 7)])
            replacement[0] = u"' 'z"
            self.assertEqual(eval(compile("# coding: utf8\n'a\xff'\n", '<source>', 'eval')), 'az')
        finally:
            _codecs.register_error('strict', previous)

    def test_ascii_cookie_byte_literal_encodes_handler_replacement(self):
        # parsestr encodes a byte literal back to the ASCII of the cookie:
        # the whole literal without a backslash, the run with one.
        previous = _codecs.lookup_error('strict')
        def handler(error):
            return u'\xe9', error.end
        try:
            _codecs.register_error('strict', handler)
            for literal, position in [("'a\xffb'", 1), ("'\\x41a\xffb'", 0), ("r'\\x\xff'", 2)]:
                try:
                    compile('# coding: ascii\nvalue = ' + literal + '\n', '<source>', 'exec')
                except SyntaxError as error:
                    self.assertEqual(error.msg, "(unicode error) 'ascii' codec can't encode character u'\\xe9' in position %d: ordinal not in range(128)" % position)
                    self.assertEqual((error.lineno, error.offset, error.text), (2, None, None))
                else:
                    self.fail('expected SyntaxError')
            namespace = {}
            exec compile("# coding: ascii\nvalue = u'a\xffb' # \xfe\n", '<source>', 'exec') in namespace
            exec compile("# coding: utf8\nother = 'a\xffb'\n", '<source>', 'exec') in namespace
            self.assertEqual((namespace['value'], namespace['other']), (u'a\xe9b', 'a\xc3\xa9b'))
        finally:
            _codecs.register_error('strict', previous)

    def test_ascii_cookie_diagnostic_restores_handler_replacement(self):
        # PyTokenizer_RestoreEncoding encodes the line of a diagnostic back
        # to the ASCII of the cookie with 'replace' and counts its offset in
        # characters.
        previous = _codecs.lookup_error('strict')
        def handler(error):
            return u'\xe9\u20ac', error.end
        cases = [("# coding: ascii\nx = '\xff' +\n", ('invalid syntax', 2, 11, "x = '??' +\n")),
                 ("# coding: us-ascii\nx = u'\xff\xfe' $\n", ('invalid syntax', 2, 13, "x = u'????' $\n")),
                 ("# coding: ascii\nx = '\xff\n", ('EOL while scanning string literal', 2, 8, "x = '??\n"))]
        try:
            _codecs.register_error('strict', handler)
            for source, expected in cases:
                with self.assertRaises(SyntaxError) as caught:
                    compile(source, '<source>', 'exec')
                error = caught.exception
                self.assertEqual((error.msg, error.lineno, error.offset, error.text), expected)
        finally:
            _codecs.register_error('strict', previous)

    def test_codec_cookie_file_decodes_with_strict_handler(self):
        # The StreamReader of a cookie codec without a fast path decodes the
        # file with the strict handler, line after line.
        previous = _codecs.lookup_error('strict')
        writes = sys.dont_write_bytecode
        def handler(error):
            return u'?', error.end
        try:
            _codecs.register_error('strict', handler)
            sys.dont_write_bytecode = True
            module = __import__('compiler_files.codec_handler', {}, {}, ['x'])
            self.assertEqual((module.x, module.y), ('a?b', u'c?d'))
        finally:
            sys.dont_write_bytecode = writes
            _codecs.register_error('strict', previous)

    def test_flags_use_int_protocol_before_mode_value(self):
        events = []

        class Argument(object):
            def __int__(self):
                events.append('int')
                return 0

        self.assertRaises(ValueError, compile, '1', 'file', 'bad', Argument(), Argument())
        self.assertEqual(events, ['int', 'int'])
        events[:] = []
        self.assertRaises(TypeError, compile, '1', 'file', None, Argument())
        self.assertEqual(events, [])

    def test_filename_conversion_precedes_flags(self):
        events = []

        class Argument(object):
            def __int__(self):
                events.append('int')
                return 0

        self.assertRaises(UnicodeEncodeError, compile, '1', u'\u20ac', 'eval', Argument())
        self.assertEqual(events, [])
        self.assertEqual(compile('1', u'file', u'eval').co_filename, 'file')

    def test_legacy_source_buffers_observe_flag_callback(self):
        source = bytearray('1')

        class Argument(object):
            def __int__(self):
                source[0] = ord('2')
                return 0

        code = compile(source, 'file', 'eval', Argument())
        self.assertEqual(eval(code), 2)
        self.assertEqual(eval(compile(buffer('3'), 'file', 'eval')), 3)
        self.assertRaises(TypeError, compile, memoryview('4'), 'file', 'eval')

    def test_syntax_phases_preserve_args_shape(self):
        for source, count, line in [('return 1', 2, 1),
                                    ('def f(a,a):\n pass', 1, 1),
                                    ('def f():\n yield 1\n return 2', 1, 3)]:
            try:
                compile(source, 'file', 'exec')
            except SyntaxError as error:
                self.assertEqual(len(error.args), count)
                self.assertEqual(error.filename, 'file')
                self.assertEqual(error.lineno, line)
                self.assertIs(error.offset, None)
                self.assertIs(error.text, None)
            else:
                self.fail('expected SyntaxError')

    def test_unicode_cookie_depends_on_compile_mode(self):
        source = u'# coding: utf-8\n1'
        for mode in ['exec', 'eval', 'single']:
            try:
                compile(source, 'file', mode)
            except SyntaxError as error:
                self.assertEqual(error.lineno, 0)
                self.assertIs(error.offset, None)
                self.assertIs(error.text, None)
            else:
                self.fail('expected SyntaxError')
        try:
            compile(u'# coding: utf-8\nx=', 'file', 'eval')
        except SyntaxError as error:
            self.assertEqual(error.lineno, 2)
            self.assertEqual(error.text, 'x=')
        else:
            self.fail('expected parser error before encoding declaration')

    def test_normalization_respects_input_mode(self):
        code = compile('if True:\n pass', 'file', 'exec', 512, 1)
        self.assertEqual(code.co_name, '<module>')
        for source, line, offset, text in [('', 0, 0, ''), ('x=1', 1, 2, 'x=1')]:
            try:
                compile(source, 'file', 'eval')
            except SyntaxError as error:
                self.assertEqual((error.lineno, error.offset, error.text),
                                 (line, offset, text))
            else:
                self.fail('expected SyntaxError')

    def test_one_byte_string_constants(self):
        # The compiler differential compares the marshal bytes of these
        # constants with CPython's: an escape-decoded one-byte string stays a
        # non-interned 's' string through concatenation with an empty one.
        values = ('' '\xff', '\xfe' '', '' + '\xfd', '\xfc' + '', '\xfb' * 1, '' '!', '%s' % '\xfa')
        self.assertEqual(values, ('\xff', '\xfe', '\xfd', '\xfc', '\xfb', '!', '\xfa'))

    def test_line_continuation_decodes_string_literal(self):
        # A literal whose only escape is a line continuation is decoded into a
        # new string too, not the shared one-byte string.
        values = ('|\
', '' '-\
')
        self.assertEqual(values, ('|', '-'))
        self.assertIsNot(values[0], chr(124))
        self.assertIsNot(values[1], chr(45))

    def test_adjacent_string_literals_join_once(self):
        # Joining the parts pairwise kept every intermediate result until the
        # end of the compilation and exceeded the compiler arena limit.
        parts = ['%04d' % index for index in range(12000)]
        source = 'value = (' + ' '.join(["'%s'" % part for part in parts]) + ')\n'
        namespace = {}
        exec compile(source, 'file', 'exec') in namespace
        self.assertEqual(namespace['value'], ''.join(parts))
        exec compile('value = (u"" ' + ' '.join(["'%s'" % part for part in parts]) + ')\n',
                     'file', 'exec') in namespace
        self.assertEqual(namespace['value'], u''.join(parts))
        self.assertEqual(eval("'a' '' 'b' u'c' 'd'"), u'abcd')
        for source, message in [("'ab' '\\xe9' 'cd' u'e' '\\x'", 'byte 0xe9 in position 2'),
                                ("u'e' 'ab' '\\xe9cd' '\\x'", 'byte 0xe9 in position 0')]:
            try:
                compile(source, 'file', 'eval')
            except SyntaxError as error:
                self.assertEqual(error.msg, "(unicode error) 'ascii' codec can't decode %s: ordinal not in range(128)" % message)
            else:
                self.fail('expected SyntaxError')

    def test_escaped_backslash_takes_its_character_before_non_ascii(self):
        # decode_unicode escapes a backslash only when it starts an escape:
        # an escaped backslash keeps the UTF-8 character after it intact.
        for source, value in [('# coding: utf-8\nu"\\\\\xc3\xa9"', u'\\\xe9'),
                              ('# coding: utf-8\nur"\\\\\xc3\xa9"', u'\\\\\xe9'),
                              ('# coding: utf-8\nu"\\\xc3\xa9"', u'\\\xe9'),
                              ('\xef\xbb\xbfu"C:\\\\\xc3\x89lise\\\\\xc3\xa9t\xc3\xa9"', u'C:\\\xc9lise\\\xe9t\xe9'),
                              (u'u"\\\\\\\\\u20ac"', u'\\\\\u20ac')]:
            self.assertEqual(eval(source), value)

    def test_unicode_source_joins_surrogate_pairs(self):
        # compile, exec and eval encode a unicode source like
        # PyUnicode_AsUTF8String, which joins a surrogate pair. The pair is
        # built at run time, as marshal joins one in a constant too.
        pair = unichr(0xd83d) + unichr(0xde00)
        self.assertEqual(eval(u"u'" + pair + u"'"), u'\U0001f600')
        namespace = {}
        exec compile(u"x = '" + pair + u"'\ny = u'\ud83d'", 'file', 'exec') in namespace
        self.assertEqual((namespace['x'], namespace['y']), ('\xf0\x9f\x98\x80', u'\ud83d'))
        exec u"z = u'" + pair[::-1] + u"'" in namespace
        self.assertEqual(namespace['z'], pair[::-1])

    def test_unknown_encoding_has_decoder_diagnostic(self):
        try:
            compile('# coding: made_up\nx=1', 'file', 'exec')
        except SyntaxError as error:
            self.assertEqual(error.msg, 'unknown encoding: made_up')
            self.assertEqual((error.filename, error.lineno, error.offset, error.text),
                             ('file', 0, 0, None))
        else:
            self.fail('expected unknown encoding failure')

    def test_return_rule_keeps_split_jump_byte_in_dead_code(self):
        # The peephole rule for RETURN LOAD_CONST None RETURN checks only the
        # byte four after the first RETURN: after END_FINALLY and POP_BLOCK it
        # splits the jump over the handler and leaves its high byte 6, which
        # is no opcode, in dead code. The function and the handler still run.
        source = ('def f():\n    a = 0\n    try:\n        try:\n            %s\n'
                  '        finally:\n            return a\n    except NameError:\n'
                  + '        a = 1\n' * 267)
        for body, result in [('a = 1', 1), ('del a', None)]:
            namespace = {}
            exec source % body in namespace
            code = namespace['f'].func_code.co_code
            index = code.index(chr(83))
            self.assertEqual([ord(c) for c in code[index:index + 3]], [83, 6, 4])
            self.assertEqual(namespace['f'](), result)
            namespace.clear()
