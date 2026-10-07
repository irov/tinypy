"""Finite ordinary descriptor, function binding and generator state outcomes."""

import sys


def emit(identity, callback):
    try:
        outcome = ('value', callback())
    except BaseException as error:
        outcome = ('error', type(error).__name__, error.args)
    print identity + '\t' + repr(outcome)
    sys.exc_clear()


def caught(callback):
    try:
        return 'value', callback()
    except BaseException as error:
        return 'error', type(error).__name__, error.args


def descriptor_case(flags, operation, classic, failure):
    events = []
    def result(label, value=None):
        events.append((label, value))
        if failure:
            raise (ValueError, KeyboardInterrupt)[failure - 1]('descriptor failure')
        return False
    def read(self, instance, owner):
        result('get')
        return 'descriptor'
    def write(self, instance, value):
        return result('set', value)
    def remove(self, instance):
        return result('delete')
    namespace = {}
    for mask, name, handler in ((1, '__get__', read), (2, '__set__', write), (4, '__delete__', remove)):
        if flags & mask:
            namespace[name] = handler
    Descriptor = type('Descriptor', (object,), namespace)
    class Classic:
        pass
    class Modern(object):
        pass
    Owner = Classic if classic else Modern
    Owner.member = Descriptor()
    value = Owner()
    value.__dict__['member'] = 17
    def invoke():
        if operation == 0:
            item = value.member
            return 'descriptor-object' if item is Owner.__dict__['member'] else item
        if operation == 1:
            value.member = 23
        else:
            del value.member
    try:
        outcome = caught(invoke)
        return outcome, events, value.__dict__.get('member', 'missing')
    finally:
        del Owner.member


for flags in xrange(8):
    for operation in xrange(3):
        for classic in (False, True):
            for failure in xrange(3):
                emit('descriptor/%d/%d/%d/%d' % (flags, operation, int(classic), failure),
                     lambda: descriptor_case(flags, operation, classic, failure))


def property_case(flags, operation, direct, failure):
    events = []
    def result(label, value=None):
        events.append((label, value))
        if failure:
            raise (ValueError, KeyboardInterrupt)[failure - 1]('property failure')
        return False
    def read(instance):
        result('get')
        return 19
    def write(instance, value):
        return result('set', value)
    def remove(instance):
        return result('delete')
    field = property(read if flags & 1 else None, write if flags & 2 else None,
                     remove if flags & 4 else None)
    class Owner(object):
        member = field
    value = Owner()
    value.__dict__['member'] = 17
    def invoke():
        if direct:
            if operation == 0:
                return field.__get__(value, Owner)
            if operation == 1:
                return field.__set__(value, 23)
            return field.__delete__(value)
        if operation == 0:
            return value.member
        if operation == 1:
            value.member = 23
        else:
            del value.member
    outcome = caught(invoke)
    return outcome, events, value.__dict__['member']


for flags in xrange(8):
    for operation in xrange(3):
        for direct in (False, True):
            for failure in xrange(3):
                emit('property/%d/%d/%d/%d' % (flags, operation, int(direct), failure),
                     lambda: property_case(flags, operation, direct, failure))


def function_field(name, value, deleting):
    def sample(value=17):
        return value
    def invoke():
        if deleting:
            delattr(sample, name)
        else:
            setattr(sample, name, value)
        result = getattr(sample, name)
        if result is None or isinstance(result, (int, str, unicode)):
            return result
        if isinstance(result, (dict, tuple)):
            return result
        return type(result).__name__
    outcome = caught(invoke)
    return outcome, sample(31)


for field_index, name in enumerate(('func_name', '__name__', 'func_defaults', '__defaults__',
                                   'func_dict', '__dict__', 'func_code', '__code__',
                                   'func_globals', '__globals__', 'func_closure', '__closure__')):
    for value_index, value in enumerate((None, 17, u'name', {}, (), (29,), 'name')):
        emit('function-field/%d/%d' % (field_index, value_index),
             lambda: function_field(name, value, False))
    emit('function-field/%d/delete' % field_index, lambda: function_field(name, None, True))


def closure_function(count):
    if count == 0:
        def plain():
            return 17
        return plain
    first = 19
    if count == 1:
        def single():
            return first
        return single
    second = 23
    def double():
        return first, second
    return double


def code_transition(old_count, new_count, name, alias):
    value = closure_function(old_count)
    replacement = closure_function(new_count)
    value.func_name = name
    previous = value.func_code
    def invoke():
        setattr(value, alias, replacement.func_code)
        return value()
    return caught(invoke), value.func_code is previous, value()


for old_count in xrange(3):
    for new_count in xrange(3):
        for name_index, name in enumerate(('renamed', 'prefix\0suffix')):
            for alias_index, alias in enumerate(('func_code', '__code__')):
                emit('code-transition/%d/%d/%d/%d' % (old_count, new_count, name_index, alias_index),
                     lambda: code_transition(old_count, new_count, name, alias))


def binding_transition(kind, change, raw_name, positional):
    events = []
    if kind == 0:
        def sample(required, fallback=17, **extra):
            return 'old', required, fallback, sorted(extra.items())
        def replacement(required, fallback=29, **extra):
            return 'new', required, fallback, sorted(extra.items())
        callable_value = sample
    elif kind == 1:
        class Owner(object):
            def sample(self, required, fallback=17, **extra):
                return 'old', required, fallback, sorted(extra.items())
        def replacement(self, required, fallback=29, **extra):
            return 'new', required, fallback, sorted(extra.items())
        callable_value = Owner().sample
        sample = callable_value.im_func
    else:
        def sample(required, fallback=17, **extra):
            yield 'old', required, fallback, sorted(extra.items())
        def replacement(required, fallback=29, **extra):
            yield 'new', required, fallback, sorted(extra.items())
        callable_value = sample
    # Retain the original code/default objects while comparing callback
    # mutation of binding snapshots; the reference binder receives borrowed
    # pointers to these public objects.
    retained_code = sample.func_code
    retained_defaults = sample.func_defaults
    changed = []
    class Keyword(str):
        def __hash__(self):
            return str.__hash__(self)
        def __eq__(self, other):
            events.append(str(other))
            if change == 3:
                raise ValueError('keyword failure')
            if not changed:
                changed.append(True)
                if change in (0, 2):
                    sample.func_defaults = (29,)
                if change in (1, 2):
                    sample.func_code = replacement.func_code
            return str.__eq__(self, other)
    mapping = {Keyword(raw_name): 23}
    arguments = (31,) if positional else ()
    def invoke():
        result = callable_value(*arguments, **mapping)
        if kind == 2:
            try:
                return result.next()
            finally:
                result.close()
        return result
    outcome = caught(invoke)
    result = callable_value(37)
    if kind == 2:
        try:
            recovery = result.next()
        finally:
            result.close()
    else:
        recovery = result
    return outcome, events, recovery, retained_code.co_name, retained_defaults


for kind in xrange(3):
    for change in xrange(4):
        for name_index, raw_name in enumerate(('required', 'fallback', 'other')):
            for positional in (False, True):
                emit('binding/%d/%d/%d/%d' % (kind, change, name_index, int(positional)),
                     lambda: binding_transition(kind, change, raw_name, positional))


def reentrant_descriptor(operation, failure):
    events = []
    class Descriptor(object):
        def __set__(self, instance, value):
            events.append(('set', value))
            del type(instance).member
            events.append('removed')
            if failure:
                raise ValueError('setter failure')
        def __delete__(self, instance):
            events.append('delete')
            del type(instance).member
            events.append('removed')
            if failure:
                raise ValueError('deleter failure')
        def __del__(self):
            events.append('released')
    class Owner(object):
        member = Descriptor()
    value = Owner()
    def invoke():
        if operation == 0:
            value.member = 17
        else:
            del value.member
    outcome = caught(invoke)
    value.member = 23
    return outcome, events, value.member


for operation in xrange(2):
    for failure in (False, True):
        emit('reentrant-descriptor/%d/%d' % (operation, int(failure)),
             lambda: reentrant_descriptor(operation, failure))


def mro_transition(operation, failure):
    events = []
    class Root(object):
        pass
    class First(Root):
        marker = 17
    class Second(Root):
        marker = 23
    class Descriptor(object):
        def __get__(self, instance, owner):
            events.append(('get', owner.marker))
            owner.__bases__ = (Second,)
            if failure:
                raise ValueError('get transition')
            return 19
        def __set__(self, instance, value):
            events.append(('set', type(instance).marker))
            type(instance).__bases__ = (Second,)
            if failure:
                raise ValueError('set transition')
        def __delete__(self, instance):
            events.append(('delete', type(instance).marker))
            type(instance).__bases__ = (Second,)
            if failure:
                raise ValueError('delete transition')
    class Owner(First):
        member = Descriptor()
    value = Owner()
    warm = value.marker, Owner.marker
    def invoke():
        if operation == 0:
            return value.member
        if operation == 1:
            value.member = 31
        else:
            del value.member
    outcome = caught(invoke)
    return warm, outcome, events, value.marker, Owner.marker, [item.__name__ for item in Owner.__mro__]


for operation in xrange(3):
    for failure in (False, True):
        emit('mro-transition/%d/%d' % (operation, int(failure)),
             lambda: mro_transition(operation, failure))


def generator_state(operation, clear):
    events = []
    def label():
        return sys.exc_info()[0].__name__ if sys.exc_info()[0] else None
    def iterate():
        try:
            raise ValueError('inner')
        except ValueError:
            events.append(('handler', label()))
            try:
                yield 17
            finally:
                events.append(('finally', label()))
                if clear:
                    sys.exc_clear()
            events.append(('after', label()))
        yield 19
    value = iterate()
    try:
        raise KeyError('outer')
    except KeyError:
        first = value.next()
        events.append(('caller', label()))
        if operation == 0:
            outcome = caught(lambda: value.send(None))
        elif operation == 1:
            outcome = caught(value.close)
        else:
            outcome = caught(lambda: value.throw(TypeError('injected')))
        events.append(('caller-after', label()))
    value.close()
    return first, outcome, events, value.gi_frame is None


for operation in xrange(3):
    for clear in (False, True):
        emit('generator-state/%d/%d' % (operation, int(clear)),
             lambda: generator_state(operation, clear))


def classic_call(mode):
    events = []
    class Classic:
        def __getattr__(self, name):
            events.append(name)
            if mode == 0:
                raise AttributeError('missing')
            if mode == 1:
                raise ValueError('lookup failure')
            if mode == 2:
                raise KeyboardInterrupt('lookup failure')
            if mode == 3:
                return None
            if mode == 4:
                return lambda value=19: value
            self.__call__ = lambda value=23: value
            return self.__call__
    value = Classic()
    return caught(lambda: value()), events, caught(lambda: value(31))


for mode in xrange(6):
    emit('classic-call/%d' % mode, lambda: classic_call(mode))


def noncallable(index):
    class Missing(object):
        pass
    class NoneCall(object):
        __call__ = None
    class Classic:
        pass
    value = (17, None, [], {}, property(), Missing(), NoneCall(), Classic())[index]
    return caught(lambda: value())


for index in xrange(8):
    emit('noncallable/%d' % index, lambda: noncallable(index))


def set_order(operation, left_kind, right_kind, size, changed, equality):
    events, state = [], {'armed': False}
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
    factories = {'set': set, 'subtype': Subset, 'frozen': frozenset}
    # Keep the original keys alive. Each callback adds at most one integer to
    # the initial small table; no live source entry is cleared or resized.
    originals = Key('left'), Key('right')
    left_items, right_items = [originals[0]], [originals[1]]
    if size == 'left':
        left_items.append(2)
    elif size == 'right':
        right_items.append(2)
    left, right = factories[left_kind](left_items), factories[right_kind](right_items)
    state.update(left=left, right=right, armed=True)
    def invoke():
        if operation == 'lt':
            return left < right
        if operation == 'le':
            return left <= right
        if operation == 'gt':
            return left > right
        return left >= right
    try:
        outcome = caught(invoke)
    finally:
        state.clear()
    labels = lambda values: tuple(sorted(value.name if isinstance(value, Key) else str(value) for value in values))
    return outcome, labels(left), labels(right), events


for left_kind, right_kind in (('set', 'set'), ('set', 'subtype'), ('subtype', 'set'), ('frozen', 'set')):
    for operation in ('lt', 'le', 'gt', 'ge'):
        for size in ('none', 'left', 'right'):
            for changed in ('none', 'left', 'right'):
                if left_kind == 'frozen' and changed == 'left':
                    continue
                for equality in ('different', 'equal', 'error'):
                    emit('set-order/%s/%s/%s/%s/%s/%s' % (left_kind, right_kind, operation, size, changed, equality),
                         lambda: set_order(operation, left_kind, right_kind, size, changed, equality))


def slice_hint(consumer, hint, mutate, subtype, classic, bounds=None):
    class Sublist(list):
        pass
    target = (Sublist if subtype else list)([5, 6, 7])
    events = []
    failure = SystemError('error return without exception set') if hint == 'system' else ValueError('hint')
    class Iterator(object):
        def __init__(self):
            self.index = 0
        def __iter__(self):
            events.append('iter')
            return self
        def __length_hint__(self):
            events.append('hint')
            if mutate:
                target.append(9)
            if hint in ('system', 'value'):
                raise failure
            return hint
        def next(self):
            events.append(('next', self.index))
            if self.index == 2:
                raise StopIteration
            value = (17, 19)[self.index]
            self.index += 1
            return value
    class ClassicIterator:
        __init__ = Iterator.__dict__['__init__']
        __iter__ = Iterator.__dict__['__iter__']
        __length_hint__ = Iterator.__dict__['__length_hint__']
        next = Iterator.__dict__['next']
    source = (ClassicIterator if classic else Iterator)()
    def invoke():
        if consumer == 'direct':
            return target.__setitem__(slice(None) if bounds is None else slice(*bounds), source)
        if consumer == 'legacy':
            low, high = (0, 3) if bounds is None else bounds
            return target.__setslice__(low, high, source)
        if consumer == 'direct-step':
            return target.__setitem__(slice(None, None, 2), source)
        if consumer == 'syntax':
            if bounds is None:
                target[:] = source
            else:
                target[bounds[0]:bounds[1]] = source
        elif consumer == 'syntax-step':
            target[::2] = source
        else:
            return sorted(source)
    try:
        outcome = ('value', invoke())
    except BaseException as error:
        outcome = ('error', type(error).__name__, error.args, error is failure)
    before_recovery = list(target)
    target[:] = [31]
    return outcome, before_recovery, events, list(target)


for consumer in ('direct', 'legacy', 'direct-step', 'syntax', 'syntax-step', 'sorted'):
    for hint in (-1, -2, 0, 'system', 'value'):
        for mutate in (False, True):
            for subtype in (False, True):
                for classic in (False, True):
                    emit('slice-hint/%s/%s/%d/%d/%d' % (consumer, hint, int(mutate), int(subtype), int(classic)),
                         lambda: slice_hint(consumer, hint, mutate, subtype, classic))


for consumer in ('direct', 'legacy', 'syntax'):
    for bounds_index, bounds in enumerate(((0, 100), (-2, 100), (0, -1), (100, 100))):
        for hint in (-1, 0, 'system'):
            for mutate in (False, True):
                for subtype in (False, True):
                    for classic in (False, True):
                        emit('slice-bounds/%s/%d/%s/%d/%d/%d' % (consumer, bounds_index, hint, int(mutate), int(subtype), int(classic)),
                             lambda: slice_hint(consumer, hint, mutate, subtype, classic, bounds))
