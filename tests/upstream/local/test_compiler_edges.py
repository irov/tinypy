"""Project-authored Python 2 compiler boundary and callback regressions."""

import unittest


class CompilerEdges(unittest.TestCase):
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

    def test_unknown_encoding_has_decoder_diagnostic(self):
        try:
            compile('# coding: made_up\nx=1', 'file', 'exec')
        except SyntaxError as error:
            self.assertEqual(error.msg, 'unknown encoding: made_up')
            self.assertEqual((error.filename, error.lineno, error.offset, error.text),
                             ('file', 0, 0, None))
        else:
            self.fail('expected unknown encoding failure')
