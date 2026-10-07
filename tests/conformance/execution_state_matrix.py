"""Finite Python 2 import/print/call products with callback and recovery state."""

import sys


def outcome(call):
    try:
        return ('value', call())
    except BaseException as error:
        message = error.args
        # CPython's negative-size allocation diagnostic includes its build path.
        # Keep the SystemError and reason; the C source location is build metadata.
        if isinstance(error, SystemError) and len(message) == 1 and message[0].endswith('bad argument to internal function'):
            message = ('bad argument to internal function',)
        return ('error', type(error).__name__, message)


def emit(identity, value):
    print identity + '\t' + repr(value)


name = '_tinypy_execution_product'
for kind in ('object', 'module'):
    for field in ('__path__', '__all__', 'member'):
        for failure in ('none', 'attribute', 'value', 'base'):
            for operation in ('fromlist', 'star'):
                events = []
                base = object if kind == 'object' else type(sys)
                class Carrier(base):
                    def __getattribute__(self, key):
                        events.append(key)
                        if key == field and failure != 'none':
                            raise {'attribute': AttributeError, 'value': ValueError, 'base': KeyboardInterrupt}[failure]('lookup')
                        return base.__getattribute__(self, key)
                module = Carrier() if kind == 'object' else Carrier(name)
                module.__path__ = []
                module.__all__ = ['member']
                module.member = 17
                sys.modules[name] = module
                events[:] = []
                scope = {}
                def run():
                    if operation == 'fromlist':
                        return __import__(name, {}, {}, ['*'], 0) is module
                    exec 'from _tinypy_execution_product import *' in scope
                    return scope.get('member')
                try:
                    result = outcome(run)
                    emit('lookup/%s/%s/%s/%s' % (kind, field, failure, operation),
                         (result, events, scope.get('member')))
                finally:
                    del sys.modules[name]
                    sys.exc_clear()


def indexed_names(kind, values, events, tail='index'):
    def get(index):
        events.append(('get', index))
        if index >= len(values):
            raise {'index': IndexError, 'stop': StopIteration, 'value': ValueError,
                   'base': KeyboardInterrupt}[tail]('tail')
        return values[index]
    class List(list):
        def __getitem__(self, index):
            return get(index)
        def __iter__(self):
            events.append('iter')
            return iter(['other'])
    class Tuple(tuple):
        def __getitem__(self, index):
            return get(index)
        def __iter__(self):
            events.append('iter')
            return iter(['other'])
    class Sequence(object):
        def __getitem__(self, index):
            return get(index)
        def __iter__(self):
            events.append('iter')
            return iter(['other'])
    class Classic:
        def __getitem__(self, index):
            return get(index)
    class Mapping(dict):
        def __getitem__(self, index):
            return get(index)
    if kind == 'list':
        return list(values)
    if kind == 'tuple':
        return tuple(values)
    if kind == 'list-subtype':
        return List(values)
    if kind == 'tuple-subtype':
        return Tuple(values)
    if kind == 'sequence':
        return Sequence()
    if kind == 'classic':
        return Classic()
    if kind == 'mapping-subtype':
        return Mapping()
    if kind == 'mapping':
        return {0: values[0]}
    if kind == 'iterator':
        return iter(values)
    if kind == 'generator':
        return (item for item in values)
    if kind == 'bytearray':
        return bytearray('member')
    if kind == 'buffer':
        return buffer('member')
    if kind == 'memoryview':
        return memoryview('member')
    if kind == 'string':
        return 'member'
    if kind == 'unicode':
        return [u'member']
    return ['member\0ignored']


sequence_kinds = ('list', 'tuple', 'list-subtype', 'tuple-subtype', 'sequence', 'classic',
                  'mapping-subtype', 'mapping', 'iterator', 'generator', 'bytearray',
                  'buffer', 'memoryview', 'string', 'unicode', 'nul')
for kind in sequence_kinds:
    for operation in ('fromlist', 'star'):
        events = []
        names = indexed_names(kind, ['member'], events)
        module = type(sys)(name)
        module.__path__ = []
        module.member = 19
        module.other = 23
        sys.modules[name] = module
        scope = {}
        def run():
            if operation == 'fromlist':
                return __import__(name, {}, {}, names, 0) is module
            module.__all__ = names
            exec 'from _tinypy_execution_product import *' in scope
            return scope.get('member')
        try:
            emit('names/%s/%s' % (kind, operation), (outcome(run), events, scope.get('member')))
        finally:
            del sys.modules[name]
            sys.exc_clear()

for kind in ('list-subtype', 'tuple-subtype', 'sequence', 'classic', 'mapping-subtype'):
    for tail in ('index', 'stop', 'value', 'base'):
        for operation in ('fromlist', 'star'):
            events = []
            module = type(sys)(name)
            module.__path__ = []
            module.member = 29
            names = indexed_names(kind, ['member'], events, tail)
            sys.modules[name] = module
            scope = {}
            def run():
                if operation == 'fromlist':
                    return __import__(name, {}, {}, names, 0) is module
                module.__all__ = names
                exec 'from _tinypy_execution_product import *' in scope
                return scope.get('member')
            try:
                emit('tail/%s/%s/%s' % (kind, tail, operation), (outcome(run), events, scope.get('member')))
            finally:
                del sys.modules[name]
                sys.exc_clear()

for path in (False, True):
    for truth in ('false', 'true', 'value', 'base', 'replace'):
        for cached in (False, True):
            events = []
            module = type(sys)(name)
            module.member = 31
            if path:
                module.__path__ = []
            class Names(list):
                def __nonzero__(self):
                    events.append('truth')
                    if truth == 'value':
                        raise ValueError('truth')
                    if truth == 'base':
                        raise KeyboardInterrupt('truth')
                    if truth == 'replace':
                        sys.modules[name] = None
                    return truth != 'false'
                def __getitem__(self, index):
                    events.append(('get', index))
                    return list.__getitem__(self, index)
            if cached:
                sys.modules[name] = module
            try:
                result = outcome(lambda: __import__(name, {}, {}, Names(['member']), 0) is module)
                emit('truth/%d/%s/%d' % (path, truth, cached), (result, events, sys.modules.get(name) is module))
            finally:
                sys.modules.pop(name, None)
                sys.exc_clear()

for field in ('__package__', '__name__'):
    for value_index, value in enumerate((None, 17, u'sys', '', 'sys', 'sys.child', 'sys\0ignored')):
        for level in (0, 1, 2):
            scope = {field: value}
            def run():
                return __import__('sys' if field == '__name__' else '', scope, {}, [], level) is sys
            emit('metadata/%s/%d/%d' % (field, value_index, level),
                 (outcome(run), scope.get('__package__')))
            sys.exc_clear()


argument_cases = (((), {}), ((), {'other': 1}), ((17,), {'other': 1}), ((None,), {}),
                  (('sys\0ignored',), {}), ((buffer('sys'),), {}), ((u'sys', {}, {}, [], 0), {}),
                  (('sys',), {'name': 'sys'}), (('sys',), {'other': 1}),
                  (('sys',) * 6, {}), (('sys', {}, {}, [], 0), {'other': 1}),
                  (('sys', {}, {}, [], 1.5), {'other': 1}))
for index, (args, kwargs) in enumerate(argument_cases):
    emit('arguments/%d' % index, outcome(lambda: __import__(*args, **kwargs) is sys))
    sys.exc_clear()

for result_index, converted in enumerate((0, 0L, True, 1.5, '0', 1L << 32, -(1L << 32), 1L << 65)):
    for protocol in ('int', 'index', 'both', 'value', 'base'):
        events = []
        def convert(self):
            events.append('int')
            if protocol in ('value', 'base'):
                raise {'value': ValueError, 'base': KeyboardInterrupt}[protocol]('conversion')
            return converted
        def index(self):
            events.append('index')
            return 0
        fields = {'__int__': convert} if protocol != 'index' else {}
        if protocol in ('index', 'both'):
            fields['__index__'] = index
        level = type('Level', (object,), fields)()
        emit('level/%d/%s' % (result_index, protocol),
             (outcome(lambda: __import__('sys', {}, {}, [], level) is sys), events))
        sys.exc_clear()

for keyword in ('name', 'globals', 'fromlist', 'level', 'other'):
    for behavior in ('true', 'false', 'value', 'base'):
        events = []
        class Key(str):
            __hash__ = str.__hash__
            def __eq__(self, other):
                events.append(str(other))
                if behavior in ('value', 'base'):
                    raise {'value': ValueError, 'base': KeyboardInterrupt}[behavior]('keyword')
                return behavior == 'true'
        values = {'name': 'sys', 'globals': {}, 'fromlist': [], 'level': 0, 'other': 17}
        args = () if keyword == 'name' else ('sys',)
        emit('keyword/%s/%s' % (keyword, behavior),
             (outcome(lambda: __import__(*args, **{Key(keyword): values[keyword]}) is sys), events))
        sys.exc_clear()

for operation in ('filter', 'zip'):
    for hint_index, hint in enumerate((-3, -2, -1, 0, 2, 'value', 'base', 'type', 'attribute', 'system')):
        events = []
        class Source(object):
            def __iter__(self):
                events.append('iter')
                return iter([1, 2])
            def __length_hint__(self):
                events.append('hint')
                if isinstance(hint, str):
                    raise {'value': ValueError, 'base': KeyboardInterrupt, 'type': TypeError,
                           'attribute': AttributeError, 'system': SystemError}[hint]('hint payload')
                return hint
        emit('hint/%s/%d' % (operation, hint_index),
             (outcome(lambda: filter(None, Source()) if operation == 'filter' else zip(Source())), events))
        sys.exc_clear()


for initial_index, initial in enumerate((0, 1, -1, True, 1L, 1.5, None, 'numeric')):
    for item_index, item_kind in enumerate(('bytes', 'newline', 'unicode', 'object', 'mutation', 'str-subtype')):
        for failure in ('none', 'get', 'set', 'writer', 'str', 'write'):
            events = []
            class Numeric(object):
                def __nonzero__(self):
                    events.append('truth')
                    raise ValueError('must not be tested')
            class Writer(object):
                def __init__(self):
                    self.flag = Numeric() if initial == 'numeric' else initial
                @property
                def softspace(self):
                    events.append(('get', 'numeric' if isinstance(self.flag, Numeric) else self.flag))
                    if failure == 'get':
                        raise KeyboardInterrupt('read flag')
                    return self.flag
                @softspace.setter
                def softspace(self, flag):
                    events.append(('set', flag))
                    if failure == 'set':
                        raise ValueError('set flag')
                    self.flag = flag
                @property
                def write(self):
                    events.append('writer')
                    if failure == 'writer':
                        raise LookupError('writer')
                    def write(text):
                        events.append(('write', text))
                        if failure == 'write':
                            raise ValueError('write')
                    return write
            writer = Writer()
            class Value(object):
                def __str__(self):
                    events.append('str')
                    if failure == 'str':
                        raise ValueError('convert')
                    if item_kind == 'mutation':
                        Writer.write = lambda self, text: events.append(('replacement', text))
                    return 'object'
            class Text(str):
                def __str__(self):
                    events.append('str')
                    return 'converted'
            item = {'bytes': 'x', 'newline': 'x\n', 'unicode': u'x\u2003'}.get(item_kind)
            if item_kind in ('object', 'mutation'):
                item = Value()
            elif item_kind == 'str-subtype':
                item = Text('original\n')
            def run():
                print >>writer, item,
            result = outcome(run)
            flag = 'numeric' if isinstance(writer.flag, Numeric) else writer.flag
            emit('print/%d/%d/%s' % (initial_index, item_index, failure), (result, events, flag))
            sys.exc_clear()

for initial in (0, 1):
    for failure in ('none', 'get', 'set', 'writer', 'write'):
        events = []
        class Writer(object):
            flag = initial
            @property
            def softspace(self):
                events.append(('get', self.flag))
                if failure == 'get':
                    raise KeyboardInterrupt('read flag')
                return self.flag
            @softspace.setter
            def softspace(self, flag):
                events.append(('set', flag))
                if failure == 'set':
                    raise ValueError('set flag')
                self.flag = flag
            @property
            def write(self):
                events.append('writer')
                if failure == 'writer':
                    raise LookupError('writer')
                def write(text):
                    events.append(('write', text))
                    if failure == 'write':
                        raise ValueError('write')
                return write
        writer = Writer()
        def run():
            print >>writer
        emit('newline/%d/%s' % (initial, failure), (outcome(run), events, writer.flag))
        sys.exc_clear()

def produce():
    yield 37
for method in ('send', 'throw', 'close', 'next', '__iter__'):
    for started in (False, True):
        for index, (args, kwargs) in enumerate((((), {}), ((None,), {}), ((None, None, None, None), {}),
                                               ((), {'value': None}), ((ValueError, None, 17), {}))):
            generator = produce()
            if started:
                generator.next()
            result = outcome(lambda: getattr(generator, method)(*args, **kwargs))
            if result == ('value', generator):
                result = ('value', 'self')
            emit('generator/%s/%d/%d' % (method, started, index),
                 (result, generator.gi_running, generator.gi_frame is None))
            generator.close()
            sys.exc_clear()
