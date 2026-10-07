"""Bounded attribute-name callbacks and descriptor dispatch outcomes."""

import sys
import _functools
import _weakref as weakref


events = []
name_mode = 0


class Name(str):
    def __hash__(self):
        events.append('hash')
        if name_mode == 4:
            raise KeyError('name hash')
        return str.__hash__(self)
    def __eq__(self, other):
        events.append(('eq', str(other)))
        if name_mode == 2:
            return False
        if name_mode == 3:
            return NotImplemented
        if name_mode == 5:
            raise KeyboardInterrupt('name equality')
        return str.__eq__(self, other)


def make_name(raw, variant):
    global name_mode
    name_mode = variant - 2
    if variant == 0:
        return raw
    if variant == 1:
        return unicode(raw)
    return Name(raw)


def normalized(value):
    if value is None or isinstance(value, (bool, int, long, str, unicode)):
        return value
    if callable(value):
        return 'callable'
    return type(value).__name__


def emit(identity, callback):
    events[:] = []
    try:
        outcome = ('value', callback())
    except BaseException as error:
        outcome = ('error', type(error).__name__, error.args)
    trace = events
    if identity in ('read/6/0/4/0', 'read/6/0/4/1', 'read/6/0/4/2'):
        # This pure False equality lookup can revisit the same stored __dict__
        # key before reaching an empty bucket: the module type dictionaries have
        # different contents/capacities, so perturb probing repeats in tinypy.
        # Coalesce only consecutive unsuccessful comparisons in these three
        # rows. Hash calls, comparison occurrence/value, errors and all other
        # callback traces remain exact.
        trace = []
        for event in events:
            if event == ('eq', '__dict__') and trace and trace[-1] == event:
                continue
            trace.append(event)
    print identity + '\t' + repr(outcome) + '\t' + repr(trace)
    sys.exc_clear()


def sample(value=17):
    return value


def sequence():
    yield 17


class Owner(object):
    member = 23
    def method(self, value=29):
        return value


module = type(sys)('temporary')
module.member = 31
iterator = sequence()
objects = ((sample, ('func_code', '__code__', '__name__', '__dict__', 'func_defaults')),
           (iterator, ('gi_code', 'gi_frame', 'gi_running')),
           (Owner, ('__name__', '__dict__', '__mro__', 'member')),
           (Owner().method, ('im_func', 'im_self', 'im_class')),
           (Owner(), ('__class__', '__dict__', 'member')),
           (property(sample), ('fget', '__doc__')),
           (module, ('__dict__', 'member')),
           (sample.func_code, ('co_name', 'co_consts')))


def read(value, name, operation):
    if operation == 2:
        return hasattr(value, name)
    if operation == 1:
        return normalized(getattr(value, name, 'missing'))
    return normalized(getattr(value, name))


for object_index, (value, fields) in enumerate(objects):
    for field_index, field in enumerate(fields):
        for variant in xrange(8):
            name = make_name(field, variant)
            for operation in xrange(3):
                emit('read/%d/%d/%d/%d' % (object_index, field_index, variant, operation),
                     lambda: read(value, name, operation))
iterator.close()


def mutate(raw, name, deleting):
    class Subject(object):
        member = 23
    if deleting:
        delattr(Subject, name)
        return getattr(Subject, raw, 'missing')
    setattr(Subject, name, 17)
    return getattr(Subject, raw)


for field_index, field in enumerate(('__name__', '__bases__', '__doc__', '__dict__', '__mro__', 'member')):
    for variant in xrange(8):
        name = make_name(field, variant)
        for deleting in (False, True):
            emit('type-mutation/%d/%d/%d' % (field_index, variant, int(deleting)),
                 lambda: mutate(field, name, deleting))


shadow_fields = ('func_code', '__name__', '__dict__', 'func_defaults')
for field_index, field in enumerate(shadow_fields):
    sample.__dict__[field] = 'shadow'
    for variant in xrange(8):
        name = make_name(field, variant)
        for operation in xrange(3):
            emit('function-shadow/%d/%d/%d' % (field_index, variant, operation),
                 lambda: read(sample, name, operation))
    sample.__dict__.pop(field)


callables = ((sample, (), 17), (Owner().method, (), 29),
             (len, ([1, 2],), 2), (_functools.partial(sample, 37), (), 37))


def call_attribute(value, name, arguments):
    wrapper = getattr(value, name)
    return wrapper is value, wrapper.__self__ is value, wrapper(*arguments)


for callable_index, (value, arguments, expected) in enumerate(callables):
    for variant in xrange(8):
        name = make_name('__call__', variant)
        emit('call/%d/%d' % (callable_index, variant),
             lambda: call_attribute(value, name, arguments))


def shadow_call(factory):
    value = sample if factory == 0 else _functools.partial(sample, 41)
    value.__dict__['__call__'] = lambda: 43
    try:
        return value.__call__(), value()
    finally:
        value.__dict__.pop('__call__')


for factory in xrange(2):
    emit('call-shadow/%d' % factory, lambda: shadow_call(factory))


class ClassicMissing:
    pass


class ClassicCalled:
    def __call__(self, value=47):
        return value


def classic_call(owner):
    method = getattr(owner, '__call__')
    return method is owner, method.im_self is None, method.im_class is owner, method(owner())


for index, owner in enumerate((ClassicMissing, ClassicCalled)):
    emit('classic-call/%d' % index, lambda: classic_call(owner))


class CallableTarget(object):
    def __call__(self, value=53):
        return value


def weak_call(factory):
    target = CallableTarget()
    if factory == 0:
        value = weakref.ref(target)
        method = value.__call__
        return method is value, method.__self__ is value, method() is target
    value = weakref.proxy(target)
    method = value.__call__
    first = method is value, method.im_self is target, method()
    target.__call__ = lambda: 59
    return first, value.__call__(), value()


for factory in xrange(2):
    emit('weak-call/%d' % factory, lambda: weak_call(factory))


def translated(factory):
    value = factory(u'a', 0, 1, 'bad')
    descriptor = UnicodeTranslateError.__dict__['encoding']
    return hasattr(value, 'encoding'), value.encoding, descriptor.__get__(value, factory)


class TranslatedChild(UnicodeTranslateError):
    pass


for index, factory in enumerate((UnicodeTranslateError, TranslatedChild)):
    emit('translated/%d' % index, lambda: translated(factory))
