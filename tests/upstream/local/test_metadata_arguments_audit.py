"""Project-authored class metadata and native receiver regressions."""

import _sre
import _functools
import unittest


def _error(callback, *args):
    try:
        callback(*args)
    except BaseException as error:
        return type(error).__name__, error.args
    raise AssertionError('operation must raise')


class MetadataArgumentsAudit(unittest.TestCase):
    def test_classic_name_rejects_nul_with_type_error(self):
        class Classic:
            pass
        original = Classic.__name__
        self.assertEqual(_error(setattr, Classic, '__name__', 'Changed\0tail'),
                         ('TypeError', ('__name__ must not contain null bytes',)))
        self.assertIs(Classic.__name__, original)

    def test_classic_name_invalid_values_preserve_identity(self):
        class Classic:
            pass
        original = Classic.__name__
        for value in (None, 17, u'Changed', [], {}, ()):
            self.assertEqual(_error(setattr, Classic, '__name__', value),
                             ('TypeError', ('__name__ must be a string object',)))
            self.assertIs(Classic.__name__, original)

    def test_classic_name_retains_string_subtype(self):
        class Text(str):
            pass
        class Classic:
            pass
        name = Text('Changed')
        Classic.__name__ = name
        self.assertIs(Classic.__name__, name)

    def test_classic_reserved_fields_cannot_be_deleted(self):
        class Classic:
            pass
        for field, category in (('__name__', 'string'), ('__dict__', 'dictionary'),
                                ('__bases__', 'tuple')):
            original = getattr(Classic, field)
            self.assertEqual(_error(delattr, Classic, field),
                             ('TypeError', ('%s must be a %s object' % (field, category),)))
            self.assertIs(getattr(Classic, field), original)

    def test_classic_dictionary_and_bases_validate_before_mutation(self):
        class Classic:
            pass
        for field, category in (('__dict__', 'dictionary'), ('__bases__', 'tuple')):
            original = getattr(Classic, field)
            for value in (None, 17, []):
                self.assertEqual(_error(setattr, Classic, field, value),
                                 ('TypeError', ('%s must be a %s object' % (field, category),)))
                self.assertIs(getattr(Classic, field), original)

    def test_classic_dictionary_replacement_retains_supplied_object(self):
        class Classic:
            pass
        namespace = {'answer': 42}
        Classic.__dict__ = namespace
        self.assertIs(Classic.__dict__, namespace)
        self.assertEqual(Classic.answer, 42)

    def test_native_regex_type_names_and_modules(self):
        pattern = _sre.compile('a', 0, [17, 8, 3, 1, 1, 1, 1, 97, 0, 19, 97, 1],
                               0, {}, [None])
        for value, name in ((pattern, 'SRE_Pattern'),
                            (pattern.match('a'), 'SRE_Match'),
                            (pattern.scanner('a'), 'SRE_Scanner')):
            owner = type(value)
            self.assertEqual(owner.__name__, name)
            self.assertEqual(owner.__module__, '_sre')
            self.assertEqual(repr(owner), "<type '_sre.%s'>" % name)

    def test_function_type_module_is_not_instance_descriptor(self):
        self.assertEqual(type(lambda: None).__module__, '__builtin__')

    def test_native_partial_keeps_public_module_and_short_name(self):
        owner = type(_functools.partial(int, '1'))
        self.assertEqual(owner.__name__, 'partial')
        self.assertEqual(owner.__module__, 'functools')
        self.assertEqual(repr(owner), "<type 'functools.partial'>")

    def test_modern_empty_bases_error_preserves_original_tuple(self):
        class Modern(object):
            pass
        original = Modern.__bases__
        self.assertEqual(_error(setattr, Modern, '__bases__', ()), ('TypeError',
                         ('can only assign non-empty tuple to Modern.__bases__, not ()',)))
        self.assertIs(Modern.__bases__, original)

    def test_python_heap_type_retains_qualified_name(self):
        class Text(str):
            pass
        name = Text('outer.Qualified')
        owner = type(name, (object,), {'__module__': 'custom'})
        self.assertIs(owner.__name__, name)
        self.assertEqual(owner.__module__, 'custom')
        self.assertEqual(repr(owner), "<class 'custom.outer.Qualified'>")

    def test_unbound_native_method_requires_receiver(self):
        for owner, method in ((int, 'conjugate'), (list, 'append'),
                              (list, '__add__'), (dict, 'keys')):
            self.assertEqual(_error(getattr(owner, method)), ('TypeError',
                ("descriptor '%s' of '%s' object needs an argument" % (method, owner.__name__),)))

    def test_unbound_native_method_reports_wrong_receiver(self):
        for owner, method in ((int, 'conjugate'), (list, 'append'),
                              (list, '__add__'), (dict, 'keys')):
            self.assertEqual(_error(getattr(owner, method), ''), ('TypeError',
                ("descriptor '%s' requires a '%s' object but received a 'str'" % (method, owner.__name__),)))

    def test_native_descriptor_binding_reports_wrong_receiver(self):
        for owner, method in ((int, 'conjugate'), (list, 'append'),
                              (list, '__add__'), (dict, 'keys')):
            descriptor = getattr(owner, method)
            self.assertEqual(_error(descriptor.__get__, '', str), ('TypeError',
                ("descriptor '%s' for '%s' objects doesn't apply to 'str' object" % (method, owner.__name__),)))
