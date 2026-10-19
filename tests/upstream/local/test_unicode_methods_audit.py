"""Project-authored Unicode translation and codec callback regressions."""

import _codecs
import sys
import unittest


def _message(operation, *args):
    try:
        operation(*args)
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError('operation must raise')


class Wide(unicode):
    pass


class UnicodeMethodsAudit(unittest.TestCase):
    def test_hex_codec_requires_strict_errors_before_source_conversion(self):
        for transform in (_codecs.encode, _codecs.decode):
            for source in ('ab', u'ab', u'\xff', object()):
                for errors in ('ignore', 'replace', 'unknown'):
                    try:
                        transform(source, 'hex', errors)
                    except AssertionError as error:
                        self.assertEqual(error.args, ())
                    else:
                        self.fail('hex must require strict errors')

    def test_translate_rechecks_first_character_after_deleted_run(self):
        for source in (u'aab', Wide(u'aab')):
            events = []
            class Table(object):
                def __getitem__(self, key):
                    events.append(key)
                    return None if key == 97 else u'X'
            result = source.translate(Table())
            self.assertEqual(result, u'X')
            self.assertIs(type(result), unicode)
            self.assertEqual(events, [97, 97, 98, 98])

    def test_translate_uses_second_lookup_result(self):
        events = []
        class Table(object):
            def __getitem__(self, key):
                events.append(key)
                if key == 97:
                    return None
                return u'X' if events.count(98) == 1 else u'Y'
        self.assertEqual(u'abab'.translate(Table()), u'YY')
        self.assertEqual(events, [97, 98, 98, 97, 98, 98])

    def test_translate_second_lookup_can_start_another_deleted_run(self):
        events = []
        class Table(object):
            def __getitem__(self, key):
                events.append(key)
                if key == 97 or (key == 98 and events.count(98) == 2):
                    return None
                return unichr(key).upper()
        self.assertEqual(u'abc'.translate(Table()), u'C')
        self.assertEqual(events, [97, 98, 98, 99, 99])

    def test_translate_releases_discarded_lookahead_before_retry(self):
        events = []
        class Replacement(unicode):
            def __del__(self):
                events.append('released')
        class Table(object):
            def __getitem__(self, key):
                events.append(key)
                if key == 97:
                    return None
                return Replacement(u'X') if events.count(98) == 1 else u'Y'
        self.assertEqual(u'ab'.translate(Table()), u'Y')
        self.assertEqual(events, [97, 98, 'released', 98])

    def test_translate_validates_discarded_lookahead(self):
        for invalid, message in ((1L, 'character mapping must return integer, None or unicode'),
                                 (-1, 'character mapping must be in range(0x%lx)')):
            events = []
            class Table(object):
                def __getitem__(self, key):
                    events.append(key)
                    return None if key == 97 else invalid
            self.assertEqual(_message(u'abc'.translate, Table()), ('TypeError', message))
            self.assertEqual(events, [97, 98])

    def test_translate_second_lookup_preserves_error_identity_and_outer_state(self):
        marker = ValueError('second lookup')
        outer = RuntimeError('outer')
        events = []
        class Table(object):
            def __getitem__(self, key):
                events.append(key)
                if key == 97:
                    return None
                if events.count(98) == 1:
                    return u'X'
                raise marker
        def translate_error():
            try:
                u'ab'.translate(Table())
            except ValueError as error:
                return error
            self.fail('second lookup must fail')
        try:
            raise outer
        except RuntimeError:
            self.assertIs(translate_error(), marker)
            self.assertIs(sys.exc_info()[1], outer)
        self.assertEqual(events, [97, 98, 98])
        self.assertEqual(u'ab'.translate({97: None, 98: u'Z'}), u'Z')

    def test_translate_missing_lookahead_is_looked_up_again(self):
        for missing in (KeyError, IndexError, LookupError):
            events = []
            class Table(object):
                def __getitem__(self, key):
                    events.append(key)
                    if key == 97:
                        return None
                    raise missing('absent')
            self.assertEqual(u'ab'.translate(Table()), u'b')
            self.assertEqual(events, [97, 98, 98])

    def test_translate_lookahead_observes_mapping_mutation(self):
        events = []
        state = {98: u'first'}
        class Table:
            def __getitem__(self, key):
                events.append(key)
                if key == 97:
                    return None
                result = state[key]
                state[key] = u'second'
                return result
        self.assertEqual(u'ab'.translate(Table()), u'second')
        self.assertEqual(events, [97, 98, 98])

    def test_ignore_handler_only_uses_clamped_end(self):
        class Encode(UnicodeEncodeError):
            pass
        class Decode(UnicodeDecodeError):
            pass
        class Translate(UnicodeTranslateError):
            pass
        for factory in (UnicodeEncodeError, Encode, UnicodeDecodeError, Decode,
                        UnicodeTranslateError, Translate):
            for start, end in ((2, 1), (3, 1), (2, 0), (2, 9)):
                error = (factory(u'abc', start, end, 'bad') if issubclass(factory, UnicodeTranslateError)
                         else factory('ascii', 'abc' if issubclass(factory, UnicodeDecodeError)
                                      else u'abc', start, end, 'bad'))
                self.assertEqual(_codecs.lookup_error('ignore')(error),
                                 (u'', min(3, max(1, end))))

    def test_decode_replace_handler_only_uses_clamped_end(self):
        for start, end in ((2, 1), (3, 1), (2, 0), (2, 9)):
            error = UnicodeDecodeError('ascii', 'abc', start, end, 'bad')
            self.assertEqual(_codecs.lookup_error('replace')(error),
                             (u'\ufffd', min(3, max(1, end))))

    def test_wrong_handler_type_reads_reported_class_and_name(self):
        for mode in ('ignore', 'replace', 'xmlcharrefreplace', 'backslashreplace'):
            events = []
            class Name(object):
                def __str__(self):
                    events.append('str')
                    return 'Reported'
            class Label(object):
                @property
                def __name__(self):
                    events.append('name')
                    return Name()
            label = Label()
            class Source(object):
                @property
                def __class__(self):
                    events.append('class')
                    return label
            self.assertEqual(_message(_codecs.lookup_error(mode), Source()),
                             ('TypeError', "don't know how to handle Reported in error callback"))
            self.assertEqual(events, ['class', 'name', 'str'])

    def test_wrong_handler_type_callback_failure_identity_and_recovery(self):
        for stage in ('class', 'name', 'str'):
            marker = ValueError(stage)
            events = []
            class Name(object):
                def __str__(self):
                    events.append('str')
                    if stage == 'str':
                        raise marker
                    return 'Reported'
            class Label(object):
                @property
                def __name__(self):
                    events.append('name')
                    if stage == 'name':
                        raise marker
                    return Name()
            label = Label()
            class Source(object):
                @property
                def __class__(self):
                    events.append('class')
                    if stage == 'class':
                        raise marker
                    return label
            try:
                _codecs.lookup_error('ignore')(Source())
            except ValueError as error:
                self.assertIs(error, marker)
            else:
                self.fail('callback must fail')
            self.assertEqual(events, ['class', 'name', 'str'][:('class', 'name', 'str').index(stage) + 1])
            self.assertEqual(_codecs.lookup_error('ignore')(UnicodeEncodeError('ascii', u'a', 0, 1, 'bad')),
                             (u'', 1))

    def test_wrong_handler_type_c_string_name_limit(self):
        for name in ('A\0B', 'A' * 401):
            class Label(object):
                __name__ = name
            label = Label()
            class Source(object):
                @property
                def __class__(self):
                    return label
            expected = name.split('\0')[0][:400]
            self.assertEqual(_message(_codecs.lookup_error('ignore'), Source()),
                             ('TypeError', "don't know how to handle %s in error callback" % expected))

    def test_supported_error_handler_ignores_reported_class_override(self):
        class Decode(UnicodeDecodeError):
            @property
            def __class__(self):
                raise AssertionError('supported type must use native classification')
        error = Decode('ascii', 'abc', 2, 1, 'bad')
        self.assertEqual(_codecs.lookup_error('ignore')(error), (u'', 1))
        self.assertEqual(_codecs.lookup_error('replace')(error), (u'\ufffd', 1))

    def test_builtin_codecs_convert_runs_around_errors(self):
        replacement = unichr(0xfffd)
        data = 'x' * 21 + '\xff' + 'y' * 9 + '\xd0\xbf' + 'z' * 17 + '\xe2\x82'
        tail = u'y' * 9 + unichr(0x43f) + u'z' * 17
        self.assertEqual(data.decode('utf-8', 'replace'), u'x' * 21 + replacement + tail + replacement)
        self.assertEqual(data.decode('utf-8', 'ignore'), u'x' * 21 + tail)
        self.assertEqual(_message(data.decode, 'utf-8'),
                         ('UnicodeDecodeError', "'utf8' codec can't decode byte 0xff in position 21: invalid start byte"))
        self.assertEqual(_codecs.utf_8_decode(data[22:], 'strict', False), (tail, 28))
        self.assertEqual(_codecs.utf_8_decode(data, 'replace', False), (u'x' * 21 + replacement + tail, 50))
        self.assertEqual(data.decode('ascii', 'replace'), u'x' * 21 + replacement + u'y' * 9 + replacement * 2 + u'z' * 17 + replacement * 2)
        self.assertEqual(data.decode('ascii', 'ignore'), u'x' * 21 + u'y' * 9 + u'z' * 17)
        latin1 = data.decode('latin-1')
        self.assertEqual((len(latin1), latin1[21], latin1[-1], latin1.encode('latin-1')), (len(data), unichr(0xff), unichr(0x82), data))
        self.assertEqual(_message(('a' * 20 + '\xe9').encode, 'utf-8'),
                         ('UnicodeDecodeError', "'ascii' codec can't decode byte 0xe9 in position 20: ordinal not in range(128)"))
        self.assertEqual(_message((u'a' * 20 + unichr(0xe9) * 2 + u'b').decode, 'utf-8'),
                         ('UnicodeEncodeError', "'ascii' codec can't encode characters in position 20-21: ordinal not in range(128)"))

    def test_builtin_codecs_resume_where_handlers_point(self):
        def middle(error):
            return (u'<%d:%d>' % (error.start, error.end), error.end + 1)
        _codecs.register_error('unicode_methods_audit_middle', middle)
        data = 'a' * 10 + '\xff' + '\xd0\xbf' * 3 + 'b' * 10
        self.assertEqual(data.decode('utf-8', 'unicode_methods_audit_middle'), u'a' * 10 + u'<10:11><12:13><14:15><16:17>' + u'b' * 9)
        data = 'a' * 10 + '\xff' + 'b' + '\xd0\xbf' * 3 + 'c' * 10
        self.assertEqual(data.decode('utf-8', 'unicode_methods_audit_middle'), u'a' * 10 + u'<10:11>' + unichr(0x43f) * 3 + u'c' * 10)
        text = u'a' * 10 + unichr(0x20ac) + unichr(0xe9) * 3 + u'b' * 10
        self.assertEqual(text.encode('latin-1', 'unicode_methods_audit_middle'), 'a' * 10 + '<10:11>' + '\xe9' * 2 + 'b' * 10)
        self.assertEqual(text.encode('ascii', 'unicode_methods_audit_middle'), 'a' * 10 + '<10:14>' + 'b' * 9)
        self.assertEqual(text.encode('latin-1', 'xmlcharrefreplace'), 'a' * 10 + '&#8364;' + '\xe9' * 3 + 'b' * 10)
        self.assertEqual(text.encode('ascii', 'backslashreplace'), 'a' * 10 + '\\u20ac' + '\\xe9' * 3 + 'b' * 10)
        self.assertEqual(text.encode('ascii', 'replace'), 'a' * 10 + '?' * 4 + 'b' * 10)
        error = None
        try:
            text.encode('latin-1')
        except UnicodeEncodeError as caught:
            error = caught
        self.assertEqual((error.start, error.end, error.reason), (10, 11, 'ordinal not in range(256)'))

    def test_utf8_encoder_joins_pairs_between_runs(self):
        high = unichr(0xd83d)
        low = unichr(0xde00)
        text = u'a' * 20 + high + low + unichr(0xe9) + high + u'b' + low + u'c' * 9 + high
        self.assertEqual(text.encode('utf-8'),
                         'a' * 20 + '\xf0\x9f\x98\x80' + '\xc3\xa9' + '\xed\xa0\xbd' + 'b' + '\xed\xb8\x80' + 'c' * 9 + '\xed\xa0\xbd')
        self.assertEqual(Wide(text).encode('utf-8'), text.encode('utf-8'))
        decoded = ('\xd0\xbf' * 40 + '\xed\xa0\xbd\xed\xb8\x80').decode('utf-8')
        self.assertEqual((type(decoded), len(decoded), decoded[39], decoded[40], decoded[-1]), (unicode, 42, unichr(0x43f), high, low))
        self.assertEqual((type(Wide(u'abc').encode('utf-8')), type(bytearray('abc').decode('latin-1'))), (str, unicode))
