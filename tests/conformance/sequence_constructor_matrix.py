"""Finite bytearray hints, count fallback, encoding and entry-size products.

Yielding iterables use positive hints; a zero hint has an empty iterator.
Mutation callbacks only preserve or grow existing bytes. No borrowed export
is retained while resizing. Results, errors, callback order and recovery are
compared exactly, without implementation-specific normalization.
"""

import _codecs
import sys


class Bytes(bytearray):
    pass


class Text(str):
    pass


class Wide(unicode):
    pass


identities = set()


def caught(callback):
    try:
        result = callback()
        if isinstance(result, bytearray):
            return 'value', type(result).__name__, str(result)
        return 'value', type(result).__name__, result
    except BaseException as error:
        return 'error', type(error).__name__, error.args


def emit(identity, callback):
    assert identity not in identities
    identities.add(identity)
    print identity + '\t' + repr(callback())
    sys.exc_clear()


def consume(operation, factory, receiver, source):
    if operation == 'construct':
        return factory(source)
    if operation == 'init':
        return receiver.__init__(source)
    if operation == 'unbound-init':
        return bytearray.__init__(receiver, source)
    if operation == 'extend':
        return receiver.extend(source)
    if operation == 'unbound-extend':
        return bytearray.extend(receiver, source)
    receiver[1:2] = source


def hint_case(operation, factory, initial, mode, payload, growth):
    receiver = factory(initial)
    events = []
    class Cursor(object):
        def __init__(self):
            self.items = iter(payload)
        def __iter__(self):
            events.append('cursor.iter')
            return self
        def next(self):
            events.append('next')
            return next(self.items)
        def __length_hint__(self):
            raise AssertionError('source hint must be used')
    class Source(object):
        def __iter__(self):
            events.append('iter')
            return Cursor()
        def __length_hint__(self):
            events.append('hint')
            if growth:
                receiver.extend('y')
            if mode == 'type':
                raise TypeError('hint marker')
            if mode == 'attribute':
                raise AttributeError('hint marker')
            if mode == 'value':
                raise ValueError('hint marker')
            return {'two': 2, 'four': 4, 'zero': 0, 'minus-one': -1,
                    'minus-two': -2, 'minus-three': -3, 'float': 1.5,
                    'not-implemented': NotImplemented}[mode]
    outcome = caught(lambda: consume(operation, factory, receiver, Source()))
    before_recovery = str(receiver)
    receiver.append(90)
    return outcome, before_recovery, str(receiver), events


operations = ('construct', 'init', 'unbound-init', 'extend', 'unbound-extend')
hint_modes = ('two', 'four', 'minus-one', 'minus-two', 'minus-three', 'float',
              'type', 'attribute', 'value', 'not-implemented')
for operation in operations:
    for factory in (bytearray, Bytes):
        for initial in ('', 'ab'):
            for mode in hint_modes:
                for payload in ((), (65, 66)):
                    for growth in (False, True):
                        emit('hint/%s/%s/%s/%s/%d/%d' % (
                            operation, factory.__name__, initial, mode, len(payload), growth),
                            lambda: hint_case(operation, factory, initial, mode, payload, growth))
            emit('hint/%s/%s/%s/zero/0/0' % (operation, factory.__name__, initial),
                 lambda: hint_case(operation, factory, initial, 'zero', (), False))


def length_case(operation, factory, mode):
    receiver = factory('ab')
    events = []
    class Source(object):
        def __iter__(self):
            events.append('iter')
            return iter([65, 66])
        def __len__(self):
            events.append('len')
            if mode == 'type':
                raise TypeError('len marker')
            if mode == 'attribute':
                raise AttributeError('len marker')
            if mode == 'value':
                raise ValueError('len marker')
            return -1 if mode == 'negative' else 2
        def __length_hint__(self):
            events.append('hint')
            return 2
    outcome = caught(lambda: consume(operation, factory, receiver, Source()))
    return outcome, str(receiver), events


for operation in operations:
    for factory in (bytearray, Bytes):
        for mode in ('positive', 'negative', 'type', 'attribute', 'value'):
            emit('length/%s/%s/%s' % (operation, factory.__name__, mode),
                 lambda: length_case(operation, factory, mode))


def count_case(operation, factory, mode, payload):
    receiver = factory('xyz')
    events = []
    class IndexTypeError(TypeError):
        pass
    class Source(object):
        def __index__(self):
            events.append(('index', str(receiver)))
            if mode in ('type', 'type-subclass'):
                raise (IndexTypeError if mode == 'type-subclass' else TypeError)('count marker')
            if mode in ('value', 'overflow'):
                raise (ValueError if mode == 'value' else OverflowError)('count marker')
            return {'zero': 0, 'two': 2, 'negative': -1, 'float': 1.5,
                    'string': '2', 'not-implemented': NotImplemented}[mode]
        def __iter__(self):
            events.append(('iter', str(receiver)))
            return iter(payload)
    outcome = caught(lambda: consume(operation, factory, receiver, Source()))
    before_recovery = str(receiver)
    receiver.append(90)
    return outcome, before_recovery, str(receiver), events


for operation in ('construct', 'init', 'unbound-init', 'slice'):
    for factory in (bytearray, Bytes):
        for mode in ('zero', 'two', 'negative', 'float', 'string', 'not-implemented',
                     'type', 'type-subclass', 'value', 'overflow'):
            for payload_number, payload in enumerate(((), (65, 66), (65, 256))):
                emit('count/%s/%s/%s/%d' % (
                    operation, factory.__name__, mode, payload_number),
                    lambda: count_case(operation, factory, mode, payload))


codec_events = []
codec_receiver = []


def encode(source, errors='strict'):
    codec_events.append(('encode', type(source).__name__, source, errors))
    if codec_receiver:
        codec_receiver[0].extend('y')
    return 'converted', len(source)


def search(name):
    if name == 'sequence_matrix_encoder':
        return encode, encode, None, None


_codecs.register(search)


def encoding_case(operation, factory, source_factory, text, encoding, errors, form):
    receiver = factory('old')
    source = source_factory(unicode(text, 'latin1')) if issubclass(source_factory, unicode) else source_factory(text)
    del codec_events[:]
    codec_receiver[:] = [receiver]
    def invoke():
        target = factory if operation == 'construct' else receiver.__init__
        if form == 'positional':
            return target(source, encoding, errors)
        if form == 'keywords':
            return target(source=source, encoding=encoding, errors=errors)
        return target(source, encoding=encoding, errors=errors)
    try:
        outcome = caught(invoke)
        before_recovery = str(receiver)
        receiver.append(90)
        return outcome, before_recovery, str(receiver), codec_events[:]
    finally:
        del codec_receiver[:]


for operation in ('construct', 'init'):
    for factory in (bytearray, Bytes):
        for source_factory in (str, Text, unicode, Wide):
            for text in ('', 'ab', '\xff'):
                for encoding in ('ascii', 'utf-8', 'latin1', 'hex',
                                 'sequence_matrix_missing_codec', 'sequence_matrix_encoder'):
                    for errors in ('strict', 'ignore'):
                        for form in ('positional', 'keywords', 'mixed'):
                            emit('encoding/%s/%s/%s/%s/%s/%s/%s' % (
                                operation, factory.__name__, source_factory.__name__,
                                repr(text), encoding, errors, form),
                                lambda: encoding_case(operation, factory, source_factory,
                                                      text, encoding, errors, form))


def mutation_case(operation, factory, initial, index, growth, phase, item_result, unbound):
    receiver = factory(initial)
    events = []
    class Index(object):
        def __int__(self):
            events.append('int')
            if phase == 'index':
                receiver.extend(growth)
            return index
    class Item(object):
        def __index__(self):
            events.append('index')
            if phase == 'value':
                receiver.extend(growth)
            if item_result == 'failure':
                raise RuntimeError('item marker')
            return item_result
    def invoke():
        if operation == 'append':
            return bytearray.append(receiver, Item()) if unbound else receiver.append(Item())
        if unbound:
            return bytearray.insert(receiver, Index(), Item())
        return receiver.insert(Index(), Item())
    outcome = caught(invoke)
    before_recovery = str(receiver)
    receiver.append(90)
    return outcome, before_recovery, str(receiver), events


for operation in ('append', 'insert'):
    for factory in (bytearray, Bytes):
        for initial in ('', 'ab'):
            for index in ((0,) if operation == 'append' else (-5, -1, 0, 1, 5)):
                for growth in ('', 'x', 'xy'):
                    for phase in (('value',) if operation == 'append' else ('index', 'value')):
                        for item_result in (0, 65, 255, 256, 'failure'):
                            for unbound in (False, True):
                                emit('mutation/%s/%s/%s/%d/%s/%s/%s/%d' % (
                                    operation, factory.__name__, initial, index,
                                    growth, phase, item_result, unbound),
                                    lambda: mutation_case(operation, factory, initial, index,
                                                          growth, phase, item_result, unbound))

assert len(identities) == 4158, len(identities)
