"""Finite Python 2 direct slices and container callback products.

Sequences contain at most three elements. Callback mutations replace values,
or add one retained object; no large allocations or unbounded iterators occur.
Every outcome includes callback order and the externally visible final state.
"""

events = []
state = {}


def emit(identity, function, *arguments):
    events[:] = []
    try:
        outcome = repr(function(*arguments))
    except BaseException as error:
        outcome = 'error:' + type(error).__name__ + ':' + repr(error.args)
    print identity + '\t' + outcome + '\t' + repr(events)


class Integer(int):
    def __int__(self):
        events.append('integer.int')
        return 2


class Wide(long):
    def __int__(self):
        events.append('wide.int')
        return 2


class Number(object):
    def __int__(self):
        events.append('number.int')
        return 1


class Index(object):
    def __index__(self):
        events.append('index')
        return 1


class Both(object):
    def __int__(self):
        events.append('both.int')
        return 1
    def __index__(self):
        events.append('both.index')
        return 2


class Wrong(object):
    def __int__(self):
        events.append('wrong.int')
        return '1'


class Failed(object):
    def __int__(self):
        events.append('failed.int')
        raise LookupError('bound')


bounds = [-2, -1, 0, 1, 4, 2L, Integer(1), Wide(1), True, 1.5,
          None, '2', Number(), Index(), Both(), Wrong(), Failed(),
          10L ** 40, -(10L ** 40), 1j]


def direct_slice(kind, start, stop):
    if kind == 'list-set':
        value = [1, 2, 3]
        value.__setslice__(start, stop, [8])
    elif kind == 'list-del':
        value = [1, 2, 3]
        value.__delslice__(start, stop)
    else:
        value = {'list': [1, 2, 3], 'tuple': (1, 2, 3),
                 'str': 'abc', 'unicode': u'abc'}[kind].__getslice__(start, stop)
    return type(value).__name__, value


for kind in ('list', 'tuple', 'str', 'unicode', 'list-set', 'list-del'):
    for start_index, start in enumerate(bounds):
        for stop_index, stop in enumerate(bounds):
            emit('legacy/%s/%02d/%02d' % (kind, start_index, stop_index),
                 direct_slice, kind, start, stop)


def integer_method(method, bound):
    value = [1, 2, 3]
    if method == 'insert':
        result = value.insert(bound, 8)
    else:
        result = value.pop(bound)
    return result, value


for method in ('insert', 'pop'):
    for index, bound in enumerate(bounds):
        emit('integer-method/%s/%02d' % (method, index), integer_method, method, bound)


class Key(object):
    def __init__(self, name):
        self.name = name
    def __hash__(self):
        events.append(('hash', self.name))
        if state.get('hash_error'):
            raise LookupError('hash')
        return 1
    def __eq__(self, other):
        events.append(('eq', self.name, other.name))
        mutation = state.get('mutation')
        if mutation == 'replace-value':
            state['left'][state['left_key']] = 17
        elif mutation == 'grow':
            state['left'][2] = 17
        elif mutation == 'clear':
            state['left'].clear()
        if state.get('equality') == 'error':
            raise LookupError('equal')
        return state.get('equality') != 'false'


def view_compare(operation, mutation, equality, orientation):
    state.clear()
    left_key, right_key = Key('left'), Key('right')
    left, right = {left_key: 1}, {right_key: 1}
    state.update(left=left, left_key=left_key, mutation=mutation, equality=equality)
    events[:] = []
    first, second = left.viewkeys(), right.viewkeys()
    if orientation:
        first, second = second, first
    try:
        if operation == 'eq':
            result = first == second
        elif operation == 'ne':
            result = first != second
        elif operation == 'lt':
            result = first < second
        elif operation == 'le':
            result = first <= second
        elif operation == 'gt':
            result = first > second
        else:
            result = first >= second
        outcome = ('value', result)
    except BaseException as error:
        outcome = ('error', type(error).__name__, error.args)
    final = len(left), sorted(left.values())
    state.clear()
    return outcome, final


for operation in ('eq', 'ne', 'lt', 'le', 'gt', 'ge'):
    for mutation in ('none', 'replace-value', 'grow', 'clear'):
        for equality in ('true', 'false', 'error'):
            for orientation in (0, 1):
                emit('view/%s/%s/%s/%d' % (operation, mutation, equality, orientation),
                     view_compare, operation, mutation, equality, orientation)


def view_contains(kind, query_kind, equality, hash_error):
    state.clear()
    original, other = Key('original'), Key('other')
    value = {original: 2}
    query = original if query_kind == 'same' else other
    if query_kind == 'list':
        query = []
    if kind == 'viewitems':
        query = (query, 2)
    if query_kind == 'short-tuple':
        query = (1,)
    elif query_kind == 'long-tuple':
        query = (1, 2, 3)
    state.update(equality=equality, hash_error=hash_error)
    events[:] = []
    try:
        outcome = ('value', query in getattr(value, kind)())
    except BaseException as error:
        outcome = ('error', type(error).__name__, error.args)
    state.clear()
    return outcome


for kind in ('viewkeys', 'viewitems'):
    for query_kind in ('same', 'distinct', 'list', 'short-tuple', 'long-tuple'):
        for equality in ('true', 'false', 'error'):
            for hash_error in (False, True):
                emit('contains/%s/%s/%s/%d' % (kind, query_kind, equality, hash_error),
                     view_contains, kind, query_kind, equality, hash_error)


class Value(object):
    def __eq__(self, other):
        events.append('value.eq')
        if state['mutation'] == 'replace-left':
            state['left'][state['left_key']] = 17
        elif state['mutation'] == 'replace-right':
            state['right'][state['right_key']] = 18
        elif state['mutation'] == 'grow-left':
            state['left'][2] = 19
        elif state['mutation'] == 'clear-left':
            state['left'].clear()
        if state['equality'] == 'error':
            raise LookupError('value')
        return state['equality'] != 'false'


def dict_compare(mutation, equality, key_kind, operation):
    state.clear()
    if key_kind == 'distinct':
        left_key, right_key = Key('left'), Key('right')
    elif key_kind == 'same':
        left_key = right_key = Key('same')
    else:
        left_key = right_key = 1
    original, other = Value(), object()
    left, right = {left_key: original}, {right_key: other}
    state.update(left=left, right=right, left_key=left_key, right_key=right_key,
                 mutation=mutation, equality=equality)
    events[:] = []
    try:
        result = left == right if operation == 'eq' else left != right
        outcome = ('value', result)
    except BaseException as error:
        outcome = ('error', type(error).__name__, error.args)
    final = len(left), len(right), left.get(left_key) is original, right.get(right_key) is other
    state.clear()
    return outcome, final


for mutation in ('none', 'replace-left', 'replace-right', 'grow-left', 'clear-left'):
    for equality in ('true', 'false', 'error'):
        for key_kind in ('integer', 'same', 'distinct'):
            for operation in ('eq', 'ne'):
                emit('dict/%s/%s/%s/%s' % (mutation, equality, key_kind, operation),
                     dict_compare, mutation, equality, key_kind, operation)


class Source(object):
    def __init__(self, values, failure):
        self.values, self.failure = values, failure
    def __iter__(self):
        events.append('iter')
        for value in self.values:
            events.append(('yield', value.name))
            yield value
        if self.failure:
            events.append('tail')
            raise LookupError('tail')


def set_consume(kind, method, source_kind, equality, failure):
    state.clear()
    original, one, two = Key('left'), Key('one'), Key('two')
    value = kind([original])
    incoming = [one, two]
    if source_kind == 'list':
        source = incoming
    elif source_kind == 'tuple':
        source = tuple(incoming)
    elif source_kind == 'set':
        source = set(incoming)
    elif source_kind == 'frozenset':
        source = frozenset(incoming)
    elif source_kind == 'dict':
        source = dict((key, 7) for key in incoming)
    else:
        source = Source(incoming, failure)
    state['equality'] = equality
    events[:] = []
    try:
        result = getattr(value, method)(source)
        if result is None:
            result = value
        outcome = ('value', type(result).__name__, sorted(key.name for key in result))
    except BaseException as error:
        outcome = ('error', type(error).__name__, error.args)
    final = sorted(key.name for key in value)
    state.clear()
    return outcome, final


class Mutable(set):
    pass


class Frozen(frozenset):
    pass


for kind in (set, frozenset, Mutable, Frozen):
    methods = ['intersection', 'difference']
    if issubclass(kind, set):
        methods.extend(['intersection_update', 'difference_update'])
    for method in methods:
        for source_kind in ('list', 'tuple', 'set', 'frozenset', 'dict', 'iterator'):
            for equality in ('true', 'false', 'error'):
                for failure in (False, True) if source_kind == 'iterator' else (False,):
                    emit('set/%s/%s/%s/%s/%d' % (kind.__name__, method, source_kind, equality, failure),
                         set_consume, kind, method, source_kind, equality, failure)


class Name(str):
    failed = False
    def __hash__(self):
        events.append('name.hash')
        if self.failed:
            raise LookupError('name')
        return str.__hash__(self)


def proxy_copy(failed, subtype):
    name = Name('member') if subtype else 'member'
    owner = type('Owner', (object,), {name: 7})
    if subtype:
        name.failed = failed
    events[:] = []
    copied = owner.__dict__.copy()
    return type(copied).__name__, copied['member'], any(key is name for key in copied)


for failed in (False, True):
    for subtype in (False, True):
        emit('proxy/%d/%d' % (failed, subtype), proxy_copy, failed, subtype)
