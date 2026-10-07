"""Project-authored native buffer and bytearray argument regressions."""

import unittest


def error(callback, *args, **kwargs):
    try:
        callback(*args, **kwargs)
    except BaseException as failure:
        return type(failure).__name__, failure.args
    raise AssertionError('operation must raise')


class BufferArgumentsAudit(unittest.TestCase):
    def test_buffer_zero_argument_wrappers(self):
        value = buffer('ab')
        for name in ('__len__', '__hash__', '__str__', '__repr__'):
            callback = getattr(value, name)
            self.assertEqual(error(callback, 1),
                             ('TypeError', ('expected 0 arguments, got 1',)))
            self.assertEqual(error(callback, extra=1),
                             ('TypeError', ("wrapper %s doesn't take keyword arguments" % name,)))

    def test_memoryview_wrappers_and_native_methods_have_distinct_counts(self):
        value = memoryview('ab')
        for name in ('__len__', '__repr__'):
            self.assertEqual(error(getattr(value, name), 1),
                             ('TypeError', ('expected 0 arguments, got 1',)))
        for name in ('tobytes', 'tolist'):
            self.assertEqual(error(getattr(value, name), 1),
                             ('TypeError', ('%s() takes no arguments (1 given)' % name,)))
            self.assertEqual(error(getattr(value, name), extra=1),
                             ('TypeError', ('%s() takes no keyword arguments' % name,)))

    def test_buffer_readonly_slots_validate_arity_before_readonly(self):
        value = buffer('ab')
        for name, message in (('__setitem__', ' expected 2 arguments, got 0'),
                              ('__delitem__', 'expected 1 arguments, got 0'),
                              ('__setslice__', 'function takes exactly 3 arguments (0 given)'),
                              ('__delslice__', 'function takes exactly 2 arguments (0 given)')):
            self.assertEqual(error(getattr(value, name)), ('TypeError', (message,)))
            self.assertEqual(error(getattr(value, name), extra=1),
                             ('TypeError', ("wrapper %s doesn't take keyword arguments" % name,)))

    def test_buffer_readonly_mapping_rejects_before_key_conversion(self):
        events = []
        class Index(object):
            def __index__(self):
                events.append('index')
                return 0
        value = buffer('ab')
        self.assertEqual(error(value.__setitem__, Index(), 'z'),
                         ('TypeError', ('buffer is read-only',)))
        self.assertEqual(error(value.__delitem__, 'wrong'),
                         ('TypeError', ('buffer is read-only',)))
        self.assertEqual(events, [])

    def test_buffer_legacy_slices_convert_int_and_clamp_negative(self):
        events = []
        class Integer(object):
            def __int__(self):
                events.append('int')
                return -1
        self.assertEqual(buffer('abc').__getslice__(Integer(), 2), 'ab')
        self.assertEqual(events, ['int'])
        self.assertEqual(error(buffer('abc').__getslice__, 0, 'bad'),
                         ('TypeError', ('an integer is required',)))

    def test_buffer_readonly_legacy_slices_convert_bounds_first(self):
        failure = ValueError('bound')
        class Integer(object):
            def __int__(self):
                raise failure
        try:
            buffer('abc').__setslice__(Integer(), 2, 'z')
        except ValueError as caught:
            self.assertIs(caught, failure)
        else:
            self.fail('bound failure must propagate')
        self.assertEqual(error(buffer('abc').__delslice__, 0, 'bad'),
                         ('TypeError', ('an integer is required',)))

    def test_buffer_comparison_reports_right_type(self):
        for value in (1, 'a', None):
            self.assertEqual(error(buffer('a').__cmp__, value), ('TypeError',
                ("buffer.__cmp__(x,y) requires y to be a 'buffer', not a '%s'" % type(value).__name__,)))

    def test_buffer_constructor_arity_and_keywords(self):
        self.assertEqual(error(buffer), ('TypeError', ('buffer() takes at least 1 argument (0 given)',)))
        self.assertEqual(error(buffer, 'a', 0, 1, 2),
                         ('TypeError', ('buffer() takes at most 3 arguments (4 given)',)))
        self.assertEqual(error(buffer, object='a'),
                         ('TypeError', ('buffer() does not take keyword arguments',)))

    def test_memoryview_accepts_object_keyword(self):
        value = bytearray('ab')
        view = memoryview(object=value)
        self.assertEqual(view.tobytes(), 'ab')
        self.assertFalse(view.readonly)
        view[0] = 'z'
        self.assertEqual(value, bytearray('zb'))

    def test_memoryview_missing_duplicate_and_extra_keywords(self):
        self.assertEqual(error(memoryview),
                         ('TypeError', ("Required argument 'object' (pos 1) not found",)))
        for kwargs in ({'object': 'b'}, {'unknown': 2}):
            self.assertEqual(error(memoryview, 'a', **kwargs),
                             ('TypeError', ('memoryview() takes at most 1 argument (2 given)',)))

    def test_bytearray_single_argument_native_methods(self):
        for name in ('append', 'extend', 'remove'):
            self.assertEqual(error(getattr(bytearray(), name)),
                             ('TypeError', ('%s() takes exactly one argument (0 given)' % name,)))
            self.assertEqual(error(getattr(bytearray(), name), 1, extra=2),
                             ('TypeError', ('%s() takes no keyword arguments' % name,)))

    def test_bytearray_native_parsed_and_noargs_counts(self):
        for name in ('reverse', '__alloc__', 'upper', 'lower', 'capitalize', 'isalnum'):
            self.assertEqual(error(getattr(bytearray('ab'), name), 1),
                             ('TypeError', ('%s() takes no arguments (1 given)' % name,)))
        self.assertEqual(error(bytearray().insert, 1),
                         ('TypeError', ('insert() takes exactly 2 arguments (1 given)',)))
        self.assertEqual(error(bytearray('a').pop, 1, 2),
                         ('TypeError', ('pop() takes at most 1 argument (2 given)',)))

    def test_bytearray_search_methods_share_count_diagnostic(self):
        for name in ('find', 'rfind', 'index', 'rindex'):
            self.assertEqual(error(getattr(bytearray('ab'), name)), ('TypeError',
                ('find/rfind/index/rindex() takes at least 1 argument (0 given)',)))

    def test_bytearray_bridge_methods_validate_before_conversion(self):
        events = []
        class Integer(object):
            def __int__(self):
                events.append('int')
                return 1
        self.assertEqual(error(bytearray('a').replace, 'a', 'b', Integer(), 0),
                         ('TypeError', ('replace() takes at most 3 arguments (4 given)',)))
        self.assertEqual(error(bytearray('a').split, 'a', Integer(), extra=1),
                         ('TypeError', ('split() takes no keyword arguments',)))
        self.assertEqual(events, [])

    def test_bytearray_concat_slot_error_orientation(self):
        self.assertEqual(error(bytearray('a').__add__, 1),
                         ('TypeError', ("can't concat bytearray to int",)))
        self.assertEqual(error(bytearray('a').__iadd__, 1),
                         ('TypeError', ("can't concat int to bytearray",)))
        self.assertEqual('a' + bytearray('b'), bytearray('ab'))

    def test_bytearray_repeat_rejects_missing_index_hook(self):
        for name in ('__mul__', '__rmul__', '__imul__'):
            self.assertEqual(error(getattr(bytearray('ab'), name), 'bad'),
                             ('TypeError', ("'str' object cannot be interpreted as an index",)))
            self.assertEqual(error(getattr(bytearray('ab'), name)),
                             ('TypeError', (' expected 1 arguments, got 0',)))

    def test_bytearray_bounds_require_index_protocol(self):
        for name in ('find', 'rfind', 'index', 'rindex', 'count', 'startswith', 'endswith'):
            self.assertEqual(error(getattr(bytearray('ab'), name), 'a', 'bad'),
                             ('TypeError', ('slice indices must be integers or None or have an __index__ method',)))

    def test_bytearray_prefix_tuple_short_circuits_buffer_validation(self):
        value = bytearray('ab')
        self.assertTrue(value.startswith(('a', u'bad')))
        self.assertTrue(value.endswith(('b', u'bad')))
        self.assertEqual(error(value.startswith, (u'bad', 'a')),
                         ('TypeError', ("Type unicode doesn't support the buffer API",)))

    def test_bytearray_prefix_reads_current_storage_after_bound_callback(self):
        value = bytearray('ab')
        prefix = bytearray('a')
        events = []
        class Index(object):
            def __index__(self):
                events.append('index')
                value[:] = 'zb'
                prefix[:] = 'z'
                return 0
        self.assertTrue(value.startswith(prefix, Index()))
        self.assertEqual(events, ['index'])
        self.assertFalse(value.startswith('', 10))

    def test_bytearray_bridge_reports_buffer_api_errors(self):
        for name in ('split', 'rsplit', 'strip', 'lstrip', 'rstrip', 'count', 'translate'):
            self.assertEqual(error(getattr(bytearray('ab'), name), 1),
                             ('TypeError', ("Type int doesn't support the buffer API",)))
        for name in ('partition', 'rpartition'):
            self.assertEqual(error(getattr(bytearray('ab'), name), 1),
                             ('TypeError', ("'int' does not have the buffer interface",)))
        self.assertEqual(error(bytearray('a').replace, 'a', u'b'),
                         ('TypeError', ("Type unicode doesn't support the buffer API",)))

    def test_bytearray_translate_checks_table_before_delete_characters(self):
        self.assertEqual(error(bytearray('ab').translate, 'short', object()),
                         ('ValueError', ('translation table must be 256 characters long',)))
        self.assertEqual(error(bytearray('ab').translate, None, object()),
                         ('TypeError', ("Type object doesn't support the buffer API",)))
        self.assertEqual(bytearray('ab').translate(None, buffer('a')), bytearray('b'))

    def test_bytearray_decode_keywords_and_count_priority(self):
        self.assertEqual(bytearray('ab').decode(encoding='ascii', errors='strict'), u'ab')
        self.assertEqual(error(bytearray('ab').decode, 'ascii', 'strict', extra=1),
                         ('TypeError', ('decode() takes at most 2 arguments (3 given)',)))

    def test_bytearray_fromhex_accepts_readonly_legacy_buffer(self):
        class Bytes(bytearray):
            pass
        self.assertEqual(bytearray.fromhex(buffer('61 62')), bytearray('ab'))
        self.assertIs(type(Bytes.fromhex(buffer('61'))), bytearray)
        for value, name in ((None, 'None'), (bytearray('61'), 'bytearray'), (memoryview('61'), 'memoryview')):
            self.assertEqual(error(bytearray.fromhex, value), ('TypeError',
                ('fromhex() argument 1 must be string or read-only buffer, not %s' % name,)))

    def test_unknown_encoding_error_retains_original_name(self):
        for value in ('ab', u'ab', bytearray('ab')):
            self.assertEqual(error(value.decode if isinstance(value, (str, bytearray)) else value.encode, 'Xx-Yy'),
                             ('LookupError', ('unknown encoding: Xx-Yy',)))

    def test_bytearray_item_conversion_replaces_only_type_errors(self):
        class Index(object):
            def __index__(self):
                raise TypeError('callback type error')
        for name in ('append', 'remove'):
            for value in (None, u'a', ('a',), Index()):
                self.assertEqual(error(getattr(bytearray('ab'), name), value),
                                 ('TypeError', ('an integer or string of size 1 is required',)))
        failure = RuntimeError('callback identity')
        class Raising(object):
            def __index__(self):
                raise failure
        try:
            bytearray('ab').append(Raising())
        except RuntimeError as caught:
            self.assertIs(caught, failure)
        else:
            self.fail('expected the callback error')

    def test_bytearray_padding_requires_one_byte_string_character(self):
        for name in ('center', 'ljust', 'rjust'):
            for value, kind in ((1, 'int'), (None, 'None'), ('', 'str'), ('aa', 'str'), (u'a', 'unicode')):
                self.assertEqual(error(getattr(bytearray('ab'), name), 5, value),
                                 ('TypeError', ('%s() argument 2 must be char, not %s' % (name, kind),)))
            self.assertEqual(len(getattr(bytearray('ab'), name)(5, 'x')), 5)

    def test_bytearray_concat_errors_name_the_receiver_subtype(self):
        class Bytes(bytearray):
            pass
        value = Bytes('ab')
        self.assertEqual(error(value.__add__, 1), ('TypeError', ("can't concat Bytes to int",)))
        self.assertEqual(error(value.__iadd__, 1), ('TypeError', ("can't concat int to Bytes",)))
        self.assertEqual(value, bytearray('ab'))

    def test_memoryview_missing_object_precedes_unknown_keyword(self):
        self.assertEqual(error(memoryview, extra=1),
                         ('TypeError', ("Required argument 'object' (pos 1) not found",)))
