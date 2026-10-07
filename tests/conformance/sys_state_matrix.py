"""Finite sys conversions, special size lookup and displayhook state products."""

import sys
import __builtin__


def caught(callback):
    try:
        return 'value', callback()
    except BaseException as error:
        return 'error', type(error).__name__, error.args


def emit(identity, callback):
    print identity + '\t' + repr(caught(callback))
    sys.exc_clear()


def integer_input(kind, payload, events):
    class Integer(object):
        def __int__(self):
            events.append('int')
            return payload
    class Index(object):
        def __index__(self):
            events.append('index')
            return payload
    class IntSubtype(int):
        def __int__(self):
            events.append('subtype-int')
            return payload
    class LongSubtype(long):
        def __int__(self):
            events.append('subtype-long')
            return payload
    if kind == 0:
        return payload
    if kind == 1:
        return long(payload)
    if kind == 2:
        return Integer()
    if kind == 3:
        return Index()
    if kind == 4:
        return IntSubtype(payload)
    return LongSubtype(payload)


def frame_case(kind, payload):
    events = []
    value = integer_input(kind, payload, events)
    def inspect():
        frame = sys._getframe(value)
        return frame.f_code.co_name
    return caught(inspect), events


def recursion_case(kind, payload):
    events = []
    value = integer_input(kind, payload, events)
    saved = sys.getrecursionlimit()
    def change():
        sys.setrecursionlimit(value)
        return sys.getrecursionlimit()
    try:
        outcome = caught(change)
        return outcome, events, sys.getrecursionlimit() == saved if outcome[0] == 'error' else True
    finally:
        sys.setrecursionlimit(saved)


def size_case(result_kind, lookup_kind, fallback, failure):
    events = []
    marker = (TypeError, ValueError, KeyboardInterrupt)[failure - 1]('size failure') if failure else None
    class Converted(object):
        def __int__(self):
            events.append('int')
            return 37
    class Number(int):
        def __int__(self):
            events.append('subtype-int')
            return 99
    returned = (37, 37L, True, 37.5, Converted(), Number(37), -1, 'bad', None, 2 ** 70)[result_kind]
    def report(self):
        events.append('sizeof')
        if marker is not None:
            raise marker
        return returned
    class Descriptor(object):
        def __get__(self, instance, owner):
            events.append('descriptor')
            if marker is not None:
                raise marker
            return lambda: returned
    class Modern(object):
        pass
    class Classic:
        pass
    Owner = Classic if lookup_kind == 3 else Modern
    if lookup_kind in (0, 3):
        Owner.__sizeof__ = report
    elif lookup_kind == 1:
        Owner.__sizeof__ = Descriptor()
    value = Owner()
    if lookup_kind == 2:
        value.__sizeof__ = lambda: events.append('instance-sizeof') or returned
    sentinel = object()
    class Zero(object):
        def __sizeof__(self):
            return 0
    def invoke():
        result = sys.getsizeof(value, sentinel) if fallback else sys.getsizeof(value)
        if result is sentinel:
            return 'fallback'
        # Native allocation sizes and GC headers are implementation dependent.
        # Observe conversion and relative custom sizes, never CPython byte layout.
        if lookup_kind in (2, 3):
            return type(result).__name__, result >= 0
        return type(result).__name__, result - sys.getsizeof(Zero())
    outcome = caught(invoke)
    identity = False
    if marker is not None:
        try:
            if fallback:
                sys.getsizeof(value, sentinel)
            else:
                sys.getsizeof(value)
        except BaseException as error:
            identity = error is marker
    return outcome, events, identity


def size_arguments(positional, keyword):
    class Sized(object):
        def __sizeof__(self):
            return 0
    value = Sized()
    args = (value, 'fallback', 17)[:positional]
    kwargs = ({}, {'object': value}, {'default': 'fallback'}, {'extra': 1},
              {'object': value, 'default': 'fallback'})[keyword]
    def invoke():
        result = sys.getsizeof(*args, **kwargs)
        return type(result).__name__, result >= 0
    return caught(invoke)


def display_case(flag_kind, failure, replacement, handling):
    events = []
    saved_stdout = sys.stdout
    saved_module = sys.modules['__builtin__']
    had_underscore = '_' in __builtin__.__dict__
    saved_underscore = __builtin__.__dict__.get('_')
    marker = ValueError('display failure')
    state = [None]
    flags = (0, 1, -1, 1L, 1.0, None)
    class Sink(object):
        def __init__(self, label):
            self.label = label
            self.flag = flags[flag_kind] if flag_kind < len(flags) else None
        def read_softspace(self):
            events.append((self.label, 'get-softspace'))
            if flag_kind == 6:
                raise AttributeError('missing softspace')
            if flag_kind == 7:
                raise KeyboardInterrupt('ignored getter')
            return self.flag
        def write_softspace(self, value):
            events.append((self.label, 'set-softspace', value))
            if flag_kind == 7:
                raise ValueError('ignored setter')
            self.flag = value
        softspace = property(read_softspace, write_softspace)
        def get_write(self):
            events.append((self.label, 'get-write'))
            if failure == 1:
                raise marker
            def write(text):
                events.append((self.label, 'write', text))
                if failure == 3 and text == 'payload':
                    raise marker
                if failure == 4 and text == '\n':
                    raise marker
                if replacement == 2 and text == 'payload':
                    sys.stdout = other
            return write
        write = property(get_write)
    sink = Sink('first')
    other = Sink('second')
    other.flag = 1
    class Shown(object):
        def __repr__(self):
            events.append(('repr', __builtin__.__dict__.get('_') is None))
            if failure == 2:
                raise marker
            if replacement == 3:
                sys.stdout = other
            return 'payload'
    # Replacing a writer during repr uses an ordinary instance attribute.
    # A property without a setter cannot be assigned, so model replacement
    # through the descriptor's returned callable instead.
    if replacement == 1:
        original = Sink.get_write
        def switchable(self):
            callback = original(self)
            return callback if state[0] is None else state[0]
        Sink.write = property(switchable)
        def representation(self):
            events.append(('repr', __builtin__.__dict__.get('_') is None))
            state[0] = lambda text: events.append(('replacement-write', text))
            return 'payload'
        Shown.__repr__ = representation
    value = Shown()
    sys.stdout = sink
    def invoke():
        try:
            result = sys.displayhook(value)
            return result is None, __builtin__.__dict__.get('_') is value
        except BaseException as error:
            return 'error', type(error).__name__, error.args, error is marker, __builtin__.__dict__.get('_') is None
    try:
        if handling:
            try:
                raise KeyError('outer')
            except KeyError as outer:
                outcome = invoke()
                preserved = sys.exc_info()[1] is outer
        else:
            outcome = invoke()
            preserved = True
        return outcome, events, preserved
    finally:
        sys.stdout = saved_stdout
        sys.modules['__builtin__'] = saved_module
        if had_underscore:
            __builtin__._ = saved_underscore
        else:
            __builtin__.__dict__.pop('_', None)


def display_missing(missing, none):
    saved_stdout = sys.stdout
    saved_module = sys.modules['__builtin__']
    had_underscore = '_' in __builtin__.__dict__
    saved_underscore = __builtin__.__dict__.get('_')
    try:
        if missing == 0:
            del sys.stdout
        else:
            del sys.modules['__builtin__']
        return caught(lambda: sys.displayhook(None if none else 17))
    finally:
        sys.stdout = saved_stdout
        sys.modules['__builtin__'] = saved_module
        if had_underscore:
            __builtin__._ = saved_underscore
        else:
            __builtin__.__dict__.pop('_', None)


def main():
    for kind in xrange(6):
        for index, value in enumerate((-2, -1, 0, 1, 1000, 2 ** 40, -(2 ** 40))):
            emit('frame/%d/%d' % (kind, index), lambda: frame_case(kind, value))
        for index, value in enumerate((1000, 1001, 0, -1, 2 ** 40, -(2 ** 40))):
            emit('recursion/%d/%d' % (kind, index), lambda: recursion_case(kind, value))
    for index, value in enumerate((None, '0', u'0', 1.5, object())):
        emit('frame-invalid/%d' % index, lambda: frame_case(0, value))
        emit('recursion-invalid/%d' % index, lambda: recursion_case(0, value))
    for function_index, function in enumerate((sys.exc_info, sys.exc_clear, sys.getrecursionlimit,
            sys.getdefaultencoding, sys._getframe, sys.setrecursionlimit, sys.displayhook, sys.exit)):
        for count in (0, 2, 3):
            if count == 0 and function_index == 4:
                continue
            emit('arity/%d/%d' % (function_index, count), lambda: function(*(None,) * count))
        for args in ((), (None,), (None, None)):
            emit('keywords/%d/%d' % (function_index, len(args)), lambda: function(*args, unknown=1))
    for result_kind in xrange(10):
        for lookup_kind in xrange(4):
            for fallback in (False, True):
                for failure in xrange(4):
                    emit('size/%d/%d/%d/%d' % (result_kind, lookup_kind, int(fallback), failure),
                         lambda: size_case(result_kind, lookup_kind, fallback, failure))
    for positional in xrange(4):
        for keyword in xrange(5):
            emit('size-arguments/%d/%d' % (positional, keyword), lambda: size_arguments(positional, keyword))
    for flag_kind in xrange(8):
        for failure in xrange(5):
            for replacement in (0, 2, 3):
                for handling in (False, True):
                    emit('display/%d/%d/%d/%d' % (flag_kind, failure, replacement, int(handling)),
                         lambda: display_case(flag_kind, failure, replacement, handling))
        emit('display-writer-pin/%d' % flag_kind, lambda: display_case(flag_kind, 0, 1, False))
    for missing in xrange(2):
        for none in (False, True):
            emit('display-missing/%d/%d' % (missing, int(none)), lambda: display_missing(missing, none))


if __name__ == '__main__':
    main()
