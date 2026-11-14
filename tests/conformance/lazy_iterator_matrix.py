"""Lazy construction, partial consumption and recovery against Python 2.7."""

import sys

try:
    xzip, xmap, xfilter
except NameError:
    from itertools import izip as xzip, ifilter as xfilter, imap as _recursion_xmap
    import os
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'upstream'))
    from local.lazy_map_reference import xmap
else:
    _recursion_xmap = xmap

try:
    xchain, xslice
except NameError:
    from itertools import chain as xchain, islice as xslice

events = []


class Source(object):
    def __init__(self, label, mode):
        self.label = label
        self.mode = mode
        self.index = 0
        self.values = {'empty': [], 'one': [0], 'pair': [1, 2],
                       'zeros': [0, 1, 0], 'next-error': [0, 1, 2],
                       'iter-error': [1]}[mode]
    def __iter__(self):
        events.append((self.label, 'iter'))
        if self.mode == 'iter-error':
            raise ValueError('iter')
        return self
    def __len__(self):
        events.append((self.label, 'len'))
        raise AssertionError('unexpected eager sizing')
    def __length_hint__(self):
        events.append((self.label, 'hint'))
        raise AssertionError('unexpected eager sizing')
    def next(self):
        index = self.index
        events.append((self.label, 'next', index))
        if index >= len(self.values):
            raise StopIteration
        self.index += 1
        if self.mode == 'next-error' and index == 1:
            raise LookupError('next')
        return self.values[index]


def consume(iterator, count):
    result = []
    for unused in xrange(count):
        try:
            result.append(('value', next(iterator)))
        except BaseException as error:
            result.append(('error', type(error).__name__))
    return result


def binary(operation, left_mode, right_mode, count):
    left = Source('left', left_mode)
    right = Source('right', right_mode)
    def add(a, b):
        events.append(('call', a, b))
        return a + b
    if operation == 'zip':
        iterator = xzip(left, right)
    elif operation == 'map-none':
        iterator = xmap(None, left, right)
    else:
        iterator = xmap(add, left, right)
    return consume(iterator, count)


def unary(operation, callback, source_mode, count):
    def function(value):
        events.append(('call', value))
        if callback == 'error' and value == 0:
            raise ValueError('callback')
        if callback == 'stop' and value == 0:
            raise StopIteration('callback')
        return value % 2 if callback == 'odd' else value
    selected = None if callback == 'none' else (42 if callback == 'invalid' else function)
    factory = xmap if operation == 'map' else xfilter
    iterator = factory(selected, Source('input', source_mode))
    return consume(iterator, count)


def emit(identity, function, *args):
    events[:] = []
    try:
        outcome = repr(function(*args))
    except BaseException as error:
        outcome = 'error:' + type(error).__name__
    print identity + '\t' + outcome + '\t' + repr(events)


def map_values(callback, sources):
    function = None if callback == 'none' else lambda *items: items
    actual = list(xmap(function, *sources))
    expected = map(function, *sources)
    assert actual == expected, (actual, expected)
    return actual


variants = ((), (None,), (0,), (1, 2), ('x', None, 3))
for callback in ('none', 'row'):
    for left_index, left in enumerate(variants):
        emit('map-values/%s/%d' % (callback, left_index), map_values, callback, (left,))
        for right_index, right in enumerate(variants):
            emit('map-values/%s/%d/%d' % (callback, left_index, right_index),
                 map_values, callback, (left, right))
            for third_index, third in enumerate(variants):
                emit('map-values/%s/%d/%d/%d' % (callback, left_index, right_index, third_index),
                     map_values, callback, (left, right, third))


modes = ('empty', 'one', 'pair', 'zeros', 'next-error', 'iter-error')
for operation in ('zip', 'map-none', 'map-add'):
    for left in modes:
        for right in modes:
            for count in (0, 1, 2, 4):
                emit('binary/%s/%s/%s/%d' % (operation, left, right, count),
                     binary, operation, left, right, count)

for operation in ('map', 'filter'):
    for callback in ('none', 'identity', 'odd', 'error', 'stop', 'invalid'):
        for source in modes:
            for count in (0, 1, 2, 4):
                emit('unary/%s/%s/%s/%d' % (operation, callback, source, count),
                     unary, operation, callback, source, count)


def chained(left_mode, right_mode, outer_mode, count):
    inputs = [Source('left', left_mode), Source('right', right_mode)]
    if outer_mode == 'direct':
        iterator = xchain(*inputs)
    else:
        outer = Source('outer', outer_mode)
        outer.values = inputs
        iterator = xchain.from_iterable(outer)
    return consume(iterator, count)


for left in modes:
    for right in modes:
        for outer in ('direct', 'pair', 'next-error', 'iter-error'):
            for count in (0, 1, 2, 4):
                emit('chain/%s/%s/%s/%d' % (left, right, outer, count),
                     chained, left, right, outer, count)


def sliced(source_mode, bounds, count):
    return consume(xslice(Source('input', source_mode), *bounds), count)


starts = (None, 0, 1, 3, 6, -1, 1.5, 2 ** 63 - 1)
stops = (None, 0, 2, 5, -1, -2, 2 ** 100, 4.5)
steps = (None, 1, 2, 0, -1, 2 ** 63 - 1)
for source in modes:
    for start_index, start in enumerate(starts):
        for stop_index, stop in enumerate(stops):
            for step_index, step in enumerate(steps):
                for count in (0, 1, 2, 4):
                    emit('slice/%s/%d/%d/%d/%d' % (source, start_index, stop_index, step_index, count),
                         sliced, source, (start, stop, step), count)
    for stop_index, stop in enumerate(stops):
        for count in (0, 1, 2, 4):
            emit('slice-stop/%s/%d/%d' % (source, stop_index, count),
                 sliced, source, (stop,), count)


class Converted(object):
    def __init__(self, result):
        self.result = result
    def __int__(self):
        events.append(('int', self.result))
        if self.result == 'error':
            raise LookupError('conversion')
        return self.result


class Indexed(object):
    def __index__(self):
        events.append('index')
        return 1


class Float(float):
    def __int__(self):
        events.append('float.int')
        return 2


class Integer(int):
    def __int__(self):
        events.append('integer.int')
        return 2


class Wide(long):
    def __int__(self):
        events.append('long.int')
        return 2


for source in modes:
    for index, value in enumerate((Converted(2), Converted(2L), Converted(-1),
                                   Converted(1.5), Converted('error'), Indexed(),
                                   Float(1.5), float('nan'), Integer(1), Wide(1),
                                   Wide(2 ** 100), True, False)):
        for position in xrange(3):
            for count in (0, 3):
                bounds = [0, None, 1]
                bounds[position] = value
                emit('slice-conversion/%s/%d/%d/%d' % (source, index, position, count),
                     sliced, source, tuple(bounds), count)


def recursive_work(operation, callback):
    if operation == 'map':
        return map(callback, [1, 2])
    if operation == 'xmap':
        # One ordinary callback and one input have identical map/imap rows.
        # Use CPython's native iterator for native callback recursion limits;
        # the Python-only row oracle adds its own constructor/next frames.
        return list(_recursion_xmap(callback, [1, 2]))
    if operation == 'filter':
        return filter(callback, [1, 2])
    if operation == 'xfilter':
        return list(xfilter(callback, [1, 2]))
    if operation == 'min':
        return min([1, 2], key=callback)
    return sorted([2, 1], key=callback)


def recursion_callback(value):
    return value + 1


def recursive_result(operation, headroom):
    callback = recursion_callback
    frame = sys._getframe()
    depth = 0
    while frame is not None:
        depth += 1
        frame = frame.f_back
    previous = sys.getrecursionlimit()
    try:
        sys.setrecursionlimit(depth + headroom)
        result = recursive_work(operation, callback)
    except BaseException as error:
        result = type(error).__name__
    finally:
        sys.setrecursionlimit(previous)
    return result


for operation in ('map', 'xmap', 'filter', 'xfilter', 'min', 'sorted'):
    for headroom in xrange(1, 11):
        try:
            result = recursive_result(operation, headroom)
        except BaseException as error:
            result = type(error).__name__
        print 'recursion/%s/%d\t%s\t[]' % (operation, headroom, repr(result))
