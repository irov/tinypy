"""Project-authored Python 2 import, print and execution-state regressions."""

import sys
import unittest


class ExecutionStateAudit(unittest.TestCase):
    def test_star_uses_attributes_of_cached_nonmodule(self):
        events = []
        class Carrier(object):
            __path__ = []
            __all__ = ['member']
            member = 17
            def __getattribute__(self, name):
                events.append(name)
                return object.__getattribute__(self, name)
        module = Carrier()
        name = '_tinypy_execution_carrier'
        sys.modules[name] = module
        try:
            scope = {}
            exec 'from _tinypy_execution_carrier import *' in scope
            self.assertEqual(scope['member'], 17)
            self.assertEqual(events, ['__path__', '__all__', '__path__', 'member', '__all__', 'member'])
        finally:
            del sys.modules[name]

    def test_fromlist_and_all_use_indexing_instead_of_iteration(self):
        events = []
        class Names(list):
            def __iter__(self):
                raise AssertionError('iteration must not be used')
            def __getitem__(self, index):
                events.append(index)
                return list.__getitem__(self, index)
        name = '_tinypy_execution_indexed'
        module = type(sys)(name)
        module.__path__ = []
        module.__all__ = Names(['member'])
        module.member = 19
        sys.modules[name] = module
        try:
            scope = {}
            exec 'from _tinypy_execution_indexed import *' in scope
            self.assertEqual(scope['member'], 19)
            self.assertEqual(events, [0, 1, 0, 1])
            events[:] = []
            self.assertIs(__import__(name, {}, {}, Names(['*anything']), 0), module)
            self.assertEqual(events, [0, 0, 1, 1])
        finally:
            del sys.modules[name]

    def test_fromlist_rejects_iterator_and_unicode_items(self):
        name = '_tinypy_execution_fromlist'
        module = type(sys)(name)
        module.__path__ = []
        sys.modules[name] = module
        try:
            for names, message in ((iter(['member']), "'listiterator' object does not support indexing"),
                                   ([u'member'], "Item in ``from list'' must be str, not unicode")):
                with self.assertRaises(TypeError) as caught:
                    __import__(name, {}, {}, names, 0)
                self.assertEqual(caught.exception.args, (message,))
        finally:
            del sys.modules[name]

    def test_indexed_all_propagates_stopiteration_and_keeps_partial_names(self):
        events = []
        class Names(object):
            def __getitem__(self, index):
                events.append(index)
                if index == 0:
                    return 'member'
                raise StopIteration('indexed failure')
        class Carrier(object):
            member = 23
            __all__ = Names()
        name = '_tinypy_execution_partial_star'
        sys.modules[name] = Carrier()
        scope = {}
        try:
            with self.assertRaises(StopIteration) as caught:
                exec 'from _tinypy_execution_partial_star import *' in scope
            self.assertEqual(caught.exception.args, ('indexed failure',))
            self.assertEqual(events, [0, 1])
            self.assertEqual(scope['member'], 23)
        finally:
            del sys.modules[name]

    def test_star_fallback_uses_mapping_keys_method(self):
        events = []
        class Namespace(dict):
            def keys(self):
                events.append('keys')
                return ['member']
        class Carrier(object):
            member = 29
            @property
            def __dict__(self):
                events.append('dict')
                return Namespace()
        name = '_tinypy_execution_keys'
        sys.modules[name] = Carrier()
        try:
            scope = {}
            exec 'from _tinypy_execution_keys import *' in scope
            self.assertEqual(scope['member'], 29)
            self.assertEqual(events, ['dict', 'keys'])
        finally:
            del sys.modules[name]

    def test_default_star_skips_private_bytes_but_keeps_unicode_keys(self):
        name = '_tinypy_execution_private'
        module = type(sys)(name)
        module.__dict__[u'_unicode_member'] = 31
        module._bytes_member = 37
        sys.modules[name] = module
        try:
            scope = {}
            exec 'from _tinypy_execution_private import *' in scope
            self.assertEqual(scope[u'_unicode_member'], 31)
            self.assertNotIn('_bytes_member', scope)
        finally:
            del sys.modules[name]

    def test_fromlist_suppresses_lookup_errors_and_preserves_handled_state(self):
        events = []
        class Carrier(object):
            __path__ = []
            def __getattribute__(self, name):
                events.append(name)
                if name == 'member':
                    raise KeyboardInterrupt('lookup')
                return object.__getattribute__(self, name)
        name = '_tinypy_execution_suppressed'
        module = Carrier()
        sys.modules[name] = module
        try:
            try:
                raise LookupError('outer')
            except LookupError as outer:
                self.assertIs(__import__(name, {}, {}, ['member'], 0), module)
                self.assertIs(sys.exc_info()[1], outer)
            self.assertEqual(events, ['__path__', 'member', '__path__'])
        finally:
            del sys.modules[name]
            sys.exc_clear()

    def test_all_lookup_suppression_is_distinct_from_star_error(self):
        events = []
        class Carrier(object):
            __path__ = []
            def __getattribute__(self, name):
                events.append(name)
                if name == '__all__':
                    raise ValueError('all lookup')
                return object.__getattribute__(self, name)
        name = '_tinypy_execution_all_error'
        module = Carrier()
        sys.modules[name] = module
        try:
            self.assertIs(__import__(name, {}, {}, ['*'], 0), module)
            self.assertEqual(events, ['__path__', '__all__'])
            events[:] = []
            with self.assertRaisesRegexp(ValueError, 'all lookup'):
                exec 'from _tinypy_execution_all_error import *' in {}
            self.assertEqual(events, ['__path__', '__all__', '__all__'])
        finally:
            del sys.modules[name]

    def test_module_loading_precedes_fromlist_truth(self):
        events = []
        class Names(object):
            def __nonzero__(self):
                events.append('truth')
                raise ValueError('truth failure')
        name = '_tinypy_execution_missing'
        self.assertNotIn(name, sys.modules)
        with self.assertRaises(ImportError) as caught:
            __import__(name, {}, {}, Names(), 0)
        self.assertEqual(caught.exception.args, ('No module named ' + name,))
        self.assertEqual(events, [])
        with self.assertRaisesRegexp(ValueError, 'truth failure'):
            __import__('sys', {}, {}, Names(), 0)
        self.assertEqual(events, ['truth'])

    def test_dotted_cached_child_can_have_a_nonpackage_parent(self):
        head = object()
        tail = object()
        sys.modules['_tinypy_execution_head'] = head
        sys.modules['_tinypy_execution_head.tail'] = tail
        try:
            self.assertIs(__import__('_tinypy_execution_head.tail', {}, {}, [], 0), head)
            self.assertIs(__import__('_tinypy_execution_head.tail', {}, {}, ['member'], 0), tail)
        finally:
            del sys.modules['_tinypy_execution_head.tail']
            del sys.modules['_tinypy_execution_head']

    def test_fromlist_truth_mutation_does_not_replace_loaded_result(self):
        name = '_tinypy_execution_truth_mutation'
        original = type(sys)(name)
        original.__path__ = []
        original.member = 41
        replacement = object()
        class Names(list):
            def __nonzero__(self):
                sys.modules[name] = replacement
                return True
        sys.modules[name] = original
        try:
            self.assertIs(__import__(name, {}, {}, Names(['member']), 0), original)
            self.assertIs(sys.modules[name], replacement)
        finally:
            del sys.modules[name]

    def test_absolute_cached_import_does_not_read_globals_metadata(self):
        events = []
        class Key(str):
            __hash__ = str.__hash__
            def __eq__(self, other):
                events.append(str(other))
                return str.__eq__(self, other)
        scope = {Key('__name__'): 'ignored'}
        self.assertIs(__import__('sys', scope, {}, [], 0), sys)
        self.assertEqual(events, [])

    def test_relative_import_metadata_types_and_cstring_package(self):
        for package in (u'', u'sys', 17):
            with self.assertRaises(ValueError) as caught:
                __import__('', {'__package__': package}, {}, [], 1)
            self.assertEqual(caught.exception.args, ('__package__ set to non-string',))
        for scope in ({'__name__': u'sys'}, {'__name__': 17}, []):
            self.assertIs(__import__('sys', scope, {}, [], 1), sys)
        self.assertIs(__import__('', {'__package__': 'sys\0ignored'}, {}, [], 1), sys)
        scope = {'__package__': '_tinypy_execution_missing_parent'}
        with self.assertRaises(SystemError) as caught:
            __import__('sys', scope, {}, [], 1)
        self.assertEqual(caught.exception.args,
                         ("Parent module '_tinypy_execution_missing_parent' not loaded, cannot perform relative import",))

    def test_import_parser_validation_and_error_precedence(self):
        cases = (((), {}, "Required argument 'name' (pos 1) not found"),
                 ((), {'other': 1}, "Required argument 'name' (pos 1) not found"),
                 ((17,), {'other': 1}, '__import__() argument 1 must be string, not int'),
                 (('sys\0ignored',), {}, '__import__() argument 1 must be string without null bytes, not str'),
                 (('sys',), {'name': 'sys'}, "Argument given by name ('name') and position (1)"),
                 (('sys',), {'other': 1}, "'other' is an invalid keyword argument for this function"))
        for args, kwargs, message in cases:
            with self.assertRaises(TypeError) as caught:
                __import__(*args, **kwargs)
            self.assertEqual(caught.exception.args, (message,))
        with self.assertRaises(ImportError) as caught:
            __import__('a/b', {}, {}, [], 0)
        self.assertEqual(caught.exception.args, ('Import by filename is not supported.',))

    def test_missing_attribute_nul_diagnostic_keeps_closing_quote(self):
        class Classic:
            pass
        values = ((object(), "'object' object has no attribute 'member'"),
                  (type(sys)('module'), "'module' object has no attribute 'member'"),
                  (lambda: None, "'function' object has no attribute 'member'"),
                  (object, "type object 'object' has no attribute 'member'"),
                  (Classic, "class Classic has no attribute 'member'"),
                  (Classic(), "Classic instance has no attribute 'member'"))
        for value, message in values:
            with self.assertRaises(AttributeError) as caught:
                getattr(value, 'member\0ignored')
            self.assertEqual(caught.exception.args, (message,))

    def test_import_level_uses_int_protocol_and_signed_c_ranges(self):
        events = []
        class Level(object):
            def __int__(self):
                events.append('int')
                return 0L
            def __index__(self):
                raise AssertionError('index protocol must not be used')
        self.assertIs(__import__('sys', {}, {}, [], Level()), sys)
        self.assertEqual(events, ['int'])
        for value, kind, message in ((1.5, TypeError, 'integer argument expected, got float'),
                                     (1L << 65, OverflowError, 'Python int too large to convert to C long'),
                                     (1L << 32, OverflowError, 'signed integer is greater than maximum'),
                                     (-(1L << 32), OverflowError, 'signed integer is less than minimum')):
            with self.assertRaises(kind) as caught:
                __import__('sys', {}, {}, [], value)
            self.assertEqual(caught.exception.args, (message,))

    def test_import_keyword_lookup_suppresses_callbacks_with_handled_exception(self):
        events = []
        class Key(str):
            __hash__ = str.__hash__
            def __eq__(self, other):
                events.append(str(other))
                raise KeyboardInterrupt('keyword failure')
        def check_keyword():
            with self.assertRaises(TypeError) as caught:
                __import__(**{Key('name'): 'sys'})
            self.assertEqual(caught.exception.args, ("Required argument 'name' (pos 1) not found",))
        try:
            raise LookupError('outer')
        except LookupError as outer:
            check_keyword()
            self.assertIs(sys.exc_info()[1], outer)
        sys.exc_clear()
        self.assertEqual(events, ['name'])

    def test_filter_and_zip_negative_hints_preserve_user_errors(self):
        events = []
        marker = SystemError('error return without exception set')
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([])
            def __length_hint__(self):
                events.append('hint')
                if failure == 'callback':
                    raise marker
                return -1
        for operation in ('filter', 'zip'):
            for failure in ('negative', 'callback'):
                events[:] = []
                with self.assertRaises(SystemError) as caught:
                    if operation == 'filter':
                        filter(None, Source())
                    else:
                        zip(Source())
                self.assertEqual(caught.exception.args, ('error return without exception set',))
                if failure == 'callback':
                    self.assertIs(caught.exception, marker)
                self.assertEqual(events, ['iter', 'hint'] if operation == 'filter' else ['hint'])

    def test_print_softspace_exchange_precedes_writer_and_str(self):
        events = []
        class Writer(object):
            flag = -1
            @property
            def softspace(self):
                events.append(('get', self.flag))
                return self.flag
            @softspace.setter
            def softspace(self, value):
                events.append(('set', value))
                self.flag = value
            @property
            def write(self):
                events.append('writer')
                return lambda text: events.append(('write', text))
        class Value(object):
            def __str__(self):
                events.append('str')
                return 'member'
        writer = Writer()
        print >>writer, Value(),
        self.assertEqual(events, [('get', -1), ('set', 0), 'writer', ('write', ' '),
                                  'writer', 'str', ('write', 'member'), ('get', 0), ('set', 1)])

    def test_print_ignores_nonint_softspace_and_suppresses_descriptor_errors(self):
        events = []
        class Writer(object):
            @property
            def softspace(self):
                events.append('get')
                if initial == 'error':
                    raise KeyboardInterrupt('read')
                return initial
            @softspace.setter
            def softspace(self, value):
                events.append(('set', value))
                raise ValueError('write flag')
            def write(self, text):
                events.append(('write', text))
        try:
            raise LookupError('outer')
        except LookupError as outer:
            for initial in (1L, 1.5, object(), 'error'):
                events[:] = []
                print >>Writer(), 'x',
                self.assertEqual(events, ['get', ('set', 0), ('write', 'x'), 'get', ('set', 1)])
                self.assertIs(sys.exc_info()[1], outer)
        sys.exc_clear()

    def test_print_pins_writer_before_conversion_callback_replaces_it(self):
        events = []
        class Writer(object):
            softspace = 0
            def write(self, text):
                events.append(('original', text))
        writer = Writer()
        class Value(object):
            def __str__(self):
                events.append('str')
                writer.write = lambda text: events.append(('replacement', text))
                return 'x'
        print >>writer, Value(),
        self.assertEqual(events, ['str', ('original', 'x')])
        print >>writer, 'y',
        self.assertEqual(events, ['str', ('original', 'x'), ('replacement', ' '), ('replacement', 'y')])

    def test_print_trailing_whitespace_uses_original_string_subtype(self):
        events = []
        class Writer(object):
            flag = 0
            @property
            def softspace(self):
                events.append(('get', self.flag))
                return self.flag
            @softspace.setter
            def softspace(self, value):
                events.append(('set', value))
                self.flag = value
            def write(self, text):
                events.append(('write', text))
        class Text(str):
            def __str__(self):
                return 'changed'
        writer = Writer()
        print >>writer, Text('original\n'),
        self.assertEqual(events, [('get', 0), ('set', 0), ('write', 'changed')])
        self.assertEqual(writer.flag, 0)

    def test_generator_argument_failures_keep_unstarted_frame(self):
        def produce():
            yield 43
        generator = produce()
        cases = (('send', (), {}, 'send() takes exactly one argument (0 given)'),
                 ('throw', (), {}, 'throw expected at least 1 arguments, got 0'),
                 ('throw', (None,), {}, 'exceptions must be classes, or instances, not NoneType'),
                 ('throw', (ValueError, None, 17), {}, 'throw() third argument must be a traceback object'),
                 ('close', (None,), {}, 'close() takes no arguments (1 given)'),
                 ('next', (None,), {}, 'expected 0 arguments, got 1'),
                 ('__iter__', (), {'value': None}, "wrapper __iter__ doesn't take keyword arguments"))
        for method, args, kwargs, message in cases:
            with self.assertRaises(TypeError) as caught:
                getattr(generator, method)(*args, **kwargs)
            self.assertEqual(caught.exception.args, (message,))
            self.assertFalse(generator.gi_running)
        self.assertEqual(generator.next(), 43)
        generator.close()

    def test_generator_send_throw_close_restore_parent_exception(self):
        events = []
        def produce():
            try:
                try:
                    yield ('initial', sys.exc_info()[0].__name__)
                except ValueError as error:
                    events.append(('caught', error.args))
                    yield ('handled', sys.exc_info()[0].__name__)
            finally:
                events.append(('finally', sys.exc_info()[0].__name__))
        try:
            raise LookupError('parent')
        except LookupError as parent:
            generator = produce()
            self.assertEqual(generator.next(), ('initial', 'LookupError'))
            self.assertIs(sys.exc_info()[1], parent)
            self.assertEqual(generator.throw(ValueError, 'injected'), ('handled', 'ValueError'))
            self.assertIs(sys.exc_info()[1], parent)
            generator.close()
            self.assertIs(sys.exc_info()[1], parent)
            self.assertIs(generator.gi_frame, None)
        sys.exc_clear()
        self.assertEqual(events, [('caught', ('injected',)), ('finally', 'LookupError')])
