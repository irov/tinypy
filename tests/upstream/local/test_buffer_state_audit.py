"""Project-authored buffer snapshots, mutable arguments and export recovery."""

import sys
import unittest


def native_unicode_bytes(text):
    units = []
    for character in text:
        code = ord(character)
        octets = [chr((code >> shift) & 255) for shift in (0, 8, 16, 24)]
        if sys.byteorder == 'big':
            octets.reverse()
        units.append(''.join(octets))
    return ''.join(units)


class BufferStateAudit(unittest.TestCase):
    def test_unicode_buffer_uses_native_wide_storage(self):
        for text in (u'', u'a', u'a\x00\xe9', u'\u4e2d', u'\U0001f642', u'\ud800'):
            expected = native_unicode_bytes(text)
            self.assertEqual(str(buffer(text)), expected)
            self.assertEqual(len(buffer(text)), len(text) * 4)
            self.assertEqual(memoryview(buffer(text)).tobytes(), expected)
            self.assertEqual(bytearray(buffer(text)), bytearray(expected))

    def test_unicode_buffer_offsets_and_children_use_bytes(self):
        expected = native_unicode_bytes(u'a\u4e2d')
        source = buffer(u'a\u4e2d', 1, 5)
        self.assertEqual(str(source), expected[1:6])
        self.assertEqual(str(buffer(source, 1, 3)), expected[2:5])
        self.assertEqual(source[::-1], expected[1:6][::-1])

    def test_unicode_buffer_keeps_subtype_owner_and_independent_cache(self):
        events = []
        class Text(unicode):
            def __del__(self):
                events.append('released')
        original = u'\xe9\U0001f642'
        self.assertEqual(str(buffer(original)), native_unicode_bytes(original))
        source = Text(original)
        view = buffer(source)
        child = memoryview(view)[1:]
        del source, view
        self.assertEqual(events, [])
        self.assertEqual(child.tobytes(), native_unicode_bytes(original)[1:])
        del child
        self.assertEqual(events, ['released'])
        self.assertEqual(str(buffer(original)), native_unicode_bytes(original))

    def test_buffer_concatenates_native_unicode_and_preserves_empty_identity(self):
        source = u'a\u4e2d'
        self.assertIs(buffer('').__add__(source), source)
        self.assertEqual(buffer('x') + source, 'x' + native_unicode_bytes(source))
        self.assertEqual(buffer('x').__add__(source), 'x' + native_unicode_bytes(source))

    def test_buffer_direct_concat_rejects_unsupported_source(self):
        for source in (None, 3, [], memoryview('x')):
            with self.assertRaises(TypeError) as error:
                buffer('').__add__(source)
            self.assertEqual(error.exception.args, ('bad argument type for built-in operation',))
        sys.exc_clear()

    def test_buffer_constructor_and_index_diagnostics(self):
        with self.assertRaises(TypeError) as error:
            buffer(memoryview('x'))
        self.assertEqual(error.exception.args, ('buffer object expected',))
        with self.assertRaises(TypeError) as error:
            buffer('x')[None]
        self.assertEqual(error.exception.args, ('sequence index must be integer',))
        with self.assertRaises(IndexError) as error:
            buffer('x')[3]
        self.assertEqual(error.exception.args, ('buffer index out of range',))
        with self.assertRaises(TypeError) as error:
            buffer('x').__mul__('2')
        self.assertEqual(error.exception.args, ("'str' object cannot be interpreted as an index",))
        sys.exc_clear()

    def test_memoryview_invalid_key_uses_type_name(self):
        for key, name in ((None, 'NoneType'), (1.5, 'float'), ('x', 'str'), ((), 'tuple')):
            view = memoryview(bytearray('ab'))
            with self.assertRaises(TypeError) as error:
                view[key]
            self.assertEqual(error.exception.args, ('cannot index memory using "%s"' % name,))
            with self.assertRaises(TypeError) as error:
                view[key] = 'x'
            self.assertEqual(error.exception.args, ('cannot index memory using "%s"' % name,))
        sys.exc_clear()

    def test_memoryview_assignment_uses_buffer_and_size_diagnostics(self):
        target = bytearray('ab')
        view = memoryview(target)
        for source, name in ((None, 'NoneType'), (3, 'int'), (u'x', 'unicode')):
            with self.assertRaises(TypeError) as error:
                view[0] = source
            self.assertEqual(error.exception.args, ("'%s' does not have the buffer interface" % name,))
        for source in ('', 'xy'):
            with self.assertRaises(ValueError) as error:
                view[0] = source
            self.assertEqual(error.exception.args, ('cannot modify size of memoryview object',))
        view[0] = memoryview('z')
        self.assertEqual(str(target), 'zb')
        del view
        target.append(33)
        self.assertEqual(str(target), 'zb!')
        sys.exc_clear()

    def test_bytearray_bounds_callbacks_observe_current_receiver(self):
        for method in ('find', 'rfind', 'count', 'startswith', 'endswith'):
            target, events = bytearray('aba'), []
            class Bound(object):
                def __index__(self):
                    events.append('index')
                    target[:] = 'ccc'
                    return 0
            result = getattr(target, method)('a', Bound())
            self.assertEqual(result, 0 if method == 'count' else False if method in ('startswith', 'endswith') else -1)
            self.assertEqual(events, ['index'])
            self.assertEqual(str(target), 'ccc')

    def test_bytearray_bounds_callbacks_observe_current_argument(self):
        for method in ('find', 'rfind', 'index', 'rindex', 'count'):
            target, needle, events = bytearray('aba'), bytearray('a'), []
            class Bound(object):
                def __index__(self):
                    events.append('index')
                    needle[:] = 'b'
                    return 0
            self.assertEqual(getattr(target, method)(needle, Bound()), 1)
            self.assertEqual(events, ['index'])

    def test_bytearray_index_error_and_callback_error_recovery(self):
        for method in ('index', 'rindex'):
            with self.assertRaises(ValueError) as error:
                getattr(bytearray('aba'), method)('z')
            self.assertEqual(error.exception.args, ('subsection not found',))
        failure = LookupError('bound')
        class Bound(object):
            def __index__(self):
                raise failure
        for method in ('find', 'rfind', 'count', 'startswith'):
            with self.assertRaises(LookupError) as error:
                getattr(bytearray('aba'), method)(None, Bound())
            self.assertIs(error.exception, failure)
        self.assertEqual(bytearray('aba').find('b'), 1)
        sys.exc_clear()

    def test_bytearray_integer_callbacks_precede_receiver_snapshot(self):
        for method in ('center', 'ljust', 'rjust', 'zfill', 'expandtabs', 'replace', 'split', 'rsplit', 'splitlines'):
            target, events = bytearray('aba'), []
            class Number(object):
                def __int__(self):
                    events.append('int')
                    target[:] = 'CCC'
                    return 2
            if method == 'replace':
                result = target.replace('a', 'x', Number())
            elif method in ('split', 'rsplit'):
                result = getattr(target, method)('b', Number())
            else:
                result = getattr(target, method)(Number())
            self.assertEqual([str(item) for item in result] if isinstance(result, list) else str(result), ['CCC'] if isinstance(result, list) else 'CCC')
            self.assertEqual(events, ['int'])

    def test_bytearray_replace_copies_mutable_arguments_after_count(self):
        old, replacement, events = bytearray('a'), bytearray('x'), []
        class Number(object):
            def __int__(self):
                old[:] = 'b'
                replacement[:] = 'y'
                events.append('int')
                return 2
        self.assertEqual(bytearray('aba').replace(old, replacement, Number()), bytearray('aya'))
        self.assertEqual(events, ['int'])

    def test_bytearray_join_observes_separator_after_materialization(self):
        target, events = bytearray('-'), []
        class Items(object):
            def __iter__(self):
                target[:] = '_'
                events.append('iter')
                return iter(['a', 'b'])
            def __len__(self):
                raise AssertionError('hint belongs to returned iterator')
        self.assertEqual(target.join(Items()), bytearray('a_b'))
        self.assertEqual(events, ['iter'])

    def test_bytearray_join_keeps_items_until_materialization_finishes(self):
        item, events = bytearray('a'), []
        def values():
            events.append('first')
            yield item
            item[:] = 'z'
            events.append('second')
            yield 'b'
        self.assertEqual(bytearray('-').join(values()), bytearray('z-b'))
        self.assertEqual(events, ['first', 'second'])

    def test_bytearray_join_drains_before_validation_and_preserves_tail_error(self):
        events = []
        failure = LookupError('tail')
        def values():
            events.append('first')
            yield None
            events.append('tail')
            raise failure
        with self.assertRaises(LookupError) as error:
            bytearray('-').join(values())
        self.assertIs(error.exception, failure)
        self.assertEqual(events, ['first', 'tail'])
        self.assertEqual(bytearray('-').join(['a', 'b']), bytearray('a-b'))
        sys.exc_clear()

    def test_bytearray_join_requires_bytes_items(self):
        for source, name in ((u'a', 'unicode'), (buffer('a'), 'buffer'), (memoryview('a'), 'memoryview')):
            with self.assertRaises(TypeError) as error:
                bytearray('-').join(['a', source])
            self.assertEqual(error.exception.args, ("can only join an iterable of bytes (item 1 has type '%s')" % name,))
        with self.assertRaises(TypeError) as error:
            bytearray('-').join(None)
        self.assertEqual(error.exception.args, ('can only join an iterable',))
        sys.exc_clear()

    def test_bytearray_join_negative_hint_and_user_error_identity(self):
        events = []
        failure = SystemError('error return without exception set')
        class Items(object):
            raised = False
            def __iter__(self):
                events.append('iter')
                return self
            def __length_hint__(self):
                events.append('hint')
                if self.raised:
                    raise failure
                return -1
            def next(self):
                raise AssertionError('hint failure stops consumption')
        for raised in (False, True):
            source = Items()
            source.raised = raised
            with self.assertRaises(SystemError) as error:
                bytearray('-').join(source)
            self.assertEqual(error.exception.args, failure.args)
            self.assertEqual(error.exception is failure, raised)
        self.assertEqual(events, ['iter', 'iter', 'hint'] * 2)
        sys.exc_clear()

    def test_fromhex_reports_first_position_and_unicode_encoding(self):
        for text, position in (('x1', 0), ('1x', 0), ('61 6x', 3), ('61 6', 3), ('\t61', 0)):
            with self.assertRaises(ValueError) as error:
                bytearray.fromhex(text)
            self.assertEqual(error.exception.args, ('non-hexadecimal number found in fromhex() arg at position %d' % position,))
        self.assertEqual(bytearray.fromhex(u'61 62'), bytearray('ab'))
        self.assertRaises(UnicodeEncodeError, bytearray.fromhex, u'\xe9')
        sys.exc_clear()

    def test_empty_unicode_results_share_canonical_value(self):
        empty = u''
        self.assertIs(empty.lower(), empty)
        self.assertIs(empty.expandtabs(), empty)
        self.assertIs(u'a'[:0], empty)
        self.assertIs(unicode(), empty)


if __name__ == '__main__':
    unittest.main()
