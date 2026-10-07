"""Finite native container argument, constructor and callback-phase products.

Successful ordinary descriptor calls compare result type; callback products also
compare error identity, events, container state and a successful recovery call.
All inputs are bounded and valid Python objects.
"""

import sys


def emit(identity, answer):
    print identity + '\t' + repr(answer)
    sys.exc_clear()


def outcome(operation, args, keywords):
    try:
        value = operation(*args, **keywords)
        return 'ok', type(value).__name__
    except BaseException as error:
        return 'error', type(error).__name__, error.args


def descriptor_arguments():
    factories = (
        ('list', lambda: [1]), ('tuple', lambda: (1,)),
        ('dict', lambda: {1: 2}), ('set', lambda: set([1])),
        ('frozenset', lambda: frozenset([1])),
        ('keys', lambda: {1: 2}.viewkeys()),
        ('items', lambda: {1: 2}.viewitems()),
        ('values', lambda: {1: 2}.viewvalues()),
        ('xrange', lambda: xrange(1)),
        ('listiterator', lambda: iter([1])),
        ('tupleiterator', lambda: iter((1,))),
        ('dictiterator', lambda: iter({1: 2})),
        ('setiterator', lambda: iter(set([1]))),
        ('reversed', lambda: reversed((1,))),
    )
    methods = ('append', 'extend', 'insert', 'pop', 'remove', 'count', 'index',
               'reverse', 'get', 'has_key', 'keys', 'values', 'items',
               'iterkeys', 'itervalues', 'iteritems', 'viewkeys', 'viewvalues',
               'viewitems', 'clear', 'copy', 'update', 'setdefault', 'popitem',
               'add', 'discard', 'union', 'intersection', 'difference',
               'symmetric_difference', 'issubset', 'issuperset', 'isdisjoint',
               'intersection_update', 'difference_update',
               'symmetric_difference_update', '__len__', '__iter__',
               '__repr__', '__hash__', '__getitem__', '__setitem__',
               '__delitem__', '__contains__', '__add__', '__mul__',
               '__getslice__', '__setslice__', '__delslice__', '__reversed__',
               '__getnewargs__', '__and__', '__or__', '__xor__', '__sub__',
               '__lt__', '__eq__', '__cmp__', '__iadd__', '__imul__',
               'next', '__length_hint__')
    for kind, factory in factories:
        for method in methods:
            if not hasattr(factory(), method):
                continue
            for form in ('bound', 'unbound'):
                for count in xrange(5):
                    for keyword in (False, True):
                        target = factory()
                        args = (0,) * count
                        callback = getattr(target, method)
                        if form == 'unbound':
                            callback = getattr(type(target), method)
                            args = (target,) + args
                        keywords = {'unexpected': 0} if keyword else {}
                        emit('descriptor/%s/%s/%s/%d/%d' % (kind, method, form, count, keyword),
                             outcome(callback, args, keywords))


def constructor_arguments():
    for constructor in (set, frozenset, xrange, enumerate, reversed):
        for count in xrange(5):
            for keyword_id, keywords in enumerate(({}, {'other': 0}, {'sequence': (1,)}, {'start': 0})):
                args = (0,) * count if constructor is xrange else ((1,),) * count
                emit('constructor/%s/%d/%d' % (constructor.__name__, count, keyword_id),
                     outcome(constructor, args, keywords))

    class Set(set):
        def __init__(self, *args, **kwargs):
            self.marker = kwargs.get('marker')
    class Frozen(frozenset):
        def __init__(self, *args, **kwargs):
            self.marker = kwargs.get('marker')
    for base, child in ((set, Set), (frozenset, Frozen)):
        for args in ((), ((1,),), ((1,), (2,))):
            for keyword in (False, True):
                keywords = {'marker': 7} if keyword else {}
                for direct in (False, True):
                    try:
                        value = base.__new__(child, *args, **keywords) if direct else child(*args, **keywords)
                        answer = 'ok', sorted(value), getattr(value, 'marker', None)
                    except BaseException as error:
                        answer = 'error', type(error).__name__, error.args
                    emit('subtype/%s/%d/%d/%d' % (base.__name__, len(args), keyword, direct), answer)


def additional_native_arguments():
    for kind, factory in (('str', lambda: 'abc'), ('unicode', lambda: u'abc')):
        for direct in (False, True):
            for count in xrange(5):
                for keyword in (False, True):
                    value = factory()
                    callback = type(value).__getnewargs__ if direct else value.__getnewargs__
                    args = (value,) + (0,) * count if direct else (0,) * count
                    emit('getnewargs/%s/%d/%d/%d' % (kind, direct, count, keyword),
                         outcome(callback, args, {'unexpected': 0} if keyword else {}))
    for form in ('class', 'instance'):
        for count in xrange(5):
            for keyword in (False, True):
                callback = dict.fromkeys if form == 'class' else {}.fromkeys
                emit('fromkeys/%s/%d/%d' % (form, count, keyword),
                     outcome(callback, (0,) * count, {'unexpected': 0} if keyword else {}))


def slice_phase(entry, policy):
    events = []
    marker = TypeError('phase marker') if policy != 'initial_value' else ValueError('phase marker')
    class Source(object):
        def __iter__(self):
            events.append('iter')
            if policy in ('initial_type', 'initial_value'):
                raise marker
            return self
        def next(self):
            events.append('next')
            if policy == 'body_type':
                raise marker
            raise StopIteration
    source = 0 if policy == 'not_iterable' else Source()
    value = [1]
    try:
        if entry == 'setslice':
            result = value.__setslice__(0, 1, source)
        elif entry == 'setitem':
            result = value.__setitem__(slice(None), source)
        elif entry == 'syntax':
            value[:] = source
            result = None
        else:
            value[::1] = source
            result = None
        answer = 'ok', result
    except BaseException as error:
        answer = 'error', type(error).__name__, error.args, error is marker
    state = value[:]
    value[:] = [2]
    return answer, events, state, value


def conversion_order(name, policy, extra, keyword, direct):
    events = []
    marker = ValueError('conversion marker')
    value = [1]
    class Index(object):
        def convert(self, label):
            events.append(label)
            if policy == 'raise':
                raise marker
            return 1.5 if policy == 'bad' else 0
        def __int__(self):
            return self.convert('int')
        def __index__(self):
            return self.convert('index')
    index = Index()
    args = (index,) if name == 'pop' else ((index, 7) if name == 'insert' else (1, index, 1))
    if extra:
        args += (0,)
    callback = getattr(value, name)
    if direct:
        callback = getattr(list, name)
        args = (value,) + args
    try:
        result = callback(*args, **({'unexpected': 0} if keyword else {}))
        answer = 'ok', result
    except BaseException as error:
        answer = 'error', type(error).__name__, error.args, error is marker
    state = value[:]
    value.append(9)
    return answer, events, state, value


def main():
    descriptor_arguments()
    constructor_arguments()
    additional_native_arguments()
    for entry in ('setslice', 'setitem', 'syntax', 'step'):
        for policy in ('not_iterable', 'initial_type', 'initial_value', 'body_type', 'empty'):
            emit('slice/%s/%s' % (entry, policy), slice_phase(entry, policy))
    for name in ('pop', 'insert', 'index'):
        for policy in ('ok', 'raise', 'bad'):
            for extra in (False, True):
                for keyword in (False, True):
                    for direct in (False, True):
                        emit('conversion/%s/%s/%d/%d/%d' % (name, policy, extra, keyword, direct),
                             conversion_order(name, policy, extra, keyword, direct))


if __name__ == '__main__':
    main()
