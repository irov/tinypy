"""Project-authored Python 2 iterator builtin and container callback witnesses."""

import unittest
import _codecs
import _struct
import _weakref as weakref
import sys


def error_text(function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except Exception as error:
        return type(error).__name__ + ': ' + str(error)
    raise AssertionError('no exception raised')


class BuiltinEdges(unittest.TestCase):
    def test_length_float_conversion_prevents_hint_fallback(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([2, 1])
            def __len__(self):
                events.append('len')
                return 2.75
            def __length_hint__(self):
                raise AssertionError('length is available')
        operations = [(list, [2, 1]), (tuple, (2, 1)),
                      (sorted, [1, 2]), (lambda x: filter(None, x), [2, 1]),
                      (lambda x: map(lambda value: value, x), [2, 1]),
                      (lambda x: zip(x), [(2,), (1,)])]
        for operation, expected in operations:
            events[:] = []
            self.assertEqual(operation(Source()), expected)
            self.assertEqual(events, ['len', 'iter'] if operation is operations[-1][0] else ['iter', 'len'])

    def test_extend_accepts_float_length_result(self):
        events = []
        class Source(object):
            def __iter__(self):
                return iter([2, 1])
            def __len__(self):
                events.append('len')
                return 2.75
            def __length_hint__(self):
                raise AssertionError('length is available')
        target = []
        target.extend(Source())
        self.assertEqual(target, [2, 1])
        self.assertEqual(events, ['len'])

    def test_length_reads_long_subtype_payload(self):
        events = []
        class Wide(long):
            def __int__(self):
                events.append('int')
                return 7
        class Source(object):
            def __len__(self):
                return Wide(2)
            def __iter__(self):
                return iter([1])
        self.assertEqual(len(Source()), 2)
        self.assertTrue(bool(Source()))
        self.assertEqual(list(Source()), [1])
        self.assertEqual(events, [])

    def test_zero_length_long_subtype_does_not_convert(self):
        class Wide(long):
            def __int__(self):
                raise AssertionError('stored value is required')
        class Source(object):
            def __len__(self):
                return Wide(0)
        self.assertEqual(len(Source()), 0)
        self.assertFalse(bool(Source()))

    def test_length_custom_integer_conversion_called_once(self):
        events = []
        class Number(object):
            def __int__(self):
                events.append('int')
                return 2
        class Source(object):
            def __len__(self):
                events.append('len')
                return Number()
            def __iter__(self):
                events.append('iter')
                return iter([1])
        self.assertEqual(list(Source()), [1])
        self.assertEqual(events, ['iter', 'len', 'int'])

    def test_classic_length_long_is_unavailable_for_materialization(self):
        events = []
        class Source:
            def __iter__(self):
                events.append('iter')
                return iter([1])
            def __len__(self):
                events.append('len')
                return 2L
            def __length_hint__(self):
                raise AssertionError('classic hints are ignored')
        self.assertEqual(list(Source()), [1])
        self.assertEqual(events, ['iter', 'len'])

    def test_length_overflow_precedes_consumption(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return self
            def __len__(self):
                events.append('len')
                return 1L << 80
            def next(self):
                raise AssertionError('no item may be consumed')
        self.assertRaises(OverflowError, list, Source())
        self.assertEqual(events, ['iter', 'len'])

    def test_float_length_overflow_is_reported(self):
        class Source(object):
            def __len__(self):
                return 1e100
            def __iter__(self):
                return iter([])
        self.assertRaises(OverflowError, len, Source())
        self.assertRaises(OverflowError, bool, Source())
        self.assertRaises(OverflowError, list, Source())

    def test_nonnumeric_length_falls_back_to_hint(self):
        events = []
        class Source(object):
            def __iter__(self):
                return iter([1])
            def __len__(self):
                events.append('len')
                return '1'
            def __length_hint__(self):
                events.append('hint')
                return 1
        self.assertEqual(list(Source()), [1])
        self.assertEqual(events, ['len', 'hint'])
        self.assertRaises(TypeError, len, Source())

    def test_hint_float_only_protocol_requires_integer_conversion(self):
        events = []
        class Number(object):
            def __float__(self):
                events.append('float')
                return 1.0
        class Source(object):
            def __iter__(self):
                return iter([1])
            def __length_hint__(self):
                return Number()
        self.assertRaises(TypeError, list, Source())
        self.assertEqual(events, [])

    def test_hint_reads_long_subtype_payload(self):
        class Wide(long):
            def __int__(self):
                raise AssertionError('stored value is required')
        class Source(object):
            def __iter__(self):
                return iter([1])
            def __length_hint__(self):
                return Wide(1)
        self.assertEqual(list(Source()), [1])

    def test_sorted_reverse_converts_before_and_after_materialization(self):
        events = []
        class Reverse(object):
            def __int__(self):
                events.append('int')
                return 1
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1, 2])
            def __len__(self):
                events.append('len')
                return 2
        self.assertEqual(sorted(Source(), reverse=Reverse()), [2, 1])
        self.assertEqual(events, ['int', 'iter', 'len', 'int'])

    def test_sorted_invalid_reverse_prevents_iteration(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1, 2])
        self.assertRaises(TypeError, sorted, Source(), reverse='1')
        self.assertEqual(events, [])

    def test_sorted_unknown_keyword_prevents_iteration(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1, 2])
        self.assertRaises(TypeError, sorted, Source(), unknown=1)
        self.assertEqual(events, [])

    def test_sorted_reverse_conversion_precedes_unknown_keyword(self):
        events = []
        class Reverse(object):
            def __int__(self):
                events.append('int')
                return 1
        self.assertRaises(TypeError, sorted, [2, 1], reverse=Reverse(), unknown=1)
        self.assertEqual(events, ['int'])

    def test_sorted_duplicate_argument_prevents_iteration(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1, 2])
        self.assertRaises(TypeError, sorted, Source(), None, cmp=None)
        self.assertEqual(events, [])

    def test_sorted_keyword_iterable_is_forwarded_to_list_sort(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1, 2])
            def __len__(self):
                events.append('len')
                return 2
        self.assertRaises(TypeError, sorted, iterable=Source())
        self.assertEqual(events, ['iter', 'len'])

    def test_sorted_second_reverse_conversion_controls_result(self):
        events = []
        class Reverse(object):
            def __int__(self):
                events.append('int')
                return len(events) - 1
        self.assertEqual(sorted([1, 2], reverse=Reverse()), [2, 1])
        self.assertEqual(events, ['int', 'int'])

    def test_sort_keyword_subtype_equality_can_ignore_known_keyword(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append(other)
                return False
            def __hash__(self):
                return str.__hash__(self)
        for name, value in [('reverse', True), ('key', lambda value: -value),
                            ('cmp', lambda left, right: cmp(right, left))]:
            events[:] = []
            self.assertEqual(sorted([2, 1], **{Keyword(name): value}), [1, 2])
            self.assertEqual(events, [name, name])
            events[:] = []
            values = [2, 1]
            values.sort(**{Keyword(name): value})
            self.assertEqual(values, [1, 2])
            self.assertEqual(events, [name])

    def test_sort_keyword_subtype_equality_can_accept_known_keyword(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append(other)
                return str.__eq__(self, other)
            def __hash__(self):
                return str.__hash__(self)
        self.assertEqual(sorted([1, 2], **{Keyword('reverse'): True}), [2, 1])
        self.assertEqual(events, ['reverse', 'reverse'])
        events[:] = []
        values = [1, 2]
        values.sort(**{Keyword('reverse'): True})
        self.assertEqual(values, [2, 1])
        self.assertEqual(events, ['reverse'])

    def test_sort_keyword_lookup_suppresses_errors_and_preserves_handled_state(self):
        events = []
        class Keyword(str):
            def __eq__(self, other):
                events.append(other)
                raise failure('keyword comparison')
            def __hash__(self):
                return str.__hash__(self)
        for failure in (ValueError, KeyboardInterrupt):
            try:
                raise LookupError('handled')
            except LookupError as handled:
                for name, value in [('reverse', True), ('key', lambda value: -value),
                                    ('cmp', lambda left, right: cmp(right, left))]:
                    events[:] = []
                    self.assertEqual(sorted([2, 1], **{Keyword(name): value}), [1, 2])
                    self.assertEqual(events, [name, name])
                    self.assertIs(sys.exc_info()[1], handled)
                    events[:] = []
                    values = [2, 1]
                    values.sort(**{Keyword(name): value})
                    self.assertEqual(values, [1, 2])
                    self.assertEqual(events, [name])
                    self.assertIs(sys.exc_info()[1], handled)
            sys.exc_clear()

    def test_fromkeys_populates_alternate_constructor_mapping(self):
        events = []
        class Mapping(object):
            def __init__(self):
                self.entries = []
            def __setitem__(self, key, value):
                events.append('setitem')
                self.entries.append((key, value))
        class Dictionary(dict):
            def __new__(cls):
                events.append('new')
                return Mapping()
        result = Dictionary.fromkeys(['x', 'y'], 7)
        self.assertIs(type(result), Mapping)
        self.assertEqual(result.entries, [('x', 7), ('y', 7)])
        self.assertEqual(events, ['new', 'setitem', 'setitem'])

    def test_fromkeys_empty_source_can_return_non_mapping(self):
        class Dictionary(dict):
            def __new__(cls):
                return 7
        self.assertEqual(Dictionary.fromkeys([]), 7)
        self.assertRaises(TypeError, Dictionary.fromkeys, ['x'])

    def test_fromkeys_reuses_cached_hashes_from_exact_sources(self):
        events = []
        class Key(object):
            def __hash__(self):
                events.append('hash')
                return 1
        key = Key()
        sources = [{key: 3}, set([key]), frozenset([key])]
        for source in sources:
            events[:] = []
            result = dict.fromkeys(source, 7)
            self.assertEqual(events, [])
            self.assertEqual(len(result), 1)
            self.assertEqual(result.values(), [7])

    def test_fromkeys_dict_subtype_uses_generic_hash_and_assignment(self):
        events = []
        class Key(object):
            def __hash__(self):
                events.append('hash')
                return 1
        class Dictionary(dict):
            def __setitem__(self, key, value):
                events.append('setitem')
                dict.__setitem__(self, key, value)
        key = Key()
        source = {key: 3}
        events[:] = []
        result = Dictionary.fromkeys(source, 7)
        self.assertIs(type(result), Dictionary)
        self.assertEqual(events, ['setitem', 'hash'])

    def test_all_any_stop_at_first_decisive_value(self):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                yield first
                raise AssertionError('unreachable tail')
        first = False
        self.assertFalse(all(Source()))
        first = True
        self.assertTrue(any(Source()))
        self.assertEqual(events, ['iter', 'iter'])

    def test_min_max_keep_first_tied_item(self):
        left, right = object(), object()
        events = []
        def key(value):
            events.append(value)
            return 1
        self.assertIs(min([left, right], key=key), left)
        self.assertEqual(events, [left, right])
        events[:] = []
        self.assertIs(max([left, right], key=key), left)
        self.assertEqual(events, [left, right])

    def test_min_key_none_is_checked_only_after_first_item(self):
        self.assertRaises(ValueError, min, [], key=None)
        self.assertRaises(TypeError, min, [1], key=None)

    def test_zip_consumes_longer_left_before_short_right_stops(self):
        left = iter([1, 2])
        right = iter([3])
        self.assertEqual(zip(left, right), [(1, 3)])
        self.assertRaises(StopIteration, next, left)

    def test_map_does_not_resume_exhausted_source(self):
        events = []
        class Source(object):
            def __init__(self):
                self.index = 0
            def __iter__(self):
                return self
            def next(self):
                self.index += 1
                events.append(self.index)
                if self.index == 2 or self.index >= 4:
                    raise StopIteration
                return self.index
        self.assertEqual(map(None, Source(), [4, 5, 6]), [(1, 4), (None, 5), (None, 6)])
        self.assertEqual(events, [1, 2])

    def test_reduce_accepts_uncallable_when_no_call_is_needed(self):
        self.assertEqual(reduce(None, [7]), 7)
        self.assertEqual(reduce(None, [], 8), 8)
        self.assertRaises(TypeError, reduce, None, [7, 8])

    def test_sum_uses_binary_addition_for_custom_start(self):
        events = []
        class Accumulator(object):
            def __add__(self, other):
                events.append(('add', other))
                return self
            def __iadd__(self, other):
                raise AssertionError('sum must not mutate via inplace addition')
        start = Accumulator()
        self.assertIs(sum([1, 2], start), start)
        self.assertEqual(events, [('add', 1), ('add', 2)])

    def test_min_releases_discarded_temporary_keys(self):
        references = []
        class Key(object):
            def __init__(self, value):
                self.value = value
            def __lt__(self, other):
                return self.value < other.value
        def key(value):
            result = Key(value)
            references.append(weakref.ref(result))
            return result
        self.assertEqual(min([2, 1], key=key), 1)
        self.assertEqual([reference() for reference in references], [None, None])

    def test_native_getters_reject_foreign_receivers(self):
        for receiver, kind in ((5, 'int'), ('abc', 'str'), (bytearray('abc'), 'bytearray')):
            for name in ('format', 'itemsize', 'ndim', 'readonly', 'shape', 'strides', 'suboffsets'):
                self.assertEqual(error_text(memoryview.__dict__[name].__get__, receiver),
                                 "TypeError: descriptor '%s' for 'memoryview' objects doesn't apply to '%s' object" % (name, kind))
            for name in ('start', 'stop', 'step'):
                self.assertEqual(error_text(slice.__dict__[name].__get__, receiver),
                                 "TypeError: descriptor '%s' for 'slice' objects doesn't apply to '%s' object" % (name, kind))
        self.assertEqual(memoryview.__dict__['shape'].__get__(memoryview('abc')), (3L,))

    def test_format_spec_errors_follow_the_spec_parser(self):
        cases = [
            ((1.5, 'd'), "ValueError: Unknown format code 'd' for object of type 'float'"),
            ((1, '.2'), 'ValueError: Precision not allowed in integer format specifier'),
            ((1, '9' * 20), 'ValueError: Too many decimal digits in format string'),
            ((1.0, '.' + '9' * 20 + 'f'), 'ValueError: Too many decimal digits in format string'),
            ((1.0, '.%df' % (2 ** 31)), 'ValueError: precision too big'),
            ((1, '.'), 'ValueError: Format specifier missing precision'),
            ((1, 'xx'), 'ValueError: Invalid conversion specification'),
            ((1, ',x'), "ValueError: Cannot specify ',' with 'x'."),
            (('abc', ','), "ValueError: Cannot specify ',' with 's'."),
            ((1, '+c'), "ValueError: Sign not allowed with integer format specifier 'c'"),
            ((-1, 'c'), 'OverflowError: %c arg not in range(0x100)'),
            ((1.5, '#'), 'ValueError: Alternate form (#) not allowed in float format specifier'),
            ((1 + 2j, '010'), 'ValueError: Zero padding is not allowed in complex format specifier'),
            ((1 + 2j, '%'), "ValueError: Unknown format code '%' for object of type 'complex'"),
            (('abc', '+'), 'ValueError: Sign not allowed in string format specifier'),
            (('abc', '#'), 'ValueError: Alternate form (#) not allowed in string format specifier'),
            ((u'abc', '010'), "ValueError: '=' alignment not allowed in string format specifier"),
            ((u'abc', u'\x01'), "ValueError: Unknown format code '\\x1' for object of type 'unicode'"),
            (([1], 'x'), "ValueError: Unknown format code 'x' for object of type 'str'"),
        ]
        for arguments, expected in cases:
            self.assertEqual(error_text(format, *arguments), expected)
        self.assertEqual(error_text('{:<{}}'.format, 'a', 2 ** 63), 'ValueError: Too many decimal digits in format string')
        self.assertEqual(len(format(1.0, '.100001f')), 100003)

    def test_percent_operands_and_messages(self):
        class IntegerWithFloat(int):
            def __float__(self):
                return 2.5
        class Classic:
            pass
        class Mapping(object):
            def __getitem__(self, key):
                return key.upper()
        self.assertEqual('%f' % IntegerWithFloat(1), '2.500000')
        self.assertEqual(u'%f' % IntegerWithFloat(1), u'2.500000')
        self.assertEqual('' % Classic(), '')
        self.assertEqual('%(a)s %%' % Mapping(), 'A %')
        self.assertEqual('%5%|%-3%|' % (), '    %|%  |')
        self.assertEqual(len('%.100001f' % 1.0), 100003)
        cases = [
            (lambda: '%d' % float('nan'), 'TypeError: %d format: a number is required, not float'),
            (lambda: '%i' % 'x', 'TypeError: %d format: a number is required, not str'),
            (lambda: '%x' % [], 'TypeError: %x format: a number is required, not list'),
            (lambda: '%f' % 'x', 'TypeError: float argument required, not str'),
            (lambda: u'%f' % 'x', 'TypeError: a float is required'),
            (lambda: '%c' % 256, 'OverflowError: unsigned byte integer is greater than maximum'),
            (lambda: '%c' % -1, 'OverflowError: unsigned byte integer is less than minimum'),
            (lambda: '%c' % 'ab', 'TypeError: %c requires int or char'),
            (lambda: u'%c' % '\xe9\xe9', 'TypeError: %c requires int or char'),
            (lambda: u'%c' % 0x110000, 'OverflowError: %c arg not in range(0x110000) (wide Python build)'),
            (lambda: '%y' % 1, "ValueError: unsupported format character 'y' (0x79) at index 1"),
            (lambda: u'ab%\u1234' % 1, "ValueError: unsupported format character '?' (0x1234) at index 3"),
            (lambda: '%lld' % 1, "ValueError: unsupported format character 'l' (0x6c) at index 2"),
            (lambda: '%.117d' % 1, 'OverflowError: formatted integer is too long (precision too large?)'),
            (lambda: '%.*f' % (2 ** 31, 1.0), 'OverflowError: Python int too large to convert to C int'),
            (lambda: '%(a)s' % 5, 'TypeError: format requires a mapping'),
            (lambda: '%(a)*d' % {'a': 1}, 'TypeError: not enough arguments for format string'),
        ]
        for function, expected in cases:
            self.assertEqual(error_text(function), expected)

    def test_text_search_bounds_require_index(self):
        self.assertEqual(error_text('abc'.find, 'b', 1.5), 'TypeError: slice indices must be integers or None or have an __index__ method')
        self.assertEqual(error_text(u'abc'.count, u'b', None, 'x'), 'TypeError: slice indices must be integers or None or have an __index__ method')
        self.assertEqual('abc'.find('b', True), 1)

    def test_codec_names_resolve_like_the_encodings_search_function(self):
        for name in (' utf 8 ', 'Latin_1', 'ISO-8859-1', 'US-ASCII', 'iso_646.irv:1991', 'ansi.x3_4.1968', '-UTF-8-'):
            self.assertEqual(u'a'.encode(name), 'a')
        for name in ('u-t-f-8', 'latin.1', 'utf.8', 'iso.646.irv.1991'):
            self.assertRaises(LookupError, u'a'.encode, name)
            self.assertRaises(LookupError, _codecs.lookup, name)
        self.assertEqual(len(_codecs.lookup('  hEx  ')), 4)
        self.assertFalse(hasattr(_codecs, '_search_path'))
        def search(name):
            if name == 'builtin_edges_returns_int':
                return (lambda text, errors='strict': (5, 1), None, None, None)
        _codecs.register(search)
        self.assertEqual(error_text(u'a'.encode, 'builtin_edges_returns_int'), 'TypeError: encoder did not return a string/unicode object (type=int)')
        self.assertEqual(error_text(bytearray, u'a', 'builtin_edges_returns_int'), "TypeError: can't concat int to bytearray")
        self.assertEqual(bytearray('ab', 'builtin_edges_returns_int'), bytearray('ab'))
        self.assertEqual(error_text(_codecs.register_error, 'builtin_edges', 5), 'TypeError: handler must be callable')

    def test_codec_error_handlers_clamp_their_ranges(self):
        replace = _codecs.lookup_error('replace')
        self.assertEqual(replace(UnicodeTranslateError(u'abc', 0, 2 ** 40, 'x')), (u'\ufffd' * 3, 3))
        self.assertEqual(replace(UnicodeEncodeError('ascii', u'abc', 1, 10, 'x')), (u'??', 3))
        self.assertEqual(replace(UnicodeEncodeError('ascii', u'', 0, 0, 'x')), (u'?', 0))
        self.assertRaises(MemoryError, replace, UnicodeEncodeError('ascii', u'abc', 5, 1, 'x'))
        self.assertEqual(_codecs.lookup_error('xmlcharrefreplace')(UnicodeEncodeError('ascii', u'abc', 5, 1, 'x')), (u'', 1))

    def test_bytearray_index_and_resize_errors(self):
        data = bytearray('abc')
        self.assertEqual(error_text(data.__getitem__, 2 ** 70), "IndexError: cannot fit 'long' into an index-sized integer")
        self.assertEqual(error_text(data.__getitem__, 'a'), 'TypeError: bytearray indices must be integers')
        self.assertEqual(error_text(data.__setitem__, 'a', 1), 'TypeError: bytearray indices must be integer')
        self.assertEqual(error_text(buffer('abc').__getitem__, 2 ** 70), "IndexError: cannot fit 'long' into an index-sized integer")
        self.assertEqual(error_text(data.__setitem__, slice(None, None, 2), 'xyz'), 'ValueError: attempt to assign bytes of size 3 to extended slice of size 2')
        view = memoryview(data)
        self.assertEqual(error_text(data.__delitem__, slice(10, None, 2)), 'BufferError: Existing exports of data: object cannot be re-sized')
        del view
        del data[10::2]
        self.assertEqual(data, bytearray('abc'))

    def test_struct_pack_into_argument_messages(self):
        target = bytearray(16)
        self.assertEqual(error_text(_struct.pack_into, '<d', target, 2 ** 70, 1.5), 'OverflowError: long int too large to convert to int')
        self.assertEqual(error_text(_struct.pack_into, '<d', 'abc', 0, 1.5), 'TypeError: argument must be read-write buffer, not str')
        _struct.pack_into('<d', target, 8.7, 1.5)
        self.assertEqual(_struct.unpack_from('<d', target, 8), (1.5,))

    def test_builtin_argument_messages(self):
        class Text(str):
            pass
        class NegativeLength(object):
            def __len__(self):
                return -1
        cases = [
            (lambda: chr(256), 'ValueError: chr() arg not in range(256)'),
            (lambda: chr(65.0), 'TypeError: integer argument expected, got float'),
            (lambda: unichr(0x110000), 'ValueError: unichr() arg not in range(0x110000) (wide Python build)'),
            (lambda: ord('ab'), 'TypeError: ord() expected a character, but string of length 2 found'),
            (lambda: ord(u''), 'TypeError: ord() expected a character, but string of length 0 found'),
            (lambda: ord(5), 'TypeError: ord() expected string of length 1, but int found'),
            (lambda: min(), 'TypeError: min expected 1 arguments, got 0'),
            (lambda: max([]), 'ValueError: max() arg is an empty sequence'),
            (lambda: min([1], foo=1), 'TypeError: min() got an unexpected keyword argument'),
            (lambda: sum(['a'], ''), "TypeError: sum() can't sum strings [use ''.join(seq) instead]"),
            (lambda: sorted([1], foo=1), "TypeError: 'foo' is an invalid keyword argument for this function"),
            (lambda: sorted([1], None, None, False, x=1), 'TypeError: sorted() takes at most 4 arguments (5 given)'),
            (lambda: getattr(1, 5), 'TypeError: getattr(): attribute name must be string'),
            (lambda: hasattr(1, None), 'TypeError: hasattr(): attribute name must be string'),
            (lambda: setattr(Text(), 5, 1), "TypeError: attribute name must be string, not 'int'"),
            (lambda: getattr(1, u'\xe9'), "UnicodeEncodeError: 'ascii' codec can't encode character u'\\xe9' in position 0: ordinal not in range(128)"),
            (lambda: next([1]), 'TypeError: list object is not an iterator'),
            (lambda: zip([1], 5), 'TypeError: zip argument #2 must support iteration'),
            (lambda: apply(len, 5), 'TypeError: apply() arg 2 expected sequence, found int'),
            (lambda: len(5), "TypeError: object of type 'int' has no len()"),
            (lambda: len(NegativeLength()), 'ValueError: __len__() should return >= 0'),
            (lambda: reduce(len), 'TypeError: reduce expected at least 2 arguments, got 1'),
            (lambda: reduce(len, [], x=1), 'TypeError: reduce() takes no keyword arguments'),
            (lambda: len([], x=1), 'TypeError: len() takes no keyword arguments'),
            (lambda: intern(u'x'), 'TypeError: intern() argument 1 must be string, not unicode'),
            (lambda: intern(Text('x')), "TypeError: can't intern subclass of string"),
        ]
        for function, expected in cases:
            self.assertEqual(error_text(function), expected)

    def test_range_falls_back_to_long_bounds(self):
        result = range(0, 2 ** 64, 2 ** 63)
        self.assertEqual(result, [0, 2 ** 63])
        self.assertEqual([type(item) for item in result], [long, long])
        self.assertEqual([type(item) for item in range(5L)], [int] * 5)
        self.assertEqual(error_text(range, 1.5), 'TypeError: range() integer end argument expected, got float.')
        self.assertEqual(error_text(range, 1.5, 2), 'TypeError: range() integer start argument expected, got float.')
        self.assertEqual(error_text(range, 0, 2 ** 63), 'OverflowError: range() result has too many items')
        self.assertEqual(error_text(range, 0, 1, 0), 'ValueError: range() step argument must not be zero')
        self.assertEqual(error_text(xrange, -2 ** 63, 2 ** 63 - 1), 'OverflowError: xrange() result has too many items')

    def test_instance_checks_bound_classinfo_recursion(self):
        nested = str
        for depth in xrange(5000):
            nested = (nested,)
        self.assertEqual(error_text(isinstance, 1, nested), 'RuntimeError: maximum recursion depth exceeded in __instancecheck__')
        self.assertEqual(error_text(issubclass, int, nested), 'RuntimeError: maximum recursion depth exceeded in __subclasscheck__')
        class Bases(object):
            def __init__(self, base):
                self.__bases__ = (base,) if base is not None else ()
        chain = Bases(None)
        for depth in xrange(5000):
            chain = Bases(chain)
        self.assertIs(issubclass(chain, Bases(None)), False)
        class RaisingBases(object):
            @property
            def __bases__(self):
                raise KeyError('bases')
        self.assertRaises(KeyError, issubclass, RaisingBases(), Bases(None))

    def test_setattr_interns_attribute_names(self):
        class Holder(object):
            pass
        holder = Holder()
        setattr(holder, ''.join(['fo', 'o']), 1)
        self.assertIs([key for key in holder.__dict__][0], 'foo')

    def test_dir_merge_survives_dict_replacement(self):
        events = []
        class Key(object):
            def __hash__(self):
                return hash('a')
            def __eq__(self, other):
                if not events:
                    events.append(other)
                    holder.__dict__ = {}
                return False
        class Holder(object):
            pass
        holder = Holder()
        namespace = {Key(): 2, 'a': 1}
        for index in range(20):
            namespace['x%d' % index] = index
        holder.__dict__ = namespace
        del namespace
        self.assertIn('x19', dir(holder))
        self.assertEqual(events, ['a'])

    def test_import_with_none_parent_is_absolute(self):
        sys.modules['builtin_edges_none_parent'] = None
        try:
            self.assertEqual(error_text(__import__, 'builtin_edges_missing', {'__name__': 'builtin_edges_none_parent.child'}, {}, [], 1),
                             'ImportError: No module named builtin_edges_missing')
        finally:
            del sys.modules['builtin_edges_none_parent']

    def test_output_softspace_is_an_attribute(self):
        stream = sys.stdout
        previous = stream.softspace
        stream.softspace = 1
        self.assertEqual(stream.softspace, 1)
        stream.softspace = previous
