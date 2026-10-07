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
