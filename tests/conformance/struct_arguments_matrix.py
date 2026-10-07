"""Finite supported-double Struct formats, argument parsers and state changes.

The SPEC aliases _struct.error to ValueError, so only that exact exception is
normalized. __sizeof__ measures runtime-specific physical layouts; its type
and positive size are compared without equating the two runtime layouts.
"""

import _struct as struct
import sys


identities = set()


class Bytes(str):
    pass


class Text(unicode):
    def __str__(self):
        raise AssertionError('str override')

    def encode(self, *args):
        raise AssertionError('encode override')


class Child(struct.Struct):
    pass


def describe(value):
    if isinstance(value, struct.Struct):
        return type(value).__name__, type(value.format).__name__, value.format, value.size
    if isinstance(value, bytearray):
        return 'bytearray', str(value)
    return value


def outcome(operation):
    try:
        return 'value', describe(operation())
    except BaseException as error:
        name = 'ValueError' if type(error) is struct.error else type(error).__name__
        return 'error', name, error.args


def emit(identity, operation):
    assert identity not in identities, 'duplicate Struct outcome identity'
    identities.add(identity)
    print identity + '\t' + repr(outcome(operation))
    sys.exc_clear()


def invoke_format(source, values, operation):
    if operation == 'calcsize':
        return struct.calcsize(source)
    if operation == 'construct':
        return struct.Struct(source)
    if operation == 'pack':
        return struct.pack(source, *values)
    sample = struct.pack(source, *values)
    if operation == 'unpack':
        return struct.unpack(source, sample)
    if operation == 'unpack_from':
        return struct.unpack_from(source, 'P' * 8 + sample + 'S' * 8, 8)
    target = bytearray('P' * 8 + 'X' * len(sample) + 'S' * 8)
    answer = struct.pack_into(source, target, 8, *values)
    return answer, str(target)


def common_operation(name, form, source):
    value = (Child if form >= 3 else struct.Struct)(source)
    if form == 0:
        return getattr(struct, name), (source,), value
    if form in (1, 3):
        return getattr(value, name), (), value
    return getattr(struct.Struct, name), (value,), value


def argument_case(name, form, source, count, keyword):
    values = (1.5,) if source == '<d' else ()
    if name in ('pack', 'unpack', 'pack_into', 'unpack_from'):
        operation, prefix, receiver = common_operation(name, form, source)
        sample = struct.pack(source, *values)
        arguments = {'pack': values, 'unpack': (sample,),
                     'pack_into': (bytearray('X' * 24), 0) + values,
                     'unpack_from': (sample,)}[name]
    elif name in ('__init__', '__sizeof__'):
        receiver = (Child if form >= 3 else struct.Struct)(source)
        operation = getattr(receiver, name) if form in (1, 3) else getattr(struct.Struct, name)
        prefix = () if form in (1, 3) else (receiver,)
        arguments = (source,) if name == '__init__' else ()
    elif name == 'construct':
        operation = struct.Struct if form == 1 else Child
        prefix, arguments = (), (source,)
    else:
        operation = getattr(struct, name)
        prefix = ()
        arguments = (source,) if name == 'calcsize' else ()
    arguments = arguments[:count] + (None,) * max(0, count - len(arguments))
    kwargs = {} if keyword == 0 else {('invalid' if keyword == 1 else u'invalid'): 0}
    result = operation(*(prefix + arguments), **kwargs)
    if name == '__sizeof__':
        return type(result).__name__, result > 0
    if name == '__init__':
        return result, describe(receiver)
    return describe(result)


def source_buffer(kind):
    sample = '\0' * 8
    return (sample, bytearray(sample), memoryview(sample), buffer(sample),
            unicode(sample), None, 1, [])[kind]


def unpack_keywords(form, kind, layout, offset):
    operation, prefix, receiver = common_operation('unpack_from', form, '<d')
    sample = source_buffer(kind)
    arguments, kwargs = (
        ((sample,), {'offset': offset}),
        ((), {'buffer': sample, 'offset': offset}),
        ((sample,), {'invalid': offset}),
        ((), {'offset': offset}),
        ((sample,), {'buffer': sample}),
        ((sample, offset), {'offset': offset}),
        ((), {'buffer': sample, 'offset': offset, 'invalid': 0}),
        ((sample,), {'unknown\0tail': offset}),
        ((sample,), {'offset\0tail': offset}),
        ((), {'buffer\0tail': sample}),
    )[layout]
    return operation(*(prefix + arguments), **kwargs)


def cache_case(operation, raises, handled):
    events = []
    outer = ValueError('outer')
    class Format(str):
        def __hash__(self):
            events.append('hash')
            if raises:
                raise KeyError('hash')
            return str.__hash__(self)
    source = Format('<d')
    def invoke():
        first = invoke_format(source, (1.5,), operation)
        second = invoke_format(source, (1.5,), operation)
        return describe(first), describe(second), sys.exc_info()[1] is outer if handled else True
    struct._clearcache()
    try:
        if handled:
            try:
                raise outer
            except ValueError:
                answer = invoke()
                preserved = sys.exc_info()[1] is outer
        else:
            answer = invoke()
            preserved = True
        return answer, events, preserved
    finally:
        struct._clearcache()


def keyword_case(form, name, failure, handled):
    events = []
    outer = LookupError('outer')
    marker = (None, KeyError('keyword'), KeyboardInterrupt('keyword'), SystemExit('keyword'))[failure]
    class Key(str):
        def __eq__(self, other):
            events.append(str(other))
            if marker is not None:
                raise marker
            return str.__eq__(self, other)
        __hash__ = str.__hash__
    operation, prefix, receiver = common_operation('unpack_from', form, '<d')
    arguments = () if name == 'buffer' else ('\0' * 8,)
    kwargs = {Key(name): '\0' * 8 if name == 'buffer' else 0}
    def invoke():
        return outcome(lambda: operation(*(prefix + arguments), **kwargs))
    if handled:
        try:
            raise outer
        except LookupError:
            answer = invoke()
            preserved = sys.exc_info()[1] is outer
    else:
        answer = invoke()
        preserved = True
    return answer, events, preserved, receiver.unpack('\0' * 8)


def reinitialize(source, target):
    value = target('<d')
    first = outcome(lambda: value.__init__(source))
    snapshot = type(value.format).__name__, value.format, value.size
    packed = value.pack(1.5)
    recovered = value.__init__('>2d')
    return first, snapshot, packed, recovered, value.unpack(value.pack(1.5, 2.5))


def allocate(target, count, keyword):
    arguments = (target,) + ('ignored',) * count
    kwargs = {} if keyword == 0 else {('format' if keyword == 1 else 'invalid'): '<d'}
    value = struct.Struct.__new__(*arguments, **kwargs)
    before = type(value).__name__, value.format, value.size, hasattr(value, '__dict__')
    value.__init__('<d')
    return before, value.unpack(value.pack(1.5))


def main():
    # 6 prefixes x 4 repeat counts x 4 spellings x 4 types x 6 operations = 2304.
    for prefix_id, prefix in enumerate(('', '@', '=', '<', '>', '!')):
        for count in xrange(4):
            plain = prefix + str(count) + 'd'
            spellings = (plain, prefix + ' ' + str(count) + 'd \t', plain + '2', plain + '\0tail')
            for spelling_id, spelling in enumerate(spellings):
                for type_id, source in enumerate((spelling, unicode(spelling), Bytes(spelling), Text(spelling))):
                    for operation in ('calcsize', 'construct', 'pack', 'unpack', 'pack_into', 'unpack_from'):
                        identity = 'format/%d/%d/%d/%d/%s' % (prefix_id, count, spelling_id, type_id, operation)
                        emit(identity, lambda: invoke_format(source, (1.5, -2.0, -0.0)[:count], operation))

    # 32 operation forms x 2 formats x 6 argument counts x 3 keyword forms = 1152.
    forms = [(name, form) for name in ('pack', 'unpack', 'pack_into', 'unpack_from') for form in xrange(5)]
    forms += [(name, form) for name in ('__init__', '__sizeof__') for form in xrange(1, 5)]
    forms += [('calcsize', 0), ('_clearcache', 0), ('construct', 1), ('construct', 3)]
    for name, form in forms:
        for source_id, source in enumerate(('<d', '<0d')):
            for count in xrange(6):
                for keyword in xrange(3):
                    identity = 'arguments/%s/%d/%d/%d/%d' % (name, form, source_id, count, keyword)
                    emit(identity, lambda: argument_case(name, form, source, count, keyword))

    # 5 invocation forms x 8 buffers x 10 layouts x 6 offsets = 2400.
    for form in xrange(5):
        for kind in xrange(8):
            for layout in xrange(10):
                for offset_id, offset in enumerate((0, 1, 0L, None, 0.5, object())):
                    identity = 'keywords/%d/%d/%d/%d' % (form, kind, layout, offset_id)
                    emit(identity, lambda: unpack_keywords(form, kind, layout, offset))

    # 5 operations x 2 hash policies x 2 handled-exception states = 20.
    for operation in ('calcsize', 'pack', 'unpack', 'pack_into', 'unpack_from'):
        for raises in (False, True):
            for handled in (False, True):
                emit('cache/%s/%d/%d' % (operation, raises, handled),
                     lambda: cache_case(operation, raises, handled))

    # 5 forms x 2 named keys x 4 equality policies x 2 exception states = 80.
    for form in xrange(5):
        for name in ('buffer', 'offset'):
            for failure in xrange(4):
                for handled in (False, True):
                    emit('key-callback/%d/%s/%d/%d' % (form, name, failure, handled),
                         lambda: keyword_case(form, name, failure, handled))

    # Failed ordinary reinitialization, then recovery: 2 targets x 6 sources = 12.
    for target_id, target in enumerate((struct.Struct, Child)):
        for source_id, source in enumerate(('<1 d', '<dX', Bytes('<1 d'), u'<1 d', None, u'<d\0\xe9')):
            emit('reinit/%d/%d' % (target_id, source_id), lambda: reinitialize(source, target))
        # Allocation-only __new__: 2 targets x 3 extra counts x 3 kwargs = 18.
        for count in xrange(3):
            for keyword in xrange(3):
                emit('allocate/%d/%d/%d' % (target_id, count, keyword), lambda: allocate(target, count, keyword))

    struct._clearcache()
    assert len(identities) == 5986, 'Struct product cardinality changed'


if __name__ == '__main__':
    main()
