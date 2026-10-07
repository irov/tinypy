"""Project-authored Python 2 decoder phase and diagnostic regressions."""

import unittest


class CompilerDiagnostics(unittest.TestCase):
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


if __name__ == '__main__':
    unittest.main()
