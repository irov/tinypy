"""Finite direct descriptor calls, optional owners, errors and recovery.

Only ordinary objects and attached, initialized descriptors are used. No native
readiness getters or physical implementation sizes are part of this product.
"""

import sys


identities = set()


def emit(identity, result):
    assert identity not in identities, identity
    identities.add(identity)
    print identity + '\t' + repr(result)
    sys.exc_clear()


class Modern(object):
    def method(self):
        return 23


class Child(Modern):
    pass


class Classic:
    def method(self):
        return 29


modern = Modern()
child = Child()
classic = Classic()


def read(value):
    return getattr(value, 'payload', 31)


method_type = type(Modern.method)
owners = (None, Modern, Child, Classic, int, 17)
instances = (None, modern, child, classic, [], 7)


def summarize(value, selected):
    if value is selected:
        return ('same',)
    if isinstance(value, method_type):
        return ('method', value.im_func is read, value.im_self is modern,
                value.im_self is child, value.im_self is classic,
                value.im_class is None, value.im_class is Modern,
                value.im_class is Child, value.im_class is Classic,
                value.im_class is int, value.im_class == 17)
    if value is None or isinstance(value, (bool, int, long, str, unicode)):
        return (type(value).__name__, value)
    return (type(value).__name__,)


def outcome(operation, selected=None):
    try:
        return ('value', summarize(operation(), selected))
    except BaseException as error:
        return ('error', type(error).__name__, error.args)


descriptors = (('function', read), ('bound', modern.method),
               ('unbound', Modern.method), ('classic-unbound', Classic.method),
               ('property', property(read)), ('static', staticmethod(read)),
               ('class', classmethod(read)),
               ('member', type(read).__dict__['func_globals']),
               ('getset', type(read).__dict__['func_name']),
               ('native-method', list.append), ('native-wrapper', int.__add__))
for label, selected in descriptors:
    for instance_index, instance in enumerate(instances):
        emit('get/%s/implicit/%d' % (label, instance_index),
             outcome(lambda: selected.__get__(instance), selected))
        for owner_index, owner in enumerate(owners):
            emit('get/%s/explicit/%d/%d' % (label, instance_index, owner_index),
                 outcome(lambda: selected.__get__(instance, owner), selected))
    for argument_index, arguments in enumerate(((), (modern, Modern, 17))):
        emit('get/%s/count/%d' % (label, argument_index),
             outcome(lambda: selected.__get__(*arguments), selected))
    for argument_index, arguments in enumerate(((modern,), (modern, Modern))):
        emit('get/%s/keyword/%d' % (label, argument_index),
             outcome(lambda: selected.__get__(*arguments, **{'missing': 17}), selected))


def write(value, item):
    value.payload = item


def remove(value):
    del value.payload


class Holder(object):
    def __init__(self):
        self.payload = 17


class Slotted(object):
    __slots__ = ('leaf',)
    def __init__(self):
        self.leaf = 19


fields = (('property', property(read, write, remove), Holder),
          ('function-member', type(read).__dict__['func_globals'], lambda: read),
          ('function-getset', type(read).__dict__['func_name'], lambda: read),
          ('member', Slotted.leaf, Slotted), ('numeric-getset', int.real, lambda: 7))
for label, field, factory in fields:
    for method_name in ('__get__', '__set__', '__delete__', '__repr__'):
        for count in range(4):
            for keyword in (False, True):
                receiver = factory()
                if method_name == '__get__':
                    arguments = (receiver, type(receiver))
                elif method_name == '__set__':
                    arguments = (receiver, 23)
                elif method_name == '__delete__':
                    arguments = (receiver,)
                else:
                    arguments = ()
                supplied = (arguments + (29, 31, 37))[:count]
                method = getattr(field, method_name)
                keywords = {'unknown': 41} if keyword else {}
                def invoke():
                    value = method(*supplied, **keywords)
                    if label == 'property' and method_name == '__repr__':
                        # Object addresses are checked by a local repr invariant.
                        return isinstance(value, str) and value == repr(field)
                    return value
                result = outcome(invoke, field)
                state = (getattr(receiver, 'payload', None), getattr(receiver, 'leaf', None), read.func_name)
                emit('slot/%s/%s/%d/%d' % (label, method_name, count, keyword), (result, state))


events = []


class Meta(type):
    def __subclasscheck__(cls, candidate):
        events.append((candidate is Modern, candidate is Classic, candidate == 17,
                       sys.exc_info()[1] is prior))
        if cls.failure is not None:
            raise cls.failure
        return cls.accept


class CallbackOwner(object):
    __metaclass__ = Meta
    accept = False
    failure = None
    def method(self):
        return self


selected = CallbackOwner.method
prior = LookupError('outer marker')
for behavior in range(4):
    CallbackOwner.accept = behavior == 1
    CallbackOwner.failure = (None, None, ValueError('subclass marker'),
                             KeyboardInterrupt('interrupt marker'))[behavior]
    for owner_index, owner in enumerate((Modern, Classic, 17)):
        del events[:]
        try:
            raise prior
        except LookupError:
            result = outcome(lambda: selected.__get__(modern, owner), selected)
            recovered = sys.exc_info()[1] is prior
        emit('subclass/%d/%d' % (behavior, owner_index),
             (result, tuple(events), recovered))
CallbackOwner.failure = None


field_name = 'leaf' + 'x' * 220
owner_name = 'Owner' + 'x' * 130
receiver_name = 'Receiver' + 'x' * 130
owner = type(owner_name, (object,), {'__slots__': (field_name,)})
receiver = type(receiver_name, (object,), {})()
selected = owner.__dict__[field_name]
for operation_name, operation in (
        ('get', lambda: selected.__get__(receiver)),
        ('set', lambda: selected.__set__(receiver, 17)),
        ('delete', lambda: selected.__delete__(receiver))):
    emit('long-receiver/' + operation_name, outcome(operation, selected))


assert len(identities) == 681, len(identities)
