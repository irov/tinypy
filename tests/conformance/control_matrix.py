"""Finite generator control and namespace states; stdout is a CPython oracle."""

import sys


class Context(object):
    def __init__(self, events):
        self.events = events
    def __enter__(self):
        self.events.append('enter')
    def __exit__(self, kind, value, traceback):
        self.events.append(('exit', None if kind is None else kind.__name__))
        return True


def controlled(events, shape):
    try:
        if shape == 'with':
            with Context(events):
                incoming = yield 'first'
                yield ('received', incoming)
        else:
            try:
                incoming = yield 'first'
                yield ('received', incoming)
            except BaseException as error:
                events.append(('caught', sys.exc_info()[0].__name__, error.args))
                if shape == 'catch':
                    yield 'caught'
                else:
                    raise
    finally:
        events.append('finally')
        if shape == 'return':
            return
        if shape == 'raise':
            raise TypeError('cleanup')
        if shape == 'yield':
            yield 'cleanup'


class Problem(ValueError):
    def __init__(self, *args):
        raise LookupError('constructor')


for shape in ('plain', 'catch', 'with', 'return', 'raise', 'yield'):
    for state in ('new', 'paused', 'finished'):
        for operation in ('next', 'send-none', 'send-value', 'throw-class',
                          'throw-instance', 'throw-stop', 'throw-failed', 'close'):
            events = []
            value = controlled(events, shape)
            if state == 'paused':
                value.next()
            elif state == 'finished':
                for unused in range(5):
                    try:
                        value.next()
                    except BaseException:
                        break
            events[:] = []
            try:
                if operation == 'next':
                    result = value.next()
                elif operation == 'send-none':
                    result = value.send(None)
                elif operation == 'send-value':
                    result = value.send(17)
                elif operation == 'throw-class':
                    result = value.throw(ValueError, ('payload', 19))
                elif operation == 'throw-instance':
                    result = value.throw(ValueError('payload'), None)
                elif operation == 'throw-stop':
                    result = value.throw(StopIteration, 'stopped')
                elif operation == 'throw-failed':
                    result = value.throw(Problem)
                else:
                    result = value.close()
                outcome = ('value', result)
            except BaseException as error:
                outcome = ('error', sys.exc_info()[0].__name__, error.args)
            print 'generator/%s/%s/%s\t%s' % (shape, state, operation,
                repr((outcome, events, value.gi_frame is None, value.gi_running)))
            for unused in range(2):
                try:
                    value.close()
                except BaseException:
                    pass


class Classic:
    pass
class ClassicMapping:
    def __getitem__(self, key):
        raise KeyError(key)
class Mapping(object):
    def __getitem__(self, key):
        raise KeyError(key)


namespaces = [[], (), '', u'', bytearray(), buffer(''), memoryview(''),
              17, object(), Classic(), ClassicMapping(), Mapping(), {}]
for index, namespace in enumerate(namespaces):
    for mode in ('eval', 'exec'):
        for source in ('17', 'name', '1/0'):
            scope = {'name': 19}
            try:
                if mode == 'eval':
                    result = eval(source, scope, namespace)
                else:
                    exec source in scope, namespace
                    result = None
                outcome = ('value', result)
            except BaseException as error:
                outcome = ('error', sys.exc_info()[0].__name__)
            print 'namespace/%d/%s/%s\t%s' % (index, mode, source,
                repr((outcome, '__builtins__' in scope)))
