"""Project-authored bytearray construction, hint and callback state checks."""

import _codecs
import sys
import unittest


class Bytes(bytearray):
    pass


class SequenceConstructorAudit(unittest.TestCase):
    def test_extend_uses_source_hint_after_obtaining_iterator(self):
        events = []
        class Cursor(object):
            def __init__(self):
                self.items = iter([65, 66])
            def __iter__(self):
                events.append('cursor.iter')
                return self
            def next(self):
                events.append('next')
                return next(self.items)
            def __length_hint__(self):
                raise AssertionError('hint belongs to source')
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return Cursor()
            def __length_hint__(self):
                events.append('hint')
                return 2
        value = bytearray('x')
        self.assertIs(value.extend(Source()), None)
        self.assertEqual(value, bytearray('xAB'))
        self.assertEqual(events, ['iter', 'hint', 'next', 'next', 'next'])

    def test_extend_hint_callback_growth_precedes_destination_append(self):
        value = bytearray('x')
        class Source(object):
            def __iter__(self):
                return iter([65])
            def __length_hint__(self):
                value.append(121)
                return 2
        value.extend(Source())
        self.assertEqual(value, bytearray('xyA'))

    def test_extend_negative_hint_minus_one_aborts_before_iteration(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([65])
            def __length_hint__(self):
                events.append('hint')
                return -1
        value = bytearray('x')
        with self.assertRaises(SystemError) as error:
            value.extend(Source())
        self.assertEqual(error.exception.args, ('error return without exception set',))
        self.assertEqual(events, ['iter', 'hint'])
        self.assertEqual(value, bytearray('x'))
        value.extend('y')
        self.assertEqual(value, bytearray('xy'))
        sys.exc_clear()

    def test_extend_other_negative_hints_report_bytearray_allocation_reason(self):
        for hint in (-2, -3):
            class Source(object):
                def __iter__(self):
                    return iter([65])
                def __length_hint__(self):
                    return hint
            value = Bytes('x')
            with self.assertRaises(SystemError) as error:
                value.extend(Source())
            self.assertEqual(error.exception.args,
                             ('Negative size passed to PyByteArray_FromStringAndSize',))
            self.assertEqual(value, bytearray('x'))
        sys.exc_clear()

    def test_unbound_extend_negative_hint_uses_call_diagnostic(self):
        class Source(object):
            def __iter__(self):
                return iter([65])
            def __length_hint__(self):
                return -1
        value = bytearray('x')
        with self.assertRaises(SystemError) as error:
            bytearray.extend(value, Source())
        self.assertEqual(error.exception.args, ('NULL result without error in PyObject_Call',))
        self.assertEqual(value, bytearray('x'))
        sys.exc_clear()

    def test_extend_negative_length_fails_before_hint(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([65])
            def __len__(self):
                events.append('len')
                return -1
            def __length_hint__(self):
                raise AssertionError('negative length is not a missing length')
        value = bytearray('x')
        with self.assertRaises(ValueError) as error:
            value.extend(Source())
        self.assertEqual(error.exception.args, ('__len__() should return >= 0',))
        self.assertEqual(events, ['iter', 'len'])
        self.assertEqual(value, bytearray('x'))
        sys.exc_clear()

    def test_extend_preserves_user_hint_failure_identity(self):
        marker = RuntimeError('hint marker')
        class Source(object):
            def __iter__(self):
                return iter([65])
            def __length_hint__(self):
                raise marker
        value = bytearray('x')
        with self.assertRaises(RuntimeError) as error:
            value.extend(Source())
        self.assertIs(error.exception, marker)
        self.assertEqual(value, bytearray('x'))
        value.append(66)
        self.assertEqual(value, bytearray('xB'))
        sys.exc_clear()

    def test_extend_empty_source_releases_positive_hint_storage(self):
        class Source(object):
            def __iter__(self):
                return iter([])
            def __length_hint__(self):
                return 4
        value = bytearray('x')
        value.extend(Source())
        self.assertEqual(value, bytearray('x'))

    def test_constructor_and_reinit_do_not_request_length_hint(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([65, 66])
            def __length_hint__(self):
                raise AssertionError('construction does not ask for hint')
        self.assertEqual(bytearray(Source()), bytearray('AB'))
        value = bytearray('old')
        value.__init__(Source())
        self.assertEqual(value, bytearray('AB'))
        self.assertEqual(events, ['iter', 'iter'])

    def test_constructor_wrong_index_result_falls_back_to_iteration(self):
        events = []
        class Source(object):
            def __index__(self):
                events.append('index')
                return 1.5
            def __iter__(self):
                events.append('iter')
                return iter([65, 66])
        self.assertEqual(bytearray(Source()), bytearray('AB'))
        self.assertEqual(events, ['index', 'iter'])

    def test_reinit_index_type_error_falls_back_after_clearing_receiver(self):
        value = Bytes('old')
        events = []
        class Source(object):
            def __index__(self):
                events.append(('index', str(value)))
                raise TypeError('index marker')
            def __iter__(self):
                events.append(('iter', str(value)))
                return iter([65])
        self.assertIs(bytearray.__init__(value, Source()), None)
        self.assertEqual(value, bytearray('A'))
        self.assertEqual(events, [('index', ''), ('iter', '')])

    def test_slice_assignment_materialization_uses_index_fallback(self):
        class Source(object):
            def __index__(self):
                raise TypeError('index marker')
            def __iter__(self):
                return iter([65, 66])
        value = bytearray('xyz')
        value[1:2] = Source()
        self.assertEqual(value, bytearray('xABz'))

    def test_constructor_non_type_index_failure_preserves_identity(self):
        marker = ValueError('index marker')
        class Source(object):
            def __index__(self):
                raise marker
            def __iter__(self):
                raise AssertionError('must not fall back')
        with self.assertRaises(ValueError) as error:
            bytearray(Source())
        self.assertIs(error.exception, marker)
        self.assertEqual(bytearray([65]), bytearray('A'))
        sys.exc_clear()

    def test_constructor_valid_index_takes_priority_over_iteration(self):
        class Source(object):
            def __index__(self):
                return 2
            def __iter__(self):
                raise AssertionError('valid count has priority')
        self.assertEqual(bytearray(Source()), bytearray('\x00\x00'))

    def test_byte_string_encoding_is_checked_even_when_payload_is_unchanged(self):
        with self.assertRaises(LookupError) as error:
            bytearray('ab', 'sequence_audit_missing_encoding')
        self.assertEqual(error.exception.args,
                         ('unknown encoding: sequence_audit_missing_encoding',))
        self.assertEqual(bytearray('ab', 'hex'), bytearray('ab'))
        sys.exc_clear()

    def test_byte_string_reinit_codec_failure_leaves_cleared_receiver(self):
        value = bytearray('old')
        with self.assertRaises(LookupError):
            value.__init__('ab', 'sequence_audit_missing_encoding')
        self.assertEqual(value, bytearray())
        value.append(65)
        self.assertEqual(value, bytearray('A'))
        sys.exc_clear()

    def test_bytearray_hex_codec_rejects_non_strict_errors_before_conversion(self):
        for source in ('ab', u'\xff'):
            with self.assertRaises(AssertionError) as error:
                bytearray(source, 'hex', 'ignore')
            self.assertEqual(error.exception.args, ())
        sys.exc_clear()

    def test_byte_string_custom_encoder_is_called_and_output_is_discarded(self):
        events = []
        def encode(source, errors='strict'):
            events.append((source, errors))
            return 'converted', len(source)
        def search(name):
            if name == 'sequence_audit_bytes_encoder':
                return encode, encode, None, None
        _codecs.register(search)
        self.assertEqual(bytearray('ab', 'sequence_audit_bytes_encoder', 'ignore'),
                         bytearray('ab'))
        self.assertEqual(events, [('ab', 'ignore')])

    def test_reinit_codec_callback_growth_is_kept_before_original_bytes(self):
        value = bytearray('old')
        events = []
        def encode(source, errors='strict'):
            events.append(str(value))
            value.extend('x')
            return 'converted', len(source)
        def search(name):
            if name == 'sequence_audit_growth_encoder':
                return encode, encode, None, None
        _codecs.register(search)
        value.__init__('ab', 'sequence_audit_growth_encoder')
        self.assertEqual(events, [''])
        self.assertEqual(value, bytearray('xab'))

    def test_discarded_encoder_result_is_released_after_original_bytes_append(self):
        value = bytearray('old')
        events = []
        class Encoded(str):
            def __del__(self):
                events.append(str(value))
        def encode(source, errors='strict'):
            return Encoded('ignored'), len(source)
        def search(name):
            if name == 'sequence_audit_release_encoder':
                return encode, encode, None, None
        _codecs.register(search)
        value.__init__('ab', 'sequence_audit_release_encoder')
        self.assertEqual(value, bytearray('ab'))
        self.assertEqual(events, ['ab'])

    def test_append_captures_length_before_value_callback_growth(self):
        value = bytearray('ab')
        class Item(object):
            def __index__(self):
                value.extend('xy')
                return 65
        value.append(Item())
        self.assertEqual(value, bytearray('abA'))

    def test_unbound_insert_captures_length_before_index_callback_growth(self):
        value = Bytes('ab')
        class Index(object):
            def __int__(self):
                value.extend('xy')
                return -1
        bytearray.insert(value, Index(), 65)
        self.assertEqual(value, bytearray('aAb'))

    def test_insert_captures_length_before_value_callback_growth(self):
        value = bytearray('ab')
        class Item(object):
            def __index__(self):
                value.extend('xy')
                return 65
        value.insert(1, Item())
        self.assertEqual(value, bytearray('aAb'))

    def test_append_failed_conversion_preserves_callback_growth_and_identity(self):
        value = bytearray('ab')
        marker = RuntimeError('value marker')
        class Item(object):
            def __index__(self):
                value.extend('xy')
                raise marker
        with self.assertRaises(RuntimeError) as error:
            value.append(Item())
        self.assertIs(error.exception, marker)
        self.assertEqual(value, bytearray('abxy'))
        value.append(65)
        self.assertEqual(value, bytearray('abxyA'))
        sys.exc_clear()
