"""Finite Python 2.7 operand and slice domains; stdout is compared to CPython."""

def describe(value):
    if isinstance(value, buffer):
        return 'buffer:' + repr(str(value))
    return type(value).__name__ + ':' + repr(value)


def emit(identity, function, *arguments):
    try:
        outcome = describe(function(*arguments))
    except Exception as error:
        outcome = 'error:' + type(error).__name__
    print identity + '\t' + outcome


numbers = [False, True, -3, 0, 2, 2 ** 31, 2 ** 63, -(2 ** 65),
           0L, 5L, -2.5, 0.0, 1.5, 0j, 1 + 2j,
           float('inf'), float('-inf'), float('nan'),
           5e-324, 1.7976931348623157e308]
division_namespace = {}
exec 'from __future__ import division\ndef true_divide(left, right):\n return left / right\n' in division_namespace
true_divide = division_namespace.pop('true_divide')


operations = [('add', lambda a, b: a + b), ('sub', lambda a, b: a - b),
              ('mul', lambda a, b: a * b), ('div', lambda a, b: a / b),
              ('floordiv', lambda a, b: a // b), ('truediv', true_divide),
              ('mod', lambda a, b: a % b), ('divmod', divmod),
              ('and', lambda a, b: a & b), ('or', lambda a, b: a | b),
              ('xor', lambda a, b: a ^ b), ('eq', lambda a, b: a == b),
              ('ne', lambda a, b: a != b), ('lt', lambda a, b: a < b),
              ('le', lambda a, b: a <= b), ('gt', lambda a, b: a > b),
              ('ge', lambda a, b: a >= b)]
for name, operation in operations:
    for left_index, left in enumerate(numbers):
        for right_index, right in enumerate(numbers):
            emit('numeric/%s/%d/%d' % (name, left_index, right_index),
                 operation, left, right)
for name, operation in [('pow', lambda a, b: a ** b), ('lshift', lambda a, b: a << b),
                        ('rshift', lambda a, b: a >> b)]:
    for left_index, left in enumerate(numbers):
        for right in [-3, -1, 0, 1, 2, 5]:
            emit('bounded/%s/%d/%d' % (name, left_index, right),
                 operation, left, right)
for name, operation in [('pos', lambda a: +a), ('neg', lambda a: -a),
                        ('invert', lambda a: ~a), ('abs', abs),
                        ('bool', bool), ('int', int), ('long', long),
                        ('float', float), ('complex', complex),
                        ('hash', hash)]:
    for index, value in enumerate(numbers):
        emit('unary/%s/%d' % (name, index), operation, value)


def sliced(value, start, stop, step):
    return value[slice(start, stop, step)]


sequences = [[], [0, 1, 2, 3, 4], (), (0, 1, 2, 3, 4),
             '', 'abcde', u'', u'a\u00e9\u20ac\U0001f600z',
             bytearray('abcde'), buffer('abcde')]
bounds = [None, -20, -3, -1, 0, 1, 3, 20]
steps = [None, -3, -1, 0, 1, 3]
for sequence_index, sequence in enumerate(sequences):
    for start_index, start in enumerate(bounds):
        for stop_index, stop in enumerate(bounds):
            for step_index, step in enumerate(steps):
                emit('slice/%d/%d/%d/%d' % (sequence_index, start_index,
                                          stop_index, step_index),
                     sliced, sequence, start, stop, step)


def replace_slice(factory, start, stop, step, replacement):
    value = factory([0, 1, 2, 3, 4])
    value[slice(start, stop, step)] = replacement
    return value


def delete_slice(factory, start, stop, step):
    value = factory([0, 1, 2, 3, 4])
    del value[slice(start, stop, step)]
    return value


for factory in [list, bytearray]:
    for start_index, start in enumerate(bounds):
        for stop_index, stop in enumerate(bounds):
            for step_index, step in enumerate(steps):
                identity = '%s/%d/%d/%d' % (factory.__name__, start_index,
                                           stop_index, step_index)
                emit('delete/' + identity, delete_slice, factory, start, stop, step)
                for replacement in [[], [8], [7, 8], [7, 8, 9, 10, 11]]:
                    emit('replace/' + identity + '/' + str(len(replacement)),
                         replace_slice, factory, start, stop, step, replacement)
