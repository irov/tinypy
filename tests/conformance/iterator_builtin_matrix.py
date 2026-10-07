"""Finite Python 2 iterator builtin domains and deterministic callback traces.

Every iterator stops after at most four values, even if exhaustion handling
regresses. Large length results are tested only where conversion precedes
allocation. CPython's map length-hint error paths leave a pending C error and
are excluded; their valid conversion paths are exercised separately.
"""


events = []


def emit(identity, function, *arguments):
    events[:] = []
    try:
        value = function(*arguments)
        outcome = type(value).__name__ + ':' + repr(value)
    except Exception as error:
        outcome = 'error:' + type(error).__name__
    print identity + '\t' + outcome + '\t' + repr(events)


class NewIterator(object):
    def __init__(self, values):
        self.values = values
        self.position = 0
    def __iter__(self):
        events.append('iter')
        return self
    def next(self):
        events.append('next:%d' % self.position)
        position = self.position
        self.position += 1
        if position >= len(self.values):
            raise StopIteration
        return self.values[position]


class ClassicIterator:
    def __init__(self, values):
        self.values = values
        self.position = 0
    def __iter__(self):
        events.append('iter')
        return self
    def next(self):
        events.append('next:%d' % self.position)
        position = self.position
        self.position += 1
        if position >= len(self.values):
            raise StopIteration
        return self.values[position]


class Sequence(object):
    def __init__(self, values):
        self.values = values
    def __getitem__(self, position):
        events.append('get:%d' % position)
        return self.values[position]


class List(list):
    def __iter__(self):
        events.append('list.iter')
        return list.__iter__(self)


class Tuple(tuple):
    def __iter__(self):
        events.append('tuple.iter')
        return tuple.__iter__(self)


def text(values):
    return ''.join(str(value) for value in values)


def key(value):
    events.append('key:' + repr(value))
    return value


def predicate(value):
    events.append('predicate:' + repr(value))
    return bool(value)


def identity(value):
    events.append('map:' + repr(value))
    return value


def add(left, right):
    events.append('add:' + repr((left, right)))
    return left + right


factories = [('list', list), ('tuple', tuple), ('str', text),
             ('unicode', lambda values: unicode(text(values))),
             ('bytearray', lambda values: bytearray(values)),
             ('buffer', lambda values: buffer(text(values))),
             ('xrange', lambda values: xrange(len(values))),
             ('list-subtype', List), ('tuple-subtype', Tuple),
             ('new-iterator', NewIterator), ('classic-iterator', ClassicIterator),
             ('sequence', Sequence)]
operations = [('list', list), ('tuple', tuple), ('sorted', sorted),
              ('all', all), ('any', any), ('min', min), ('max', max),
              ('sum', sum), ('filter-none', lambda value: filter(None, value)),
              ('filter-callback', lambda value: filter(predicate, value)),
              ('map-none', lambda value: map(None, value)),
              ('map-callback', lambda value: map(identity, value)),
              ('zip', lambda value: zip(value)),
              ('reduce', lambda value: reduce(add, value)),
              ('reduce-initial', lambda value: reduce(add, value, 5)),
              ('min-key', lambda value: min(value, key=key)),
              ('max-key', lambda value: max(value, key=key)),
              ('sorted-key', lambda value: sorted(value, key=key))]
for factory_name, factory in factories:
    for values_index, values in enumerate(([], [0], [1], [2, 0, 1])):
        for operation_name, operation in operations:
            emit('source/%s/%d/%s' % (factory_name, values_index, operation_name),
                 operation, factory(values))


class Integer(int):
    def __int__(self):
        events.append('integer.int')
        return 3


class Wide(long):
    def __int__(self):
        events.append('wide.int')
        return 3


class Number(object):
    def __int__(self):
        events.append('number.int')
        return 2


class Wrong(object):
    def __int__(self):
        events.append('wrong.int')
        return '2'


class Failed(object):
    def __int__(self):
        events.append('failed.int')
        raise LookupError('conversion failed')


class FloatOnly(object):
    def __float__(self):
        events.append('float')
        return 2.0


class Index(object):
    def __index__(self):
        events.append('index')
        return 2


results = [False, 2, 2L, Integer(2), Wide(2), 2.75, -1, None, '2',
           Number(), Wrong(), Failed(), FloatOnly(), Index()]
length_operations = [('len', len), ('bool', bool), ('list', list),
                     ('tuple', tuple), ('sorted', sorted),
                     ('zip', lambda value: zip(value)),
                     ('filter', lambda value: filter(None, value))]


def length_source(classic, result, length):
    if classic:
        class Source:
            pass
    else:
        class Source(object):
            pass
    def source_iter(self):
        events.append('iter')
        return iter([2, 0, 1])
    def source_length(self):
        events.append('len')
        return result
    def source_hint(self):
        events.append('hint')
        return result if not length else 2
    Source.__iter__ = source_iter
    if length:
        Source.__len__ = source_length
    Source.__length_hint__ = source_hint
    return Source()


for classic in (False, True):
    for result_index, result in enumerate(results):
        for operation_name, operation in length_operations:
            emit('length/%d/%d/%s' % (classic, result_index, operation_name),
                 operation, length_source(classic, result, True))
        for operation_name, operation in length_operations[2:]:
            emit('hint/%d/%d/%s' % (classic, result_index, operation_name),
                 operation, length_source(classic, result, False))
        if result_index in (0, 1, 2, 3, 4, 5, 7, 8, 9, 13):
            emit('map-valid-length/%d/%d' % (classic, result_index),
                 lambda source: map(None, source),
                 length_source(classic, result, True))


class Reverse(object):
    def __init__(self, first, second):
        self.first = first
        self.second = second
        self.calls = 0
    def __int__(self):
        self.calls += 1
        events.append('reverse:%d' % self.calls)
        return self.first if self.calls == 1 else self.second


def ordered_sorted(form, reverse):
    source = length_source(False, 3, True)
    if form == 'positional':
        return sorted(source, None, None, reverse)
    if form == 'keyword':
        return sorted(source, reverse=reverse)
    if form == 'unknown':
        return sorted(source, reverse=reverse, unknown=1)
    if form == 'duplicate':
        return sorted(source, None, cmp=None, reverse=reverse)
    if form == 'iterable-keyword':
        return sorted(iterable=source, reverse=reverse)
    if form == 'too-many':
        return sorted(source, None, None, reverse, 5)
    return sorted(reverse=reverse)


for reverse_index in range(10):
    for form in ('positional', 'keyword', 'unknown', 'duplicate',
                 'iterable-keyword', 'too-many', 'missing'):
        reverse = [False, True, 0, -1, 1L, Wide(1), 1.0, '1',
                   Reverse(0, 1), Reverse(1, 0)][reverse_index]
        emit('sorted-arguments/%d/%s' % (reverse_index, form),
             ordered_sorted, form, reverse)


def keyword_sort(builtin, name, equal, mode):
    class Keyword(str):
        def __eq__(self, other):
            events.append('keyword:' + other)
            if equal == 'error':
                raise ValueError('keyword comparison')
            if equal == 'base-error':
                raise KeyboardInterrupt('keyword comparison')
            return equal
        def __hash__(self):
            return str.__hash__(self)
    values = [2, 1]
    value = {'reverse': True, 'key': lambda item: -item,
             'cmp': lambda left, right: cmp(right, left)}[name]
    keywords = {Keyword(name): value}
    arguments = []
    if mode == 'duplicate':
        arguments = [None] * (('cmp', 'key', 'reverse').index(name) + 1)
    elif mode == 'unknown':
        keywords['unknown'] = 1
    if builtin:
        return sorted(values, *arguments, **keywords)
    values.sort(*arguments, **keywords)
    return values


for builtin in (False, True):
    for name in ('cmp', 'key', 'reverse'):
        for equal_index, equal in enumerate((False, True, 'error', 'base-error')):
            for mode in ('single', 'duplicate', 'unknown'):
                emit('keyword-subtype/%d/%s/%d/%s' % (builtin, name, equal_index, mode),
                     keyword_sort, builtin, name, equal, mode)


def two_sources(operation, left_values, right_values):
    left = NewIterator(left_values)
    right = NewIterator(right_values)
    if operation == 'zip':
        return zip(left, right)
    if operation == 'map-none':
        return map(None, left, right)
    return map(add, left, right)


for operation in ('zip', 'map-none', 'map-add'):
    for left_index, left in enumerate(([], [0], [1, 2], [2, 0, 1])):
        for right_index, right in enumerate(([], [0], [1, 2], [2, 0, 1])):
            emit('two-sources/%s/%d/%d' % (operation, left_index, right_index),
                 two_sources, operation, left, right)


class HashKey(object):
    def __hash__(self):
        events.append('hash')
        return 1
    def __eq__(self, other):
        events.append('eq')
        return self is other


class Dict(dict):
    def __setitem__(self, name, value):
        events.append('setitem')
        return dict.__setitem__(self, name, value)


class Alternate(dict):
    def __new__(cls):
        events.append('new')
        return Mapping()


class Mapping(object):
    def __init__(self):
        self.items = []
    def __setitem__(self, name, value):
        events.append('mapping.setitem')
        self.items.append((name, value))


class NonMapping(dict):
    def __new__(cls):
        events.append('new')
        return 7


def fromkeys(constructor, source, value):
    result = constructor.fromkeys(source, value)
    if isinstance(result, Mapping):
        return type(result).__name__ + ':' + repr(sorted(result.items))
    if isinstance(result, dict):
        return type(result).__name__ + ':' + repr(sorted(result.items()))
    return result


for constructor in (dict, Dict, Alternate, NonMapping):
    for source_index, source in enumerate(([], ['b'], ['b', 'a', 'b'],
                                           ('b', 'a'), {'a': 1, 'b': 2},
                                           set(['a', 'b']), frozenset(['a', 'b']))):
        for value_index, value in enumerate((None, False, 3)):
            emit('fromkeys/%s/%d/%d' % (constructor.__name__, source_index, value_index),
                 fromkeys, constructor, source, value)
for constructor in (dict, Dict):
    for source_index in range(5):
        instance = HashKey()
        source = [[instance], (instance,), {instance: 1},
                  set([instance]), frozenset([instance])][source_index]
        def counted_fromkeys(constructor, source):
            result = constructor.fromkeys(source, 3)
            return len(result)
        emit('cached-hash/%s/%d' % (constructor.__name__, source_index),
             counted_fromkeys, constructor, source)
