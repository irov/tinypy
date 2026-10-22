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
    def test_decoders_read_live_mutable_input_after_error_handler(self):
        events = []
        def handler(error):
            events.append((error.object, error.start, error.end))
            owner[1] = ord('b')
            return u'Z', error.end
        _codecs.register_error('unicode_methods_live_input', handler)
        operations = (
            lambda: owner.decode('ascii', 'unicode_methods_live_input'),
            lambda: owner.decode('utf8', 'unicode_methods_live_input'),
            lambda: _codecs.ascii_decode(owner, 'unicode_methods_live_input')[0],
            lambda: _codecs.utf_8_decode(owner, 'unicode_methods_live_input', True)[0],
            lambda: _codecs.ascii_decode(buffer(owner), 'unicode_methods_live_input')[0],
            lambda: _codecs.ascii_decode(memoryview(owner), 'unicode_methods_live_input')[0],
            lambda: unicode(buffer(owner), 'ascii', 'unicode_methods_live_input'))
        for operation in operations:
            owner = bytearray('\xffa')
            events[:] = []
            self.assertEqual(operation(), u'Zb')
            self.assertEqual(owner, bytearray('\xffb'))
            self.assertEqual(events, [('\xffa', 0, 1)])

    def test_decoder_exception_retains_snapshot_when_mutation_adds_an_error(self):
        events = []
        def handler(error):
            events.append((error.object, error.start, error.end))
            owner[1] = 255
            return u'Z', error.end
        _codecs.register_error('unicode_methods_live_error', handler)
        for decode in (_codecs.ascii_decode, _codecs.utf_8_decode):
            owner = bytearray('\xffa')
            events[:] = []
            self.assertEqual(decode(owner, 'unicode_methods_live_error'), (u'ZZ', 2))
            self.assertEqual(events, [('\xffa', 0, 1), ('\xffa', 1, 2)])

    def test_utf8_decoder_rechecks_mutated_sequence_and_reason(self):
        events = []
        def handler(error):
            events.append((error.object, error.start, error.end, error.reason))
            if len(events) == 1:
                owner[1] = 240
            return u'Z', error.end
        _codecs.register_error('unicode_methods_live_utf8', handler)
        for final, expected in ((False, (u'Z', 1)), (True, (u'ZZ', 4))):
            owner = bytearray('\xff\xe2\x82\xac')
            events[:] = []
            self.assertEqual(_codecs.utf_8_decode(owner, 'unicode_methods_live_utf8', final), expected)
            trace = [('\xff\xe2\x82\xac', 0, 1, 'invalid start byte')]
            if final:
                trace.append(('\xff\xe2\x82\xac', 1, 4, 'unexpected end of data'))
            self.assertEqual(events, trace)

    def test_decoder_position_callback_can_mutate_unread_bytes(self):
        events = []
        class Position(object):
            def __int__(self):
                events.append('position')
                owner[1] = ord('b')
                return 1
        def handler(error):
            events.append('handler')
            return u'Z', Position()
        _codecs.register_error('unicode_methods_live_position', handler)
        for decode in (_codecs.ascii_decode, _codecs.utf_8_decode):
            owner = bytearray('\xffa')
            events[:] = []
            self.assertEqual(decode(owner, 'unicode_methods_live_position'), (u'Zb', 2))
            self.assertEqual(events, ['handler', 'position'])

    def test_mutable_decoder_keeps_nested_buffer_and_view_bounds(self):
        events = []
        def handler(error):
            events.append(error.object)
            owner[2] = ord('b')
            return u'Z', error.end
        _codecs.register_error('unicode_methods_live_bounds', handler)
        sources = (lambda: buffer(owner, 1, 2),
                   lambda: buffer(buffer(owner, 1), 0, 2),
                   lambda: memoryview(owner)[1:3])
        for source in sources:
            owner = bytearray('p\xffaq')
            events[:] = []
            self.assertEqual(_codecs.ascii_decode(source(), 'unicode_methods_live_bounds'), (u'Zb', 2))
            self.assertEqual(events, ['\xffa'])
        owner = bytearray('p\xffaq')
        self.assertEqual(unicode(buffer(owner, 1, 2), 'ascii', 'unicode_methods_live_bounds'), u'Zb')

    def test_decoder_holds_array_export_and_releases_it_after_failure(self):
        events = []
        def handler(error):
            try:
                owner.append(98)
            except BufferError:
                events.append('exported')
            else:
                raise AssertionError('decode must retain its export')
            if fail:
                raise marker
            return u'Z', error.end
        marker = ValueError('handler')
        _codecs.register_error('unicode_methods_live_export', handler)
        operations = (lambda: owner.decode('ascii', 'unicode_methods_live_export'),
                      lambda: _codecs.ascii_decode(owner, 'unicode_methods_live_export')[0])
        for fail in (False, True):
            for operation in operations:
                owner = bytearray('\xffa')
                events[:] = []
                if fail:
                    with self.assertRaises(ValueError) as caught:
                        operation()
                    self.assertIs(caught.exception, marker)
                else:
                    self.assertEqual(operation(), u'Za')
                self.assertEqual(events, ['exported'])
                owner.append(98)
                self.assertEqual(owner, bytearray('\xffab'))

    def test_search_methods_use_default_codec_replacement(self):
        previous = _codecs.lookup_error('strict')
        def handler(error):
            return u'\u1234Z', error.end
        try:
            _codecs.register_error('strict', handler)
            for text, needle in ((u'a\u1234Zb', '\xff'), ('a\xffb', u'\u1234Z')):
                for name in ('find', 'rfind', 'index', 'rindex'):
                    self.assertEqual(getattr(text, name)(needle), 1)
                self.assertEqual(text.count(needle), 1)
                self.assertTrue(text.startswith(needle, 1))
                self.assertTrue(text.endswith(needle, 0, -1))
                self.assertTrue(text.startswith((u'x', needle), 1))
                self.assertTrue(text.endswith((u'x', needle), 0, -1))
        finally:
            _codecs.register_error('strict', previous)

    def test_search_bounds_are_parsed_once_before_codec_and_adjusted_after(self):
        events = []
        previous = _codecs.lookup_error('strict')
        def handler(error):
            events.append('codec')
            return u'\u1234Z', error.end
        class Start(object):
            def __index__(self):
                events.append('start')
                return -3
        class End(object):
            def __index__(self):
                events.append('end')
                return -1
        try:
            _codecs.register_error('strict', handler)
            for text, needle in ((u'a\u1234Zb', '\xff'), ('a\xffb', u'\u1234Z')):
                for name, expected in (('find', 1), ('count', 1),
                                       ('startswith', True), ('endswith', True)):
                    events[:] = []
                    self.assertEqual(getattr(text, name)(needle, Start(), End()), expected)
                    self.assertEqual(events, ['start', 'end', 'codec'])
        finally:
            _codecs.register_error('strict', previous)

    def test_text_transformations_use_default_codec_replacement(self):
        previous = _codecs.lookup_error('strict')
        def handler(error):
            return u'\u1234Z', error.end
        operations = (
            (lambda: u'a\u1234Zb'.replace('\xff', 'x'), u'axb'),
            (lambda: 'a\xffb'.replace(u'\u1234Z', 'x'), u'axb'),
            (lambda: u'a\u1234Zb'.split('\xff'), [u'a', u'b']),
            (lambda: 'a\xffb'.rsplit(u'\u1234Z'), [u'a', u'b']),
            (lambda: u'a\u1234Zb'.partition('\xff'), (u'a', u'\u1234Z', u'b')),
            (lambda: 'a\xffb'.rpartition(u'\u1234Z'), (u'a', u'\u1234Z', u'b')),
            (lambda: u'\u1234ZaZ'.strip('\xff'), u'a'),
            (lambda: '\xffa\xff'.strip(u'\u1234Z'), u'a'))
        try:
            _codecs.register_error('strict', handler)
            for operation, expected in operations:
                self.assertEqual(operation(), expected)
        finally:
            _codecs.register_error('strict', previous)

    def test_percent_format_decodes_and_reparses_remaining_format(self):
        events = []
        previous = _codecs.lookup_error('strict')
        def handler(error):
            events.append(error.object)
            return replacement, error.end
        try:
            _codecs.register_error('strict', handler)
            for replacement, expected in ((u'Z', u'aZ'), (u'%%', u'a%'), (u'', u'a')):
                events[:] = []
                self.assertEqual('%s\xff' % u'a', expected)
                self.assertEqual(events, ['%s\xff'])
            replacement = u'%s'
            self.assertEqual('%s\xff' % (u'a', u'b'), u'ab')
            replacement = u'Z'
            events[:] = []
            self.assertEqual('\xff%s\xff' % u'a', u'ZaZ')
            self.assertEqual(events, ['%s\xff', '\xff'])
            self.assertEqual(u'%c' % '\xff', u'Z')
            replacement = u'XY'
            self.assertEqual(u'%c' % '\xff', u'X')
            replacement = u''
            self.assertEqual(u'%c' % '\xff', u'\x00')
        finally:
            _codecs.register_error('strict', previous)

    def test_unicode_format_spec_decodes_default_codec_replacement(self):
        previous = _codecs.lookup_error('strict')
        def handler(error):
            return u'\u1234', error.end
        try:
            _codecs.register_error('strict', handler)
            self.assertEqual(format(u'a', '\xff>3s'), u'\u1234\u1234a')
            self.assertEqual(u'{}'.format('\xff'), u'\u1234')
        finally:
            _codecs.register_error('strict', previous)

    def test_decoders_use_registered_standard_error_handlers(self):
        events = []
        def handler(error):
            events.append((type(error).__name__, error.start, error.end))
            return u'\u1234Z', error.end
        for name in ('strict', 'ignore', 'replace', 'xmlcharrefreplace', 'backslashreplace'):
            previous = _codecs.lookup_error(name)
            try:
                _codecs.register_error(name, handler)
                self.assertIs(_codecs.lookup_error(name), handler)
                for codec in ('ascii', 'utf8'):
                    events[:] = []
                    self.assertEqual('a\xffb\xff'.decode(codec, name),
                                     u'a\u1234Zb\u1234Z')
                    self.assertEqual(events, [('UnicodeDecodeError', 1, 2),
                                              ('UnicodeDecodeError', 3, 4)])
                self.assertEqual(_codecs.ascii_decode('\xff', name), (u'\u1234Z', 1))
                self.assertEqual(_codecs.utf_8_decode('\xff', name, True), (u'\u1234Z', 1))
            finally:
                _codecs.register_error(name, previous)

    def test_encoders_keep_builtin_policies_except_backslashreplace(self):
        events = []
        def handler(error):
            events.append((type(error).__name__, error.start, error.end))
            return u'Z', error.end
        for name, expected in (('strict', None), ('ignore', ''),
                               ('replace', '??'),
                               ('xmlcharrefreplace', '&#4660;&#4661;'),
                               ('backslashreplace', 'Z')):
            previous = _codecs.lookup_error(name)
            try:
                _codecs.register_error(name, handler)
                for codec in ('ascii', 'latin1'):
                    events[:] = []
                    if expected is None:
                        with self.assertRaises(UnicodeEncodeError):
                            u'\u1234\u1235'.encode(codec, name)
                    else:
                        self.assertEqual(u'\u1234\u1235'.encode(codec, name), expected)
                    self.assertEqual(events,
                                     [('UnicodeEncodeError', 0, 2)] if name == 'backslashreplace' else [])
            finally:
                _codecs.register_error(name, previous)

    def test_default_ascii_coercion_uses_registered_strict_replacement(self):
        events = []
        def handler(error):
            events.append((error.encoding, error.start, error.end))
            return u'\u1234Z', error.end
        previous = _codecs.lookup_error('strict')
        operations = (
            (lambda: '\xff'.decode('ascii'), u'\u1234Z'),
            (lambda: unicode('\xff'), u'\u1234Z'),
            (lambda: unicode('\xff', 'ascii'), u'\u1234Z'),
            (lambda: u'a' + '\xff', u'a\u1234Z'),
            (lambda: '\xff' + u'a', u'\u1234Za'),
            (lambda: u''.join(['a', '\xff']), u'a\u1234Z'),
            (lambda: ''.join([u'a', '\xff']), u'a\u1234Z'),
            (lambda: u'\u1234Z' == '\xff', True),
            (lambda: '\xff' == u'\u1234Z', True),
            (lambda: '\xff' < u'\u1235', True),
            (lambda: '\xff' in u'\u1234Z', True),
            (lambda: u'\u1234' in '\xff', True),
            (lambda: u'%s' % '\xff', u'\u1234Z'),
            (lambda: '\xff'.encode('utf8'), '\xe1\x88\xb4Z'))
        try:
            _codecs.register_error('strict', handler)
            for operation, expected in operations:
                events[:] = []
                self.assertEqual(operation(), expected)
                self.assertEqual(events, [('ascii', 0, 1)])
        finally:
            _codecs.register_error('strict', previous)

    def test_decode_error_handler_is_cached_within_one_conversion(self):
        events = []
        previous = _codecs.lookup_error('strict')
        def second(error):
            events.append('second')
            return u'B', error.end
        def first(error):
            events.append('first')
            _codecs.register_error('strict', second)
            return u'A', error.end
        try:
            _codecs.register_error('strict', first)
            self.assertEqual('\xff\xff'.decode('ascii'), u'AA')
            self.assertEqual(events, ['first', 'first'])
            self.assertEqual(unicode('\xff'), u'B')
            self.assertEqual(events, ['first', 'first', 'second'])
        finally:
            _codecs.register_error('strict', previous)

    def test_join_rechecks_list_after_item_codec_callback(self):
        previous = _codecs.lookup_error('strict')
        source = []
        def handler(error):
            source[:] = replacement
            return u'Z', error.end
        try:
            _codecs.register_error('strict', handler)
            for replacement, expected in (([], u'a|Z'),
                                           ([u'x', u'y', u'c'], u'a|Z|c')):
                source[:] = [u'a', '\xff', u'b']
                self.assertEqual(u'|'.join(source), expected)
            # CPython retains its default space if a singleton grows while
            # decoding its only item, before a separator was needed.
            replacement = [u'x', u'y']
            source[:] = ['\xff']
            self.assertEqual(u'|'.join(source), u'Z y')
        finally:
            _codecs.register_error('strict', previous)

    def test_join_rechecks_list_after_separator_codec_callback(self):
        previous = _codecs.lookup_error('strict')
        source = []
        def handler(error):
            source[:] = replacement
            return u'Z', error.end
        try:
            _codecs.register_error('strict', handler)
            for replacement, expected in (([], u''), ([u'x'], u'x'),
                                           ([u'x', u'y', u'z'], u'xZyZz')):
                source[:] = [u'a', u'b']
                self.assertEqual('\xff'.join(source), expected)
        finally:
            _codecs.register_error('strict', previous)

    def test_mixed_unicode_comparison_preserves_callback_failures(self):
        previous = _codecs.lookup_error('strict')
        marker = RuntimeError('codec failed')
        def handler(error):
            raise marker
        try:
            _codecs.register_error('strict', handler)
            for operation in (lambda: '\xff' == u'Z',
                              lambda: u'Z' != '\xff',
                              lambda: '\xff' < u'Z',
                              lambda: '\xff' in u'Z'):
                with self.assertRaises(RuntimeError) as caught:
                    operation()
                self.assertIs(caught.exception, marker)
        finally:
            _codecs.register_error('strict', previous)

    def test_mixed_ordering_declines_a_decoding_type_error(self):
        previous = _codecs.lookup_error('strict')
        calls = []
        class Reflected(str):
            def __gt__(self, other):
                return 'reflected'
        def handler(error):
            calls.append(error.start)
            return None
        text = 'a\xff'
        try:
            _codecs.register_error('strict', handler)
            self.assertTrue(text < u'a')
            self.assertTrue(u'a' >= text)
            self.assertEqual(sorted([u'b', text]), [text, u'b'])
            self.assertEqual(max(text, u'a'), u'a')
            self.assertFalse(text == u'a')
            self.assertIs(u'a'.__lt__(text), NotImplemented)
            self.assertIs(u'a'.__eq__(text), NotImplemented)
            self.assertEqual(u'a' < Reflected(text), 'reflected')
            self.assertTrue(Wide(u'a') < text)
            del calls[:]
            self.assertEqual(cmp(text, u'a'), -1)
            self.assertEqual(cmp(u'a', text), 1)
            self.assertEqual(calls, [1] * 6)
        finally:
            _codecs.register_error('strict', previous)

    def test_prefix_methods_report_a_decoding_type_error_as_argument_type(self):
        previous = _codecs.lookup_error('strict')
        marker = TypeError('handler')
        def handler(error):
            raise marker
        try:
            _codecs.register_error('strict', handler)
            self.assertEqual(_message(u'a'.startswith, 'a\xff'), ('TypeError', 'startswith first arg must be str, unicode, or tuple, not str'))
            self.assertEqual(_message('a\xff'.endswith, u'a'), ('TypeError', 'endswith first arg must be str, unicode, or tuple, not unicode'))
            with self.assertRaises(TypeError) as caught:
                u'a'.startswith((u'x', 'a\xff'))
            self.assertIs(caught.exception, marker)
        finally:
            _codecs.register_error('strict', previous)

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

    def test_builtin_codecs_take_dense_errors_as_runs(self):
        replacement = unichr(0xfffd)
        data = '\xff' * 3 + 'a' + '\x80\xff' + 'b' * 9 + '\xff' + 'c' * 17 + '\xfe'
        self.assertEqual(data.decode('ascii', 'ignore'), u'a' + u'b' * 9 + u'c' * 17)
        self.assertEqual(data.decode('ascii', 'replace'), replacement * 3 + u'a' + replacement * 2 + u'b' * 9 + replacement + u'c' * 17 + replacement)
        self.assertEqual(data.decode('utf-8', 'replace'), data.decode('ascii', 'replace'))
        self.assertEqual(('abcdefg\xff' * 20).decode('ascii', 'ignore'), u'abcdefg' * 20)
        self.assertEqual((('x' * 63 + '\xe9') * 3).decode('ascii', 'replace'), (u'x' * 63 + replacement) * 3)
        mixed = 'a\xff' * 3 + '\xc3\xa9\xc3' + 'b\xe2\x82\xac\xe2\x82' + 'c\xed\xa0\xbd\xc0\x80\xf4\x90'
        self.assertEqual(mixed.decode('utf-8', 'replace'),
                         u'a' + replacement + u'a' + replacement + u'a' + replacement + unichr(0xe9) + replacement + u'b' + unichr(0x20ac) + replacement + u'c' + unichr(0xd83d) + replacement * 3)
        self.assertEqual(mixed.decode('utf-8', 'ignore'), u'aaa' + unichr(0xe9) + u'b' + unichr(0x20ac) + u'c' + unichr(0xd83d))
        self.assertEqual(_codecs.utf_8_decode(mixed[:-8], 'ignore', False), (u'aaa' + unichr(0xe9) + u'b' + unichr(0x20ac), 13))
        positions = []

        def record(error):
            positions.append((error.start, error.end))
            return (u'', error.end)
        _codecs.register_error('unicode_methods_audit_record', record)
        self.assertEqual(('\xff\xfe' + 'a' + '\x80').decode('ascii', 'unicode_methods_audit_record'), u'a')
        text = unichr(0xe9) * 3 + u'a' + unichr(0x20ac) + unichr(0x444) + unichr(0xff) + u'b' * 9 + unichr(0x100)
        self.assertEqual(text.encode('ascii', 'unicode_methods_audit_record'), 'a' + 'b' * 9)
        self.assertEqual(text.encode('latin-1', 'unicode_methods_audit_record'), '\xe9' * 3 + 'a\xff' + 'b' * 9)
        self.assertEqual(positions, [(0, 1), (1, 2), (3, 4), (0, 3), (4, 7), (16, 17), (4, 6), (16, 17)])
        self.assertEqual(text.encode('ascii', 'ignore'), 'a' + 'b' * 9)
        self.assertEqual(text.encode('ascii', 'replace'), '???a???' + 'b' * 9 + '?')
        self.assertEqual(text.encode('latin-1', 'replace'), '\xe9' * 3 + 'a??\xff' + 'b' * 9 + '?')
        self.assertEqual(text.encode('latin-1', 'xmlcharrefreplace'), '\xe9' * 3 + 'a&#8364;&#1092;\xff' + 'b' * 9 + '&#256;')
        self.assertEqual(text.encode('ascii', 'backslashreplace'), '\\xe9' * 3 + 'a\\u20ac\\u0444\\xff' + 'b' * 9 + '\\u0100')
        self.assertEqual(_message(text.encode, 'latin-1'),
                         ('UnicodeEncodeError', "'latin-1' codec can't encode characters in position 4-5: ordinal not in range(256)"))
        self.assertEqual((type(''.decode('ascii')), type(u''.encode('latin-1')), _codecs.utf_8_decode('', 'strict', False)), (unicode, str, (u'', 0)))

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
