"""Finite Python 2 functional protocols, iterator errors and recovery.

Input sequences contain at most three values. Numeric protocol conversions
produce small integers or immediate errors. General map inputs have valid
lengths; no claim about its ignored hint error policy is made by this matrix.
"""

from _functools import partial
from _functools import reduce as functools_reduce
import _weakref as weakref

events = []


def emit(identity, function, *arguments):
    events[:] = []
    try:
        value = function(*arguments)
        outcome = type(value).__name__ + ':' + repr(value)
    except BaseException as error:
        outcome = 'error:' + type(error).__name__
    print identity + '\t' + outcome + '\t' + repr(events)


class Integer(int):
    def __int__(self):
        events.append('integer.int')
        return 3
    def __index__(self):
        events.append('integer.index')
        return 4


class Wide(long):
    def __int__(self):
        events.append('wide.int')
        return 3
    def __index__(self):
        events.append('wide.index')
        return 4


class Number(object):
    def __int__(self):
        events.append('number.int')
        return 2


class Index(object):
    def __index__(self):
        events.append('index')
        return 2L


class Both(object):
    def __int__(self):
        events.append('both.int')
        return 2
    def __index__(self):
        events.append('both.index')
        return 3


class Wrong(object):
    def __int__(self):
        events.append('wrong.int')
        return '2'
    def __index__(self):
        events.append('wrong.index')
        return '2'


class Failed(object):
    def __int__(self):
        events.append('failed.int')
        raise LookupError('conversion')
    def __index__(self):
        events.append('failed.index')
        raise LookupError('conversion')


numbers = [False, True, -1, 0, 2, 2L, Integer(2), Wide(2), 2.75,
           '2', None, Number(), Index(), Both(), Wrong(), Failed()]


def ranged(number, arity):
    if arity == 1:
        return list(xrange(number))
    if arity == 2:
        return list(xrange(number, 5))
    return list(xrange(0, 5, number))


def counted(start, values):
    result = list(enumerate(values, start))
    return [(type(index).__name__, index, value) for index, value in result]


for number_index, number in enumerate(numbers):
    for arity in (1, 2, 3):
        emit('xrange/%d/%d' % (number_index, arity), ranged, number, arity)
    for values_index, values in enumerate(([], ['a'], ['a', 'b', 'c'])):
        emit('enumerate/%d/%d' % (number_index, values_index), counted, number, values)
for start_index, start in enumerate(((1L << 63) - 1, 1L << 63,
                                    -(1L << 63), -(1L << 63) - 1,
                                    (1L << 80), -(1L << 80))):
    emit('enumerate-wide/%d' % start_index, counted, start, ['a', 'b', 'c'])


def backward(classic, length):
    if classic:
        class Sequence:
            pass
    else:
        class Sequence(object):
            pass
    def sequence_length(self):
        events.append('len')
        return length
    def sequence_item(self, index):
        events.append('item:%d' % index)
        return index
    Sequence.__len__ = sequence_length
    Sequence.__getitem__ = sequence_item
    iterator = reversed(Sequence())
    hint = iterator.__length_hint__()
    return hint, list(iterator), iterator.__length_hint__()


for classic in (False, True):
    for number_index, number in enumerate(numbers):
        emit('reversed-length/%d/%d' % (classic, number_index), backward, classic, number)


def keyword_operation(operation, name, comparison, form):
    class Keyword(str):
        def __eq__(self, other):
            events.append('eq:' + other)
            if comparison == 'error':
                raise ValueError('keyword')
            if comparison == 'base-error':
                raise KeyboardInterrupt('keyword')
            return str.__eq__(self, other) if comparison == 'equal' else False
        def __hash__(self):
            return str.__hash__(self)
    value = [2, 1] if name == 'sequence' else (4 if name == 'start' else lambda item: -item)
    keywords = {Keyword(name): value}
    if form == 'extra':
        keywords['unknown'] = 1
    if operation == 'enumerate':
        if form == 'missing':
            return list(enumerate(**keywords))
        if form == 'duplicate':
            return list(enumerate([2, 1], 7, **keywords))
        return list(enumerate([2, 1], **keywords))
    if form == 'missing':
        return (min if operation == 'min' else max)(**keywords)
    if form == 'duplicate':
        return (min if operation == 'min' else max)(2, 1, **keywords)
    return (min if operation == 'min' else max)([2, 1], **keywords)


for operation, names in [('enumerate', ('sequence', 'start', 'unknown')),
                         ('min', ('key', 'unknown')), ('max', ('key', 'unknown'))]:
    for name in names:
        for comparison in ('equal', 'unequal', 'error', 'base-error'):
            for form in ('single', 'extra', 'missing', 'duplicate'):
                emit('keywords/%s/%s/%s/%s' % (operation, name, comparison, form),
                     keyword_operation, operation, name, comparison, form)


class Source(object):
    def __init__(self, failure):
        self.position = 0
        self.failure = failure
    def __iter__(self):
        events.append('iter')
        if self.failure == 'iter':
            raise LookupError('iterator')
        return self
    def __len__(self):
        events.append('len')
        return 3
    def next(self):
        position = self.position
        self.position += 1
        events.append('next:%d' % position)
        if position >= 3:
            raise StopIteration
        if self.failure == position:
            raise ValueError('item')
        return [2, 0, 1][position]


def add(left, right):
    events.append('add:' + repr((left, right)))
    return left + right


def identity(value):
    events.append('call:' + repr(value))
    return value


consumers = [('all', all), ('any', any), ('min', min), ('max', max),
             ('sum', sum), ('reduce', lambda value: reduce(add, value)),
             ('functools-reduce', lambda value: functools_reduce(add, value)),
             ('filter', lambda value: filter(identity, value)),
             ('map', lambda value: map(identity, value)),
             ('zip', lambda value: zip(value, [4, 5, 6])),
             ('enumerate', lambda value: list(enumerate(value))),
             ('sorted', sorted)]


def consumer_recovery(operation, failure):
    source = Source(failure)
    try:
        value = operation(source)
        first = type(value).__name__ + ':' + repr(value)
    except BaseException as error:
        first = 'error:' + type(error).__name__
    source.failure = None
    return first, list(source)


for operation_name, operation in consumers:
    for failure_index, failure in enumerate((None, 'iter', 0, 1, 2)):
        emit('consumer-recovery/%s/%d' % (operation_name, failure_index),
             consumer_recovery, operation, failure)


def reduce_lifetime(operation, count, initialized):
    observed = []
    class Total(object):
        def __init__(self, name):
            self.name = name
        def __del__(self):
            events.append('released:' + self.name)
    class Items(object):
        def __init__(self):
            self.position = 0
        def __iter__(self):
            return self
        def next(self):
            events.append(('next', self.position,
                           observed[0]() is not None if observed else None))
            self.position += 1
            if self.position > count:
                raise StopIteration
            return self.position
    def combine(left, right):
        events.append('combine:%d' % right)
        result = Total(str(right))
        observed.append(weakref.ref(result))
        return result
    result = operation(combine, Items(), Total('initial')) if initialized else operation(combine, Items())
    return result.name if isinstance(result, Total) else result


for operation_index, operation in enumerate((reduce, functools_reduce)):
    for count in range(4):
        for initialized in (False, True):
            emit('reduce-lifetime/%d/%d/%d' % (operation_index, count, initialized),
                 reduce_lifetime, operation, count, initialized)


def record(*arguments, **keywords):
    events.append('target')
    return arguments, sorted(keywords.items())


def partial_call(stored, supplied, stored_keywords, supplied_keywords):
    value = partial(record, *stored, **stored_keywords)
    result = value(*supplied, **supplied_keywords)
    return result, value.args, sorted(value.keywords.items())


argument_sets = [(), (1,), (1, 2)]
keyword_sets = [{}, {'x': 1}, {'x': 2, 'y': 3}]
for stored_index, stored in enumerate(argument_sets):
    for supplied_index, supplied in enumerate(argument_sets):
        for stored_keywords_index, stored_keywords in enumerate(keyword_sets):
            for supplied_keywords_index, supplied_keywords in enumerate(keyword_sets):
                emit('partial-call/%d/%d/%d/%d' % (stored_index, supplied_index,
                                                  stored_keywords_index, supplied_keywords_index),
                     partial_call, stored, supplied, stored_keywords, supplied_keywords)


class Arguments(tuple):
    def __iter__(self):
        events.append('args.iter')
        return iter([7, 8])
    def __len__(self):
        events.append('args.len')
        return 2.75


class FailedArguments(tuple):
    def __iter__(self):
        events.append('args.iter')
        raise LookupError('arguments')


class Keywords(dict):
    def keys(self):
        events.append('keywords.keys')
        return []
    def __iter__(self):
        events.append('keywords.iter')
        return iter([])


def partial_state(arguments, keywords, attributes):
    value = partial(record, 1, old=2)
    try:
        value.__setstate__((record, arguments, keywords, attributes))
        state_result = 'ok'
    except BaseException as error:
        state_result = 'error:' + type(error).__name__
    return (state_result, type(value.args).__name__, type(value.keywords).__name__,
            value.args is arguments, value.keywords is keywords,
            value.__dict__ is attributes, value())


for arguments_index, arguments in enumerate(((), (3,), Arguments([1]),
                                            FailedArguments([1]), [], None)):
    for keywords_index, keywords in enumerate((None, {}, {'x': 2}, Keywords(x=2), [])):
        for attributes_index, attributes in enumerate((None, {}, {'label': 'state'})):
            emit('partial-state/%d/%d/%d' % (arguments_index, keywords_index, attributes_index),
                 partial_state, arguments, keywords, attributes)


def partial_hashes(call, additional):
    class Keyword(str):
        def __hash__(self):
            events.append('hash')
            return str.__hash__(self)
    keywords = {Keyword('x'): 1}
    events[:] = []
    value = partial(record, **keywords)
    if call:
        return value(**additional)
    return len(value.keywords)


for call in (False, True):
    for additional_index, additional in enumerate(({}, {'x': 3}, {'y': 4})):
        emit('partial-hash/%d/%d' % (call, additional_index), partial_hashes, call, additional)
