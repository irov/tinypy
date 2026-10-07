"""Finite cold/warm native attribute and numeric C-descriptor products.

The first phase runs before any ordinary attribute read of either selected
native type. Separate portable cases isolate the different warming operations.
Only scalar or nullable safe type fields are read through direct descriptors.
Documentation contents and physical object sizes are not compared.
"""

import sys


identities = set()
events = []


def emit(identity, result):
    assert identity not in identities, identity
    identities.add(identity)
    print identity + '\t' + repr(result)
    sys.exc_clear()


def summarize(value):
    if value is None or isinstance(value, (bool, int, long, float, complex, str, unicode)):
        return (type(value).__name__, value)
    if isinstance(value, type):
        return ('type', value.__name__)
    if isinstance(value, tuple):
        return ('tuple', tuple(summarize(item) for item in value))
    return (type(value).__name__, getattr(value, '__name__', None))


def outcome(operation, mode=None):
    try:
        value = operation()
        if mode == 'repr':
            if ' object at 0x' in value:
                value = value.split(' object at 0x')[0] + ' object at <address>>'
            return ('value', value)
        if mode == 'hash':
            return ('value', type(value).__name__)
        if mode == 'flags':
            return ('value', bool(value & (1 << 12)))
        if mode == 'doc':
            return ('value', value is None or isinstance(value, str))
        return ('value', summarize(value))
    except BaseException as error:
        return ('error', type(error).__name__, error.args)


class Name(str):
    def __hash__(self):
        events.append('hash')
        raise ValueError('name hash')

    def __eq__(self, other):
        events.append(('eq', repr(other)))
        return str.__eq__(self, other)


selected = (('wrapper', (7).__add__), ('descriptor', list.append))
names = ('__name__', '__self__', '__objclass__', '__doc__', '__module__',
         'missing', u'__name__', 'missing\0tail', 'x' * 160, Name('__name__'))
for label, value in selected:
    for key_index, key in enumerate(names):
        for operation_label, operation in (
                ('set', lambda: setattr(value, key, 17)),
                ('delete', lambda: delattr(value, key))):
            del events[:]
            result = outcome(operation)
            state = outcome(lambda: setattr(value, '__name__', 23))
            emit('cold/write/%s/%d/%s' % (label, key_index, operation_label),
                 (result, state, tuple(events)))
    for key_index, key in enumerate(('__name__', 17, None, u'\xff', 'missing')):
        for operation_label, operation in (
                ('set', lambda: object.__setattr__(value, key, 17)),
                ('delete', lambda: object.__delattr__(value, key))):
            emit('cold/direct/%s/%d/%s' % (label, key_index, operation_label),
                 (outcome(operation), outcome(lambda: setattr(value, '__name__', 23))))
    operations = (
        ('repr', lambda: repr(value), 'repr'),
        ('str', lambda: str(value), 'repr'),
        ('bool', lambda: bool(value), None),
        ('eq', lambda: value == value, None),
        ('ne', lambda: value != value, None),
        ('callable', lambda: callable(value), None),
        ('instance', lambda: isinstance(value, object), None),
        ('subclass', lambda: issubclass(type(value), object), None),
        ('construct', lambda: type(value)(), None),
        ('call', (lambda: value(2)) if label == 'wrapper' else (lambda: value([], 2)), None))
    for operation_label, operation, mode in operations:
        emit('cold/operation/%s/%s' % (label, operation_label),
             (outcome(operation, mode), outcome(lambda: setattr(value, '__name__', 23))))
    owner = type(value)
    for field in ('__name__', '__dict__', '__base__', '__mro__', '__flags__'):
        descriptor = type.__dict__[field]
        emit('cold/type-field/%s/%s' % (label, field),
             (outcome(lambda: descriptor.__get__(owner, type), 'flags' if field == '__flags__' else None),
              outcome(lambda: setattr(value, '__name__', 23))))
    for operation_index, operation in enumerate((
            lambda: object.__getattribute__(value, 17),
            lambda: getattr(value, 17), lambda: getattr(value, u'\xff'))):
        emit('cold/invalid-read/%s/%d' % (label, operation_index),
             (outcome(operation), outcome(lambda: setattr(value, '__name__', 23))))
    for operation_index, operation in enumerate((
            lambda: setattr(owner, 'missing', 17), lambda: delattr(owner, 'missing'))):
        emit('cold/type-write/%s/%d' % (label, operation_index),
             (outcome(operation), outcome(lambda: setattr(value, '__name__', 23))))

wrapper, descriptor = selected[0][1], selected[1][1]
emit('cold/wrapper-hash',
     (outcome(lambda: hash(wrapper), 'hash'), outcome(lambda: setattr(wrapper, '__name__', 23))))
emit('transition/descriptor-hash',
     (outcome(lambda: hash(descriptor), 'hash'), outcome(lambda: setattr(descriptor, '__name__', 23))))
emit('transition/wrapper-failed-read',
     (outcome(lambda: getattr(wrapper, 'missing')), outcome(lambda: setattr(wrapper, '__name__', 23))))

for label, value in selected:
    for key_index, key in enumerate(names):
        for operation_label, operation in (
                ('read', lambda: getattr(value, key)),
                ('set', lambda: setattr(value, key, 17)),
                ('delete', lambda: delattr(value, key)),
                ('direct-read', lambda: object.__getattribute__(value, key)),
                ('direct-set', lambda: object.__setattr__(value, key, 17)),
                ('direct-delete', lambda: object.__delattr__(value, key))):
            del events[:]
            result = outcome(operation, 'doc' if key == '__doc__' else None)
            emit('warm/attribute/%s/%d/%s' % (label, key_index, operation_label),
                 (result, tuple(events)))
    for field in ('__name__', '__self__', '__objclass__', '__doc__', '__module__', 'missing'):
        namespace = type(value).__dict__
        entry = namespace.get(field)
        emit('warm/metadata/%s/%s' % (label, field),
             None if entry is None else (type(entry).__name__, entry.__name__, entry.__objclass__ is type(value)))
    for key_index, key in enumerate((17, None, [], u'\xff')):
        for operation_label, operation in (
                ('read', lambda: object.__getattribute__(value, key)),
                ('set', lambda: object.__setattr__(value, key, 17)),
                ('delete', lambda: object.__delattr__(value, key))):
            emit('warm/invalid-name/%s/%d/%s' % (label, key_index, operation_label), outcome(operation))

ready_values = (('wrapper-descriptor', object.__repr__), ('builtin', len),
                ('bound-builtin', [].append), ('getset', type.__dict__['__name__']),
                ('member', type.__dict__['__basicsize__']))
for label, value in ready_values:
    for field in ('__name__', '__self__', '__objclass__', '__doc__', '__module__', 'missing'):
        previous = getattr(value, field, None)
        for operation_label, operation in (
                ('read', lambda: getattr(value, field)),
                ('set', lambda: setattr(value, field, 17)),
                ('delete', lambda: delattr(value, field))):
            emit('ready/attribute/%s/%s/%s' % (label, field, operation_label),
                 outcome(operation, 'doc' if field == '__doc__' else None))
        if field == '__module__' and label in ('builtin', 'bound-builtin'):
            value.__module__ = previous

for value_index, value in enumerate((type.__dict__['__name__'], type.__dict__['__basicsize__'])):
    owner = type(value)
    for field in ('__name__', '__objclass__', '__doc__'):
        descriptor = owner.__dict__[field]
        emit('c-metadata/%d/%s/kind' % (value_index, field),
             (type(descriptor).__name__, descriptor.__name__, descriptor.__objclass__ is owner))
        for operation_label, operation in (
                ('read', lambda: descriptor.__get__(value, owner)),
                ('set', lambda: descriptor.__set__(value, 17)),
                ('delete', lambda: descriptor.__delete__(value)),
                ('wrong-read', lambda: descriptor.__get__('wrong', str)),
                ('wrong-set', lambda: descriptor.__set__('wrong', 17)),
                ('wrong-delete', lambda: descriptor.__delete__('wrong'))):
            emit('c-metadata/%d/%s/%s' % (value_index, field, operation_label),
                 outcome(operation, 'doc' if field == '__doc__' else None))

class IntChild(int):
    pass


class LongChild(long):
    pass


class FloatChild(float):
    pass


class ComplexChild(complex):
    pass


receivers = (7, IntChild(7), 7L, LongChild(7), 2.5, FloatChild(2.5),
             2 + 3j, ComplexChild(2 + 3j), True, 'wrong', None)
for owner in (int, long, float, complex):
    fields = ('real', 'imag', 'numerator', 'denominator') if owner in (int, long) else ('real', 'imag')
    for field in fields:
        descriptor = owner.__dict__[field]
        emit('numeric/%s/%s/metadata' % (owner.__name__, field),
             (type(descriptor).__name__, descriptor.__name__, descriptor.__objclass__ is owner))
        for receiver_index, receiver in enumerate(receivers):
            for operation_label, operation in (
                    ('read', lambda: descriptor.__get__(receiver, type(receiver))),
                    ('set', lambda: descriptor.__set__(receiver, 17)),
                    ('delete', lambda: descriptor.__delete__(receiver))):
                emit('numeric/%s/%s/%d/%s' % (owner.__name__, field, receiver_index, operation_label),
                     outcome(operation))

for base, number in ((int, 7), (long, 7L), (float, 2.5), (complex, 2 + 3j)):
    fields = ('real', 'imag', 'numerator', 'denominator') if base in (int, long) else ('real', 'imag')
    for mode in ('native', 'class', 'dictionary'):
        for field in fields:
            child = type('Child', (base,), {field: 'class field'} if mode == 'class' else {})
            value = child(number)
            if mode == 'dictionary':
                value.__dict__[field] = 'dictionary field'
            for operation_label, operation in (
                    ('read', lambda: getattr(value, field)),
                    ('set', lambda: setattr(value, field, 17)),
                    ('delete', lambda: delattr(value, field))):
                emit('numeric-priority/%s/%s/%s/%s' % (base.__name__, mode, field, operation_label),
                     outcome(operation))

for label, value in selected:
    for operation_label, operation in (
            ('read', lambda: getattr(value, 'missing')),
            ('set', lambda: setattr(value, '__name__', 17)),
            ('delete', lambda: delattr(value, '__name__'))):
        original = ValueError('handled')
        try:
            raise original
        except ValueError:
            result = outcome(operation)
            emit('handled/%s/%s' % (label, operation_label),
                 (result, sys.exc_info()[1] is original))

assert len(identities) == 913, len(identities)
