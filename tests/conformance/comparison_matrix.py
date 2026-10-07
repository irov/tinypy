"""Finite Python 2 comparison domains, protocol traces and return conversions.

Fallback address ordering is excluded: custom classes always implement __cmp__
and exact builtin inputs use deterministic numeric, text or type-name ordering.
"""


operations = [('eq', lambda a, b: a == b), ('ne', lambda a, b: a != b),
              ('lt', lambda a, b: a < b), ('le', lambda a, b: a <= b),
              ('gt', lambda a, b: a > b), ('ge', lambda a, b: a >= b),
              ('cmp', cmp)]


def emit(identity, function, *arguments):
    try:
        value = function(*arguments)
        outcome = type(value).__name__ + ':' + repr(value)
    except Exception as error:
        outcome = 'error:' + type(error).__name__
    print identity + '\t' + outcome + '\t' + repr(events)


events = []
builtins = [None, False, 0, 1L, 1.25, 0j, 'a', u'a', [], (), {},
            set(), frozenset(), slice(0, 1), buffer('a'), bytearray('a')]
for name, operation in operations:
    for left_index, left in enumerate(builtins):
        for right_index, right in enumerate(builtins):
            emit('builtin/%s/%d/%d' % (name, left_index, right_index),
                 operation, left, right)


def comparison_method(label, name, result):
    def method(self, other):
        events.append(label + '.' + name)
        return result
    return method


def compared_class(classic, label, result):
    if classic:
        class Compared:
            pass
    else:
        class Compared(object):
            pass
    for name in ('eq', 'ne', 'lt', 'le', 'gt', 'ge'):
        setattr(Compared, '__' + name + '__',
                comparison_method(label, name, result))
    Compared.__cmp__ = comparison_method(label, 'cmp', -1)
    return Compared


for left_style in (False, True):
    for right_style in (False, True):
        for left_result_index, left_result in enumerate((False, True, NotImplemented)):
            for right_result_index, right_result in enumerate((False, True, NotImplemented)):
                Left = compared_class(left_style, 'left', left_result)
                Right = compared_class(right_style, 'right', right_result)
                for name, operation in operations:
                    events[:] = []
                    emit('different/%d/%d/%d/%d/%s' %
                         (left_style, right_style, left_result_index, right_result_index, name),
                         operation, Left(), Right())

for classic in (False, True):
    for result_index, result in enumerate((False, True, NotImplemented)):
        Base = compared_class(classic, 'base', result)
        class Sub(Base):
            pass
        for name in ('eq', 'ne', 'lt', 'le', 'gt', 'ge'):
            setattr(Sub, '__' + name + '__',
                    comparison_method('sub', name, result))
        for relation, left_class, right_class in [('same', Base, Base),
                                                 ('sub-right', Base, Sub),
                                                 ('sub-left', Sub, Base)]:
            for name, operation in operations:
                events[:] = []
                emit('%s/%d/%d/%s' % (relation, classic, result_index, name),
                     operation, left_class(), right_class())


class Integer(int):
    def __int__(self):
        events.append('integer.int')
        return 31

    def __hash__(self):
        events.append('integer.hash')
        return 37


class Wide(long):
    def __int__(self):
        events.append('long.int')
        return 41

    def __hash__(self):
        events.append('long.hash')
        return 43


class Number(object):
    def __int__(self):
        events.append('number.int')
        return 7


class Failed(object):
    def __int__(self):
        events.append('failed.int')
        raise LookupError('conversion failed')


class Index(object):
    def __index__(self):
        events.append('index')
        return 7


class Wrong(object):
    def __int__(self):
        events.append('wrong.int')
        return '7'


returns = [False, True, -1, 0, 2, 2L, 1L << 100, 1.75, -1.75,
           '2', None, Integer(5), Wide(5), Number(), Failed(), Index(), Wrong()]
for classic in (False, True):
    Compared = compared_class(classic, 'compared', NotImplemented)
    for result_index, result in enumerate(returns):
        for method, function in [('hash', hash), ('len', len), ('len', bool),
                                 ('nonzero', bool), ('cmp', cmp)]:
            events[:] = []
            if method == 'nonzero':
                if hasattr(Compared, '__len__'):
                    delattr(Compared, '__len__')
            elif hasattr(Compared, '__nonzero__'):
                delattr(Compared, '__nonzero__')
            setattr(Compared, '__' + method + '__',
                    comparison_method('compared', method, result))
            arguments = [Compared(), Compared()] if method == 'cmp' else [Compared()]
            emit('return/%d/%d/%s/%s' %
                 (classic, result_index, method, function.__name__),
                 function, *arguments)
