"""Finite Python 2 namespace products: order, callbacks, errors and state."""

import sys


def outcome(call):
    try:
        return ('value', call())
    except BaseException as error:
        return ('error', type(error).__name__, error.args)


class Mapping(object):
    def __init__(self, events, failure):
        self.events = events
        self.failure = failure
        self.data = {'name': 11}
    def __getitem__(self, key):
        self.events.append(('get', key))
        if self.failure in ('get', 'all'):
            raise ValueError('get payload')
        return self.data[key]
    def __setitem__(self, key, value):
        self.events.append(('set', key, value))
        if self.failure in ('set', 'all'):
            raise ValueError('set payload')
        self.data[key] = value
    def __delitem__(self, key):
        self.events.append(('del', key))
        if self.failure in ('del', 'all'):
            raise ValueError('del payload')
        del self.data[key]


class Dictionary(dict):
    def __getitem__(self, key):
        events.append(('getitem', key))
        return dict.__getitem__(self, key)
    def __missing__(self, key):
        events.append(('missing', key))
        raise KeyError(key)
    def __setitem__(self, key, value):
        events.append(('setitem', key, value))
        dict.__setitem__(self, key, value)
    def __delitem__(self, key):
        events.append(('delitem', key))
        dict.__delitem__(self, key)


sources = ('17', 'name', 'abs(-3)', 'missing', 'result=name', 'del name',
           'del missing', 'name=19\nraise ValueError("stop")',
           'global name\nname=23', 'global name\ndel name')
for form in ('bytes', 'unicode', 'code'):
    for index, source in enumerate(sources):
        mode = 'eval' if index < 4 else 'exec'
        for failure in ('none', 'get', 'set', 'del', 'all', 'dict'):
            events = []
            scope = {'name': 7}
            local = Dictionary(name=11) if failure == 'dict' else Mapping(events, failure)
            command = source if form == 'bytes' else unicode(source) if form == 'unicode' else compile(source, '<namespace>', mode)
            def run():
                if mode == 'eval':
                    return eval(command, scope, local)
                exec command in scope, local
            result = outcome(run)
            data = local if failure == 'dict' else local.data
            print 'mapping/%s/%d/%s\t%s' % (form, index, failure,
                repr((result, events, sorted(data.items()), scope.get('name'))))

invalid_sources = ('name\0', u'name\0', 17, bytearray('17'), buffer('17'), memoryview('17'))
for index, source in enumerate(invalid_sources):
    for mode in ('eval', 'exec', 'tuple2', 'tuple3'):
        for invalid in ('none', 'globals', 'locals', 'both'):
            scope = {} if invalid not in ('globals', 'both') else 7
            local = {} if invalid not in ('locals', 'both') else 9
            def run():
                if mode == 'eval':
                    return eval(source, scope, local)
                if mode == 'exec':
                    exec source in scope, local
                elif mode == 'tuple2':
                    exec (source, scope)
                else:
                    exec (source, scope, local)
            result = outcome(run)
            print 'arguments/%d/%s/%s\t%s' % (index, mode, invalid,
                repr((result, '__builtins__' in scope if isinstance(scope, dict) else None)))

for fail_at in (0, 1, 2, 3, 4):
    events = []
    class BuiltinKey(str):
        def __eq__(self, other):
            events.append(('eq', str(other)))
            if fail_at and len(events) >= fail_at:
                raise ValueError('lookup')
            return False
        __hash__ = str.__hash__
    scope = {BuiltinKey('__builtins__'): None}
    result = outcome(lambda: eval('17', scope))
    print 'builtin-key/%s\t%s' % (fail_at, repr((result, events, len(scope))))

global_sources = ('name', 'abs(-3)', 'global name\nresult = name',
                  'global missing\nresult = missing', 'global name\nname = 19',
                  'global name\ndel name')
for form in ('bytes', 'unicode', 'code'):
    for index, source in enumerate(global_sources):
        events = []
        scope = Dictionary(name=7, __builtins__={'abs': abs})
        mode = 'eval' if index < 2 else 'exec'
        command = source if form == 'bytes' else unicode(source) if form == 'unicode' else compile(source, '<namespace>', mode)
        def run():
            if mode == 'eval':
                return eval(command, scope)
            exec command in scope
        result = outcome(run)
        print 'globals-subtype/%s/%d\t%s' % (form, index, repr((result, events)))

name_sources = ('global name\nresult = (name, name)', 'result = (name, name)',
                'global name\nname = 19', 'global name\ndel name', 'del name')
for index, source in enumerate(name_sources):
    for action in ('equal', 'different', 'raise'):
        for key_base in (str, unicode):
            for scope_type in (dict, Dictionary):
                events = []
                class Name(key_base):
                    def __eq__(self, other):
                        events.append(('eq', str(other)))
                        if action == 'raise':
                            raise ValueError('key payload')
                        return action == 'equal'
                    __hash__ = key_base.__hash__
                key = Name('name')
                scope = scope_type({key: 7, '__builtins__': {'name': 11}})
                code = compile(source, '<namespace>', 'exec')
                for repeat in (0, 1):
                    def run():
                        exec code in scope
                    result = outcome(run)
                    print 'name-key/%d/%s/%s/%s/%d\t%s' % (index, action,
                        key_base.__name__, scope_type.__name__, repeat,
                        repr((result, events, dict.get(scope, 'result'), len(scope))))
                    events[:] = []
for derived in (False, True):
    class ChildModule(type(sys)):
        pass
    module_type = ChildModule if derived else type(sys)
    for state in ('allocated', 'initialized', 'attribute'):
        for operation in ('fromlist', 'star'):
            module = module_type.__new__(module_type)
            if state == 'initialized':
                module.__init__('matrix_module')
            elif state == 'attribute':
                module.member = 17
            name = '_tinypy_namespace_matrix_module'
            sys.modules[name] = module
            scope = {}
            try:
                def run():
                    if operation == 'fromlist':
                        return __import__(name, {}, {}, ['missing'], 0) is module
                    exec 'from _tinypy_namespace_matrix_module import *' in scope
                    return sorted((key, value) for key, value in scope.items()
                                  if key != '__builtins__')
                result = outcome(run)
                dictionary = module.__dict__
                print 'nullable-module/%s/%s/%s\t%s' % (derived, state, operation,
                    repr((result, None if dictionary is None else sorted(dictionary))))
            finally:
                del sys.modules[name]
for derived in (False, True):
    class PackageModule(type(sys)):
        pass
    module_type = PackageModule if derived else type(sys)
    for state in ('allocated', 'initialized', 'named', 'renamed'):
        for invalid_all in (False, True):
            for index, fromlist in enumerate(([1], ['missing'], ['*'], ['member'], [])):
                module = module_type.__new__(module_type)
                if state == 'initialized':
                    module.__init__('different_cached_name')
                elif state == 'named':
                    module.__name__ = 'different_cached_name'
                elif state == 'renamed':
                    module.__init__('initial_cached_name')
                    module.__name__ = 'different_cached_name'
                module.__path__ = []
                module.member = 17
                if invalid_all:
                    module.__all__ = [1]
                name = '_tinypy_namespace_matrix_package'
                sys.modules[name] = module
                try:
                    result = outcome(lambda: __import__(name, {}, {}, fromlist, 0) is module)
                    print 'nullable-package/%s/%s/%s/%s\t%s' % (derived, state,
                        invalid_all, index, repr((result, sorted(module.__dict__))))
                finally:
                    del sys.modules[name]
sys.exc_clear()
