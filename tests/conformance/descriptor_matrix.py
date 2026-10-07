"""Finite descriptor and weakref state transitions compared with CPython 2.7."""

import _weakref as weakref
import sys


def read(instance):
    """getter documentation"""
    return instance.payload


def write(instance, value):
    instance.payload = value


def remove(instance):
    del instance.payload


class Field(property):
    pass


class Holder(object):
    def __init__(self):
        self.payload = 23


class NonCallable(object):
    """project marker documentation"""


marker = NonCallable()


def label(value):
    if value is read:
        return 'read'
    if value is write:
        return 'write'
    if value is remove:
        return 'remove'
    if value is marker:
        return 'noncallable'
    return value


def property_state(value):
    return (label(value.fget), label(value.fset), label(value.fdel),
            value.__doc__, property.__dict__['__doc__'].__get__(value, type(value)))


def error_kind():
    return sys.exc_info()[0].__name__


for factory_index, factory in enumerate((property, Field)):
    for mask in range(8):
        accessors = (read if mask & 1 else None, write if mask & 2 else None,
                     remove if mask & 4 else None)
        for doc_index, doc in enumerate((None, 'explicit', '', 0, u'unicode')):
            original = factory(*(accessors + (doc,)))
            for method in ('getter', 'setter', 'deleter'):
                for replacement_index, replacement in enumerate((None, read, marker)):
                    copied = getattr(original, method)(replacement)
                    print 'property-copy/%d/%d/%d/%s/%d\t%s' % (
                        factory_index, mask, doc_index, method, replacement_index,
                        repr((type(copied) is factory, copied is original,
                              property_state(copied))))
            for operation in ('class-get', 'get', 'set', 'delete'):
                instance = Holder()
                try:
                    if operation == 'class-get':
                        result = original.__get__(None, Holder) is original
                    elif operation == 'get':
                        result = original.__get__(instance, Holder)
                    elif operation == 'set':
                        result = original.__set__(instance, 29)
                    else:
                        result = original.__delete__(instance)
                    outcome = ('value', result)
                except BaseException:
                    outcome = ('error', error_kind())
                print 'property-bind/%d/%d/%d/%s\t%s' % (
                    factory_index, mask, doc_index, operation,
                    repr((outcome, getattr(instance, 'payload', None))))
            original.__init__()


keyword_sets = ({}, {'fget': read}, {'fset': write}, {'fdel': remove},
                {'doc': 'keyword'}, {'unknown': 7},
                {'fget': read, 'fset': write}, {'doc': None})
arguments = (read, write, remove, 'positional', 17)
for factory_index, factory in enumerate((property, Field)):
    for count in range(6):
        for keyword_index, keywords in enumerate(keyword_sets):
            value = factory(read, write, remove, 'before')
            try:
                result = value.__init__(*arguments[:count], **keywords)
                outcome = ('value', result)
            except BaseException:
                outcome = ('error', error_kind())
            print 'property-init/%d/%d/%d\t%s' % (
                factory_index, count, keyword_index,
                repr((outcome, property_state(value))))
            value.__init__()


class Keyword(str):
    def __new__(cls, name, behavior, events):
        value = str.__new__(cls, name)
        value.behavior = behavior
        value.events = events
        return value
    def __eq__(self, other):
        self.events.append(other)
        if self.behavior == 2:
            raise KeyError('lookup')
        if self.behavior == 3:
            raise KeyboardInterrupt('lookup')
        return bool(self.behavior)
    def __hash__(self):
        return str.__hash__(self)


for factory_index, factory in enumerate((property, Field)):
    for name_index, name in enumerate(('fget', 'fset', 'fdel', 'doc')):
        for behavior in range(4):
            events = []
            key = Keyword(name, behavior, events)
            argument = (read, write, remove, 'keyword')[name_index]
            value = factory(**{key: argument})
            print 'property-keyword/%d/%d/%d\t%s' % (
                factory_index, name_index, behavior,
                repr((property_state(value), events)))
            value.__init__()


for factory_index, factory in enumerate((staticmethod, classmethod)):
    for initial_index, initial in enumerate((read, None, 17)):
        for count in range(4):
            for keyword_index, keywords in enumerate(({}, {'ignored': 7})):
                value = factory(initial)
                try:
                    result = value.__init__(*(read,) * count, **keywords)
                    outcome = ('value', result)
                except BaseException as error:
                    outcome = ('error', error_kind(), error.args)
                print 'callable-init/%d/%d/%d/%d\t%s' % (
                    factory_index, initial_index, count, keyword_index,
                    repr((outcome, label(value.__func__))))
        for instance_index, instance in enumerate((None, Holder())):
            for owner_index, owner in enumerate((Holder, None, 17)):
                value = factory(initial)
                try:
                    result = value.__get__(instance, owner)
                    if factory is staticmethod:
                        state = label(result)
                    else:
                        state = (label(result.im_func), result.im_self is Holder,
                                 result.im_self is type(instance), result.im_self is owner)
                    outcome = ('value', state)
                except BaseException:
                    outcome = ('error', error_kind())
                print 'callable-bind/%d/%d/%d/%d\t%s' % (
                    factory_index, initial_index, instance_index, owner_index,
                    repr(outcome))


class Target(object):
    def __eq__(self, other):
        return 'equal'
    def __ne__(self, other):
        return 'different'


class Ref(weakref.ref):
    pass


class OtherRef(weakref.ref):
    pass


for left_index, left_type in enumerate((weakref.ref, Ref, OtherRef)):
    for right_index, right_type in enumerate((weakref.ref, Ref, OtherRef)):
        for same_target in (False, True):
            for dead in (False, True):
                first = Target()
                second = first if same_target else Target()
                left, right = left_type(first), right_type(second)
                if dead:
                    del first, second
                print 'weakref-equality/%d/%d/%d/%d\t%s' % (
                    left_index, right_index, same_target, dead,
                    repr((left == right, left != right,
                          weakref.ref.__eq__(left, right) is NotImplemented)))
                del left, right


for factory_index, factory in enumerate((weakref.ref, Ref, weakref.proxy)):
    for callback_index, callback in enumerate((None, 17, 'callback', object())):
        target = Target()
        value = factory(target, callback)
        print 'weakref-constructor/%d/%d\t%s' % (
            factory_index, callback_index,
            repr((weakref.getweakrefcount(target), value is factory(target, callback)
                  if callback is None else False)))
        del value
        print 'weakref-released/%d/%d\t%s' % (
            factory_index, callback_index, repr(weakref.getweakrefcount(target)))
