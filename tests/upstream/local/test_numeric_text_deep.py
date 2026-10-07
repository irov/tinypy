"""Project-authored numeric and text protocol matrices for Python 2.7."""

import _codecs
import unittest


class _IntArgument(object):
    def __init__(self, value, events):
        self.value = value
        self.events = events

    def __int__(self):
        self.events.append('int')
        return self.value


class _IndexArgument(object):
    def __init__(self, value, events):
        self.value = value
        self.events = events

    def __index__(self):
        self.events.append('index')
        return self.value


class NumericTextDeep(unittest.TestCase):
    def test_text_numeric_arguments_use_int_protocol(self):
        for text in ('a b\tc\nd', u'a b\tc\nd'):
            operations = (
                (text.replace, ('a', 'z'), 'z b\tc\nd'),
                (text.split, (None,), ['a', 'b', 'c\nd']),
                (text.rsplit, (None,), ['a b', 'c', 'd']),
                (text.center, (), text),
                (text.ljust, (), text),
                (text.rjust, (), text),
                (text.zfill, (), text),
                (text.expandtabs, (), 'a b c\nd'),
                (text.splitlines, (), ['a b\tc\n', 'd']),
            )
            for operation, prefix, expected in operations:
                events = []
                self.assertEqual(operation(*(prefix + (_IntArgument(2, events),))), expected)
                self.assertEqual(events, ['int'])
                events = []
                self.assertRaises(TypeError, operation, *(prefix + (_IndexArgument(2, events),)))
                self.assertEqual(events, [])
                for invalid in (2.5, '2', u'2', object()):
                    self.assertRaises(TypeError, operation, *(prefix + (invalid,)))

    def test_text_numeric_argument_failures_and_integer_subtypes(self):
        class Integer(int):
            def __int__(self):
                raise AssertionError('stored integer value must be used')
        class Failure(object):
            def __int__(self):
                raise ValueError('conversion failed')
        class Long(long):
            def __int__(self):
                return 0
        for text in ('a\nb', u'a\nb'):
            for operation, prefix in ((text.replace, ('a', 'z')), (text.split, (None,)),
                                      (text.center, ()), (text.expandtabs, ()), (text.splitlines, ())):
                self.assertEqual(operation(*(prefix + (Integer(2),))), operation(*(prefix + (2,))))
                self.assertRaises(ValueError, operation, *(prefix + (Failure(),)))
                self.assertEqual(operation(*(prefix + (Long(2),))), operation(*(prefix + (0,))))
                for invalid in ('wrong', 2.5, object()):
                    events = []
                    self.assertRaises(TypeError, operation, *(prefix + (_IntArgument(invalid, events),)))
                    self.assertEqual(events, ['int'])

    def test_text_c_int_arguments_enforce_width(self):
        for text in ('a\nb', u'a\nb'):
            for value in (-(2 ** 31), 2 ** 31 - 1):
                self.assertEqual(text.splitlines(value), ['a\n', 'b'])
            for value in (-(2 ** 31) - 1, 2 ** 31, 2 ** 100, -(2 ** 100)):
                self.assertRaises(OverflowError, text.splitlines, value)
                self.assertRaises(OverflowError, text.expandtabs, value)
            self.assertEqual(text.splitlines(0), ['a', 'b'])
            self.assertEqual(text.splitlines(False), ['a', 'b'])

    def test_text_search_bounds_use_index_protocol(self):
        for text in ('ababa', u'ababa'):
            for operation in (text.find, text.rfind, text.count, text.startswith, text.endswith):
                events = []
                operation('a', _IndexArgument(2, events))
                self.assertEqual(events, ['index'])
                events = []
                self.assertRaises(TypeError, operation, 'a', _IntArgument(2, events))
                self.assertEqual(events, [])

    def test_text_search_boundary_matrix(self):
        rows = (
            ('', 0, 2, (0, 2, 3, True, True)),
            ('', 2, 2, (2, 2, 1, True, True)),
            ('', 3, 3, (-1, -1, 0, False, False)),
            ('', -10, 0, (0, 0, 1, True, True)),
            ('a', -10, 10, (0, 0, 1, True, False)),
            ('b', 1, 2, (1, 1, 1, True, True)),
            ('ab', 1, 2, (-1, -1, 0, False, False)),
            ('', 2 ** 100, 2 ** 100, (-1, -1, 0, False, False)),
        )
        for text in ('ab', u'ab'):
            for needle, start, end, expected in rows:
                for value in (str(needle), unicode(needle)):
                    actual = tuple(operation(value, start, end) for operation in
                                   (text.find, text.rfind, text.count, text.startswith, text.endswith))
                    selected_expected = expected
                    if (isinstance(text, unicode) or isinstance(value, unicode)) and not value and start > len(text):
                        selected_expected = expected[:3] + (True, True)
                    self.assertEqual(actual, selected_expected)

    def test_codec_method_keywords_and_duplicate_arguments(self):
        for text in ('ab', u'ab'):
            for operation in (text.encode, text.decode):
                self.assertEqual(operation(encoding='utf-8'), operation('utf-8'))
                self.assertEqual(operation(errors='strict'), operation('ascii', 'strict'))
                self.assertEqual(operation('utf-8', errors='strict'), operation('utf-8', 'strict'))
                self.assertEqual(operation(encoding=u'utf-8', errors=u'strict'), operation('utf-8', 'strict'))
                self.assertRaises(TypeError, operation, 'ascii', encoding='utf-8')
                self.assertRaises(TypeError, operation, 'ascii', 'strict', errors='ignore')
                self.assertRaises(TypeError, operation, unknown='argument')
                self.assertRaises(TypeError, operation, encoding=None)
                self.assertRaises(TypeError, operation, errors=None)
        self.assertEqual(u'\u20ac'.encode(encoding='ascii', errors='replace'), '?')
        self.assertEqual('\xff'.decode(encoding='ascii', errors='replace'), u'\ufffd')

    def test_codec_name_validation_matrix(self):
        for name in ('ascii\x00', u'ascii\x00', 'strict\x00', u'strict\x00'):
            for operation in ('a'.encode, 'a'.decode, u'a'.encode, u'a'.decode,
                              _codecs.lookup, _codecs.lookup_error):
                self.assertRaises(TypeError, operation, name)
            self.assertRaises(TypeError, _codecs.register_error, name, lambda error: (u'', error.end))
            self.assertRaises(TypeError, 'a'.encode, 'ascii', name)
            self.assertRaises(TypeError, _codecs.utf_8_decode, 'a', name)
        for operation in ('a'.encode, 'a'.decode, u'a'.encode, u'a'.decode,
                          _codecs.lookup, _codecs.lookup_error):
            self.assertRaises(UnicodeEncodeError, operation, u'\xff')
        self.assertRaises(UnicodeEncodeError, _codecs.register_error, u'\xff', lambda error: (u'', error.end))
        self.assertRaises(UnicodeEncodeError, 'a'.encode, 'ascii', u'\xff')
        self.assertEqual(_codecs.utf_8_decode('a\x00b'), (u'a\x00b', 3))

    def test_utf8_incremental_prefix_matrix(self):
        sequences = ('\xc2\xa2', '\xe2\x82\xac', '\xf0\x90\x80\x80', '\xed\xa0\x80')
        for sequence in sequences:
            for width in range(1, len(sequence)):
                prefix = sequence[:width]
                for errors in ('strict', 'ignore', 'replace'):
                    self.assertEqual(_codecs.utf_8_decode(prefix, errors), (u'', 0))
                    self.assertEqual(_codecs.utf_8_decode('ab' + prefix, errors, False), (u'ab', 2))
                self.assertRaises(UnicodeDecodeError, _codecs.utf_8_decode, prefix, 'strict', True)
                self.assertEqual(_codecs.utf_8_decode(prefix, 'ignore', True), (u'', width))
                self.assertEqual(_codecs.utf_8_decode(prefix, 'replace', True), (u'\ufffd', width))
            decoded = sequence.decode('utf-8')
            self.assertEqual(_codecs.utf_8_decode(sequence), (decoded, len(sequence)))
        for prefix in ('\xe0\x80', '\xf0\x80', '\xf4\x90'):
            self.assertEqual(_codecs.utf_8_decode(prefix), (u'', 0))
        for invalid in ('\x80', '\xc0', '\xc1', '\xf5', '\xff'):
            self.assertRaises(UnicodeDecodeError, _codecs.utf_8_decode, invalid)
            self.assertEqual(_codecs.utf_8_decode(invalid, 'replace'), (u'\ufffd', 1))

    def test_registered_utf8_decoder_finishes_input(self):
        for operation in (_codecs.lookup('utf-8')[1], lambda value: _codecs.decode(value, 'utf-8'),
                          lambda value: value.decode('utf-8')):
            self.assertRaises(UnicodeDecodeError, operation, '\xc3')
        self.assertEqual(_codecs.lookup('utf-8')[1]('\xc3', 'replace'), (u'\ufffd', 1))
        self.assertEqual(_codecs.decode('\xc3', 'utf-8', 'replace'), u'\ufffd')

    def test_utf8_final_flag_uses_c_int_protocol(self):
        events = []
        self.assertEqual(_codecs.utf_8_decode('\xc3', 'replace', _IntArgument(1, events)), (u'\ufffd', 1))
        self.assertEqual(events, ['int'])
        for value in (None, 1.5, '1', _IndexArgument(1, [])):
            self.assertRaises(TypeError, _codecs.utf_8_decode, 'a', 'strict', value)
        for value in (-(2 ** 31) - 1, 2 ** 31, 2 ** 100):
            self.assertRaises(OverflowError, _codecs.utf_8_decode, 'a', 'strict', value)

    def test_incremental_codec_error_callback_preserves_source(self):
        events = []
        def replace(error):
            events.append((error.object, error.start, error.end))
            return u'Z', error.end
        _codecs.register_error('local-deep-replacement', replace)
        self.assertEqual(_codecs.utf_8_decode('\xffa\xc3', 'local-deep-replacement'), (u'Za', 2))
        self.assertEqual(events, [('\xffa\xc3', 0, 1)])
        self.assertEqual(_codecs.utf_8_decode('\xffa\xc3', 'local-deep-replacement', True), (u'ZaZ', 3))
        self.assertEqual(events, [('\xffa\xc3', 0, 1), ('\xffa\xc3', 0, 1), ('\xffa\xc3', 2, 3)])
        for invalid in (-2, 2, 2 ** 100, '1'):
            def invalid_position(error):
                return u'x', invalid
            _codecs.register_error('local-deep-invalid-position', invalid_position)
            expected = TypeError if isinstance(invalid, str) else (OverflowError if invalid > 2 else IndexError)
            self.assertRaises(expected, _codecs.utf_8_decode, '\xff', 'local-deep-invalid-position')

    def test_specific_codec_arity_and_subtype_overrides(self):
        class Bytes(str):
            def encode(self, *args):
                raise AssertionError('override must not run')
            def decode(self, *args):
                raise AssertionError('override must not run')
        class Text(unicode):
            def encode(self, *args):
                raise AssertionError('override must not run')
            def decode(self, *args):
                raise AssertionError('override must not run')
        for value in (Bytes('ab'), Text('ab')):
            for operation in (_codecs.utf_8_decode, _codecs.ascii_decode, _codecs.latin_1_decode):
                self.assertEqual(operation(value), (u'ab', 2))
            for operation in (_codecs.utf_8_encode, _codecs.ascii_encode, _codecs.latin_1_encode):
                self.assertEqual(operation(value), ('ab', 2))
        for operation in (_codecs.ascii_decode, _codecs.latin_1_decode):
            self.assertRaises(TypeError, operation, 'ab', 'strict', False)

    def test_specific_codec_buffer_input_matrix(self):
        for value in (bytearray('ab'), memoryview('ab'), buffer('ab')):
            for operation in (_codecs.utf_8_decode, _codecs.ascii_decode, _codecs.latin_1_decode):
                self.assertEqual(operation(value), (u'ab', 2))
        for value in (bytearray('ab'), memoryview('ab')):
            for operation in (_codecs.utf_8_encode, _codecs.ascii_encode, _codecs.latin_1_encode):
                self.assertRaises(TypeError, operation, value)
        for operation in (_codecs.utf_8_encode, _codecs.ascii_encode, _codecs.latin_1_encode):
            self.assertEqual(operation(buffer('ab')), ('ab', 2))
        self.assertEqual(_codecs.utf_8_decode(memoryview('\xc3')), (u'', 0))
        self.assertEqual(_codecs.latin_1_decode(bytearray('\xff')), (u'\xff', 1))

    def test_lowlevel_decoder_argument_callback_order(self):
        events = []
        for source, expected in ((None, TypeError), (object(), TypeError), (u'\xff', UnicodeEncodeError)):
            self.assertRaises(expected, _codecs.utf_8_decode, source, 'strict', _IntArgument(1, events))
            self.assertEqual(events, [])
        target = bytearray('a')
        class Final(object):
            def __int__(self):
                events.append('final')
                target[0] = ord('b')
                return 1
        self.assertEqual(_codecs.utf_8_decode(target, 'strict', Final()), (u'b', 1))
        self.assertEqual(events, ['final'])

    def test_unicode_translate_accepts_only_stored_integer_values(self):
        for mapped in (None, 65, True, u'xy', u''):
            expected = u'' if mapped is None else (mapped if isinstance(mapped, unicode) else unichr(mapped))
            self.assertEqual(u'aa'.translate({97: mapped}), expected * 2)
        for invalid in (-1, 65L, 0x110000, 2 ** 100, -(2 ** 100), 'x', 3.5):
            self.assertRaises(TypeError, u'a'.translate, {97: invalid})
        for argument_type in (_IntArgument, _IndexArgument):
            events = []
            self.assertRaises(TypeError, u'a'.translate, {97: argument_type(65, events)})
            self.assertEqual(events, [])

    def test_percent_field_protocols(self):
        for text in ('%*s', u'%*s'):
            for argument_type in (_IntArgument, _IndexArgument):
                events = []
                self.assertRaises(TypeError, lambda: text % (argument_type(3, events), 'x'))
                self.assertEqual(events, [])
            self.assertEqual(text % (3, 'x'), '  x')
            self.assertRaises(TypeError, lambda: text % (3L, 'x'))
        for text in ('%.*s', u'%.*s'):
            for argument_type in (_IntArgument, _IndexArgument):
                events = []
                self.assertRaises(TypeError, lambda: text % (argument_type(2, events), 'abc'))
                self.assertEqual(events, [])
            self.assertEqual(text % (2, 'abc'), 'ab')
            self.assertRaises(TypeError, lambda: text % (2L, 'abc'))
        for text in ('%c', u'%c'):
            events = []
            self.assertEqual(text % _IntArgument(65, events), 'A')
            self.assertEqual(events, ['int'])
            events = []
            self.assertRaises(TypeError, lambda: text % _IndexArgument(65, events))
            self.assertEqual(events, [])
        self.assertRaises(OverflowError, lambda: '%c' % (2 ** 100))
        self.assertRaises(TypeError, lambda: u'%c' % (2 ** 100))

    def test_numeric_bit_lengths_and_integer_ratio_matrix(self):
        for width in (1, 14, 15, 16, 30, 31, 62, 63, 64, 100, 257):
            value = 1 << width
            for sign in (-1, 1):
                self.assertEqual((sign * value).bit_length(), width + 1)
                self.assertEqual((sign * (value - 1)).bit_length(), width)
        self.assertEqual((0).bit_length(), 0)
        rows = ((0.0, (0, 1)), (-0.0, (0, 1)), (0.5, (1, 2)), (-1.25, (-5, 4)),
                (0.1, (3602879701896397L, 36028797018963968L)),
                (2.0 ** -1074, (1, 1L << 1074)))
        for value, expected in rows:
            self.assertEqual(value.as_integer_ratio(), expected)
        self.assertRaises(ValueError, float('nan').as_integer_ratio)
        for value in (float('inf'), float('-inf')):
            self.assertRaises(OverflowError, value.as_integer_ratio)

    def test_float_hex_roundtrip_and_signed_zero_matrix(self):
        for value in (0.0, -0.0, 0.1, -0.1, 2.0 ** -1074, 2.0 ** -1022,
                      2.0 ** 1023, float('inf'), float('-inf')):
            restored = float.fromhex(value.hex())
            self.assertEqual(restored, value)
            self.assertEqual(restored.hex().startswith('-'), value.hex().startswith('-'))
        nan = float.fromhex(float('nan').hex())
        self.assertTrue(nan != nan)
        for source in ('0x1p-1075', '0x1.0000000000001p-1075'):
            expected = 0.0 if source == '0x1p-1075' else 2.0 ** -1074
            self.assertEqual(float.fromhex(source), expected)
        for source in ('', '0xp1', '0x.p1', '1p', '1p+', '0x1z', '1\x00'):
            self.assertRaises(ValueError, float.fromhex, source)

    def test_float_fromhex_subclass_creation_and_error_propagation(self):
        events = []
        class Number(float):
            def __new__(cls, value):
                events.append(('new', value))
                return float.__new__(cls, value + 1)
            def __init__(self, value):
                events.append(('init', value))
        result = Number.fromhex('0x1p2')
        self.assertIs(type(result), Number)
        self.assertEqual(result, 5.0)
        self.assertEqual(events, [('new', 4.0), ('init', 4.0)])
        self.assertRaises(ValueError, Number.fromhex, 'invalid')
        self.assertEqual(events, [('new', 4.0), ('init', 4.0)])

    def test_float_format_metadata_argument_and_state_matrix(self):
        originals = [(name, float.__getformat__(name)) for name in ('float', 'double')]
        try:
            for name, original in originals:
                self.assertEqual(float.__getformat__(name + '\x00ignored'), original)
                self.assertRaises(TypeError, float.__getformat__, unicode(name))
                self.assertRaises(TypeError, float.__getformat__, 1)
                self.assertIs(float.__setformat__(unicode(name), u'unknown'), None)
                self.assertEqual(float.__getformat__(name), 'unknown')
                for invalid in (name + '\x00', 1, None):
                    self.assertRaises(TypeError, float.__setformat__, invalid, 'unknown')
                for invalid in ('unknown\x00', 1, None):
                    self.assertRaises(TypeError, float.__setformat__, name, invalid)
                self.assertRaises(ValueError, float.__setformat__, name, 'invalid')
                self.assertEqual(float.__getformat__(name), 'unknown')
                float.__setformat__(name, original)
                self.assertEqual(float.__getformat__(name), original)
        finally:
            for name, original in originals:
                float.__setformat__(name, original)

    def test_unknown_float_format_struct_policy(self):
        import _struct
        original = float.__getformat__('double')
        values = (float('inf'), float('-inf'), float('nan'))
        encoded = [(prefix, [_struct.pack(prefix + 'd', value) for value in values])
                   for prefix in ('', '@', '=', '<', '>', '!')]
        try:
            float.__setformat__('double', 'unknown')
            for prefix, records in encoded:
                for value, data in zip(values, records):
                    if prefix in ('', '@'):
                        self.assertEqual(_struct.pack(prefix + 'd', value), data)
                        unpacked = _struct.unpack(prefix + 'd', data)[0]
                        self.assertTrue(unpacked != unpacked if value != value else unpacked == value)
                    else:
                        self.assertRaises(SystemError, _struct.pack, prefix + 'd', value)
                        self.assertRaises(ValueError, _struct.unpack, prefix + 'd', data)
                if prefix in ('', '@'):
                    self.assertNotEqual(_struct.pack(prefix + 'd', -0.0), _struct.pack(prefix + 'd', 0.0))
                else:
                    self.assertEqual(_struct.pack(prefix + 'd', -0.0), _struct.pack(prefix + 'd', 0.0))
        finally:
            float.__setformat__('double', original)

    def test_struct_pack_into_retains_partial_progress(self):
        import _struct
        original = float.__getformat__('double')
        try:
            float.__setformat__('double', 'unknown')
            target = bytearray('X' * 18)
            self.assertRaises(SystemError, _struct.pack_into, '<2d', target, 1, 1.0, float('inf'))
            self.assertEqual(str(target), 'X' + _struct.pack('<d', 1.0) + '\x00' * 8 + 'X')
        finally:
            float.__setformat__('double', original)
        target = bytearray('X' * 16)
        self.assertRaises(_struct.error, _struct.pack_into, '<2d', target, 0, 1.0, object())
        self.assertEqual(str(target), _struct.pack('<d', 1.0) + '\x00' * 8)

    def test_struct_float_argument_protocols(self):
        import _struct
        class Floating(object):
            def __float__(self):
                return 1.5
        class Integer(int):
            def __float__(self):
                return 1.5
        class Long(long):
            def __float__(self):
                return 1.5
        class Float(float):
            def __float__(self):
                raise AssertionError('stored float value must be used')
        class Invalid(object):
            def __float__(self):
                return 1
        class Failure(object):
            def __float__(self):
                raise KeyError('float conversion failed')
        for source in (Floating(), Integer(3), Long(3), Float(1.5)):
            self.assertEqual(_struct.pack('<d', source), _struct.pack('<d', 1.5))
        for source in (Invalid(), Failure(), '1.5', 1L << 20000):
            self.assertRaises(_struct.error, _struct.pack, '<d', source)

    def test_struct_buffer_writes_bypass_python_hooks(self):
        import _struct
        class Bytes(bytearray):
            def __setitem__(self, key, value):
                raise AssertionError('buffer write must bypass Python item hooks')
        for factory in (lambda target: target, memoryview):
            target = Bytes('X' * 16)
            _struct.pack_into('<2d', factory(target), 0, 1.0, 2.0)
            self.assertEqual(str(target), _struct.pack('<2d', 1.0, 2.0))
        target = Bytes('X' * 16)
        self.assertRaises(TypeError, _struct.pack_into, '<2d', buffer(target), 0, 1.0, 2.0)
        self.assertEqual(str(target), 'X' * 16)

    def test_struct_offset_integer_protocol_matrix(self):
        import _struct
        class Integer(int):
            def __int__(self):
                raise AssertionError('stored int must be used')
        class Long(long):
            def __int__(self):
                return 8
        class Float(float):
            def __int__(self):
                return 8
        class Both(object):
            def __int__(self):
                return 8
            def __index__(self):
                raise AssertionError('index protocol must not be used')
        class Failure(object):
            def __int__(self):
                raise KeyError('offset conversion')
        initial = _struct.pack('<3d', 1.0, 2.0, 3.0)
        for compiled in (False, True):
            struct = _struct.Struct('<d') if compiled else None
            for packing in (False, True):
                operation = struct.pack_into if packing and compiled else struct.unpack_from if compiled else _struct.pack_into if packing else _struct.unpack_from
                rows = ((0, 0), (0L, 0), (Integer(0), 0),
                        (Long(0), 0 if packing else 8), (Both(), 8),
                        (-8, 16), (-8L, 16))
                if packing:
                    rows += ((0.5, 0), (-0.5, 0), (8.75, 8),
                             (-8.75, 16), (Float(0.0), 8))
                for offset, position in rows:
                    target = bytearray(initial)
                    arguments = (target, offset) + ((4.0,) if packing else ())
                    if not compiled:
                        arguments = ('<d',) + arguments
                    result = operation(*arguments)
                    if packing:
                        self.assertIs(result, None)
                        self.assertEqual(str(target), initial[:position] + _struct.pack('<d', 4.0) + initial[position + 8:])
                    else:
                        self.assertEqual(result, ((1.0, 2.0, 3.0)[position // 8],))
                failures = [(_IndexArgument(0, []), TypeError),
                            (_IntArgument(None, []), TypeError),
                            (_IntArgument(0.0, []), TypeError),
                            (_IntArgument('0', []), TypeError),
                            (Failure(), KeyError), (1L << 100, OverflowError),
                            (-(1L << 100), OverflowError),
                            (_IntArgument(1L << 100, []), OverflowError),
                            (-25, _struct.error), (17, _struct.error)]
                if packing:
                    failures += [(float('inf'), OverflowError),
                                 (float('nan'), ValueError), (1e100, OverflowError)]
                else:
                    failures += [(value, TypeError) for value in
                                 (0.5, -0.5, 8.75, Float(0.0), float('inf'), float('nan'))]
                for offset, error in failures:
                    target = bytearray(initial)
                    arguments = (target, offset) + ((4.0,) if packing else ())
                    if not compiled:
                        arguments = ('<d',) + arguments
                    self.assertRaises(error, operation, *arguments)
                    self.assertEqual(str(target), initial)

    def test_struct_offset_callbacks_acquire_buffer_first(self):
        import _struct
        initial = _struct.pack('<2d', 1.0, 2.0)
        for compiled in (False, True):
            struct = _struct.Struct('<d') if compiled else None
            for packing in (False, True):
                operation = struct.pack_into if packing and compiled else struct.unpack_from if compiled else _struct.pack_into if packing else _struct.unpack_from
                for factory in (lambda value: value, memoryview):
                    target = bytearray(initial)
                    source = factory(target)
                    events = []
                    class Replace(object):
                        def __int__(self):
                            events.append('int')
                            target[:] = _struct.pack('<2d', 3.0, 4.0)
                            return 0
                    arguments = (source, Replace()) + ((5.0,) if packing else ())
                    if not compiled:
                        arguments = ('<d',) + arguments
                    result = operation(*arguments)
                    self.assertEqual(events, ['int'])
                    if packing:
                        self.assertEqual(str(target), _struct.pack('<2d', 5.0, 4.0))
                    else:
                        self.assertEqual(result, (3.0,))
                    class Resize(object):
                        def __int__(self):
                            target.append(65)
                            return 0
                    arguments = (source, Resize()) + ((5.0,) if packing else ())
                    if not compiled:
                        arguments = ('<d',) + arguments
                    self.assertRaises(BufferError, operation, *arguments)
                    self.assertEqual(len(target), 16)
                    del arguments, source
                    target.append(65)
                    self.assertEqual(len(target), 17)
                for source in (None, 1, u'\0' * 8, '\0' * 8,
                               memoryview('\0' * 8), buffer(bytearray(initial))):
                    events = []
                    arguments = (source, _IntArgument(0, events)) + ((5.0,) if packing else ())
                    if not compiled:
                        arguments = ('<d',) + arguments
                    if packing:
                        self.assertRaises(TypeError, operation, *arguments)
                        self.assertEqual(events, [])
                    elif source is None:
                        self.assertRaises(_struct.error, operation, *arguments)
                        self.assertEqual(events, ['int'])
                    elif isinstance(source, int):
                        self.assertRaises(TypeError, operation, *arguments)
                        self.assertEqual(events, [])
                    else:
                        operation(*arguments)
                        self.assertEqual(events, ['int'])

    def test_struct_float_callbacks_keep_buffer_exported(self):
        import _struct
        for compiled in (False, True):
            operation = _struct.Struct('<d').pack_into if compiled else _struct.pack_into
            for factory in (lambda value: value, memoryview):
                target = bytearray('X' * 8)
                source = factory(target)
                class Number(object):
                    def __float__(self):
                        target.append(65)
                        return 1.0
                arguments = (source, 0, Number())
                if not compiled:
                    arguments = ('<d',) + arguments
                self.assertRaises(_struct.error, operation, *arguments)
                self.assertEqual(str(target), '\0' * 8)
                del arguments, source
                target.append(65)
                self.assertEqual(str(target), '\0' * 8 + 'A')

    def test_struct_unpack_from_keyword_matrix(self):
        import _struct
        source = _struct.pack('<2d', 1.0, 2.0)
        for compiled in (False, True):
            operation = _struct.Struct('<d').unpack_from if compiled else _struct.unpack_from
            prefix = () if compiled else ('<d',)
            self.assertEqual(operation(*prefix, buffer=source), (1.0,))
            self.assertEqual(operation(*prefix, buffer=source, offset=8), (2.0,))
            self.assertEqual(operation(*(prefix + (source,)), offset=8), (2.0,))
            self.assertEqual(operation(*prefix, **{u'buffer': source, u'offset': 8}), (2.0,))
            self.assertRaises(TypeError, operation, *(prefix + (source,)), buffer=source)
            self.assertRaises(TypeError, operation, *(prefix + (source, 0)), offset=0)
            self.assertRaises(TypeError, operation, *prefix, buffer=source, invalid=0)
            self.assertRaises(TypeError, operation, *prefix, offset=0)

    def test_struct_read_buffer_unicode_and_legacy_bounds(self):
        import _struct
        class Unicode(unicode):
            def encode(self, *args):
                raise AssertionError('buffer conversion bypasses encode override')
        for compiled in (False, True):
            struct = _struct.Struct('<d') if compiled else None
            unpack = struct.unpack if compiled else _struct.unpack
            unpack_from = struct.unpack_from if compiled else _struct.unpack_from
            prefix = () if compiled else ('<d',)
            for source in (u'\0' * 8, Unicode(u'\0' * 8), '\0' * 8,
                           bytearray('\0' * 8), memoryview('\0' * 8), buffer('\0' * 8)):
                self.assertEqual(unpack(*(prefix + (source,))), (0.0,))
                self.assertEqual(unpack_from(*(prefix + (source,))), (0.0,))
            for source in (None, 1, [], u'\u00e9' * 8):
                self.assertRaises(_struct.error, unpack, *(prefix + (source,)))
            self.assertRaises(UnicodeEncodeError, unpack_from, *(prefix + (u'\u00e9' * 8,)))
            target = bytearray(_struct.pack('<2d', 1.0, 2.0))
            source = buffer(target)
            class Append(object):
                def __int__(self):
                    target.append(65)
                    return 0
            self.assertEqual(unpack_from(*(prefix + (source, Append()))), (1.0,))
            self.assertEqual(len(target), 17)
            target = bytearray(_struct.pack('<d', 1.0))
            source = buffer(target)
            class Grow(object):
                def __int__(self):
                    target.extend(_struct.pack('<d', 2.0))
                    return 8
            self.assertRaises(_struct.error, unpack_from, *(prefix + (source, Grow())))
            self.assertEqual(len(target), 16)
