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
