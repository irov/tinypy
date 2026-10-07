"""Project-authored Python 2 numeric and codec callback regressions."""

import _codecs
import _sre
import sys
import unittest


def _message(invoke, *arguments):
    try:
        invoke(*arguments)
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError('operation must raise')


def _letter():
    return _sre.compile('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1],
                        0, {}, [None])


class TextNumericCallbackAudit(unittest.TestCase):
    def test_coerce_promotions_read_subtype_payload_without_callbacks(self):
        events = []
        class Integer(int):
            def __long__(self):
                events.append('long')
                return 91L
            def __float__(self):
                events.append('float')
                return 92.0
            def __complex__(self):
                events.append('complex')
                return 93+4j
        class Long(long):
            __float__ = Integer.__dict__['__float__']
            __complex__ = Integer.__dict__['__complex__']
        class Float(float):
            __complex__ = Integer.__dict__['__complex__']
        for owner in (1L, 1.5, 1+2j):
            for source in (Integer(2), Long(2), Float(2.5)):
                result = owner.__coerce__(source)
                if result is NotImplemented:
                    continue
                expected = type(owner)(2.5 if isinstance(source, float) else 2)
                self.assertEqual(result, (owner, expected))
                self.assertIs(type(result[1]), type(source) if isinstance(source, type(owner)) else type(owner))
        self.assertEqual(events, [])

    def test_coerce_same_kind_preserves_both_subtype_identities(self):
        for base in (int, long, float, complex):
            class Child(base):
                pass
            left, right = Child(2), Child(3)
            result = base.__coerce__(left, right)
            self.assertIs(result[0], left)
            self.assertIs(result[1], right)
        self.assertIs(int.__coerce__(True, False)[0], True)
        self.assertIs(int.__coerce__(True, False)[1], False)

    def test_coerce_long_rounding_ignores_float_conversion(self):
        class Long(long):
            def __float__(self):
                raise AssertionError('coerce must read digits')
        value = Long((1L << 53) + 1)
        self.assertEqual(float.__coerce__(1.5, value), (1.5, float(1L << 53)))
        self.assertEqual(complex.__coerce__(1j, value), (1j, complex(1L << 53)))

    def test_coerce_long_overflow_survives_subtype_conversion(self):
        class Long(long):
            def __float__(self):
                return 1.0
            def __complex__(self):
                return 1j
        for owner in (1.5, 1j):
            self.assertEqual(_message(owner.__coerce__, Long(1L << 1100)),
                             ('OverflowError', 'long int too large to convert to float'))
        self.assertEqual((1.5).__coerce__(2), (1.5, 2.0))

    def test_long_getnewargs_copies_payload_without_long_hook(self):
        class Long(long):
            def __long__(self):
                raise AssertionError('getnewargs must read digits')
        for number in (0L, -7L, (1L << 100) + 3):
            source = Long(number)
            result = source.__getnewargs__()
            self.assertEqual(result, (number,))
            self.assertIs(type(result[0]), long)
            self.assertIsNot(result[0], source)

    def test_float_trunc_reads_payload_without_int_hook(self):
        class Float(float):
            def __int__(self):
                raise AssertionError('trunc must read double')
        for number, expected in ((1.75, 1), (-1.75, -1), (0.0, 0),
                                 (-0.0, 0), (float(1L << 70), 1L << 70)):
            self.assertEqual(Float(number).__trunc__(), expected)
        self.assertIs(type(True.__trunc__()), int)
        self.assertEqual(False.__trunc__(), 0)

    def test_float_trunc_nonfinite_errors_ignore_int_hook(self):
        class Float(float):
            def __int__(self):
                return 7
        for number, error in (('nan', 'ValueError'), ('inf', 'OverflowError'),
                              ('-inf', 'OverflowError')):
            observed = _message(Float(number).__trunc__)
            self.assertEqual(observed[0], error)
        self.assertEqual(Float('1.5').__trunc__(), 1)

    def test_codec_positions_use_int_protocol_for_classic_and_new_style(self):
        events = []
        class Position:
            def __int__(self):
                events.append('int')
                return 1
            def __index__(self):
                raise AssertionError('handler position uses int')
        class NewPosition(object):
            __int__ = Position.__dict__['__int__']
            __index__ = Position.__dict__['__index__']
        for position in (Position(), NewPosition()):
            def handler(error):
                events.append('handler')
                return u'X', position
            _codecs.register_error('local-callback-position', handler)
            self.assertEqual(u'\xff'.encode('ascii', 'local-callback-position'), 'X')
            self.assertEqual('\xff'.decode('ascii', 'local-callback-position'), u'X')
        self.assertEqual(events, ['handler', 'int'] * 4)

    def test_codec_int_subtype_is_raw_and_long_subtype_calls_int(self):
        events = []
        class Integer(int):
            def __int__(self):
                events.append('int')
                return 0
        class Long(long):
            __int__ = Integer.__dict__['__int__']
        for decode in (False, True):
            for position, expected, trace in ((Integer(1), 'X', ['handler']),
                                              (Long(1), 'XY', ['handler', 'int', 'handler'])):
                events[:] = []
                def handler(error):
                    events.append('handler')
                    return (u'X', position) if events.count('handler') == 1 else (u'Y', error.end)
                _codecs.register_error('local-callback-subtype', handler)
                result = '\xff'.decode('ascii', 'local-callback-subtype') if decode else u'\xff'.encode('ascii', 'local-callback-subtype')
                self.assertEqual(result, expected)
                self.assertEqual(events, trace)

    def test_codec_position_failures_and_recovery_have_parser_diagnostics(self):
        class IndexOnly(object):
            def __index__(self):
                raise AssertionError('index protocol must not run')
        positions = ((None, 'TypeError', 'an integer is required'),
                     (IndexOnly(), 'TypeError', 'an integer is required'),
                     (1.5, 'TypeError', 'integer argument expected, got float'),
                     (1L << 100, 'OverflowError', 'Python int too large to convert to C long'))
        for decode in (False, True):
            for position, kind, message in positions:
                def handler(error):
                    return u'X', position
                _codecs.register_error('local-callback-invalid-position', handler)
                invoke = '\xff'.decode if decode else u'\xff'.encode
                self.assertEqual(_message(invoke, 'ascii', 'local-callback-invalid-position'), (kind, message))
        self.assertEqual('abc'.decode('ascii'), u'abc')

    def test_codec_replacement_type_is_checked_before_position_callback(self):
        events = []
        class Position(object):
            def __int__(self):
                events.append('int')
                raise ValueError('position failed')
        for decode in (False, True):
            for replacement in ('X', u'X'):
                events[:] = []
                def handler(error):
                    return replacement, Position()
                _codecs.register_error('local-callback-order', handler)
                observed = _message('\xff'.decode if decode else u'\xff'.encode,
                                    'ascii', 'local-callback-order')
                if isinstance(replacement, unicode):
                    self.assertEqual(observed, ('ValueError', 'position failed'))
                    self.assertEqual(events, ['int'])
                else:
                    self.assertEqual(observed, ('TypeError', '%s error handler must return (unicode, int) tuple' % ('decoding' if decode else 'encoding')))
                    self.assertEqual(events, [])

    def test_codec_result_shape_has_encode_or_decode_diagnostic(self):
        for decode in (False, True):
            for answer in (None, [], [u'X', 1], (u'X',), (u'X', 1, 2), (1, 1)):
                def handler(error):
                    return answer
                _codecs.register_error('local-callback-shape', handler)
                self.assertEqual(_message('\xff'.decode if decode else u'\xff'.encode,
                                          'ascii', 'local-callback-shape'),
                                 ('TypeError', '%s error handler must return (unicode, int) tuple' % ('decoding' if decode else 'encoding')))

    def test_codec_negative_positions_and_bounds_report_adjusted_position(self):
        for decode in (False, True):
            for position, expected in ((-1, 'XX'), (-2, -1), (2, 2)):
                events = []
                def handler(error):
                    events.append('handler')
                    return u'X', position if len(events) == 1 else error.end
                _codecs.register_error('local-callback-bounds', handler)
                invoke = '\xff'.decode if decode else u'\xff'.encode
                if isinstance(expected, str):
                    self.assertEqual(invoke('ascii', 'local-callback-bounds'), expected)
                else:
                    self.assertEqual(_message(invoke, 'ascii', 'local-callback-bounds'),
                                     ('IndexError', 'position %d from error handler out of bounds' % expected))

    def test_codec_handler_is_cached_across_registry_mutation(self):
        for decode, encoding in ((False, 'ascii'), (False, 'latin-1'),
                                  (True, 'ascii'), (True, 'utf-8')):
            events = []
            def replacement(error):
                events.append('replacement')
                return u'Y', error.end
            def original(error):
                events.append('original')
                _codecs.register_error('local-callback-cached', replacement)
                return u'X', error.end
            _codecs.register_error('local-callback-cached', original)
            source = '\xffa\xff' if decode else u'\u1234a\u1234'
            result = source.decode(encoding, 'local-callback-cached') if decode else source.encode(encoding, 'local-callback-cached')
            self.assertEqual(result, 'XaX')
            self.assertEqual(events, ['original', 'original'])
            self.assertIs(_codecs.lookup_error('local-callback-cached'), replacement)

    def test_codec_exception_is_reused_and_refreshes_range_and_reason(self):
        for decode in (False, True):
            seen, details = [], []
            def handler(error):
                seen.append(error)
                details.append((error.start, error.end, error.reason))
                end = error.end
                error.reason, error.start, error.end = 'changed', -9, -8
                return u'X', end
            _codecs.register_error('local-callback-exception', handler)
            result = '\xffa\xff'.decode('ascii', 'local-callback-exception') if decode else u'\xffa\xff'.encode('ascii', 'local-callback-exception')
            self.assertEqual(result, 'XaX')
            self.assertIs(seen[0], seen[1])
            self.assertEqual(details, [(0, 1, 'ordinal not in range(128)'),
                                       (2, 3, 'ordinal not in range(128)')])

    def test_codec_exception_keeps_modified_object_and_encoding(self):
        seen = []
        def handler(error):
            seen.append((error.encoding, error.object))
            end = error.end
            error.encoding, error.object = 'edited', 'replacement'
            return u'X', end
        _codecs.register_error('local-callback-metadata', handler)
        self.assertEqual('\xffa\xff'.decode('ascii', 'local-callback-metadata'), u'XaX')
        self.assertEqual(seen, [('ascii', '\xffa\xff'), ('edited', 'replacement')])

    def test_decoder_object_replacement_keeps_original_input_and_bounds(self):
        for replacement in ('Z', 'WXYZ'):
            for position, expected in ((1, u'Xa'), (-1, u'Xa'), (2, u'X')):
                def handler(error):
                    error.object = replacement
                    return u'X', position
                _codecs.register_error('local-callback-input', handler)
                self.assertEqual('\xffa'.decode('ascii', 'local-callback-input'), expected)
            def out_of_bounds(error):
                error.object = replacement
                return u'X', 3
            _codecs.register_error('local-callback-input', out_of_bounds)
            self.assertEqual(_message('\xffa'.decode, 'ascii', 'local-callback-input'),
                             ('IndexError', 'position 3 from error handler out of bounds'))

    def test_position_callback_object_replacement_keeps_original_input(self):
        events = []
        class Position(object):
            def __init__(self, error, replacement):
                self.error, self.replacement = error, replacement
            def __int__(self):
                events.append('int')
                self.error.object = self.replacement
                return -1
        for decode in (False, True):
            for replacement in ('Z', 'WXYZ'):
                events[:] = []
                def handler(error):
                    events.append('handler')
                    return u'X', Position(error, replacement if decode else unicode(replacement))
                _codecs.register_error('local-callback-input-position', handler)
                result = '\xffa'.decode('ascii', 'local-callback-input-position') if decode else u'\xffa'.encode('ascii', 'local-callback-input-position')
                self.assertEqual(result, 'Xa')
                self.assertEqual(events, ['handler', 'int'])

    def test_unencodable_replacement_raises_original_exception_and_single_span(self):
        for encoding, source, reason in (('ascii', u'a\xff\xffb', 'ordinal not in range(128)'),
                                         ('latin-1', u'a\u1234\u1235b', 'ordinal not in range(256)')):
            seen = []
            def handler(error):
                seen.append(error)
                return u'\u1234', error.end
            _codecs.register_error('local-callback-unencodable', handler)
            try:
                source.encode(encoding, 'local-callback-unencodable')
            except UnicodeEncodeError as error:
                self.assertIs(error, seen[0])
                self.assertEqual((error.encoding, error.object, error.start, error.end, error.reason),
                                 (encoding, source, 1, 2, reason))
            else:
                self.fail('replacement is not encodable')

    def test_encoder_releases_position_before_unencodable_replacement(self):
        for decode in (False, True):
            events, seen = [], []
            class Position(object):
                def __init__(self, error):
                    self.error = error
                def __int__(self):
                    events.append('int')
                    return 1
                def __del__(self):
                    events.append('del')
                    self.error.reason = 'destroyed'
            def handler(error):
                seen.append(error)
                return u'\u1234', Position(error)
            _codecs.register_error('local-callback-position-release', handler)
            if decode:
                self.assertEqual('\xff'.decode('ascii', 'local-callback-position-release'), u'\u1234')
                self.assertEqual(seen[0].reason, 'destroyed')
            else:
                try:
                    u'\xff'.encode('ascii', 'local-callback-position-release')
                except UnicodeEncodeError as error:
                    self.assertIs(error, seen[0])
                    self.assertEqual(error.reason, 'ordinal not in range(128)')
                else:
                    self.fail('replacement is not encodable')
            self.assertEqual(events, ['int', 'del'])

    def test_unicode_replacement_subtype_payload_ignores_conversion_hooks(self):
        class Unicode(unicode):
            def __unicode__(self):
                raise AssertionError('replacement payload required')
            def __str__(self):
                raise AssertionError('replacement payload required')
        class Answer(tuple):
            pass
        def handler(error):
            return Answer((Unicode(u'X\0Y'), error.end))
        _codecs.register_error('local-callback-replacement-subtype', handler)
        self.assertEqual(u'\xff'.encode('ascii', 'local-callback-replacement-subtype'), 'X\0Y')
        self.assertEqual('\xff'.decode('ascii', 'local-callback-replacement-subtype'), u'X\0Y')

    def test_codec_callback_failure_preserves_handled_exception(self):
        outer = ValueError('outer')
        def handler(error):
            raise KeyError('handler')
        _codecs.register_error('local-callback-failure', handler)
        try:
            raise outer
        except ValueError:
            before = sys.exc_info()[1]
            self.assertEqual(_message('\xff'.decode, 'ascii', 'local-callback-failure'),
                             ('KeyError', "'handler'"))
            self.assertIs(sys.exc_info()[1], before)
            self.assertEqual('abc'.decode('ascii'), u'abc')

    def test_nested_codec_calls_use_independent_error_state(self):
        seen = []
        def inner(error):
            seen.append(('inner', error))
            return u'I', error.end
        def outer(error):
            seen.append(('outer', error))
            return u'\xff'.encode('ascii', 'local-callback-inner').decode('ascii'), error.end
        _codecs.register_error('local-callback-inner', inner)
        _codecs.register_error('local-callback-outer', outer)
        self.assertEqual('\xffa\xff'.decode('ascii', 'local-callback-outer'), u'IaI')
        self.assertIs(seen[0][1], seen[2][1])
        self.assertIsNot(seen[0][1], seen[1][1])
        self.assertIsNot(seen[1][1], seen[3][1])

    def test_builtin_codec_handlers_read_payload_without_attribute_callbacks(self):
        events = []
        class Encode(UnicodeEncodeError):
            def __getattribute__(self, name):
                if name in ('object', 'start', 'end'):
                    events.append(name)
                    raise ValueError('attribute callback')
                return UnicodeEncodeError.__getattribute__(self, name)
        class Decode(UnicodeDecodeError):
            __getattribute__ = Encode.__dict__['__getattribute__']
        class Translate(UnicodeTranslateError):
            __getattribute__ = Encode.__dict__['__getattribute__']
        for error in (Encode('ascii', u'a\xff', 1, 2, 'bad'),
                       Decode('ascii', 'a\xff', 1, 2, 'bad'),
                       Translate(u'a\xff', 1, 2, 'bad')):
            self.assertEqual(_codecs.lookup_error('ignore')(error), (u'', 2))
            self.assertEqual(_codecs.lookup_error('replace')(error),
                             (u'?' if isinstance(error, UnicodeEncodeError) else u'\ufffd', 2))
        self.assertEqual(_codecs.lookup_error('xmlcharrefreplace')(Encode('ascii', u'a\xff', 1, 2, 'bad')), (u'&#255;', 2))
        self.assertEqual(_codecs.lookup_error('backslashreplace')(Encode('ascii', u'a\xff', 1, 2, 'bad')), (u'\\xff', 2))
        self.assertEqual(events, [])

    def test_strict_codec_handler_requires_an_exception_instance(self):
        invoke = _codecs.lookup_error('strict')
        for value in (ValueError, 1, None, object()):
            self.assertEqual(_message(invoke, value),
                             ('TypeError', 'codec must pass exception instance'))
        error = ValueError('same instance')
        try:
            invoke(error)
        except ValueError as raised:
            self.assertIs(raised, error)
        else:
            self.fail('strict handler must raise supplied exception')

    def test_builtin_handlers_report_actual_unsupported_type(self):
        for name in ('ignore', 'replace', 'xmlcharrefreplace', 'backslashreplace'):
            invoke = _codecs.lookup_error(name)
            for value in (ValueError('bad'), 7, UnicodeError('bad')):
                self.assertEqual(_message(invoke, value),
                                 ('TypeError', "don't know how to handle %s in error callback" % type(value).__name__))
        for name in ('xmlcharrefreplace', 'backslashreplace'):
            self.assertEqual(_message('\xff'.decode, 'ascii', name),
                             ('TypeError', "don't know how to handle UnicodeDecodeError in error callback"))

    def test_builtin_handlers_validate_mutated_object_payload(self):
        for error, message in ((UnicodeEncodeError('ascii', u'a', 0, 1, 'bad'), 'unicode'),
                               (UnicodeDecodeError('ascii', 'a', 0, 1, 'bad'), 'str'),
                               (UnicodeTranslateError(u'a', 0, 1, 'bad'), 'unicode')):
            for value in (None, 1):
                error.object = value
                self.assertEqual(_message(_codecs.lookup_error('ignore'), error),
                                 ('TypeError', 'object attribute must be %s' % message))

    def test_sre_invalid_replacement_reports_item_after_all_callbacks(self):
        events = []
        for answer in (bytearray('X'), buffer('X'), memoryview('X'), 7):
            events[:] = []
            def replacement(match):
                events.append(match.span())
                return answer
            self.assertEqual(_message(_letter().sub, replacement, 'aba'),
                             ('TypeError', 'sequence item 0: expected string, %s found' % type(answer).__name__))
            self.assertEqual(events, [(0, 1), (2, 3)])
        self.assertEqual(_letter().sub('X', 'aba'), 'XbX')

    def test_join_reports_item_index_and_unicode_promotion_order(self):
        for separator, values, message in (('', [1, u'x'], 'sequence item 0: expected string, int found'),
                                           ('', [u'x', 1], 'sequence item 1: expected string or Unicode, int found'),
                                           (u'', ['a', 1], 'sequence item 1: expected string or Unicode, int found')):
            self.assertEqual(_message(separator.join, values), ('TypeError', message))
        self.assertEqual(''.join(['a\0', u'b']), u'a\0b')

    def test_sre_embedded_null_subject_and_replacement_callbacks(self):
        seen = []
        def replacement(match):
            seen.append((match.span(), match.group(), match.string))
            return u'X\0'
        source = 'a\0a'
        self.assertEqual(_letter().sub(replacement, source), u'X\0\0X\0')
        self.assertEqual([(span, text) for span, text, unused in seen],
                         [((0, 1), 'a'), ((2, 3), 'a')])
        self.assertTrue(all(owner is source for unused, text, owner in seen))
