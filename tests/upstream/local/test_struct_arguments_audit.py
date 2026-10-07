"""Project-authored Python 2 tests for the supported double Struct surface."""
import _struct as struct
import _weakref
import sys
import unittest


class StructArgumentsAudit(unittest.TestCase):
    def assert_error(self, error_type, message, operation, *args, **kwargs):
        try:
            operation(*args, **kwargs)
        except error_type as error:
            self.assertEqual(error.args, (message,))
        else:
            self.fail('expected ' + error_type.__name__)

    def test_calcsize_argument_count(self):
        for count in (0, 2, 3):
            self.assert_error(TypeError,
                              'calcsize() takes exactly one argument (%d given)' % count,
                              struct.calcsize, *(['<d'] * count))
        self.assertEqual(struct.calcsize('<d'), 8)

    def test_clearcache_argument_count(self):
        for count in (1, 2):
            self.assert_error(TypeError,
                              '_clearcache() takes no arguments (%d given)' % count,
                              struct._clearcache, *([None] * count))
        self.assertIs(struct._clearcache(), None)

    def test_module_keyword_rejection_precedes_arguments(self):
        for name in ('calcsize', 'pack', 'unpack', 'pack_into', '_clearcache'):
            operation = getattr(struct, name)
            self.assert_error(TypeError, name + '() takes no keyword arguments',
                              operation, invalid=1)

    def test_module_unpack_uses_unpack_tuple_parser(self):
        for count in (0, 1, 3):
            self.assert_error(TypeError, 'unpack expected 2 arguments, got %d' % count,
                              struct.unpack, *(['<d'] * count))

    def test_pack_missing_format_and_item_count(self):
        self.assert_error(TypeError, 'missing format argument', struct.pack)
        for count in (0, 2):
            self.assert_error(struct.error,
                              'pack expected 1 items for packing (got %d)' % count,
                              struct.pack, '<d', *([1.0] * count))

    def test_pack_into_missing_arguments_and_item_count(self):
        self.assert_error(TypeError, 'missing format argument', struct.pack_into)
        self.assert_error(struct.error, 'pack_into expected buffer argument',
                          struct.pack_into, '<d')
        self.assert_error(struct.error, 'pack_into expected offset argument',
                          struct.pack_into, '<d', bytearray('X' * 8))
        self.assert_error(struct.error, 'pack_into expected 1 items for packing (got 0)',
                          struct.pack_into, '<d', bytearray('X' * 8), 0)

    def test_compiled_unpack_bound_and_unbound_arguments(self):
        value = struct.Struct('<d')
        for operation, prefix in ((value.unpack, ()), (struct.Struct.unpack, (value,))):
            for count in (0, 2):
                self.assert_error(TypeError,
                                  'unpack() takes exactly one argument (%d given)' % count,
                                  operation, *(prefix + ('X' * 8,) * count))
            self.assert_error(TypeError, 'unpack() takes no keyword arguments',
                              operation, *prefix, invalid=1)

    def test_compiled_pack_and_pack_into_keywords(self):
        value = struct.Struct('<d')
        for name in ('pack', 'pack_into'):
            self.assert_error(TypeError, name + '() takes no keyword arguments',
                              getattr(value, name), invalid=1)
        self.assert_error(struct.error, 'pack_into expected buffer argument', value.pack_into)
        self.assert_error(struct.error, 'pack_into expected offset argument',
                          value.pack_into, bytearray('X' * 8))

    def test_constructor_required_keyword_and_count(self):
        self.assertEqual(struct.Struct(format='<d').format, '<d')
        for kwargs in ({}, {'invalid': '<d'}, {'offset': 0}):
            self.assert_error(TypeError, "Required argument 'format' (pos 1) not found",
                              struct.Struct, **kwargs)
        self.assert_error(TypeError, 'Struct() takes at most 1 argument (2 given)',
                          struct.Struct, '<d', format='<d')

    def test_init_argument_errors_preserve_current_state(self):
        value = struct.Struct('>2d')
        old_format = value.format
        self.assert_error(TypeError, 'Struct() takes at most 1 argument (2 given)',
                          value.__init__, '<d', invalid=0)
        self.assert_error(TypeError, "Required argument 'format' (pos 1) not found",
                          value.__init__, invalid=0)
        self.assertIs(value.format, old_format)
        self.assertEqual(value.size, 16)

    def test_format_type_errors_are_constructor_errors(self):
        for source in (None, 1, bytearray('<d'), buffer('<d'), memoryview('<d')):
            message = 'Struct() argument 1 must be string, not ' + type(source).__name__
            for operation in (struct.Struct, struct.calcsize):
                self.assert_error(TypeError, message, operation, source)

    def test_unicode_format_normalizes_without_overrides(self):
        class Text(unicode):
            def __str__(self):
                raise AssertionError('str override')
            def encode(self, *args):
                raise AssertionError('encode override')
        for source in (u'<d', Text(u'<d')):
            value = struct.Struct(source)
            self.assertIs(type(value.format), str)
            self.assertEqual(value.format, '<d')
            self.assertEqual(value.pack(1.5), struct.pack('<d', 1.5))

    def test_byte_string_format_preserves_subtype_identity(self):
        class Text(str):
            pass
        source = Text('<d')
        value = struct.Struct(source)
        self.assertIs(value.format, source)
        self.assertIs(type(value.format), Text)

    def test_format_nul_suffix_and_trailing_repeat_count(self):
        for source in ('<d\0tail', u'<d\0tail', '<d2', '<2', '<0d\0tail'):
            value = struct.Struct(source)
            self.assertEqual(value.format, str(source))
            expected = 0 if source.startswith('<2') or source.startswith('<0') else 8
            self.assertEqual(value.size, expected)
            self.assertEqual(struct.calcsize(source), expected)

    def test_unicode_format_encoding_checks_whole_source(self):
        source = u'<d\0\xe9'
        for operation in (struct.Struct, struct.calcsize):
            try:
                operation(source)
            except UnicodeEncodeError as error:
                self.assertEqual(error.args, ('ascii', source, 3, 4, 'ordinal not in range(128)'))
                self.assertEqual(error.object, source)
            else:
                self.fail('expected UnicodeEncodeError')
        self.assertEqual(struct.calcsize('<d'), 8)

    def test_failed_reinit_publishes_format_preserves_compiled_fields(self):
        value = struct.Struct('<d')
        source = '<1 d'
        self.assert_error(struct.error, 'bad char in struct format', value.__init__, source)
        self.assertIs(value.format, source)
        self.assertEqual(value.size, 8)
        self.assertEqual(value.pack(1.5), struct.pack('<d', 1.5))
        self.assertIs(value.__init__('>2d'), None)
        self.assertEqual(value.size, 16)
        self.assertEqual(value.unpack(value.pack(1.5, 2.5)), (1.5, 2.5))

    def test_unpack_length_diagnostic_includes_format_size(self):
        for source in (None, 1, 'short', bytearray('short')):
            self.assert_error(struct.error, 'unpack requires a string argument of length 8',
                              struct.unpack, '<d', source)
            self.assert_error(struct.error, 'unpack requires a string argument of length 8',
                              struct.Struct('<d').unpack, source)

    def test_unpack_from_required_and_duplicate_keywords(self):
        sample = struct.pack('<d', 1.5)
        for operation, prefix in ((struct.unpack_from, ('<d',)),
                                  (struct.Struct('<d').unpack_from, ())):
            self.assert_error(TypeError, "Required argument 'buffer' (pos 1) not found",
                              operation, *prefix, offset=0)
            self.assert_error(TypeError, "Argument given by name ('buffer') and position (1)",
                              operation, *(prefix + (sample,)), buffer=sample)
            self.assert_error(TypeError, 'unpack_from() takes at most 2 arguments (3 given)',
                              operation, *(prefix + (sample, 0)), offset=0)

    def test_unpack_from_buffer_conversion_precedes_unknown_keyword(self):
        for operation, prefix in ((struct.unpack_from, ('<d',)),
                                  (struct.Struct('<d').unpack_from, ())):
            self.assert_error(TypeError,
                              'unpack_from() argument 1 must be string or buffer, not int',
                              operation, *(prefix + (1,)), invalid=0)
            self.assert_error(TypeError, "'invalid' is an invalid keyword argument for this function",
                              operation, *(prefix + ('\0' * 8,)), invalid=0)
            self.assert_error(TypeError, 'keywords must be strings',
                              operation, *(prefix + ('\0' * 8,)), **{u'invalid': 0})

    def test_unpack_from_offset_error_identity_and_recovery(self):
        problem = KeyError('offset')
        events = []
        class Offset(object):
            def __int__(self):
                events.append('int')
                raise problem
        value = struct.Struct('<d')
        source = struct.pack('<d', 1.5)
        try:
            value.unpack_from(source, Offset())
        except KeyError as error:
            self.assertIs(error, problem)
        else:
            self.fail('expected KeyError')
        self.assertEqual(events, ['int'])
        self.assertEqual(value.unpack_from(source), (1.5,))

    def test_unpack_from_keyword_equality_callbacks(self):
        events = []
        class Key(str):
            def __eq__(self, other):
                events.append(str(other))
                return str.__eq__(self, other)
            __hash__ = str.__hash__
        source = struct.pack('<d', 1.5)
        value = struct.Struct('<d')
        self.assertEqual(value.unpack_from(**{Key('buffer'): source, Key('offset'): 0}), (1.5,))
        self.assertEqual(events, ['buffer', 'offset'])

    def test_keyword_equality_failures_preserve_handled_exception(self):
        source = struct.pack('<d', 1.5)
        for problem in (KeyError('keyword'), KeyboardInterrupt('keyword'), SystemExit('keyword')):
            events = []
            class Key(str):
                def __eq__(self, other):
                    events.append(str(other))
                    raise problem
                __hash__ = str.__hash__
            def missing_buffer():
                try:
                    struct.Struct('<d').unpack_from(**{Key('buffer'): source})
                except TypeError as error:
                    return error.args
                self.fail('expected TypeError')
            try:
                raise ValueError('outer')
            except ValueError as outer:
                self.assertEqual(missing_buffer(), ("Required argument 'buffer' (pos 1) not found",))
                self.assertIs(sys.exc_info()[1], outer)
            self.assertEqual(events, ['buffer'])
        sys.exc_clear()

    def test_unpack_from_keyword_nul_suffix(self):
        source = struct.pack('<d', 1.5)
        self.assertEqual(struct.Struct('<d').unpack_from(source, **{'offset\0tail': 100}), (1.5,))
        self.assert_error(TypeError, "'unknown' is an invalid keyword argument for this function",
                          struct.Struct('<d').unpack_from, source, **{'unknown\0tail': 0})

    def test_module_format_cache_observes_hash(self):
        events = []
        class Text(str):
            def __hash__(self):
                events.append('hash')
                return str.__hash__(self)
        source = Text('<d')
        struct._clearcache()
        try:
            self.assertEqual(struct.calcsize(source), 8)
            self.assertEqual(events, ['hash', 'hash'])
            events[:] = []
            self.assertEqual(struct.calcsize(source), 8)
            self.assertEqual(events, ['hash'])
            events[:] = []
            value = struct.Struct(source)
            self.assertEqual(value.pack(1.5), '\0' * 6 + '\xf8?')
            self.assertEqual(events, [])
        finally:
            struct._clearcache()

    def test_module_format_cache_suppresses_hash_errors(self):
        events = []
        class Text(str):
            def __hash__(self):
                events.append('hash')
                raise KeyError('hash')
        source = Text('<d')
        struct._clearcache()
        try:
            try:
                raise ValueError('outer')
            except ValueError as outer:
                self.assertEqual(struct.calcsize(source), 8)
                self.assertIs(sys.exc_info()[1], outer)
            self.assertEqual(events, ['hash', 'hash'])
        finally:
            struct._clearcache()
            sys.exc_clear()

    def test_compiled_methods_ignore_private_attribute_spoof(self):
        events = []
        class Child(struct.Struct):
            def __getattribute__(self, name):
                if name in ('_format', '_size'):
                    events.append(name)
                    raise AssertionError('private lookup')
                return struct.Struct.__getattribute__(self, name)
        value = Child('<d')
        value._format = '>2d'
        value._size = 16
        sample = value.pack(1.5)
        self.assertEqual(value.unpack(sample), (1.5,))
        self.assertEqual(value.size, 8)
        self.assertEqual(events, [])

    def test_native_type_metadata_and_descriptors(self):
        value = struct.Struct('<d')
        self.assertEqual(struct.Struct.__name__, 'Struct')
        self.assertEqual(struct.Struct.__module__, '__builtin__')
        self.assertNotIn('__module__', struct.Struct.__dict__)
        self.assertEqual(struct.Struct.__doc__, 'Compiled struct object')
        self.assertFalse(hasattr(value, '__dict__'))
        for name in ('format', 'size'):
            descriptor = struct.Struct.__dict__[name]
            self.assertEqual(type(descriptor).__name__, 'getset_descriptor')
            self.assertIs(descriptor.__objclass__, struct.Struct)
            self.assertEqual(descriptor.__name__, name)
            message = "attribute '%s' of 'Struct' objects is not writable" % name
            self.assert_error(AttributeError, message, setattr, value, name, 1)
            self.assert_error(AttributeError, message, delattr, value, name)
        for name in ('__getattribute__', '__setattr__', '__delattr__', '__init__'):
            descriptor = struct.Struct.__dict__[name]
            self.assertEqual(type(descriptor).__name__, 'wrapper_descriptor')
            self.assertIs(descriptor.__objclass__, struct.Struct)

    def test_native_type_is_immutable_subtypes_have_dict(self):
        message = "can't set attributes of built-in/extension type 'Struct'"
        self.assert_error(TypeError, message, setattr, struct.Struct, 'tag', 1)
        self.assert_error(TypeError, message, delattr, struct.Struct, 'tag')
        class Child(struct.Struct):
            pass
        value = Child('<d')
        value.tag = 1
        self.assertEqual(value.__dict__, {'tag': 1})
        self.assertEqual(value.pack(1.5), struct.pack('<d', 1.5))

    def test_allocation_only_new_and_later_initialization(self):
        class Child(struct.Struct):
            pass
        for target in (struct.Struct, Child):
            value = struct.Struct.__new__(target, 'ignored', invalid=1)
            self.assertIs(type(value), target)
            self.assertIs(value.format, None)
            self.assertEqual(value.size, -1)
            value.__init__('<d')
            self.assertEqual(value.pack(1.5), struct.pack('<d', 1.5))
        self.assert_error(TypeError, 'Struct.__new__(): not enough arguments', struct.Struct.__new__)

    def test_sizeof_arguments_and_weakref_payload_release(self):
        value = struct.Struct('<d')
        self.assertIs(type(value.__sizeof__()), long)
        self.assertTrue(value.__sizeof__() > 0)
        self.assert_error(TypeError, '__sizeof__() takes no arguments (1 given)', value.__sizeof__, 1)
        self.assert_error(TypeError, '__sizeof__() takes no keyword arguments', value.__sizeof__, value=1)
        events = []
        reference = _weakref.ref(value, lambda ref: events.append('released'))
        self.assertFalse(hasattr(value, '__weakref__'))
        del value
        self.assertIs(reference(), None)
        self.assertEqual(events, ['released'])
