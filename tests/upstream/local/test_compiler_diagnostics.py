"""Project-authored Python 2 decoder phase and diagnostic regressions."""

import unittest
import _codecs


class CompilerDiagnostics(unittest.TestCase):
    def test_adjacent_literal_wraps_only_unicode_error_from_codec_callback(self):
        previous = _codecs.lookup_error('strict')
        def handler(error):
            raise UnicodeError('literal callback')
        try:
            _codecs.register_error('strict', handler)
            error = self.syntax_error("u'a' '\\xff'")
            self.assertEqual(error.msg, '(unicode error) literal callback')
            self.assertEqual((error.lineno, error.offset, error.text), (1, None, None))
        finally:
            _codecs.register_error('strict', previous)

    def syntax_error(self, source, mode='eval'):
        try:
            compile(source, '<diagnostic>', mode)
        except SyntaxError as error:
            return error
        self.fail('expected SyntaxError')

    def test_unicode_escape_diagnostic_payload(self):
        error = self.syntax_error("u'a\\x0q'")
        message = "(unicode error) 'unicodeescape' codec can't decode bytes in position 1-3: truncated \\xXX escape"
        self.assertEqual(error.msg, message)
        self.assertEqual(error.args, (message, ('<diagnostic>', 1, None, None)))
        self.assertEqual((error.filename, error.lineno, error.offset, error.text),
                         ('<diagnostic>', 1, None, None))

    def test_unicode_invalid_codepoint(self):
        error = self.syntax_error("u'\\Uffffffff'")
        self.assertEqual(error.msg, "(unicode error) 'unicodeescape' codec can't decode bytes in position 0-9: illegal Unicode character")

    def test_raw_unicode_decoder_label_and_reason(self):
        error = self.syntax_error("ur'a\\u12zzzzzzzz'")
        self.assertEqual(error.msg, "(unicode error) 'rawunicodeescape' codec can't decode bytes in position 1-4: truncated \\uXXXX")
        error = self.syntax_error("ur'\\Uffffffff'")
        self.assertTrue(error.msg.endswith('\\Uxxxxxxxx out of range'))

    def test_named_escape_failure_spans(self):
        for contents, span, reason in [('\\N', '0-1', 'malformed \\N character escape'),
                                      ('\\N{}', '0-2', 'malformed \\N character escape'),
                                      ('a\\N{MADE UP}', '1-11', 'unknown Unicode character name')]:
            error = self.syntax_error("u'" + contents + "'")
            self.assertEqual(error.msg, "(unicode error) 'unicodeescape' codec can't decode bytes in position " + span + ': ' + reason)

    def test_valid_unicode_escape_values(self):
        self.assertEqual(eval(compile("u'\\ud800'", '<diagnostic>', 'eval')), u'\ud800')
        self.assertEqual(eval(compile("u'\\U0010ffff'", '<diagnostic>', 'eval')), u'\U0010ffff')
        self.assertEqual(eval(compile("u'\\N{LATIN SMALL LETTER A}'", '<diagnostic>', 'eval')), u'a')

    def test_utf8_cookie_preserves_byte_literals_and_comments(self):
        self.assertEqual(eval(compile("# coding: utf-8\n'\xe9'", '<diagnostic>', 'eval')), '\xe9')
        namespace = {}
        exec compile('# coding: utf-8\n# \xe9\nx=7', '<diagnostic>', 'exec') in namespace
        self.assertEqual(namespace['x'], 7)

    def test_utf8_unicode_literal_decode_error_is_late(self):
        source = '# coding: utf-8\nx=u"\xe9"'
        error = self.syntax_error(source, 'exec')
        self.assertEqual(error.msg, "(unicode error) 'utf8' codec can't decode byte 0xe9 in position 0: unexpected end of data")
        self.assertEqual((error.lineno, error.offset, error.text), (2, None, None))
        error = self.syntax_error(source)
        self.assertEqual(error.msg, 'invalid syntax')
        self.assertEqual(error.text, 'x=u"\xef\xbf\xbd"')

    def test_utf8_failure_position_within_nonascii_run(self):
        error = self.syntax_error("# coding: utf-8\nu'\xc3\xa9\xe9'")
        self.assertEqual(error.msg, "(unicode error) 'utf8' codec can't decode byte 0xe9 in position 2: unexpected end of data")

    def test_escape_spans_use_source_encoding_domain(self):
        latin1 = self.syntax_error("# coding: latin-1\nu'\xe9\\xq'")
        utf8 = self.syntax_error("# coding: utf-8\nu'\xc3\xa9\\xq'")
        self.assertTrue(latin1.msg.endswith('position 1-2: truncated \\xXX escape'))
        self.assertTrue(utf8.msg.endswith('position 10-11: truncated \\xXX escape'))

    def test_registry_latin1_codec_expands_unicode_escapes(self):
        # A Latin-1 codec of the registry decodes the source before it is
        # tokenized, so its characters expand to ten-byte escapes too.
        for name in ['latin1', 'l1', 'cp819']:
            error = self.syntax_error("# coding: %s\nu'\xe9\\xq'" % name)
            self.assertTrue(error.msg.endswith('position 10-11: truncated \\xXX escape'), error.msg)
        error = self.syntax_error("# coding: latin1\nur'\xe9\\u12x'")
        self.assertTrue(error.msg.endswith('position 10-13: truncated \\uXXXX'), error.msg)
        value = eval('# coding: latin1\n(u"\xe9", ur"\\\xe9", u"\\\xe9", ur"\\\\\xe9", ur"\xe9\\u00e9")')
        self.assertEqual(value, (u'\xe9', u'\\\xe9', u'\\\xe9', u'\\\\\xe9', u'\xe9\xe9'))

    def test_ascii_source_decoder_precedes_unicode_cookie_error(self):
        for source, byte in [('# coding: ascii\nx="\xe9"', 'e9'),
                             (u'# coding: ascii\nx="\xe9"', 'c3')]:
            for newline in ['\n', '\r', '\r\n']:
                error = self.syntax_error(source.replace('\n', newline), 'exec')
                self.assertEqual(error.msg, "'ascii' codec can't decode byte 0x" + byte + ' in position 19: ordinal not in range(128)')
                self.assertEqual((error.lineno, error.offset, error.text), (0, 0, None))

    def test_parser_text_restores_declared_encoding(self):
        for source, text in [('# coding: latin-1\nx=u"\xe9"', 'x=u"?"'),
                             (u'# coding: latin-1\nx=u"\xe9"', 'x=u"\xe9"'),
                             ('x="\xe9"', 'x="\xe9"')]:
            error = self.syntax_error(source)
            self.assertEqual(error.text, text)
            self.assertEqual(error.offset, 2)

    def test_restored_text_joins_surrogate_pairs(self):
        # The UTF-8 encoder of the restoration joins a surrogate pair; the
        # Latin-1 one replaces each surrogate.
        pair = '\xed\xa0\xbd\xed\xb8\x80'
        for source, location in [('# coding: utf-8\nx = "%s" $\n' % pair, (2, 12, 'x = "\xf0\x9f\x98\x80" $\n')),
                                 ('\xef\xbb\xbfx = "%s" $\n' % pair, (1, 12, 'x = "\xf0\x9f\x98\x80" $\n')),
                                 ('# coding: utf8\nx = (1,\n#%s' % pair, (3, 6, '#\xf0\x9f\x98\x80\n')),
                                 ('# coding: utf-8\nx = "\xed\xa0\xbd\xed\xb8" $\n', (2, 14, 'x = "\xed\xa0\xbd\xef\xbf\xbd" $\n')),
                                 ('# coding: latin-1\nx = "%s" $\n' % pair, (2, 10, 'x = "??" $\n'))]:
            error = self.syntax_error(source, 'exec')
            self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('invalid syntax',) + location)

    def test_failed_adjacent_literal_keeps_its_error(self):
        # A str part after a unicode one that fails the ASCII check fails the
        # literal; a later decoding error does not replace that error.
        message = "(unicode error) 'ascii' codec can't decode byte 0xc2 in position 0: ordinal not in range(128)"
        for source, mode in [("'\xc2' u''", 'eval'), ("x = ('\xc2' u'', '\xc3' u'')\n", 'exec'),
                             ("x = '\xc2' u''\nx = u'\\U00110000'\n", 'exec'), ("'\xc2' u'' + u'\\x'", 'eval')]:
            error = self.syntax_error(source, mode)
            self.assertEqual((error.msg, error.lineno), (message, 1))

    def test_raw_unicode_escape_error_starts_at_backslash_run(self):
        for source, span in [(r"ur'\\\U00110000'", '0-11'), (r"ur'ab\\\U00110000'", '2-13'),
                             (r"ur'\\\u12x'", '0-5'), (r"ur'ab\\\\\u12x'", '2-9'), (r"ur'\u12x'", '0-3')]:
            error = self.syntax_error(source)
            self.assertTrue(error.msg.startswith("(unicode error) 'rawunicodeescape' codec can't decode bytes in position " + span + ':'), error.msg)

    def test_late_future_import_after_statement_on_its_line(self):
        # The future scanner reports it like PyErr_SyntaxLocation, the
        # compiler of a later line with the location tuple.
        message = 'from __future__ imports must occur at the beginning of the file'
        error = self.syntax_error('x = 1; from __future__ import division\n', 'exec')
        self.assertEqual((error.args, error.lineno), ((message,), 1))
        error = self.syntax_error('x = 1\nfrom __future__ import division\n', 'exec')
        self.assertEqual(error.args, (message, ('<diagnostic>', 2, None, None)))

    def test_syntax_warning_is_shown_like_showwarning(self):
        import sys

        class Refused(UnicodeError):
            pass

        class Stream(object):
            def __init__(self, error):
                self.error = error
                self.data = []

            def write(self, text):
                if self.error is not None:
                    raise self.error('refused')
                self.data.append(text)

        source = 'def f():\n    from os import *\n'
        saved = sys.stderr
        try:
            stream = Stream(None)
            sys.stderr = stream
            compile(source, '<diagnostic>', 'exec')
            self.assertEqual(stream.data, ['<diagnostic>:1: SyntaxWarning: import * only allowed at module level\n'])
            for error in [IOError, Refused]:
                sys.stderr = Stream(error)
                compile(source, '<diagnostic>', 'exec')
            sys.stderr = Stream(ValueError)
            self.assertRaises(ValueError, compile, source, '<diagnostic>', 'exec')
            sys.stderr = None
            compile(source, '<diagnostic>', 'exec')
            del sys.stderr
            self.assertRaises(AttributeError, compile, source, '<diagnostic>', 'exec')
        finally:
            sys.stderr = saved

    def import_error(self, name):
        try:
            __import__('compiler_files.' + name)
        except SyntaxError as error:
            return error
        self.fail('expected SyntaxError')

    def test_semantic_error_text_reads_the_file_again(self):
        # PyErr_ProgramText reads the line with universal newlines through a
        # 1000-byte buffer, which keeps the last part of a longer line.
        for name in ['crlf', 'cr']:
            error = self.import_error(name)
            self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('cannot assign to None', 3, None, 'None = 3\n'))
        error = self.import_error('long_line')
        self.assertEqual((error.msg, error.lineno, len(error.text), error.text[:8]), ("can't assign to operator", 2, 612, '1 + 1 + '))

    def test_codec_cookie_file_reads_like_stream_reader(self):
        # The UTF-8 codec accepts surrogates; the read that raises fails the
        # line the tokenizer reads then, and the end of the file adds no line.
        error = self.import_error('codec_error')
        self.assertEqual((error.msg, error.lineno, error.offset, error.text),
                         ("'utf8' codec can't decode byte 0xe9 in position 0: invalid continuation byte", 3, 0, None))
        error = self.import_error('codec_string')
        self.assertEqual((error.msg, error.lineno, error.offset, error.text),
                         ('EOF while scanning triple-quoted string literal', 3, 11, 'v0 = """aa\n'))
        # A lone carriage return stays in its line, a surrogate pair is joined
        # and a sequence the end of the file cuts is dropped.
        module = __import__('compiler_files.codec_lines', {}, {}, ['x'])
        self.assertEqual((module.x, module.z, hasattr(module, 'y')), (u'\U0001f600', 1, False))

    def test_end_of_file_offset_restores_the_stale_buffer(self):
        # At the end of a file the tokenizer buffer holds a terminator over
        # the first byte of the last line, which still restores the offset.
        for name, offset in [('end_latin1_codec', 5), ('end_latin1', 6), ('end_utf8_invalid', 14)]:
            error = self.import_error(name)
            self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('invalid syntax', 4, offset, ''))

    def test_final_lone_cr_ends_the_file_at_once(self):
        # Reading ahead after the CR finds the end of the file, so an open
        # token gets no further newline.
        error = self.import_error('final_cr_string')
        self.assertEqual((error.msg, error.lineno, error.offset, error.text),
                         ('EOF while scanning triple-quoted string literal', 3, 11, 's = """a\nb\n'))

    def test_byte_escape_message(self):
        for contents in ['\\x', '\\x0', '\\xg0', 'a\\x0q']:
            try:
                compile("'" + contents + "'", '<diagnostic>', 'eval')
            except ValueError as error:
                self.assertEqual(error.args, ('invalid \\x escape',))
            else:
                self.fail('expected byte escape failure')

    def test_adjacent_unicode_and_byte_literals_decode_failure(self):
        for source in ["'\\xff' u''", "u'' '\\xff'"]:
            error = self.syntax_error(source)
            self.assertEqual(error.msg, "(unicode error) 'ascii' codec can't decode byte 0xff in position 0: ordinal not in range(128)")
            self.assertEqual((error.lineno, error.offset, error.text), (1, None, None))

    def test_tabs_and_spaces_are_not_checked_by_default(self):
        namespace = {}
        exec compile('if 1:\n\tx = 1\n        y = 2\n', '<diagnostic>', 'exec') in namespace
        self.assertEqual((namespace['x'], namespace['y']), (1, 2))
        error = self.syntax_error('if 1:\n    x = 1\n\ty = 2\n', 'exec')
        self.assertEqual(type(error).__name__, 'IndentationError')
        self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('unexpected indent', 3, 1, '\ty = 2\n'))

    def test_tab_size_comment_reads_the_size_like_atoi(self):
        body = 'if 1:\n        a = 1\n\t    b = 2\n'
        for comment in ['# tab-width: 4', '# tab-width:\t4', '# set tabsize= 4', '# vim: :ts= 4', '# tab-width: +4', '# tab-width: 04x']:
            namespace = {}
            exec compile(comment + '\n' + body, '<diagnostic>', 'exec') in namespace
            self.assertEqual((namespace['a'], namespace['b']), (1, 2))
        for comment in ['# tab-width: -4', '# tab-width: 41', '# tab-width: x4']:
            error = self.syntax_error(comment + '\n' + body, 'exec')
            self.assertEqual((type(error).__name__, error.msg, error.lineno), ('IndentationError', 'unexpected indent', 4))

    def test_expected_indented_block_precedes_unexpected_unindent(self):
        for source, lineno, offset, text in [('if 1:\n    if 2:\nx = 1\n', 3, 0, 'x = 1\n'),
                                             ('if 1:\n    if 2:\n', 2, 10, '    if 2:\n')]:
            error = self.syntax_error(source, 'exec')
            self.assertEqual(type(error).__name__, 'IndentationError')
            self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('expected an indented block', lineno, offset, text))

    def test_multiline_token_error_quotes_logical_line(self):
        error = self.syntax_error('x = """a\nb"""; y = = 1\n', 'exec')
        self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('invalid syntax', 2, 20, 'x = """a\nb"""; y = = 1\n'))
        error = self.syntax_error("x = 1\ny = '''abc\ndef\n", 'exec')
        self.assertEqual((error.msg, error.lineno, error.offset, error.text),
                         ('EOF while scanning triple-quoted string literal', 3, 15, "y = '''abc\ndef\n"))

    def test_final_crlf_adds_a_line_to_exec_strings(self):
        error = self.syntax_error('x = (\r\n', 'exec')
        self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('invalid syntax', 2, 1, '\n'))
        error = self.syntax_error('x = (\r\n', 'single')
        self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('unexpected EOF while parsing', 1, 6, 'x = (\n'))

    def test_cookie_spellings(self):
        for name in ['latin', 'l1', 'cp819', 'iso_8859_1_1987', '646', 'ANSI_X3.4-1968', 'u8', 'utf8_ucs2',
                     'latin-1-extra', 'iso-latin-1-x', 'UTF_8-sig']:
            compile('# coding: %s\nx = 1\n' % name, '<diagnostic>', 'exec')
        for name in ['u-t-f-8', 'la-tin-1', 'a-s-c-i-i']:
            error = self.syntax_error('# coding: %s\nx = 1\n' % name, 'exec')
            self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('unknown encoding: ' + name, 0, 0, None))

    def test_codec_path_cookie_decodes_the_whole_string(self):
        error = self.syntax_error('# coding: utf8\nx = "\xe9"\n', 'exec')
        self.assertEqual((error.msg, error.lineno, error.offset, error.text),
                         ("'utf8' codec can't decode byte 0xe9 in position 20: invalid continuation byte", 0, 0, None))
        compile('# coding: utf-8\nx = "\xe9"\n', '<diagnostic>', 'exec')
        error = self.syntax_error('# coding: latin1\nf(\xe9)\n', 'exec')
        self.assertEqual((error.offset, error.text), (3, 'f(\xe9)\n'))
        error = self.syntax_error('# coding: latin-1\nf(\xe9)\n', 'exec')
        self.assertEqual((error.offset, error.text), (3, 'f(?)\n'))
        error = self.syntax_error(u'# coding: latin1\nmain(\xff)\n', 'exec')
        self.assertEqual((error.offset, error.text), (6, 'main(\xc3\xbf)\n'))

    def test_codec_path_cookie_joins_surrogate_pairs(self):
        # The string decoded through the codec is encoded again, which joins
        # a surrogate pair; the fast path keeps the bytes. The expected text
        # decodes at run time, as marshal joins a pair in a constant.
        pair = '\xed\xa0\xbd\xed\xb8\x80'
        for cookie, text in [('utf8', '\xf0\x9f\x98\x80'), ('u8', '\xf0\x9f\x98\x80'), ('utf-8', pair)]:
            namespace = {}
            exec compile('# coding: %s\nx = "%s"\ny = u"%s"\n' % (cookie, pair, pair), '<diagnostic>', 'exec') in namespace
            self.assertEqual((namespace['x'], namespace['y']), (text, text.decode('utf-8')))
        self.assertEqual(eval('# coding: utf8\nu"%s"' % pair), u'\U0001f600')

    def test_bom_with_cookie(self):
        for source, name in [('\xef\xbb\xbf# coding: latin-1\nx=1\n', 'iso-8859-1'),
                             ('\xef\xbb\xbf\n# coding: latin1\nx=1\n', 'latin1'),
                             ('\xef\xbb\xbf# coding: utf8\nx=1\n', 'utf8'),
                             (u'\ufeff# coding: latin-1\nx = 1\n', 'iso-8859-1')]:
            error = self.syntax_error(source, 'exec')
            self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('encoding problem: %s with BOM' % name, 0, 0, None))
        compile('\xef\xbb\xbf# coding: UTF-8\nx = 1\n', '<diagnostic>', 'exec')

    def test_expression_string_cookie_needs_a_newline(self):
        # decode_str reads a cookie only on a line a newline ends; exec strings
        # get one at their end, expression and interactive strings do not.
        for mode in ['eval', 'single']:
            for source, location in [('# coding: bogus', ('unexpected EOF while parsing', 1, 15)),
                                     ('#!x\n# coding: bogus', ('invalid syntax', 2, 15)),
                                     ('#!x\r# coding: bogus', ('invalid syntax', 2, 15)),
                                     ('\xef\xbb\xbf# coding: latin-1', ('unexpected EOF while parsing', 1, 17)),
                                     (u'#\n# coding: bogus', ('invalid syntax', 2, 15))]:
                error = self.syntax_error(source, mode)
                self.assertEqual((error.msg, error.lineno, error.offset), location)
            for source in ['# coding: bogus\n', '# coding: bogus\r', '#!x\n# coding: bogus\n']:
                error = self.syntax_error(source, mode)
                self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('unknown encoding: bogus', 0, 0, None))
        for source in ['# coding: bogus', '#!x\r# coding: bogus', u'# coding: bogus']:
            error = self.syntax_error(source, 'exec')
            self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('unknown encoding: bogus', 0, 0, None))

    def test_unicode_source_bom_is_an_encoding_declaration(self):
        for source, mode in [(u'\ufeffx = 1\n', 'exec'), (u'\ufeff1', 'eval')]:
            error = self.syntax_error(source, mode)
            self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('encoding declaration in Unicode string', 0, None, None))

    def test_eol_offsets_count_host_bytes(self):
        for source, offset in [('x = "\xe9\n', 6), ('# coding: latin-1\nx = "\xe9\n', 6),
                               ('# coding: utf-8\nx = "\xc3\xa9\n', 9), (u'# coding: utf-8\nx = "\xe9\n', 9)]:
            error = self.syntax_error(source, 'exec')
            self.assertEqual((error.msg, error.offset), ('EOL while scanning string literal', offset))

    def test_trailing_nonascii_byte_in_expression_is_eof(self):
        for mode in ['eval', 'single']:
            error = self.syntax_error('\xe9', mode)
            self.assertEqual((error.msg, error.lineno, error.offset, error.text), ('unexpected EOF while parsing', 1, 1, '\xe9'))
        error = self.syntax_error('\xe9', 'exec')
        self.assertEqual((error.msg, error.text), ('invalid syntax', '\xe9\n'))

    def test_nested_blocks_error_has_no_location(self):
        source = ''.join(' ' * depth + 'for i in x:\n' for depth in range(21)) + ' ' * 21 + 'pass\n'
        error = self.syntax_error(source, 'exec')
        self.assertEqual((error.msg, error.filename, error.lineno, error.offset, error.text), ('too many statically nested blocks', None, None, None, None))
        self.assertEqual((error.args, str(error)), (('too many statically nested blocks',), 'too many statically nested blocks'))

    def test_delete_of_nested_scope_variable_has_no_location(self):
        error = self.syntax_error('def f():\n x = 1\n def a(): return x\n del x\n', 'exec')
        message = "can not delete variable 'x' referenced in nested scope"
        self.assertEqual((error.msg, error.filename, error.lineno, error.offset, error.text), (message, None, None, None, None))
        self.assertEqual((error.args, str(error)), ((message,), message))


if __name__ == '__main__':
    unittest.main()
