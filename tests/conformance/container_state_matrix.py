"""Finite Python 2 pair conversion and container callback state products.

Mutation products retain the original keys and add one noncolliding integer to
an initial small table. They do not clear live set algebra entries or resize
the reference runtime's source storage during callbacks. The negative hint
boundary is represented by an ordinary raised SystemError in TinyPy, with the
same caller-specific message and recovery behavior as the reference runtime.
"""

from _functools import partial


def pair_case(kind, length, tail, hint, consumer, prefix):
    events = []
    values = [7, 8, 9, 10][:length]
    if length == 0:
        values = []
    class Iterator(object):
        def __init__(self):
            self.index = 0
        def __iter__(self):
            events.append('iter')
            return self
        def __length_hint__(self):
            events.append('hint')
            if hint == 'lookup':
                raise LookupError('hint')
            if hint == 'type':
                raise TypeError('hint')
            return -1 if hint == 'negative' else len(values)
        def next(self):
            events.append(('next', self.index))
            if self.index == len(values):
                if tail == 'lookup':
                    raise LookupError('tail')
                if tail == 'type':
                    raise TypeError('tail')
                raise StopIteration
            value = values[self.index]
            self.index += 1
            return value
    class ClassicIterator:
        __init__ = Iterator.__dict__['__init__']
        __iter__ = Iterator.__dict__['__iter__']
        __length_hint__ = Iterator.__dict__['__length_hint__']
        next = Iterator.__dict__['next']
    iterator = ClassicIterator() if kind == 'classic' else Iterator()
    class PairList(list):
        def __iter__(self):
            events.append('pair-iter')
            return iterator
    class PairTuple(tuple):
        def __iter__(self):
            events.append('pair-iter')
            return iterator
    class Iterable(object):
        def __iter__(self):
            events.append('pair-iter')
            return iterator
        def __len__(self):
            raise AssertionError('original pair length is not queried')
    if kind == 'list':
        pair = list(values)
    elif kind == 'tuple':
        pair = tuple(values)
    elif kind == 'list-subtype':
        pair = PairList([1, 2])
    elif kind == 'tuple-subtype':
        pair = PairTuple([1, 2])
    elif kind == 'iterable':
        pair = Iterable()
    elif kind == 'none':
        pair = None
    else:
        pair = iterator
    source = [(100 + index, 200 + index) for index in range(prefix)] + [pair]
    target = {-1: -2} if consumer != 'new' else {}
    try:
        if consumer == 'new':
            target = dict(source)
        elif consumer == 'init':
            dict.__init__(target, source)
        else:
            target.update(source)
        status = ('ok',)
    except Exception as error:
        status = (type(error).__name__, error.args)
    target.update([(500, 501)])
    return status, sorted(target.items()), events


def set_case(operation, left_kind, right_kind, extras, changed, equality):
    events = []
    state = {'armed': False}
    class Key(object):
        def __init__(self, name):
            self.name = name
        def __hash__(self):
            return 1
        def __eq__(self, other):
            events.append((self.name, other.name))
            if state['armed']:
                state['armed'] = False
                if changed != 'none':
                    state[changed].add(2)
            if equality == 'error':
                raise LookupError('comparison')
            return equality == 'equal'
    class Subset(set):
        pass
    class Subfrozen(frozenset):
        pass
    factories = {'set': set, 'subtype': Subset, 'frozen': frozenset, 'frozen-subtype': Subfrozen}
    originals = Key('left'), Key('right')
    left_values, right_values = [originals[0]], [originals[1]]
    if extras == 'left':
        left_values.append(2)
    elif extras == 'right':
        right_values.append(2)
    left, right = factories[left_kind](left_values), factories[right_kind](right_values)
    state.update(left=left, right=right, armed=True)
    try:
        if operation == 'subset':
            result = left.issubset(right)
        elif operation == 'superset':
            result = left.issuperset(right)
        elif operation == 'eq':
            result = left == right
        elif operation == 'le':
            result = left <= right
        elif operation == 'disjoint':
            result = left.isdisjoint(right)
        elif operation == 'intersection':
            result = left.intersection(right)
        elif operation == 'and':
            result = left & right
        elif operation == 'intersection-update':
            result = left.intersection_update(right)
        elif operation == 'difference-update':
            result = left.difference_update(right)
        elif operation == 'xor':
            result = left ^ right
        elif operation == 'symmetric':
            result = left.symmetric_difference(right)
        elif operation == 'symmetric-update':
            result = left.symmetric_difference_update(right)
        elif operation == 'ixor':
            result = left.__ixor__(right)
        elif operation == 'update':
            result = left.update(right)
        elif operation == 'union':
            result = left.union(right)
        if isinstance(result, (set, frozenset)):
            result = (type(result).__name__, labels(result))
        status = ('ok', result)
    except Exception as error:
        status = (type(error).__name__, error.args)
    finally:
        state.clear()
    return status, labels(left), labels(right), events


def labels(source):
    return tuple(sorted(item.name if hasattr(item, 'name') else str(item) for item in source))


def cached_hash_case(source_kind, operation, failed):
    events = []
    class Key(object):
        def __hash__(self):
            events.append('hash')
            if self.failed:
                raise LookupError('hash')
            return 1
    class Mapping(dict):
        def __iter__(self):
            events.append('iter')
            return iter([2])
    class Subset(set):
        def __iter__(self):
            events.append('iter')
            return iter([2])
    key = Key()
    key.failed = False
    mapping = {key: 7}
    factories = {'dict': dict, 'dict-subtype': Mapping, 'set': set, 'set-subtype': Subset, 'frozen': frozenset}
    source = factories[source_kind](mapping)
    key.failed = failed
    events[:] = []
    target = set([3])
    try:
        if operation == 'set':
            target = set(source)
        elif operation == 'frozen':
            target = frozenset(source)
        elif operation == 'update':
            target.update(source)
        elif operation == 'init':
            set.__init__(target, source)
        elif operation == 'union':
            target = target.union(source)
        elif operation == 'symmetric':
            target = target.symmetric_difference(source)
        else:
            target.symmetric_difference_update(source)
        status = ('ok', type(target).__name__)
    except Exception as error:
        status = (type(error).__name__, error.args)
    normalized = tuple(sorted('key' if item is key else str(item) for item in target))
    return status, normalized, events


def update_case(target_kind, source_kind, mutation, consumer):
    events, state = [], {}
    class Key(object):
        def __init__(self, name):
            self.name = name
        def __hash__(self):
            return 1
        def __eq__(self, other):
            events.append((self.name, other.name))
            source = state['source']
            if mutation == 'clear':
                source.clear()
            elif isinstance(source, dict):
                source[2] = 7
            else:
                source.add(2)
            return False
    class Mapping(dict):
        pass
    originals = Key('target'), Key('source')
    target = {originals[0]: 8} if target_kind == 'dict' else set([originals[0]])
    if source_kind == 'set':
        source = set([originals[1]])
    elif source_kind == 'dict-subtype':
        source = Mapping({originals[1]: 3})
    else:
        source = {originals[1]: 3}
    state['source'] = source
    try:
        if consumer == 'method':
            target.update(source)
        else:
            (dict if target_kind == 'dict' else set).update(target, source)
        status = ('ok',)
    except Exception as error:
        status = (type(error).__name__, error.args)
    finally:
        state.clear()
    return status, labels(target), labels(source), events


def partial_case(key_count, callback, positional, call_keyword):
    events, state = [], {}
    def body(label, arguments, keywords):
        events.append(label)
        return label, arguments, sorted((str(key), value) for key, value in keywords.items())
    def old(*args, **kwargs):
        return body('old', args, kwargs)
    def new(*args, **kwargs):
        return body('new', args, kwargs)
    class Key(str):
        def __hash__(self):
            # Keep one collision pair; a third independent keyword does not
            # depend on unspecified order among larger collision groups.
            return 3 if str.__eq__(self, 'c') else 1
        def __eq__(self, other):
            events.append((str(self), str(other)))
            if state.get('armed'):
                state['armed'] = False
                if callback == 'replace':
                    state['partial'].__setstate__((new, (99,), {}, None))
                elif callback == 'error':
                    raise LookupError('copy')
            return str.__eq__(self, other)
    originals = [Key(name) for name in ('a', 'b', 'c')[:key_count]]
    mapping = dict((key, index + 7) for index, key in enumerate(originals))
    target = partial(old)
    stored_args = (5,) if positional else ()
    target.__setstate__((old, stored_args, mapping, None))
    state.update(partial=target, armed=True)
    events[:] = []
    try:
        try:
            result = target(6, extra=10) if call_keyword else target(6)
            status = ('ok', result)
        except Exception as error:
            status = (type(error).__name__, error.args)
        state['armed'] = False
        recovered = target(11)
        current = target.func is new, target.args, len(target.keywords)
        return status, recovered, current, events
    finally:
        state.clear()


def negative_hint_case(consumer, user_error):
    events = []
    failure = SystemError('user hint')
    class Source(object):
        def __iter__(self):
            events.append('iter')
            return self
        def __length_hint__(self):
            events.append('hint')
            if user_error:
                raise failure
            return -1
        def next(self):
            raise AssertionError('invalid hint prevents iteration')
    target = [7]
    try:
        if consumer == 'list':
            result = list(Source())
        elif consumer == 'tuple':
            result = tuple(Source())
        elif consumer == 'init':
            result = list.__init__(target, Source())
        elif consumer == 'extend':
            result = target.extend(Source())
        elif consumer == 'iadd':
            target += Source()
            result = target
        elif consumer == 'join':
            result = ''.join(Source())
        else:
            result = type('Owner', (object,), {'__slots__': Source()})
        status = ('ok', result)
    except Exception as error:
        status = (type(error).__name__, error.args, error is failure)
    target.extend([8])
    return status, target, events


def other_negative_hint_case(consumer, hint):
    events = []
    class Source(object):
        def __iter__(self):
            events.append('iter')
            return self
        def __length_hint__(self):
            events.append('hint')
            return self.hint
        def next(self):
            events.append('next')
            raise StopIteration
    source = Source()
    source.hint = hint
    operation = list if consumer == 'list' else tuple
    try:
        status = ('ok', operation(source))
    except SystemError as error:
        # PyErr_BadInternalCall embeds CPython's build source path/line. Record
        # its exception category and complete reason instead of that location.
        status = ('SystemError', str(error).endswith('bad argument to internal function'))
    initial_events = events[:]
    source.hint = 0
    recovered = operation(source)
    return status, initial_events, recovered, events


def main():
    for kind in ('list', 'tuple', 'list-subtype', 'tuple-subtype', 'iterable', 'iterator', 'classic', 'none'):
        lengths = (0,) if kind == 'none' else range(5)
        tails = ('none',) if kind in ('list', 'tuple', 'none') else ('none', 'lookup', 'type')
        hints = ('none',) if kind in ('list', 'tuple', 'none') else ('normal', 'negative', 'lookup', 'type')
        for length in lengths:
            for tail in tails:
                for hint in hints:
                    for consumer in ('new', 'init', 'update'):
                        for prefix in range(3):
                            identifier = 'pair/%s/%s/%s/%s/%s/%s' % (kind, length, tail, hint, consumer, prefix)
                            print identifier + '\t' + repr(pair_case(kind, length, tail, hint, consumer, prefix))
    for source_kind in ('dict', 'dict-subtype', 'set', 'set-subtype', 'frozen'):
        for operation in ('set', 'frozen', 'update', 'init', 'union', 'symmetric', 'symmetric-update'):
            for failed in (False, True):
                identifier = 'hash/%s/%s/%s' % (source_kind, operation, failed)
                print identifier + '\t' + repr(cached_hash_case(source_kind, operation, failed))
    for left_kind, right_kind in (('set', 'set'), ('subtype', 'set'), ('set', 'subtype'), ('frozen', 'set'), ('set', 'frozen'), ('frozen-subtype', 'set')):
        for operation in ('subset', 'superset', 'eq', 'le', 'disjoint', 'intersection', 'and', 'intersection-update', 'difference-update', 'xor', 'symmetric', 'symmetric-update', 'ixor', 'update', 'union'):
            if left_kind.startswith('frozen') and operation in ('intersection-update', 'difference-update', 'symmetric-update', 'ixor', 'update'):
                continue
            for extras in ('none', 'left', 'right'):
                for changed in ('none', 'left', 'right'):
                    if changed == 'left' and left_kind.startswith('frozen'):
                        continue
                    if changed == 'right' and right_kind.startswith('frozen'):
                        continue
                    for equality in ('different', 'equal', 'error'):
                        identifier = 'set/%s/%s/%s/%s/%s/%s' % (left_kind, right_kind, operation, extras, changed, equality)
                        print identifier + '\t' + repr(set_case(operation, left_kind, right_kind, extras, changed, equality))
    for target_kind, source_kind in (('set', 'set'), ('set', 'dict'), ('dict', 'dict'), ('dict', 'dict-subtype')):
        for mutation in ('grow', 'clear'):
            for consumer in ('method', 'descriptor'):
                identifier = 'update/%s/%s/%s/%s' % (target_kind, source_kind, mutation, consumer)
                print identifier + '\t' + repr(update_case(target_kind, source_kind, mutation, consumer))
    for key_count in (1, 2, 3):
        for callback in ('none', 'replace', 'error'):
            for positional in (False, True):
                for call_keyword in (False, True):
                    identifier = 'partial/%s/%s/%s/%s' % (key_count, callback, positional, call_keyword)
                    print identifier + '\t' + repr(partial_case(key_count, callback, positional, call_keyword))
    for consumer in ('list', 'tuple', 'init', 'extend', 'iadd', 'join', 'slots'):
        for user_error in (False, True):
            identifier = 'negative-hint/%s/%s' % (consumer, user_error)
            print identifier + '\t' + repr(negative_hint_case(consumer, user_error))
    for consumer in ('list', 'tuple'):
        for hint in (-2, -3):
            identifier = 'other-negative-hint/%s/%s' % (consumer, hint)
            print identifier + '\t' + repr(other_negative_hint_case(consumer, hint))


if __name__ == '__main__':
    main()
