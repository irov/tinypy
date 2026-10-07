"""Project-authored live buffer bounds, ownership and first-hash regressions."""

import sys
import unittest


def expected_repr(view, owner, offset, size):
    return '<read-only buffer for %s, size %d, offset %d at %s>' % (
        hex(id(owner)), size, offset, hex(id(view)))


class BufferLifetimeAudit(unittest.TestCase):
    def test_requested_size_observes_source_growth(self):
        for offset in (0, 1, 5):
            source = bytearray('ab')
            view = buffer(source, offset, 10)
            self.assertEqual(str(view), 'ab'[offset:offset + 10])
            source.extend('cdefghi')
            self.assertEqual(str(view), 'abcdefghi'[offset:offset + 10])
            self.assertEqual(len(view), len(str(view)))

    def test_requested_bounds_survive_shrink_and_regrowth(self):
        source = bytearray('abcdef')
        view = buffer(source, 3, 5)
        source[:] = 'a'
        self.assertEqual(str(view), '')
        source.extend('bcdefghijk')
        self.assertEqual(str(view), 'defgh')

    def test_past_end_index_recovers_after_growth(self):
        source = bytearray('ab')
        view = buffer(source, 5, 2)
        with self.assertRaises(IndexError) as error:
            view[0]
        self.assertEqual(error.exception.args, ('buffer index out of range',))
        source.extend('cdefghi')
        self.assertEqual(view[0], 'f')
        self.assertEqual(view[-1], 'g')
        sys.exc_clear()

    def test_finite_requested_size_remains_a_limit(self):
        source = bytearray('a')
        view = buffer(source, 0, 3)
        source.extend('bcdef')
        self.assertEqual(str(view), 'abc')
        self.assertEqual(view[:], 'abc')
        self.assertEqual(view[::-1], 'cba')

    def test_direct_slots_read_current_source(self):
        source = bytearray('a')
        view = buffer(source, 1, 5)
        source.extend('bcdefg')
        self.assertEqual(view.__len__(), 5)
        self.assertEqual(view.__str__(), 'bcdef')
        self.assertEqual(view.__getitem__(0), 'b')
        self.assertEqual(view.__getitem__(slice(1, 4)), 'cde')
        self.assertEqual(view.__getslice__(1, 4), 'cde')

    def test_repr_contains_stored_bounds_and_owner_identity(self):
        for offset, size in ((0, -1), (1, 10), (8, 10), (0, 0)):
            source = bytearray('ab')
            view = buffer(source, offset, size)
            expected = expected_repr(view, source, offset, size)
            self.assertEqual(repr(view), expected)
            self.assertEqual(view.__repr__(), expected)
            source.extend('cdefghi')
            self.assertEqual(repr(view), expected)

    def test_nested_finite_buffer_clips_requested_size(self):
        source = bytearray('ab')
        parent = buffer(source, 1, 10)
        child = buffer(parent, 1, 12)
        self.assertEqual(repr(child), expected_repr(child, source, 2, 9))
        source.extend('cdefghijklmnop')
        self.assertEqual(str(child), 'cdefghijk')

    def test_nested_to_end_buffer_preserves_root_offset(self):
        source = bytearray('ab')
        parent = buffer(source, 5)
        child = buffer(parent, 2)
        grandchild = buffer(child, 1, 3)
        self.assertEqual(repr(child), expected_repr(child, source, 7, -1))
        self.assertEqual(repr(grandchild), expected_repr(grandchild, source, 8, 3))
        source.extend('cdefghijklmnop')
        self.assertEqual(str(child), 'hijklmnop')
        self.assertEqual(str(grandchild), 'ijk')

    def test_nested_past_requested_end_stays_empty(self):
        source = bytearray('ab')
        child = buffer(buffer(source, 1, 3), 5)
        self.assertEqual(repr(child), expected_repr(child, source, 6, 0))
        source.extend('cdefghijklmnop')
        self.assertEqual(str(child), '')

    def test_nested_buffer_retains_root_without_intermediate(self):
        events = []
        class Source(bytearray):
            def __del__(self):
                events.append('released')
        source = Source('abcdef')
        parent = buffer(source, 1, 4)
        child = buffer(parent, 1, 2)
        del source, parent
        self.assertEqual(events, [])
        self.assertEqual(str(child), 'cd')
        del child
        self.assertEqual(events, ['released'])

    def test_unicode_nested_offsets_remain_native_byte_offsets(self):
        for text in (u'', u'ab', u'\xe9\U0001f642'):
            raw = str(buffer(text))
            child = buffer(buffer(text, 1, 6), 2, 10)
            self.assertEqual(str(child), raw[3:7])
            self.assertEqual(repr(child), expected_repr(child, text, 3, 4))

    def test_buffer_bypasses_source_subtype_hooks(self):
        events = []
        class Source(bytearray):
            def __len__(self):
                events.append('len')
                return 100
            def __getitem__(self, key):
                events.append('getitem')
                return 'wrong'
            def __str__(self):
                events.append('str')
                return 'wrong'
            def __repr__(self):
                events.append('repr')
                return 'wrong'
        source = Source('abc')
        view = buffer(source, 1, 2)
        self.assertEqual(str(view), 'bc')
        self.assertEqual(len(view), 2)
        self.assertEqual(view[0], 'b')
        self.assertEqual(repr(view), expected_repr(view, source, 1, 2))
        self.assertEqual(events, [])

    def test_first_hash_survives_same_length_mutation(self):
        source = bytearray('ab')
        view = buffer(source)
        first = hash(view)
        source[0] = ord('z')
        self.assertEqual(str(view), 'zb')
        self.assertEqual(hash(view), first)
        self.assertEqual(view.__hash__(), first)
        self.assertNotEqual(hash(view), hash(str(view)))

    def test_first_hash_survives_growth_shrink_and_regrowth(self):
        source = bytearray('ab')
        view = buffer(source, 1, 10)
        first = view.__hash__()
        for text in ('abcdefghijkl', '', 'XYZ'):
            source[:] = text
            self.assertEqual(hash(view), first)
            self.assertEqual(view.__hash__(), first)
            self.assertEqual(str(view), text[1:11])

    def test_first_hash_uses_current_bytes_at_first_call(self):
        source = bytearray('ab')
        view = buffer(source, 1, 10)
        source.extend('cdefghi')
        self.assertEqual(hash(view), hash('bcdefghi'))
        source[:] = 'XYZ'
        self.assertEqual(hash(view), hash('bcdefghi'))

    def test_nested_buffers_have_independent_hash_caches(self):
        source = bytearray('abc')
        parent = buffer(source, 1)
        child = buffer(parent)
        before = hash(parent)
        source[:] = 'XYZ'
        self.assertEqual(hash(parent), before)
        self.assertEqual(hash(child), hash('YZ'))
        source[:] = '123'
        self.assertEqual(hash(child), hash('YZ'))

    def test_cached_hash_preserves_existing_dictionary_key_lookup(self):
        source = bytearray('ab')
        view = buffer(source)
        mapping = {view: 'stored'}
        source[:] = 'XY'
        self.assertEqual(mapping[view], 'stored')
        self.assertIn(view, mapping)
        self.assertEqual(str(view), 'XY')

    def test_constructor_converts_both_bounds_before_current_read(self):
        source, events = bytearray('ab'), []
        class Offset(object):
            def __int__(self):
                events.append('offset')
                source.extend('cde')
                return 1
        class Size(object):
            def __int__(self):
                events.append('size')
                source.extend('fgh')
                return 5
        view = buffer(source, Offset(), Size())
        self.assertEqual(events, ['offset', 'size'])
        self.assertEqual(str(view), 'bcdef')
        self.assertEqual(repr(view), expected_repr(view, source, 1, 5))

    def test_constructor_callback_exception_identity_and_recovery(self):
        events, failure = [], LookupError('size callback')
        class Offset(object):
            def __int__(self):
                events.append('offset')
                return 1
        class Size(object):
            def __int__(self):
                events.append('size')
                raise failure
        with self.assertRaises(LookupError) as error:
            buffer('abc', Offset(), Size())
        self.assertIs(error.exception, failure)
        self.assertEqual(events, ['offset', 'size'])
        self.assertEqual(str(buffer('abc', 1, 1)), 'b')
        sys.exc_clear()

    def test_legacy_character_consumers_use_encoded_unicode_bounds(self):
        source = u'abc'
        view = buffer(buffer(source, 1, 10), 1, 3)
        self.assertEqual(u'abc'.find(view), 2)
        self.assertEqual('abc'.find(view), 2)
        self.assertEqual(u'ab'.center(4, view), u'cabc')
        self.assertEqual(str(view), str(buffer(source))[2:5])

    def test_numeric_constructors_use_character_buffer(self):
        source = buffer(u'12', 1, 10)
        self.assertEqual(int(source), 2)
        self.assertEqual(long(source), 2L)
        self.assertEqual(float(source), 2.0)
        self.assertEqual(float(buffer(u'1.5')), 1.5)
        self.assertEqual(len(buffer(u'12')), 8)

    def test_numeric_constructors_mask_character_encoding_failure(self):
        for constructor, message in (
                (int, "int() argument must be a string or a number, not 'buffer'"),
                (long, "long() argument must be a string or a number, not 'buffer'"),
                (float, 'float() argument must be a string or a number')):
            for size in (-1, 0):
                with self.assertRaises(TypeError) as error:
                    constructor(buffer(u'\xe9', 0, size))
                self.assertEqual(error.exception.args, (message,))
            self.assertEqual(constructor(buffer(u'12')), constructor('12'))
        sys.exc_clear()

    def test_explicit_base_rejects_buffer_before_character_encoding(self):
        for constructor in (int, long):
            with self.assertRaises(TypeError) as error:
                constructor(buffer(u'\xe9'), 10)
            self.assertEqual(error.exception.args, (
                "%s() can't convert non-string with explicit base" % constructor.__name__,))
        sys.exc_clear()

    def test_character_buffer_bypasses_unicode_subtype_hooks(self):
        events = []
        class Source(unicode):
            def __str__(self):
                events.append('str')
                return 'wrong'
            def __unicode__(self):
                events.append('unicode')
                return u'wrong'
            def encode(self, *args):
                events.append('encode')
                return 'wrong'
        view = buffer(Source(u'12'))
        self.assertEqual(int(view), 12)
        self.assertEqual(float(view), 12.0)
        self.assertEqual('12'.find(view), 0)
        self.assertEqual(events, [])

    def test_nul_invalid_prefix_precedes_embedded_nul_diagnostic(self):
        for factory in (str, bytearray, buffer):
            for constructor in (int, long):
                with self.assertRaises(ValueError) as error:
                    constructor(factory('abc\x00'))
                self.assertEqual(error.exception.args, (
                    "invalid literal for %s() with base 10: 'abc'" % constructor.__name__,))
                with self.assertRaises(ValueError) as error:
                    constructor(factory('1\x00'))
                self.assertEqual(error.exception.args, (
                    'null byte in argument for %s()' % constructor.__name__,))
        sys.exc_clear()

    def test_float_nul_diagnostic_depends_on_numeric_prefix(self):
        for factory in (str, bytearray, buffer):
            for text, message in (
                    ('\x00', 'could not convert string to float: '),
                    ('abc\x00', 'could not convert string to float: abc'),
                    ('1x\x00', 'invalid literal for float(): 1x'),
                    ('1e\x00', 'invalid literal for float(): 1e')):
                with self.assertRaises(ValueError) as error:
                    float(factory(text))
                self.assertEqual(error.exception.args, (message,))
        sys.exc_clear()

    def test_explicit_base_nul_error_uses_full_original_string(self):
        for constructor in (int, long):
            for text in ('abc\x00', '1\x00', ' \x00'):
                for base in (0, 2, 10, 36):
                    with self.assertRaises(ValueError) as error:
                        constructor(text, base)
                    self.assertEqual(error.exception.args, (
                        'invalid literal for %s() with base %d: %s' % (
                            constructor.__name__, base, repr(text)),))
        sys.exc_clear()
