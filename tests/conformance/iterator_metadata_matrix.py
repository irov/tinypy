"""Finite iterator taxonomy, descriptor arguments and callback-state products."""

import sys


class Sequence(object):
    def __len__(self):
        return 2
    def __getitem__(self, index):
        if index < 2:
            return index
        raise IndexError


class ClassicSequence:
    def __len__(self):
        return 2
    def __getitem__(self, index):
        if index < 2:
            return index
        raise IndexError


class Text(str):
    pass


class Unicode(unicode):
    pass


class List(list):
    pass


class Tuple(tuple):
    pass


class Bytearray(bytearray):
    pass


def callable_iterator():
    values = [1, 0]
    return iter(lambda: values.pop(0), 0)


FACTORIES = (
    ('list', lambda: iter([1])), ('tuple', lambda: iter((1,))),
    ('str', lambda: iter('a')), ('unicode', lambda: iter(u'a')),
    ('buffer', lambda: iter(buffer('a'))), ('bytearray', lambda: iter(bytearray('a'))),
    ('dictkey', lambda: iter({1: 2})), ('dictvalue', lambda: {1: 2}.itervalues()),
    ('dictitem', lambda: {1: 2}.iteritems()), ('set', lambda: iter(set([1]))),
    ('callable', callable_iterator), ('reverse_list', lambda: reversed([1])),
    ('reverse_tuple', lambda: reversed((1,))), ('reverse_str', lambda: reversed('a')),
    ('reverse_unicode', lambda: reversed(u'a')),
    ('reverse_bytearray', lambda: reversed(bytearray('a'))),
    ('xrange', lambda: iter(xrange(1))), ('xrange_reverse', lambda: reversed(xrange(1))),
    ('enumerate', lambda: enumerate([1])), ('sequence', lambda: iter(Sequence())),
    ('classic_sequence', lambda: iter(ClassicSequence())),
    ('reverse_sequence', lambda: reversed(Sequence())),
    ('str_subtype', lambda: iter(Text('a'))),
    ('unicode_subtype', lambda: iter(Unicode('a'))),
    ('list_subtype', lambda: iter(List([1]))),
    ('tuple_subtype', lambda: iter(Tuple((1,)))),
    ('bytearray_subtype', lambda: iter(Bytearray('a'))),
)


def emit(identity, answer):
    print identity + '\t' + repr(answer)
    sys.exc_clear()


def describe(value, iterator):
    if value is iterator:
        return 'self', type(value).__name__
    return 'value', type(value).__name__, value


def bind(iterator, name, form):
    if form == 'bound':
        return getattr(iterator, name), ()
    descriptor = getattr(type(iterator), name)
    if form == 'descriptor':
        return descriptor.__get__(iterator, type(iterator)), ()
    return descriptor, (iterator,)


def metadata_and_arguments():
    for label, factory in FACTORIES:
        iterator = factory()
        owner = type(iterator)
        methods = ('next', '__iter__') + (('__length_hint__',) if hasattr(iterator, '__length_hint__') else ())
        metadata = []
        for name in methods:
            descriptor, bound = getattr(owner, name), getattr(iterator, name)
            metadata.append((name, type(descriptor).__name__, type(bound).__name__, bound.__name__,
                             bound.__self__ is iterator,
                             hasattr(bound, '__objclass__'),
                             getattr(bound, '__objclass__', None) is owner))
        emit('metadata/%s' % label, (owner.__name__, owner.__module__, repr(owner),
                                   iter(iterator) is iterator, metadata))
        for name in methods:
            for progress in (0, 1, 3):
                for form in ('bound', 'unbound', 'descriptor'):
                    for count in (0, 1, 2):
                        for keywords in (False, True):
                            iterator = factory()
                            for unused in xrange(progress):
                                next(iterator, None)
                            callback, prefix = bind(iterator, name, form)
                            try:
                                result = callback(*(prefix + (0,) * count), **({'other': 0} if keywords else {}))
                                answer = 'ok', describe(result, iterator)
                            except BaseException as error:
                                answer = 'error', type(error).__name__, error.args
                            recovery = describe(next(iterator, None), iterator)
                            emit('arguments/%s/%s/%d/%s/%d/%d' % (label, name, progress, form, count, keywords),
                                 (answer, recovery))


def generic_hint(kind, policy, progress, form, handling):
    events = []
    marker, outer = ValueError('length marker'), KeyError('outer')
    def length(source):
        events.append(('len', sys.exc_info()[1] is outer))
        if policy == 'raise':
            raise marker
        return {'zero': 0, 'int': 3, 'float': 3.5, 'string': '3',
                'none': None, 'negative': -2, 'large': 2 ** 80}[policy]
    def item(source, index):
        events.append(('item', index))
        if index < 2:
            return index
        raise IndexError
    if kind == 'classic':
        class Source:
            __len__ = length
            __getitem__ = item
    else:
        base = {'object': object, 'str': str, 'unicode': unicode}[kind]
        class Source(base):
            __len__ = length
            __getitem__ = item
    if policy == 'missing':
        del Source.__len__
    source = Source('ab') if kind in ('str', 'unicode') else Source()
    iterator = iter(source)
    for unused in xrange(progress):
        next(iterator, None)
    callback, prefix = bind(iterator, '__length_hint__', form)
    def observe():
        try:
            answer = 'ok', callback(*prefix)
        except BaseException as error:
            answer = 'error', type(error).__name__, error.args, error is marker
        return answer
    if handling:
        try:
            raise outer
        except KeyError:
            answer = observe()
            preserved = sys.exc_info()[1] is outer
    else:
        answer, preserved = observe(), True
    recovery = next(iterator, 'done')
    return answer, events, preserved, recovery


def reentry_hint(kind, action, form):
    events, state = [], {}
    def item(source, index):
        events.append(('item', index))
        if index < 2:
            return index
        raise IndexError
    def length(source):
        events.append('len')
        if not state.get('entered'):
            state['entered'] = True
            for unused in xrange({'none': 0, 'next': 1, 'drain': 3}[action]):
                next(state['iterator'], None)
        return 3
    base = {'object': object, 'str': str, 'unicode': unicode}[kind]
    class Source(base):
        __getitem__ = item
        __len__ = length
    source = Source('ab') if kind != 'object' else Source()
    iterator = iter(source)
    state['iterator'] = iterator
    callback, prefix = bind(iterator, '__length_hint__', form)
    try:
        first = callback(*prefix)
        second = callback(*prefix)
        recovery = next(iterator, 'done')
        return first, second, recovery, events
    finally:
        state.clear()


def mutable_iterator(kind, progress, mutation, reverse):
    source = [1] if kind == 'list' else bytearray([1])
    iterator = reversed(source) if reverse else iter(source)
    for unused in xrange(progress):
        next(iterator, None)
    if mutation == 'grow':
        source.append(2)
    elif mutation == 'clear':
        del source[:]
    else:
        source[0] = 3
    hints, values = [], []
    for unused in xrange(4):
        hints.append(iterator.__length_hint__())
        values.append(next(iterator, None))
    source.append(4)
    return hints, values, iterator.__length_hint__(), next(iterator, None), list(source)


def main():
    metadata_and_arguments()
    for kind in ('object', 'classic', 'str', 'unicode'):
        for policy in ('zero', 'int', 'float', 'string', 'none', 'negative', 'large', 'raise', 'missing'):
            for progress in (0, 1, 2, 3):
                for form in ('bound', 'unbound', 'descriptor'):
                    for handling in (False, True):
                        emit('generic/%s/%s/%d/%s/%d' % (kind, policy, progress, form, handling),
                             generic_hint(kind, policy, progress, form, handling))
    for kind in ('object', 'str', 'unicode'):
        for action in ('none', 'next', 'drain'):
            for form in ('bound', 'unbound', 'descriptor'):
                emit('reentry/%s/%s/%s' % (kind, action, form), reentry_hint(kind, action, form))
    for kind in ('list', 'bytearray'):
        for progress in (0, 1, 2, 3):
            for mutation in ('grow', 'clear', 'replace'):
                for reverse in (False, True):
                    emit('mutation/%s/%d/%s/%d' % (kind, progress, mutation, reverse),
                         mutable_iterator(kind, progress, mutation, reverse))


if __name__ == '__main__':
    main()
