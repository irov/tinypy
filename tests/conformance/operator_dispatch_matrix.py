"""Finite Python 2 binary, reflected, inplace, coercion and power products."""


OPERATORS = (('add', '+'), ('sub', '-'), ('mul', '*'), ('div', '/'),
             ('floordiv', '//'), ('truediv', '/'), ('mod', '%'),
             ('divmod', None), ('pow', '**'), ('lshift', '<<'),
             ('rshift', '>>'), ('and', '&'), ('xor', '^'), ('or', '|'))
BASES = (object, int, long, float, complex, set, frozenset)
POLICIES = ('missing', 'value', 'not-implemented', 'error')
COERCIONS = ('missing', 'none', 'not-implemented', 'self', 'numbers',
             'classic', 'new-style', 'bad', 'error')
FUNCTIONS = {}


for name, symbol in OPERATORS:
    for inplace in (False, True):
        if name == 'divmod' and inplace:
            continue
        prefix = 'from __future__ import division\n' if name == 'truediv' else ''
        if name == 'divmod':
            body = 'return divmod(left,right)\n'
        elif inplace:
            body = 'left%s=right\nreturn left\n' % symbol
        else:
            body = 'return left%sright\n' % symbol
        source = prefix + 'def operation(left,right):\n' + ''.join(
            ' ' + line for line in body.splitlines(True))
        namespace = {}
        exec compile(source, 'operator-product', 'exec', 0, 1) in namespace
        FUNCTIONS[name, inplace] = namespace.pop('operation')


def emit(identity, callback):
    events = []
    try:
        value = callback(events)
        if isinstance(value, (set, frozenset)):
            result = (type(value).__name__, tuple(sorted(value)))
        else:
            result = (type(value).__name__, repr(value))
        answer = ('value', result, events)
    except BaseException as error:
        answer = ('error', type(error).__name__, str(error), events)
    print identity + '\t' + repr(answer)


def hook(events, name, policy):
    def invoke(self, other):
        events.append(name)
        if policy == 'not-implemented':
            return NotImplemented
        if policy == 'error':
            raise LookupError(name)
        return 17
    return invoke


def instance(kind, label):
    if issubclass(kind, (set, frozenset)):
        value = kind([3])
    elif issubclass(kind, (int, long, float, complex)):
        value = kind(3)
    else:
        value = kind()
    value.label = label
    return value


def binary(name, base, left_policy, right_policy, relation, events):
    left_methods = {}
    right_methods = {}
    if left_policy != 'missing':
        left_methods['__' + name + '__'] = hook(events, 'left', left_policy)
    if right_policy != 'missing':
        right_methods['__r' + name + '__'] = hook(events, 'right', right_policy)
    if relation == 'same':
        left_methods.update(right_methods)
    left_kind = type('Left', (base,), left_methods)
    if relation == 'same':
        right_kind = left_kind
    else:
        right_kind = type('Right', (left_kind if relation == 'child' else base,), right_methods)
    left, right = instance(left_kind, 'L'), instance(right_kind, 'R')
    return FUNCTIONS[name, False](left, right)


for name, symbol in OPERATORS:
    for base in BASES:
        for left_policy in POLICIES:
            for right_policy in POLICIES:
                for relation in ('same', 'child', 'unrelated'):
                    emit('binary/%s/%s/%s/%s/%s' % (
                        name, base.__name__, left_policy, right_policy, relation),
                        lambda events: binary(name, base, left_policy, right_policy, relation, events))


def inplace(name, base, policy, fallback, relation, events):
    methods = {'__i' + name + '__': hook(events, 'inplace', policy)}
    if fallback != 'missing':
        methods['__' + name + '__'] = hook(events, 'left', fallback)
    left_kind = type('Left', (base,), methods)
    right_kind = left_kind if relation == 'same' else type('Right', (base,), {})
    return FUNCTIONS[name, True](instance(left_kind, 'L'), instance(right_kind, 'R'))


for name, symbol in OPERATORS:
    if name == 'divmod':
        continue
    for base in BASES:
        for policy in ('value', 'not-implemented', 'error'):
            for fallback in ('missing', 'value', 'not-implemented'):
                for relation in ('same', 'unrelated'):
                    emit('inplace/%s/%s/%s/%s/%s' % (
                        name, base.__name__, policy, fallback, relation),
                        lambda events: inplace(name, base, policy, fallback, relation, events))


def coerce_hook(name, mode, events):
    def convert(self, other):
        events.append((name, getattr(other, 'label', 'number')))
        if mode == 'none':
            return None
        if mode == 'not-implemented':
            return NotImplemented
        if mode == 'self':
            return self, 5
        if mode == 'numbers':
            return 3, 2
        if mode == 'bad':
            return [self, other]
        if mode == 'error':
            raise LookupError(name)
        if mode == 'classic':
            class Replacement:
                pass
        else:
            class Replacement(object):
                pass
        for operation, symbol in OPERATORS:
            setattr(Replacement, '__' + operation + '__', hook(events, 'replacement', 'value'))
            setattr(Replacement, '__r' + operation + '__', hook(events, 'replacement-r', 'value'))
            if operation != 'divmod':
                setattr(Replacement, '__i' + operation + '__', hook(events, 'replacement-i', 'value'))
        return Replacement(), 5
    return convert


def classic(name, left_coerce, left_policy, right_coerce, right_policy, events):
    class Left:
        label = 'L'
    class Right:
        label = 'R'
    if left_coerce != 'missing':
        Left.__coerce__ = coerce_hook('left-coerce', left_coerce, events)
    if left_policy != 'missing':
        setattr(Left, '__' + name + '__', hook(events, 'left', left_policy))
    if right_coerce != 'missing':
        Right.__coerce__ = coerce_hook('right-coerce', right_coerce, events)
    if right_policy != 'missing':
        setattr(Right, '__r' + name + '__', hook(events, 'right', right_policy))
    return FUNCTIONS[name, False](Left(), Right())


for name, symbol in OPERATORS:
    for left_coerce in COERCIONS:
        for left_policy in ('missing', 'value', 'not-implemented'):
            for right_coerce in ('none', 'numbers', 'error'):
                for right_policy in ('value', 'not-implemented'):
                    emit('classic/%s/%s/%s/%s/%s' % (
                        name, left_coerce, left_policy, right_coerce, right_policy),
                        lambda events: classic(name, left_coerce, left_policy, right_coerce, right_policy, events))


def classic_inplace(name, mode, policy, fallback, events):
    class Left:
        label = 'L'
    if mode != 'missing':
        Left.__coerce__ = coerce_hook('coerce', mode, events)
    setattr(Left, '__i' + name + '__', hook(events, 'inplace', policy))
    setattr(Left, '__' + name + '__', hook(events, 'left', fallback))
    return FUNCTIONS[name, True](Left(), 2)


for name, symbol in OPERATORS:
    if name == 'divmod':
        continue
    for mode in COERCIONS:
        for policy in ('value', 'not-implemented', 'error'):
            for fallback in ('value', 'not-implemented'):
                emit('classic-inplace/%s/%s/%s/%s' % (name, mode, policy, fallback),
                     lambda events: classic_inplace(name, mode, policy, fallback, events))


def power_hook(events, label, policy):
    def invoke(self, *arguments):
        events.append((label, len(arguments)))
        if policy == 'not-implemented':
            return NotImplemented
        if policy == 'error':
            raise LookupError(label)
        return 17
    return invoke


def power_argument(base, label, policy, number, events):
    methods = {}
    if policy != 'missing':
        methods['__pow__'] = power_hook(events, label, policy)
    kind = type(label, (base,), methods)
    if base is object:
        return kind()
    return kind(number)


def ternary(base, policy, position, modulus, events):
    values = [2, 3, modulus]
    number = values[position]
    values[position] = power_argument(base, 'Operand', policy, number, events)
    return pow(*values)


for base in (object, int, long, float, complex):
    for policy in POLICIES:
        for position in (0, 1, 2):
            for modulus in (5, -5, 0):
                emit('ternary-position/%s/%s/%s/%s' % (
                    base.__name__, policy, position, modulus),
                    lambda events: ternary(base, policy, position, modulus, events))
        for modulus in (None, 5):
            emit('ternary-base/%s/%s/%s' % (base.__name__, policy, modulus),
                 lambda events: ternary(base, policy, 0, modulus, events))


def ternary_slots(base, left_policy, right_policy, third_policy, events):
    values = [power_argument(base, 'Base', left_policy, 2, events),
              power_argument(base, 'Exponent', right_policy, 3, events),
              power_argument(base, 'Modulus', third_policy, 5, events)]
    return pow(*values)


for base in (object, int, long, float, complex):
    for left_policy in POLICIES:
        for right_policy in POLICIES:
            for third_policy in POLICIES:
                emit('ternary-slots/%s/%s/%s/%s' % (
                    base.__name__, left_policy, right_policy, third_policy),
                    lambda events: ternary_slots(base, left_policy, right_policy, third_policy, events))


def classic_power(coercion, policy, modulus, events):
    class Classic:
        pass
    if coercion != 'missing':
        Classic.__coerce__ = coerce_hook('coerce', coercion, events)
    Classic.__pow__ = power_hook(events, 'classic', policy)
    return pow(Classic(), 3, modulus)


for coercion in COERCIONS:
    for policy in ('value', 'not-implemented', 'error'):
        for modulus in (None, 5):
            emit('classic-power/%s/%s/%s' % (coercion, policy, modulus),
                 lambda events: classic_power(coercion, policy, modulus, events))


for base in (2, 2L):
    for exponent in (-3, -3L, 0, 0L, 3, 3L):
        for modulus in (None, 0, 0L, 5, 5L, -5, 1.5, 1j, 'x', object()):
            identity = 'power-values/%s/%s/%s/%s/%s' % (
                type(base).__name__, type(exponent).__name__, exponent,
                type(modulus).__name__, repr(modulus) if type(modulus) is not object else 'object')
            emit(identity, lambda events: pow(base, exponent, modulus))


def descriptor_binary(name, comparison, left_get, right_get, left_policy, events):
    class Reflected(object):
        def __init__(self, label, lookup):
            self.label, self.lookup = label, lookup
        def __get__(self, receiver, owner):
            events.append(('get', self.label, receiver is None))
            if receiver is None:
                if self.lookup == 'attribute':
                    raise AttributeError(self.label)
                if self.lookup == 'error':
                    raise LookupError(self.label)
                return self
            return hook(events, 'right', 'value').__get__(receiver, owner)
        def __ne__(self, other):
            events.append(('ne', self.label, other.label))
            if comparison == 'error':
                raise LookupError('different')
            if comparison == 'not-implemented':
                return NotImplemented
            return comparison == 'different'
    left_methods = {'__' + name + '__': hook(events, 'left', left_policy),
                    '__r' + name + '__': Reflected('base', left_get)}
    left_kind = type('Left', (object,), left_methods)
    right_kind = type('Right', (left_kind,), {'__r' + name + '__': Reflected('child', right_get)})
    return FUNCTIONS[name, False](left_kind(), right_kind())


for name, symbol in OPERATORS:
    for comparison in ('equal', 'different', 'not-implemented', 'error'):
        for left_get in ('value', 'attribute', 'error'):
            for right_get in ('value', 'attribute', 'error'):
                for left_policy in ('value', 'not-implemented', 'error'):
                    emit('reflected-descriptor/%s/%s/%s/%s/%s' % (
                        name, comparison, left_get, right_get, left_policy),
                        lambda events: descriptor_binary(name, comparison, left_get, right_get, left_policy, events))
