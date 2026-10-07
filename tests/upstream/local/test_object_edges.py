"""Project-authored call binding and native exception state regressions."""

import unittest
import _weakref as weakref


def _unicode_error(factory, start=2, end=4):
    if factory is UnicodeTranslateError:
        return factory(u'text', start, end, 'reason')
    return factory('ascii', u'text' if factory is UnicodeEncodeError else 'text',
                   start, end, 'reason')


_UNICODE_ERRORS = (UnicodeEncodeError, UnicodeDecodeError, UnicodeTranslateError)


class ObjectEdges(unittest.TestCase):
    def test_named_keyword_subtypes_compare_without_hashing(self):
        for base in (str, unicode):
            events = []
            class Key(base):
                fail = False
                def __hash__(self):
                    events.append('hash')
                    if self.fail:
                        raise RuntimeError('keyword hash')
                    return base.__hash__(self)
                def __eq__(self, other):
                    events.append(('eq', str(other)))
                    return base.__eq__(self, other)
            def invoke(key):
                return key
            key = Key('key')
            keywords = {key: 17}
            events[:] = []
            key.fail = True
            self.assertEqual(invoke(**keywords), 17)
            self.assertEqual(events, [('eq', 'key')])

    def test_keyword_equality_can_match_different_spelling(self):
        events = []
        class Key(str):
            def __eq__(self, name):
                events.append(str(name))
                return name == 'second'
        def invoke(first=17, second=19):
            return first, second
        self.assertEqual(invoke(**{Key('other'): 23}), (17, 23))
        self.assertEqual(events, ['first', 'second'])

    def test_keyword_equality_failure_propagates(self):
        class Key(str):
            def __eq__(self, name):
                raise ValueError('binding equality')
        def invoke(key):
            return key
        keywords = {Key('key'): 17}
        with self.assertRaisesRegexp(ValueError, 'binding equality'):
            invoke(**keywords)

    def test_extra_keyword_hashes_once_when_bound(self):
        events = []
        class Key(str):
            def __hash__(self):
                events.append('hash')
                return str.__hash__(self)
        key = Key('extra')
        keywords = {key: 17}
        events[:] = []
        def invoke(**received):
            return received.values()
        self.assertEqual(invoke(**keywords), [17])
        self.assertEqual(events, ['hash'])

    def test_binding_retains_defaults_during_equality_callback(self):
        def invoke(key, value=17):
            return key, value
        original_defaults = invoke.func_defaults
        class Key(str):
            def __eq__(self, name):
                invoke.func_defaults = (23,)
                return str.__eq__(self, name)
        self.assertEqual(invoke(**{Key('key'): 7}), (7, 17))
        self.assertEqual(original_defaults, (17,))
        self.assertEqual(invoke.func_defaults, (23,))

    def test_binding_uses_original_code_during_equality_callback(self):
        def invoke(first, second=19):
            return first, second
        def replacement(changed):
            return changed
        original_code = invoke.func_code
        class Key(str):
            def __eq__(self, name):
                invoke.func_code = replacement.func_code
                return str.__eq__(self, name)
        self.assertEqual(invoke(**{Key('first'): 7}), (7, 19))
        self.assertEqual(original_code.co_argcount, 2)
        self.assertEqual(invoke.func_code.co_argcount, 1)

    def test_keyword_mapping_is_materialized_before_star_iterable(self):
        events = []
        class Mapping(object):
            def keys(self):
                events.append('keys')
                return ['value']
            def __getitem__(self, key):
                events.append(('get', key))
                return 19
        class Sequence(object):
            def __iter__(self):
                events.append('iter')
                return iter([17])
            def __len__(self):
                events.append('len')
                return 1
        def invoke(first, value):
            return first, value
        self.assertEqual(invoke(*Sequence(), **Mapping()), (17, 19))
        self.assertEqual(events, ['keys', ('get', 'value'), 'iter', 'len'])

    def test_mapping_failure_prevents_star_iteration(self):
        events = []
        class Mapping(object):
            def keys(self):
                events.append('keys')
                raise ValueError('mapping first')
            def __getitem__(self, key):
                return 17
        class Sequence(object):
            def __iter__(self):
                events.append('iter')
                return iter([])
        def invoke(*args, **keywords):
            return args, keywords
        with self.assertRaisesRegexp(ValueError, 'mapping first'):
            invoke(*Sequence(), **Mapping())
        self.assertEqual(events, ['keys'])

    def test_keyword_mapping_uses_keys_protocol_for_sequences_and_empty_mappings(self):
        events = []
        class Empty(object):
            def keys(self):
                events.append('keys')
                return []
        class Sequence(list):
            def keys(self):
                events.append('keys')
                return ['value']
            def __getitem__(self, key):
                events.append(('get', key))
                return 17
        def invoke(**keywords):
            return keywords
        self.assertEqual(invoke(**Empty()), {})
        self.assertEqual(events, ['keys'])
        events[:] = []
        self.assertEqual(invoke(**Sequence()), {'value': 17})
        self.assertEqual(events, ['keys', ('get', 'value')])
        with self.assertRaises(TypeError) as caught:
            invoke(**[('value', 17)])
        self.assertIn('argument after ** must be a mapping', str(caught.exception))

    def test_keyword_mapping_errors_preserve_order_and_remap_attribute_errors(self):
        events = []
        class KeysOnly(object):
            def keys(self):
                events.append('keys')
                return ['value']
        class FailedKeys(object):
            def keys(self):
                events.append('keys')
                raise AttributeError('keys payload')
        class FailedItem(object):
            def keys(self):
                events.append('keys')
                return ['value']
            def __getitem__(self, key):
                events.append(('get', key))
                raise AttributeError('item payload')
        def invoke(**keywords):
            return keywords
        self.assertRaises(TypeError, lambda: invoke(**KeysOnly()))
        self.assertEqual(events, ['keys'])
        events[:] = []
        with self.assertRaises(TypeError) as caught:
            invoke(**FailedKeys())
        self.assertIn('argument after ** must be a mapping', str(caught.exception))
        self.assertEqual(events, ['keys'])
        events[:] = []
        with self.assertRaises(TypeError) as caught:
            invoke(**FailedItem())
        self.assertIn('argument after ** must be a mapping', str(caught.exception))
        self.assertEqual(events, ['keys', ('get', 'value')])

    def test_star_tuple_subtype_bypasses_iteration_overrides(self):
        class Sequence(tuple):
            def __iter__(self):
                raise ValueError('tuple iteration')
            def __len__(self):
                raise ValueError('tuple length')
            def __getitem__(self, key):
                raise ValueError('tuple indexing')
        def invoke(*args):
            return args
        self.assertEqual(invoke(13, *Sequence((17, 19))), (13, 17, 19))

    def test_star_iterable_length_failure_propagates(self):
        events = []
        class Sequence(object):
            def __iter__(self):
                events.append('iter')
                return iter([17])
            def __len__(self):
                events.append('len')
                raise ValueError('star length')
        def invoke(*args):
            return args
        with self.assertRaisesRegexp(ValueError, 'star length'):
            invoke(*Sequence())
        self.assertEqual(events, ['iter', 'len'])

    def test_star_type_errors_are_replaced_except_for_generators(self):
        class Sequence(object):
            def __iter__(self):
                raise TypeError('iterator payload')
        def invoke(*args):
            return args
        with self.assertRaises(TypeError) as caught:
            invoke(*Sequence())
        self.assertIn('argument after * must be an iterable', str(caught.exception))
        def sequence():
            raise TypeError('generator payload')
            yield 17
        with self.assertRaisesRegexp(TypeError, 'generator payload'):
            invoke(*sequence())

    def test_unicode_error_blank_native_fields(self):
        for factory in _UNICODE_ERRORS:
            error = factory.__new__(factory)
            self.assertEqual((error.args, error.start, error.end), ((), 0, 0))
            self.assertIs(error.object, None)
            self.assertIs(error.reason, None)
            self.assertEqual(str(error), '')
            self.assertIs(type(error.__str__()), unicode)
            if factory is not UnicodeTranslateError:
                self.assertIs(error.encoding, None)

    def test_unicode_error_failed_reinit_clears_reference_fields(self):
        for factory in _UNICODE_ERRORS:
            error = _unicode_error(factory)
            self.assertRaises(TypeError, error.__init__, 'wrong count')
            self.assertEqual(error.args, ('wrong count',))
            self.assertEqual((error.start, error.end), (2, 4))
            self.assertIs(error.object, None)
            self.assertIs(error.reason, None)
            self.assertEqual(str(error), '')
            if factory is not UnicodeTranslateError:
                self.assertIs(error.encoding, None)

    def test_unicode_error_parse_preserves_partial_position_updates(self):
        for factory in _UNICODE_ERRORS:
            error = _unicode_error(factory)
            prefix = (u'text',) if factory is UnicodeTranslateError else (
                'ascii', u'text' if factory is UnicodeEncodeError else 'text')
            self.assertRaises(TypeError, error.__init__, *(prefix + (13, None, 'r')))
            self.assertEqual((error.start, error.end), (13, 4))
            self.assertIs(error.object, None)
            self.assertIs(error.reason, None)
            self.assertRaises(TypeError, error.__init__, *(prefix + (17, 19, None)))
            self.assertEqual((error.start, error.end), (17, 19))

    def test_unicode_error_init_positions_use_int_and_parser_order(self):
        for factory in _UNICODE_ERRORS:
            events = []
            error = _unicode_error(factory)
            class Position(object):
                def __init__(self, value):
                    self.value = value
                def __int__(self):
                    events.append((self.value, error.object, error.start,
                                   error.end, error.reason))
                    return self.value
            text = u'text' if factory is not UnicodeDecodeError else 'text'
            prefix = (text,) if factory is UnicodeTranslateError else ('ascii', text)
            error.__init__(*(prefix + (Position(7), Position(9), 'new')))
            error.args = ()
            self.assertEqual(events, [(7, text, 2, 4, None), (9, text, 7, 4, None)])
            self.assertEqual((error.start, error.end, error.reason), (7, 9, 'new'))
            self.assertIs(type(error.start), int)

    def test_unicode_error_native_fields_bypass_instance_dict(self):
        for factory in _UNICODE_ERRORS:
            error = _unicode_error(factory)
            error.__dict__.update(start=99, end=101, object='shadow', reason='shadow')
            self.assertEqual((error.start, error.end, error.reason), (2, 4, 'reason'))
            self.assertEqual(error.object, u'text' if factory is not UnicodeDecodeError else 'text')
            self.assertEqual(type(factory.start).__name__, 'member_descriptor')

    def test_unicode_error_position_setters_convert_and_record_failed_minus_one(self):
        class Position(object):
            def __int__(self):
                return 7
        class Index(object):
            def __index__(self):
                return 9
        class Long(long):
            def __int__(self):
                return 11
        for factory in _UNICODE_ERRORS:
            error = _unicode_error(factory)
            for name in ('start', 'end'):
                setattr(error, name, Position())
                self.assertEqual(getattr(error, name), 7)
                self.assertRaises(TypeError, setattr, error, name, Index())
                self.assertEqual(getattr(error, name), -1)
                self.assertRaises(OverflowError, setattr, error, name, 1L << 100)
                self.assertEqual(getattr(error, name), -1)
                setattr(error, name, Long(5))
                self.assertEqual(getattr(error, name), 5)
                setattr(error, name, 3.5)
                self.assertEqual(getattr(error, name), 3)
                self.assertIs(type(getattr(error, name)), int)
                self.assertRaises(TypeError, delattr, error, name)
                self.assertEqual(getattr(error, name), 3)

    def test_unicode_error_reference_fields_hold_and_release_values(self):
        class Carrier(object):
            pass
        for factory in _UNICODE_ERRORS:
            error = _unicode_error(factory)
            carrier = Carrier()
            reference = weakref.ref(carrier)
            error.reason = carrier
            del carrier
            self.assertIsNot(reference(), None)
            del error.reason
            self.assertIs(reference(), None)
            self.assertIs(error.reason, None)
            del error.object
            self.assertIs(error.object, None)
            self.assertEqual(str(error), '')

    def test_unicode_error_reference_cleanup_uses_physical_layout_after_mro_change(self):
        for factory in _UNICODE_ERRORS:
            filtered = [False]
            class Meta(type):
                def mro(cls):
                    if filtered[0]:
                        return [cls, Exception, BaseException, object]
                    return type.mro(cls)
            class Child(factory):
                __metaclass__ = Meta
            class Carrier(object):
                pass
            value = Child(u'text', 0, 1, 'reason') if factory is UnicodeTranslateError else Child(
                'ascii', 'text' if factory is UnicodeDecodeError else u'text', 0, 1, 'reason')
            carrier = Carrier()
            reference = weakref.ref(carrier)
            value.reason = carrier
            del carrier
            filtered[0] = True
            Child.__bases__ = (factory,)
            self.assertNotIn(factory, Child.__mro__)
            self.assertIsNot(reference(), None)
            del value
            self.assertIs(reference(), None)

    def test_unicode_error_single_character_uses_explicit_unicode_escapes(self):
        for factory in (UnicodeEncodeError, UnicodeTranslateError):
            for character, escape in ((u'a', "u'\\x61'"), (u'\n', "u'\\x0a'"),
                                      (u"'", "u'\\x27'"), (u'\xe9', "u'\\xe9'"),
                                      (u'\u20ac', "u'\\u20ac'"),
                                      (u'\U0001f642', "u'\\U0001f642'")):
                value = factory(character, 0, 1, 'reason') if factory is UnicodeTranslateError else factory(
                    'ascii', character, 0, 1, 'reason')
                self.assertIn('character ' + escape + ' in position 0: reason', str(value))

    def test_unicode_error_metadata_rendering_bounds_cstrings(self):
        for factory in _UNICODE_ERRORS:
            value = _unicode_error(factory, 0, 1)
            value.reason = 'before\0after'
            if factory is not UnicodeTranslateError:
                value.encoding = 'codec\0after'
            text = str(value)
            self.assertTrue(text.endswith(': before'))
            self.assertNotIn('after', text)
            if factory is not UnicodeTranslateError:
                self.assertTrue(text.startswith("'codec' codec"))
                value.encoding = 'x' * 401
            value.reason = 'r' * 401
            text = str(value)
            self.assertTrue(text.endswith(': ' + 'r' * 400))
            self.assertNotIn('r' * 401, text)
            if factory is not UnicodeTranslateError:
                self.assertTrue(text.startswith("'" + 'x' * 400 + "' codec"))

    def test_unicode_error_string_converts_modified_metadata_in_order(self):
        for factory in _UNICODE_ERRORS:
            events = []
            error = _unicode_error(factory, 0, 1)
            class Reason(object):
                def __str__(self):
                    events.append('reason')
                    return 'changed'
            class Encoding(object):
                def __str__(self):
                    events.append('encoding')
                    return 'custom'
            error.reason = Reason()
            if factory is not UnicodeTranslateError:
                error.encoding = Encoding()
            text = str(error)
            self.assertIn('changed', text)
            self.assertEqual(events, ['reason'] if factory is UnicodeTranslateError else ['reason', 'encoding'])
            if factory is not UnicodeTranslateError:
                self.assertIn("'custom' codec", text)

    def test_unicode_error_string_preserves_metadata_callback_exception(self):
        class Reason(object):
            def __str__(self):
                raise ValueError('reason rendering')
        for factory in _UNICODE_ERRORS:
            error = _unicode_error(factory)
            error.reason = Reason()
            with self.assertRaisesRegexp(ValueError, 'reason rendering'):
                str(error)

    def test_unicode_error_keyword_failure_preserves_existing_fields(self):
        for factory in _UNICODE_ERRORS:
            error = _unicode_error(factory)
            arguments = error.args
            self.assertRaises(TypeError, error.__init__, ignored=17)
            self.assertEqual(error.args, arguments)
            self.assertEqual((error.start, error.end, error.reason), (2, 4, 'reason'))
            self.assertIsNot(error.object, None)
