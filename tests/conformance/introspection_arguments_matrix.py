"""Finite ordinary constructor, introspection and saved-frame state products.

Object byte sizes depend on the interpreter layout; sizeof rows compare the
protocol, integer result type and nonnegative result rather than physical bytes.
Callback rows retain original inputs and compare deterministic successful/error
lookups. Pure-False hash-table probe repetition is checked in the local fixture.
"""

import sys


def sample(value=17):
    return value


def make_frame():
    return sys._getframe()


def make_generator():
    yield 19


def summarize(value):
    if value is None or value is NotImplemented or isinstance(value, (int, long, float, str, unicode)):
        return (type(value).__name__, value)
    if isinstance(value, tuple):
        return ('tuple', tuple(summarize(item) for item in value))
    if isinstance(value, dict):
        return ('dict', sorted((summarize(key), summarize(item)) for key, item in value.items()))
    if isinstance(value, type):
        return ('type', value.__name__)
    if callable(value):
        return ('callable', getattr(value, '__name__', type(value).__name__))
    return ('object', type(value).__name__)


def outcome(call, sized=False, function=False):
    try:
        result = call()
        if sized:
            return ('value', type(result) is int, result >= 0)
        if function:
            return ('value', result.__name__, result.func_defaults, result.__module__)
        return ('value', summarize(result))
    except BaseException as error:
        return ('error', type(error).__name__, error.args)


def emit(identity, value):
    print identity + '\t' + repr(value)


frame, generator = make_frame(), make_generator()
try:
    raise ValueError('ordinary')
except ValueError:
    trace = sys.exc_info()[2]
sys.exc_clear()
objects = (('object', object()), ('int', 3), ('long', 3L), ('str', 'x'),
           ('tuple', (1,)), ('list', [1]), ('dict', {'member': 1}),
           ('function', sample), ('code', sample.func_code), ('frame', frame),
           ('traceback', trace), ('generator', generator),
           ('module', type(sys)('_tinypy_introspection_product')))
arguments = ((), (0,), (2,), ('x',), (None,), (0, 1))
keywords = ({}, {'argument': 1}, {'protocol': 2})
for kind, value in objects:
    for method in ('__sizeof__', '__reduce__', '__reduce_ex__', '__getnewargs__'):
        if not hasattr(value, method):
            emit('method/%s/%s/absent' % (kind, method), outcome(lambda: getattr(value, method)))
            continue
        bound = getattr(value, method)
        for index, items in enumerate(arguments):
            for key_index, named in enumerate(keywords):
                emit('method/%s/%s/%d/%d' % (kind, method, index, key_index),
                     outcome(lambda: bound(*items, **named), method == '__sizeof__'))
                sys.exc_clear()

Function = type(sample)
positional = ((), (sample.func_code,), (sample.func_code, {}), (17, {}),
              (sample.func_code, []), (sample.func_code, {}, u'name'),
              (sample.func_code, {}, 17), (sample.func_code, {}, None, []),
              (sample.func_code, {}, None, (), []),
              (sample.func_code, {}, None, (), (None,)),
              (sample.func_code, {}, None, (), (), 1), (None, {}),
              (sample.func_code, None), (sample.func_code, {}, None, None, None))
for index, items in enumerate(positional):
    emit('function/positional/%d' % index, outcome(lambda: Function(*items), function=True))
    sys.exc_clear()
named_inputs = ({}, {'code': sample.func_code}, {'globals': {}},
                {'code': sample.func_code, 'globals': {}},
                {'code': sample.func_code, 'globals': {}, 'extra': 1},
                {'argdefs': []}, {'closure': []}, {'name': 17})
for index, named in enumerate(named_inputs):
    emit('function/named/%d' % index, outcome(lambda: Function(**named), function=True))
    sys.exc_clear()
for index, key in enumerate(('extra\0tail', 'x' * 201, 'name\0tail', 'code\0tail', 'closure\0tail')):
    emit('function/keyword-name/%d' % index,
         outcome(lambda: Function(sample.func_code, {}, **{key: 'ignored'}), function=True))
    sys.exc_clear()
for index, named in enumerate(({'code': sample.func_code}, {'globals': {}},
                                {'name': 'renamed'}, {'argdefs': (23,)}, {'closure': ()})):
    emit('function/duplicate/%d' % index,
         outcome(lambda: Function(sample.func_code, {}, **named), function=True))
    sys.exc_clear()


def enclosing():
    value = 29
    def closed():
        return value
    return closed


closed = enclosing()
for index, closure in enumerate((None, (), (None,), closed.func_closure)):
    emit('function/closure/%d' % index,
         outcome(lambda: Function(closed.func_code, {}, None, None, closure), function=True))
    sys.exc_clear()

events = []
for mode in ('true', 'raise'):
    class Key(str):
        def __hash__(self):
            return str.__hash__(self)
        def __eq__(self, other):
            events.append(('eq', str(other), sys.exc_info()[0].__name__ if sys.exc_info()[0] else None))
            if mode == 'raise':
                raise KeyboardInterrupt('keyword')
            return True
    for field in ('code', 'globals', 'name', 'argdefs', 'closure'):
        values = {'code': sample.func_code, 'globals': {}, 'name': 'renamed', 'argdefs': (31,), 'closure': ()}
        named = {name: value for name, value in values.items() if name != field}
        named[Key(field)] = values[field]
        events[:] = []
        try:
            raise KeyError('outer')
        except KeyError:
            original = sys.exc_info()[1]
            result = outcome(lambda: Function(**named), function=True)
            emit('function/key/%s/%s' % (mode, field), (result, events, sys.exc_info()[1] is original))
        sys.exc_clear()
    namespace = {Key('__name__'): 'sample_module'}
    events[:] = []
    try:
        raise KeyError('outer')
    except KeyError:
        original = sys.exc_info()[1]
        result = outcome(lambda: Function(sample.func_code, namespace), function=True)
        emit('function/module/%s' % mode, (result, events, sys.exc_info()[1] is original))
    sys.exc_clear()

for mode in ('missing', 'callable', 'none', 'wrong', 'call-error', 'value', 'base'):
    for protocol in (0, 1, 2, 3):
        for method in ('__reduce__', '__reduce_ex__'):
            events[:] = []
            failure = ValueError('newargs')
            class Subject(object):
                def __getattribute__(self, name):
                    if name in ('__getnewargs__', '__getstate__', '__dict__', '__class__', '__reduce__'):
                        events.append(('get', name))
                    if name == '__getnewargs__':
                        if mode == 'missing':
                            raise AttributeError('newargs')
                        if mode == 'none':
                            return None
                        if mode == 'wrong':
                            return lambda: []
                        if mode == 'call-error':
                            def raised():
                                raise failure
                            return raised
                        if mode == 'value':
                            raise failure
                        if mode == 'base':
                            raise KeyboardInterrupt('newargs')
                        return lambda: ()
                    if name == '__getstate__':
                        return lambda: 37
                    return object.__getattribute__(self, name)
            subject = Subject()
            result = outcome(lambda: getattr(subject, method)(protocol))
            emit('reduce/getter/%s/%d/%s' % (mode, protocol, method), (result, events))
            sys.exc_clear()

class Protocol(object):
    def __int__(self):
        events.append('int')
        return 2


class LongProtocol(long):
    def __int__(self):
        events.append('long-int')
        return 2


class IntProtocol(int):
    def __int__(self):
        events.append('int-int')
        return 2


class Reduced(object):
    def __reduce__(self):
        events.append('reduce')
        return 41


protocols = (-1, 0, 1, 2, 3, 2L, 2.5, Protocol(), LongProtocol(0),
             IntProtocol(0), 2 ** 40, -(2 ** 40), 2 ** 70)
for index, protocol in enumerate(protocols):
    for kind, subject in (('object', object()), ('override', Reduced())):
        for method in ('__reduce__', '__reduce_ex__'):
            events[:] = []
            emit('reduce/protocol/%d/%s/%s' % (index, kind, method),
                 (outcome(lambda: getattr(subject, method)(protocol)), events))
            sys.exc_clear()

class Slotted(object):
    __slots__ = ('field',)
    def __getattribute__(self, name):
        if name in ('field', '__getstate__', '__getnewargs__', '__dict__'):
            events.append(('get', name))
        return object.__getattribute__(self, name)


subject = Slotted()
subject.field = 43
for protocol in (0, 1, 2, 3):
    events[:] = []
    emit('reduce/slots/%d' % protocol, (outcome(lambda: subject.__reduce_ex__(protocol)), events))
    sys.exc_clear()

import copy_reg
original_slotnames = copy_reg._slotnames
try:
    class SlotNames(object):
        pass
    for index, returned in enumerate((None, [], (), 17, '')):
        events[:] = []
        def slotnames(cls):
            events.append(cls.__name__)
            return returned
        copy_reg._slotnames = slotnames
        emit('reduce/slotnames/%d' % index,
             (outcome(lambda: SlotNames().__reduce_ex__(2)), events))
        sys.exc_clear()
    for index, cached in enumerate((['field'], [], ())):
        class Cached(object):
            __slotnames__ = cached
        subject = Cached()
        subject.field = 47
        events[:] = []
        emit('reduce/slotnames-cache/%d' % index,
             (outcome(lambda: subject.__reduce_ex__(2)), events))
        sys.exc_clear()
finally:
    copy_reg._slotnames = original_slotnames

for kind, value, fields in (
        ('frame', frame, ('f_back', 'f_code', 'f_builtins', 'f_globals', 'f_locals', 'f_lasti', 'f_restricted')),
        ('traceback', trace, ('tb_next', 'tb_frame', 'tb_lasti', 'tb_lineno')),
        ('generator', generator, ('gi_frame', 'gi_code', 'gi_running', '__name__')),
        ('code', sample.func_code, ('co_argcount', 'co_filename', 'co_consts', 'co_freevars'))):
    for field in fields:
        for operation in ('set', 'delete'):
            def run():
                if operation == 'set':
                    setattr(value, field, 17)
                else:
                    delattr(value, field)
            emit('field/readonly/%s/%s/%s' % (kind, field, operation), outcome(run))
            sys.exc_clear()

for field in ('f_exc_type', 'f_exc_value', 'f_exc_traceback'):
    descriptor = type(frame).__dict__[field]
    emit('field/descriptor-kind/%s' % field,
         (type(descriptor).__name__, descriptor.__name__, descriptor.__objclass__ is type(frame)))
    for kind, value in (('int', 17), ('none', None), ('object', object())):
        for operation in ('attribute', 'descriptor'):
            descriptor = type(frame).__dict__[field]
            def run():
                if operation == 'attribute':
                    setattr(frame, field, value)
                else:
                    descriptor.__set__(frame, value)
                identical = getattr(frame, field) is value
                if operation == 'attribute':
                    delattr(frame, field)
                else:
                    descriptor.__delete__(frame)
                return identical, getattr(frame, field) is None
            emit('field/stopped/%s/%s/%s' % (field, kind, operation), outcome(run))
            sys.exc_clear()


def replace_frame_field(field, operation, kind):
    target, events = make_frame(), []
    class Previous(object):
        def __del__(self):
            namespace = sys._getframe(1).f_locals
            try:
                current = getattr(namespace['target'], field)
                namespace['events'].append(current is None)
            finally:
                # Release the transient snapshot; the callback owns no frame.
                namespace.clear()
    setattr(target, field, Previous())
    incoming = None if kind == 'none' else object()
    if operation == 'attribute':
        setattr(target, field, incoming)
    else:
        type(target).__dict__[field].__set__(target, incoming)
    result = events[:], getattr(target, field) is incoming
    delattr(target, field)
    return result


for field in ('f_exc_type', 'f_exc_value', 'f_exc_traceback'):
    for operation in ('attribute', 'descriptor'):
        for kind in ('object', 'none'):
            emit('field/finalizer/%s/%s/%s' % (field, operation, kind),
                 outcome(lambda: replace_frame_field(field, operation, kind)))
            sys.exc_clear()

for mode in ('keep', 'none', 'delete', 'caught'):
    sys.exc_clear()
    outer, inner = KeyError('outer'), ValueError('inner')
    observed = []
    def run():
        active_frame = sys._getframe()
        try:
            raise inner
        except ValueError:
            observed.append((active_frame.f_exc_type is KeyError,
                             active_frame.f_exc_value is outer,
                             sys.exc_info()[1] is inner))
            if mode == 'none':
                active_frame.f_exc_type = active_frame.f_exc_value = active_frame.f_exc_traceback = None
            elif mode == 'delete':
                del active_frame.f_exc_type
            elif mode == 'caught':
                active = sys.exc_info()
                active_frame.f_exc_type, active_frame.f_exc_value, active_frame.f_exc_traceback = active
    try:
        raise outer
    except KeyError:
        run()
        state = sys.exc_info()
        emit('field/active/%s' % mode, (observed, state[0].__name__ if state[0] else None,
                                     state[1] is outer, state[1] is inner, state[2] is None))
        state = None
    sys.exc_clear()

generator.close()
del frame, trace, generator, objects
sys.exc_clear()
