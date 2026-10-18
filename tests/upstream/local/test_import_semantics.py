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
        # init_builtin copies back the namespace saved when the module was
        # created.
        import __builtin__
        import exceptions
        saved_len = len
        saved_maxint = sys.maxint
        saved_meta_path = sys.meta_path
        del __builtin__.len
        sys.maxint = 5
        exceptions.ValueError = None
        meta_path = sys.meta_path = list(saved_meta_path)
        try:
            self.assertTrue(reload(__builtin__) is __builtin__)
            self.assertTrue(reload(sys) is sys)
            self.assertTrue(reload(exceptions) is exceptions)
            self.assertTrue(__builtin__.len is saved_len)
            self.assertEqual(sys.maxint, saved_maxint)
            self.assertTrue(exceptions.ValueError is ValueError)
            self.assertTrue(sys.meta_path is meta_path)
        finally:
            __builtin__.len = saved_len
            sys.maxint = saved_maxint
            sys.meta_path = saved_meta_path
            exceptions.ValueError = ValueError
        self.assertEqual(self.failure(reload, sys.modules['__main__']), 'ImportError: Cannot re-init internal module __main__')

    def test_removed_builtin_module_is_created_again(self):
        names = ('__builtin__', '__future__', '_codecs', '_functools', '_sre', '_struct', '_weakref', 'copy_reg', 'exceptions', 'sys')
        for name in names:
            module = __import__(name)
            del sys.modules[name]
            try:
                created = __import__(name)
                self.assertFalse(created is module)
                self.assertTrue(sys.modules[name] is created)
                self.assertEqual(created.__name__, name)
            finally:
                sys.modules[name] = module
        _struct = __import__('_struct')
        del sys.modules['_struct']
        try:
            self.assertEqual(__import__('_struct').calcsize('>d'), 8)
        finally:
            sys.modules['_struct'] = _struct

    def test_builtin_namespace_has_package_and_doc(self):
        import __builtin__
        self.assertTrue(__builtin__.__package__ is None)
        self.assertTrue('__doc__' in vars(__builtin__))
        namespace = {}
        exec 'package = __package__\ndocumented = __doc__ is not None' in namespace
        self.assertTrue(namespace['package'] is None)
        self.assertTrue(namespace['documented'])

    def test_import_name_with_null_bytes(self):
        self.assertEqual(self.failure(__import__, u'm\x00'), 'TypeError: __import__() argument 1 must be string without null bytes, not unicode')
        self.assertEqual(self.failure(__import__, 'm\x00'), 'TypeError: __import__() argument 1 must be string without null bytes, not str')

    def test_relative_import_continues_from_the_package(self):
        # get_parent takes the package from sys.modules, so its ancestors are
        # not imported again, whether they are missing or None.
        package = type(sys)('tinypy_relative_root.package')
        package.__path__ = []
        module = type(sys)('tinypy_relative_root.package.module')
        sys.modules['tinypy_relative_root.package'] = package
        sys.modules['tinypy_relative_root.package.module'] = module
        importer = {'__name__': 'tinypy_relative_root.package.importer'}
        try:
            self.assertTrue(__import__('module', importer, {}, ['VALUE'], 1) is module)
            self.assertTrue(__import__('module', importer, {}, ['VALUE'], -1) is module)
            sys.modules['tinypy_relative_root'] = None
            self.assertTrue(__import__('module', importer, {}, ['VALUE'], 1) is module)
            self.assertTrue(__import__('', importer, {}, ['module'], 1) is package)
        finally:
            for name in ('tinypy_relative_root', 'tinypy_relative_root.package', 'tinypy_relative_root.package.module'):
                sys.modules.pop(name, None)

    def test_relative_import_after_a_failed_package(self):
        # The subpackage of a failed package stays in sys.modules; a relative
        # import in it does not run the failed package again.
        self.assertEqual(self.failure(__import__, 'local.review_failed_package'), 'RuntimeError: package fails')
        try:
            relative = sys.modules['local.review_failed_package.sub.relative']
            self.assertEqual(relative.sibling_value(), 1)
            self.assertEqual(relative.RUNS, ['local.review_failed_package'])
            self.assertFalse('local.review_failed_package' in sys.modules)
        finally:
            for name in list(sys.modules):
                if name.startswith('local.review_failed_package'):
                    del sys.modules[name]

    def test_reload_during_its_reload_returns_the_module(self):
        from local import review_self_reload
        try:
            self.assertTrue(reload(review_self_reload) is review_self_reload)
            self.assertEqual(review_self_reload.RUNS, 2)
        finally:
            del sys.modules['local.review_self_reload']

    def test_reload_keeps_the_module_builtins(self):
        from local import review_own_builtins
        builtins = {'len': lambda items: 7}
        review_own_builtins.__builtins__ = builtins
        try:
            self.assertTrue(reload(review_own_builtins) is review_own_builtins)
            self.assertEqual(review_own_builtins.VALUE, 7)
            self.assertTrue(review_own_builtins.__builtins__ is builtins)
        finally:
            del sys.modules['local.review_own_builtins']

    def test_meta_path_finders(self):
        # PEP 302: sys.meta_path finders are asked first; an ImportError of
        # find_module means that nobody provides the module, and a loader
        # returning None reports it as missing.
        calls = []

        class Loader(object):
            def __init__(self, kind):
                self.kind = kind

            def load_module(self, name):
                if self.kind == 'none':
                    return None
                if self.kind == 'fresh':
                    return type(sys)(name)
                module = sys.modules.setdefault(name, type(sys)(name))
                if name == 'tinypy_meta_package':
                    module.__path__ = ['tinypy-meta']
                module.loads = getattr(module, 'loads', 0) + 1
                return module

        class Finder(object):
            def find_module(self, name, path=None):
                calls.append((name, path))
                if name in ('tinypy_meta_module', 'tinypy_meta_package', 'tinypy_meta_package.sub'):
                    return Loader('module')
                if name == 'tinypy_meta_none':
                    return Loader('none')
                if name == 'tinypy_meta_fresh':
                    return Loader('fresh')
                if name == 'tinypy_meta_error':
                    raise ValueError('finder failed')
                if name.startswith('tinypy_meta'):
                    raise ImportError('finder has no ' + name)
                return None

        finder = Finder()
        sys.meta_path.insert(0, finder)
        try:
            module = __import__('tinypy_meta_module')
            self.assertEqual((module.loads, calls[-1]), (1, ('tinypy_meta_module', None)))
            package = __import__('tinypy_meta_package.sub')
            self.assertEqual(calls[-2:], [('tinypy_meta_package', None), ('tinypy_meta_package.sub', ['tinypy-meta'])])
            self.assertTrue(package.sub is sys.modules['tinypy_meta_package.sub'])
            self.assertEqual(self.failure(__import__, 'tinypy_meta_missing'), 'ImportError: No module named tinypy_meta_missing')
            self.assertTrue(__import__('tinypy_meta_package', {}, {}, ['missing']) is package)
            self.assertFalse(hasattr(package, 'missing'))
            importer = {'__name__': 'tinypy_meta_package.importer'}
            self.assertTrue(__import__('tinypy_meta_module', importer, {}, [], -1) is module)
            self.assertTrue(sys.modules['tinypy_meta_package.tinypy_meta_module'] is None)
            self.assertEqual(self.failure(__import__, 'tinypy_meta_none'), 'ImportError: No module named tinypy_meta_none')
            self.assertEqual(self.failure(__import__, 'tinypy_meta_error'), 'ValueError: finder failed')
            registered = type(sys)('tinypy_meta_fresh')
            sys.modules['tinypy_meta_fresh'] = registered
            reloaded = reload(registered)
            self.assertEqual(reloaded.__name__, 'tinypy_meta_fresh')
            self.assertFalse(reloaded is registered)
            self.assertTrue(sys.modules['tinypy_meta_fresh'] is registered)
        finally:
            sys.meta_path.remove(finder)
            for name in list(sys.modules):
                if name.startswith('tinypy_meta'):
                    del sys.modules[name]

    def test_search_path_is_read_when_importing(self):
        # find_module searches sys.path and the __path__ of a package as the
        # program leaves them.
        import local
        directory = local.__path__[0] + '/review_path_entry'
        sys.path.insert(0, directory)
        try:
            self.assertEqual(__import__('review_path_module').VALUE, 'path entry')
        finally:
            sys.path.remove(directory)
            sys.modules.pop('review_path_module', None)
        package = type(sys)('tinypy_extended_package')
        package.__path__ = []
        sys.modules['tinypy_extended_package'] = package
        try:
            self.assertEqual(self.failure(__import__, 'tinypy_extended_package.review_path_module'), 'ImportError: No module named review_path_module')
            package.__path__.append(directory)
            self.assertEqual(__import__('tinypy_extended_package.review_path_module').review_path_module.VALUE, 'path entry')
        finally:
            sys.modules.pop('tinypy_extended_package', None)
            sys.modules.pop('tinypy_extended_package.review_path_module', None)

    def test_package_directory_comes_before_module_file(self):
        from local import review_shadowed
        try:
            self.assertEqual(review_shadowed.KIND, 'package')
            self.assertEqual(self.failure(__import__, 'local.Review_shadowed'), 'ImportError: No module named Review_shadowed')
        finally:
            del sys.modules['local.review_shadowed']

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

    def test_star_import_requires_callable_keys(self):
        # import_all_from takes the names of __dict__ with PyMapping_Keys,
        # whose PyObject_CallMethod requires keys to be callable.
        class Namespace(object):
            keys = None

        class Module(object):
            def __getattribute__(self, name):
                if name == '__dict__':
                    return Namespace()
                raise AttributeError(name)

        def import_star():
            exec 'from tinypy_uncallable_keys import *' in {}

        sys.modules['tinypy_uncallable_keys'] = Module()
        try:
            self.assertEqual(self.failure(import_star), "TypeError: attribute of type 'NoneType' is not callable")
        finally:
            del sys.modules['tinypy_uncallable_keys']
