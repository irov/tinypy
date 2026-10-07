"""Bounded live buffer storage, nested constraints and first-hash products.

Repr replaces only the two expected in-process owner/self addresses with role
tokens; all remaining content is exact. Hashes are compared by their equality
to first/current byte hashes, because interpreter hash seeds may differ.
No exported memoryview is kept across a backing bytearray resize.
"""

import sys


class Text(str):
    pass


class Wide(unicode):
    pass


class Bytes(bytearray):
    pass


identities = set()


def emit(identity, callback):
    assert identity not in identities
    identities.add(identity)
    print identity + '\t' + repr(callback())
    sys.exc_clear()


def represented(view, root):
    return repr(view).replace('for ' + hex(id(root)) + ', ', 'for <owner>, ').replace(
        'at ' + hex(id(view)) + '>', 'at <self>>')


def normalized(value):
    if isinstance(value, bytearray):
        return type(value).__name__, str(value)
    return type(value).__name__, value


def caught(callback):
    try:
        return 'value', normalized(callback())
    except BaseException as failure:
        return 'error', type(failure).__name__, failure.args


def state(view, root):
    return len(view), str(view), represented(view, root), view.__str__(), view.__len__()


factories = (str, Text, unicode, Wide, bytearray, Bytes)
texts = ('', 'ab', 'a\x00z')
offsets = (0, 1, 5)
sizes = (-1, 0, 1, 10)
operations = (
    ('str', lambda view: str(view)),
    ('len', lambda view: len(view)),
    ('first', lambda view: view[0]),
    ('last', lambda view: view[-1]),
    ('slice', lambda view: view[::-1]),
    ('direct', lambda view: view.__getitem__(slice(1, 5))),
    ('legacy', lambda view: view.__getslice__(1, 5)),
    ('hash', lambda view: (hash(view) == hash(str(view)), view.__hash__() == hash(view))),
)


def base_case(factory, text, offset, size, operation):
    root = factory(text)
    view = buffer(root, offset, size)
    return caught(lambda: operation(view)), state(view, root)


for factory in factories:
    for content, text in enumerate(texts):
        for offset in offsets:
            for size in sizes:
                for name, operation in operations:
                    emit('base/%s/%d/%d/%d/%s' % (factory.__name__, content, offset, size, name),
                         lambda: base_case(factory, text, offset, size, operation))


def nested_case(factory, text, offset, size, inner_offset, inner_size):
    root = factory(text)
    parent = buffer(root, offset, size)
    child = buffer(parent, inner_offset, inner_size)
    before = state(child, root)
    del parent
    if isinstance(root, bytearray):
        root[:] = 'abcdefghijklmnop'
    return before, state(child, root), caught(lambda: child[0]), caught(lambda: child[-1])


for factory in factories:
    for content, text in enumerate(texts):
        for offset in offsets:
            for size in sizes:
                for inner_offset in offsets:
                    for inner_size in sizes:
                        emit('nested/%s/%d/%d/%d/%d/%d' % (
                            factory.__name__, content, offset, size, inner_offset, inner_size),
                            lambda: nested_case(factory, text, offset, size, inner_offset, inner_size))


mutations = ('', 'Z', 'XY', 'abcdefghijklmnop')


def mutation_case(factory, text, offset, size, replacement):
    root = factory(text)
    view = buffer(root, offset, size)
    before = state(view, root)
    root[:] = replacement
    after = state(view, root), caught(lambda: view[0]), caught(lambda: view[-1])
    root[:] = '123456789'
    return before, after, state(view, root)


for factory in (bytearray, Bytes):
    for content, text in enumerate(texts):
        for offset in offsets:
            for size in sizes:
                for mutation, replacement in enumerate(mutations):
                    emit('mutation/%s/%d/%d/%d/%d' % (
                        factory.__name__, content, offset, size, mutation),
                        lambda: mutation_case(factory, text, offset, size, replacement))


def hash_case(factory, text, offset, size, replacement, direct):
    root = factory(text)
    view = buffer(root, offset, size)
    first = view.__hash__() if direct else hash(view)
    mapping = {view: 'kept'}
    root[:] = replacement
    child = buffer(view)
    return (hash(view) == first, view.__hash__() == first,
            hash(view) == hash(str(view)), hash(child) == hash(str(child)),
            mapping[view], state(view, root), state(child, root))


for factory in (bytearray, Bytes):
    for content, text in enumerate(texts):
        for offset in offsets:
            for size in sizes:
                for mutation, replacement in enumerate(('', 'XYZ', 'abcdefghijklmnop')):
                    for direct in (False, True):
                        emit('hash/%s/%d/%d/%d/%d/%d' % (
                            factory.__name__, content, offset, size, mutation, direct),
                            lambda: hash_case(factory, text, offset, size, replacement, direct))


def callback_case(phase, action, result):
    root, events = bytearray('ab'), []
    failure = LookupError('bound callback')
    class Bound(object):
        def __int__(self):
            events.append('convert')
            if action == 'grow':
                root.extend('cdefghi')
            elif action == 'replace':
                root[:] = 'XYZ'
            if result == 'raise':
                raise failure
            return {'integer': 1, 'long': 1L, 'bad': 1.5}[result]
    try:
        view = buffer(root, Bound(), 5) if phase == 'offset' else buffer(root, 1, Bound())
        outcome = 'value', state(view, root)
    except BaseException as error:
        outcome = 'error', type(error).__name__, error.args, error is failure
    return outcome, events, str(root), str(buffer(root, 0, 1))


for phase in ('offset', 'size'):
    for action in ('none', 'grow', 'replace'):
        for result in ('integer', 'long', 'bad', 'raise'):
            emit('callback/%s/%s/%s' % (phase, action, result),
                 lambda: callback_case(phase, action, result))


character_operations = (
    ('raw', lambda view: str(view)),
    ('unicode', lambda view: unicode(view)),
    ('bytearray', lambda view: bytearray(view)),
    ('find', lambda view: u'abc'.find(view)),
    ('bytesfind', lambda view: 'abc'.find(view)),
    ('int', lambda view: int(view)),
    ('center', lambda view: u'ab'.center(4, view)),
    ('long', lambda view: long(view)),
    ('float', lambda view: float(view)),
    ('intbase', lambda view: int(view, 10)),
    ('longbase', lambda view: long(view, 10)),
)


def character_case(factory, text, offset, size, nesting, operation):
    root = factory(text)
    view = buffer(root, offset, size)
    if nesting == 1:
        view = buffer(view)
    elif nesting == 2:
        view = buffer(view, 1, 3)
    return caught(lambda: operation(view)), represented(view, root)


for factory in (unicode, Wide):
    for content, text in enumerate((u'', u'abc', u'12', u'\xe9', u'1.5', u'1\x002')):
        for offset, size in ((0, -1), (1, 10), (0, 1), (5, 10)):
            for nesting in (0, 1, 2):
                for name, operation in character_operations:
                    emit('character/%s/%d/%d/%d/%d/%s' % (
                        factory.__name__, content, offset, size, nesting, name),
                        lambda: character_case(factory, text, offset, size, nesting, operation))


def lifetime_case(kind, nesting, offset):
    events = []
    class ByteOwner(bytearray):
        def __del__(self):
            events.append('released')
    class TextOwner(str):
        def __del__(self):
            events.append('released')
    owner = ByteOwner('abcdefgh') if kind == 'bytes' else TextOwner('abcdefgh')
    first = buffer(owner, offset, 10)
    child = first if nesting == 0 else buffer(first, 1, 3)
    if nesting == 2:
        child = buffer(child, 1)
    before = state(child, owner)
    del first, owner
    pinned = list(events), str(child)
    del child
    return before, pinned, events


for kind in ('bytes', 'text'):
    for nesting in (0, 1, 2):
        for offset in offsets:
            emit('lifetime/%s/%d/%d' % (kind, nesting, offset),
                 lambda: lifetime_case(kind, nesting, offset))


prefixes = ('', 'abc', '1', '1x', '+', ' ', 'nan', '1e', 'nanx', 'infinityx', '.x', '+.', 'nan()')
for factory in (str, bytearray, buffer):
    for content, prefix in enumerate(prefixes):
        for nul in (False, True):
            text = prefix + ('\x00' if nul else '')
            for constructor in (int, long, float):
                emit('prefix/%s/%d/%d/%s' % (factory.__name__, content, nul, constructor.__name__),
                     lambda: caught(lambda: constructor(factory(text))))
            for constructor in (int, long):
                for base in (0, 2, 10, 36):
                    emit('baseprefix/%s/%d/%d/%s/%d' % (factory.__name__, content, nul, constructor.__name__, base),
                         lambda: caught(lambda: constructor(factory(text), base)))


# The host coordinator also verifies this exact positive count and unique IDs.
assert len(identities) == 7524
