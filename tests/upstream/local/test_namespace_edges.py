"""Project-authored Python 2 namespace callbacks and execution regressions."""

import sys
import unittest


class NamespaceEdges(unittest.TestCase):
    def test_allocation_only_package_checks_fromlist_and_all(self):
        class ChildModule(type(sys)):
            pass
        for module_type in (type(sys), ChildModule):
            module = module_type.__new__(module_type)
            name = '_tinypy_namespace_nullable_package'
            module.__name__ = 'different_cached_name'
            module.__path__ = []
            module.__all__ = [1]
            sys.modules[name] = module
            try:
                for fromlist in ([1], ['*']):
                    with self.assertRaises(TypeError) as caught:
                        __import__(name, {}, {}, fromlist, 0)
                    self.assertEqual(caught.exception.args,
                                     ("Item in ``from list'' must be str, not int",))
                self.assertIs(__import__(name, {}, {}, ['missing'], 0), module)
                self.assertEqual(module.__name__, 'different_cached_name')
            finally:
                del sys.modules[name]

    def test_uninitialized_module_fromlist_keeps_namespace_uncreated(self):
        module_type = type(sys)
        module = module_type.__new__(module_type)
        name = '_tinypy_namespace_nullable_module'
        self.assertNotIn(name, sys.modules)
        sys.modules[name] = module
        try:
            self.assertIs(__import__(name, {}, {}, ['missing'], 0), module)
            self.assertIs(module.__dict__, None)
        finally:
            del sys.modules[name]

    def test_uninitialized_module_star_reports_missing_namespace_keys(self):
        module_type = type(sys)
        module = module_type.__new__(module_type)
        name = '_tinypy_namespace_nullable_module'
        self.assertNotIn(name, sys.modules)
        sys.modules[name] = module
        scope = {}
        try:
            with self.assertRaises(AttributeError) as caught:
                exec 'from _tinypy_namespace_nullable_module import *' in scope
            self.assertEqual(caught.exception.args,
                             ("'NoneType' object has no attribute 'keys'",))
            self.assertIs(module.__dict__, None)
            self.assertIn('__builtins__', scope)
        finally:
            del sys.modules[name]

    def test_uninitialized_module_builtins_creates_empty_dictionary(self):
        module_type = type(sys)
        module = module_type.__new__(module_type)
        self.assertIs(module.__dict__, None)
        scope = {'__builtins__': module}
        self.assertEqual(eval('17', scope), 17)
        self.assertEqual(module.__dict__, {})
        self.assertIs(scope['__builtins__'], module)
        self.assertIs(eval('None', scope), None)
        with self.assertRaises(NameError):
            eval('len([])', scope)

    def test_exec_byte_source_rejects_nul_after_builtins_insertion(self):
        scope = {}
        with self.assertRaises(TypeError) as caught:
            exec 'name\0' in scope
        self.assertEqual(caught.exception.args, ('expected string without null bytes',))
        self.assertIn('__builtins__', scope)

    def test_exec_unicode_source_rejects_nul(self):
        with self.assertRaises(TypeError) as caught:
            exec u'name\0' in {}
        self.assertEqual(caught.exception.args, ('expected string without null bytes',))

    def test_tuple_exec_nul_error_matches_explicit_namespaces(self):
        for count in (2, 3):
            scope = {}
            command = ('name\0', scope) if count == 2 else ('name\0', scope, {})
            with self.assertRaises(TypeError) as caught:
                exec command
            self.assertEqual(caught.exception.args, ('expected string without null bytes',))
            self.assertIn('__builtins__', scope)

    def test_exec_namespace_errors_precede_nul_error(self):
        for scope, local, message in ((17, {}, 'exec: arg 2 must be a dictionary or None'),
                                       ({}, 17, 'exec: arg 3 must be a mapping or None')):
            with self.assertRaises(TypeError) as caught:
                exec 'name\0' in scope, local
            self.assertEqual(caught.exception.args, (message,))

    def test_eval_builtins_insertion_failure_stops_before_execution(self):
        events = []
        class Key(str):
            def __eq__(self, other):
                events.append(str(other))
                raise ValueError('builtin lookup')
            __hash__ = str.__hash__
        key = Key('__builtins__')
        scope = {key: None}
        with self.assertRaises(ValueError) as caught:
            eval('17', scope)
        self.assertEqual(caught.exception.args, ('builtin lookup',))
        self.assertEqual(events, ['__builtins__', '__builtins__'])
        self.assertEqual(len(scope), 1)
        self.assertEqual(eval('19', {}), 19)

    def test_eval_insertion_failure_precedes_source_type_error(self):
        class Key(str):
            def __eq__(self, other):
                raise LookupError('insertion')
            __hash__ = str.__hash__
        with self.assertRaisesRegexp(LookupError, 'insertion'):
            eval(17, {Key('__builtins__'): None})

    def test_frame_builtins_lookup_consumes_failure(self):
        events = []
        class Key(str):
            def __eq__(self, other):
                events.append(str(other))
                if len(events) == 3:
                    raise ValueError('frame lookup')
                return False
            __hash__ = str.__hash__
        scope = {Key('__builtins__'): None}
        self.assertEqual(eval('17', scope), 17)
        self.assertEqual(events, ['__builtins__'] * 3)
        self.assertEqual(len(scope), 2)

    def test_ignored_lookup_preserves_handled_exception(self):
        class Key(str):
            def __eq__(self, other):
                raise KeyboardInterrupt('ignored lookup')
            __hash__ = str.__hash__
        marker = ValueError('previous')
        try:
            raise marker
        except ValueError:
            self.assertEqual(eval('name', {Key('name'): 7, '__builtins__': {'name': 11}}), 11)
            self.assertIs(sys.exc_info()[0], ValueError)
            self.assertIs(sys.exc_info()[1], marker)
        sys.exc_clear()

    def test_store_global_bypasses_dictionary_subtype_setitem(self):
        events = []
        class Scope(dict):
            def __setitem__(self, key, value):
                events.append(key)
                raise ValueError('subtype setter')
        scope = Scope(name=7)
        exec 'global name\nname = 19' in scope
        self.assertEqual(scope['name'], 19)
        self.assertEqual(events, [])

    def test_delete_global_bypasses_dictionary_subtype_delitem(self):
        events = []
        class Scope(dict):
            def __delitem__(self, key):
                events.append(key)
                raise ValueError('subtype deleter')
        scope = Scope(name=7)
        exec 'global name\ndel name' in scope
        self.assertNotIn('name', scope)
        self.assertEqual(events, [])

    def test_local_store_and_delete_use_dictionary_subtype_hooks(self):
        events = []
        class Scope(dict):
            def __setitem__(self, key, value):
                events.append(('set', key, value))
                dict.__setitem__(self, key, value)
            def __delitem__(self, key):
                events.append(('del', key))
                dict.__delitem__(self, key)
        scope = Scope()
        exec 'name = 19\ndel name' in {}, scope
        self.assertEqual(events, [('set', 'name', 19), ('del', 'name')])

    def test_global_name_cache_preserves_equality_callbacks(self):
        events = []
        class Key(str):
            def __eq__(self, other):
                events.append(str(other))
                return True
            __hash__ = str.__hash__
        scope = {Key('name'): 7}
        code = compile('global name\nresult = (name, name)', '<namespace>', 'exec')
        exec code in scope
        exec code in scope
        self.assertEqual(scope['result'], (7, 7))
        self.assertEqual(events, ['name'] * 4)

    def test_global_name_cache_preserves_callbacks_after_lookup_restart(self):
        events = []
        scope = {}
        class Replacement(str):
            def __eq__(self, other):
                events.append('replacement')
                return True
            __hash__ = str.__hash__
        replacement = Replacement('name')
        class Original(str):
            def __eq__(self, other):
                events.append('original')
                scope.clear()
                scope[replacement] = 11
                return True
            __hash__ = str.__hash__
        original = Original('name')
        scope[original] = 7
        try:
            exec 'global name\nresult = (name, name)' in scope
            self.assertEqual(scope['result'], (11, 11))
            self.assertEqual(events, ['original', 'replacement', 'replacement'])
        finally:
            scope.clear()

    def test_builtin_name_cache_preserves_equality_callbacks(self):
        events = []
        class Key(str):
            def __eq__(self, other):
                events.append(str(other))
                return True
            __hash__ = str.__hash__
        scope = {'__builtins__': {Key('name'): 11}}
        exec 'global name\nresult = (name, name)' in scope
        self.assertEqual(scope['result'], (11, 11))
        self.assertEqual(events, ['name', 'name'])

    def test_global_miss_cache_preserves_equality_callbacks(self):
        events = []
        class Key(str):
            def __eq__(self, other):
                events.append(str(other))
                return False
            __hash__ = str.__hash__
        scope = {Key('name'): 7, '__builtins__': {'name': 11}}
        exec 'global name\nresult = (name, name)' in scope
        self.assertEqual(scope['result'], (11, 11))
        self.assertEqual(events, ['name', 'name'])

    def test_global_lookup_propagates_equality_failure(self):
        events = []
        class Key(str):
            def __eq__(self, other):
                events.append(str(other))
                raise ValueError('global lookup')
            __hash__ = str.__hash__
        scope = {Key('name'): 7, '__builtins__': {'name': 11}}
        with self.assertRaisesRegexp(ValueError, 'global lookup'):
            exec 'global name\nresult = name' in scope
        self.assertEqual(events, ['name'])
        self.assertNotIn('result', scope)

    def test_exact_local_lookup_consumes_equality_failure(self):
        events = []
        class Key(str):
            def __eq__(self, other):
                events.append(str(other))
                raise ValueError('local lookup')
            __hash__ = str.__hash__
        scope = {Key('name'): 7, '__builtins__': {'name': 11}}
        self.assertEqual(eval('name', scope), 11)
        self.assertEqual(events, ['name', 'name'])

    def test_local_dictionary_subtype_lookup_propagates_failure(self):
        class Scope(dict):
            def __getitem__(self, key):
                raise ValueError('mapping lookup')
        with self.assertRaisesRegexp(ValueError, 'mapping lookup'):
            eval('name', {'name': 7}, Scope())

    def test_store_name_propagates_dictionary_equality_failure(self):
        class Key(str):
            def __eq__(self, other):
                raise ValueError('store lookup')
            __hash__ = str.__hash__
        for statement in ('name = 19', 'global name\nname = 19'):
            key = Key('name')
            scope = {key: 7}
            with self.assertRaisesRegexp(ValueError, 'store lookup'):
                exec statement in scope
            self.assertEqual(scope[key], 7)
            self.assertEqual(len(scope), 2)

    def test_delete_name_uses_one_dictionary_lookup(self):
        for statement in ('del name', 'global name\ndel name'):
            events = []
            class Key(str):
                def __eq__(self, other):
                    events.append(str(other))
                    return True
                __hash__ = str.__hash__
            scope = {Key('name'): 7}
            exec statement in scope
            self.assertEqual(events, ['name'])
            self.assertEqual(len(scope), 1)

    def test_delete_name_replaces_comparison_failure_with_name_error(self):
        class Key(str):
            def __eq__(self, other):
                raise ValueError('delete lookup')
            __hash__ = str.__hash__
        for statement, message in (('del name', "name 'name' is not defined"),
                                   ('global name\ndel name', "global name 'name' is not defined")):
            with self.assertRaises(NameError) as caught:
                exec statement in {Key('name'): 7}
            self.assertEqual(caught.exception.args, (message,))
