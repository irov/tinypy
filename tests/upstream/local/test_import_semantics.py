"""Project-authored Python 2 import and reload semantics."""

import sys
import unittest


class ImportSemantics(unittest.TestCase):
    def failure(self, function, *args):
        try:
            function(*args)
        except Exception as error:
            return type(error).__name__ + ': ' + str(error)
        self.fail('expected an exception')

    def test_reload_of_unresolvable_module(self):
        module = type(sys)('tinypy_reload_missing')
        sys.modules['tinypy_reload_missing'] = module
        try:
            self.assertEqual(self.failure(reload, module), 'ImportError: No module named tinypy_reload_missing')
            self.assertTrue(sys.modules['tinypy_reload_missing'] is module)
        finally:
            del sys.modules['tinypy_reload_missing']

    def test_reload_of_submodule_names_the_component(self):
        parent = type(sys)('tinypy_reload_parent')
        child = type(sys)('tinypy_reload_parent.child')
        sys.modules['tinypy_reload_parent.child'] = child
        try:
            self.assertEqual(self.failure(reload, child), 'ImportError: reload(): parent tinypy_reload_parent not in sys.modules')
            sys.modules['tinypy_reload_parent'] = parent
            self.assertEqual(self.failure(reload, child), 'ImportError: No module named child')
        finally:
            sys.modules.pop('tinypy_reload_parent', None)
            del sys.modules['tinypy_reload_parent.child']

    def test_reload_of_builtin_module(self):
        self.assertTrue(reload(sys) is sys)

    def test_submodule_binding_failure_fails_the_import(self):
        # add_submodule: a module gets the submodule in its namespace whatever
        # its __setattr__ does; the error of another parent's setattr fails
        # the import, which leaves the submodule in sys.modules. The parents
        # stand in for this package, where the submodule is found.
        import local

        class Refusing(object):
            __path__ = local.__path__

            def __setattr__(self, name, value):
                raise ValueError('refused ' + name)

        class Module(type(sys)):
            def __setattr__(self, name, value):
                raise ValueError('refused ' + name)
        package = Module('local')
        package.__dict__['__path__'] = local.__path__
        try:
            sys.modules['local'] = Refusing()
            self.assertEqual(self.failure(__import__, 'local.review_bound_module'), 'ValueError: refused review_bound_module')
            self.assertEqual(sys.modules.pop('local.review_bound_module').VALUE, 1)
            self.assertEqual(self.failure(__import__, 'local', {}, {}, ['review_bound_module']), 'ValueError: refused review_bound_module')
            self.assertEqual(sys.modules.pop('local.review_bound_module').VALUE, 1)
            sys.modules['local'] = package
            self.assertTrue(__import__('local.review_bound_module') is package)
            self.assertTrue(package.review_bound_module is sys.modules['local.review_bound_module'])
        finally:
            sys.modules['local'] = local
            sys.modules.pop('local.review_bound_module', None)

    def test_missing_dotted_name_reports_the_rest(self):
        self.assertEqual(self.failure(__import__, 'tinypy_missing_top.sub'), 'ImportError: No module named tinypy_missing_top.sub')
        self.assertEqual(self.failure(__import__, 'tinypy_missing_top.sub.deeper'), 'ImportError: No module named tinypy_missing_top.sub.deeper')
        self.assertEqual(self.failure(__import__, 'sys.missing.deeper'), 'ImportError: No module named missing.deeper')
        package = type(sys)('tinypy_empty_package')
        package.__path__ = []
        sys.modules['tinypy_empty_package'] = package
        try:
            self.assertEqual(self.failure(__import__, 'tinypy_empty_package.missing.deeper'), 'ImportError: No module named missing.deeper')
            self.assertTrue(__import__('tinypy_empty_package', {}, {}, ['missing']) is package)
        finally:
            del sys.modules['tinypy_empty_package']
        self.assertEqual(self.failure(__import__, ''), 'ValueError: Empty module name')

    def test_system_exit_with_an_index_error_ends_no_name_walk(self):
        class Names(object):
            def __getitem__(self, index):
                if index == 1:
                    sys.exit(IndexError('stop'))
                return ['present'][index]

        def exit_code(function, *args):
            try:
                function(*args)
            except SystemExit as error:
                return repr(error.code)
            return 'finished'

        def import_star():
            exec 'from tinypy_exit_names import *' in {}

        package = type(sys)('tinypy_exit_names')
        package.__all__ = Names()
        package.__path__ = []
        package.present = 1
        sys.modules['tinypy_exit_names'] = package
        try:
            self.assertEqual(exit_code(import_star), "IndexError('stop',)")
            self.assertEqual(exit_code(__import__, 'tinypy_exit_names', {}, {}, Names()), "IndexError('stop',)")
        finally:
            del sys.modules['tinypy_exit_names']
