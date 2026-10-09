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
