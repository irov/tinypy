"""Finite Python 2 text parser, padding and character-buffer products.

Native Unicode buffers use the current platform's real bytes for decode;
character-buffer consumers observe the separately encoded character view.
Codec lookup is observed through its supported encoder and decoder entries;
stream reader/writer classes are outside the implemented codec contract.
"""

import _codecs
import sys


class ByteText(str):
    pass


class UnicodeText(unicode):
    pass


class Fill(object):
    def __unicode__(self):
        events.append('unicode')
        return u'x'


events = []
identities = set()
marker = ValueError('marker')
iteration_marker = TypeError('iteration marker')


def describe(value, original=None):
    if isinstance(value, (tuple, list)):
        return type(value).__name__, tuple(describe(item, original) for item in value)
    if isinstance(value, buffer):
        return 'buffer', str(value), value is original
    return type(value).__name__, value, value is original


def emit(identity, operation):
    assert identity not in identities, identity
    identities.add(identity)
    events[:] = []
    try:
        result = 'value', operation()
    except BaseException as error:
        result = 'error', type(error).__name__, str(error), error is marker, error is iteration_marker
    print identity + '\t' + repr((result, tuple(events)))
    sys.exc_clear()


def invoke(source, name, form, args, kwargs):
    kind = unicode if isinstance(source, unicode) else str
    descriptor = getattr(kind, name)
    if form == 0:
        return getattr(source, name)(*args, **kwargs)
    if form == 1:
        return descriptor(source, *args, **kwargs)
    return descriptor.__get__(source, kind)(*args, **kwargs)


table = ''.join(chr(value) for value in xrange(256))
names = ('center', 'ljust', 'rjust', 'join', 'find', 'rfind', 'index', 'rindex',
         'count', 'startswith', 'endswith', 'strip', 'lstrip', 'rstrip',
         'replace', 'split', 'rsplit', 'translate', 'lower', 'upper', 'title',
         'capitalize', 'swapcase', 'isalpha', 'isdigit', 'isalnum', 'isspace',
         'islower', 'isupper', 'istitle', 'zfill', 'splitlines', 'expandtabs',
         'partition', 'rpartition', 'encode', 'decode', '_formatter_parser',
         '_formatter_field_name_split')
assert len(names) == 39


def arguments(source, name):
    if name in ('center', 'ljust', 'rjust'):
        return 5, 'x'
    if name == 'join':
        return (['a', 'b'],)
    if name in ('find', 'rfind', 'index', 'rindex', 'count', 'startswith', 'endswith'):
        return 'a', 0, 3
    if name in ('strip', 'lstrip', 'rstrip'):
        return ('a',)
    if name == 'replace':
        return 'a', 'x', 1
    if name in ('split', 'rsplit'):
        return ' ', 1
    if name == 'translate':
        return ({97: u'x'},) if isinstance(source, unicode) else (table, '')
    if name == 'zfill':
        return (5,)
    if name in ('splitlines', 'expandtabs'):
        return (1,)
    if name in ('partition', 'rpartition'):
        return ('a',)
    if name in ('encode', 'decode'):
        return 'ascii', 'strict'
    return ()


for source_id, source in enumerate(('a b', ByteText('a b'), u'a b', UnicodeText(u'a b'))):
    applicable = names + (('isdecimal', 'isnumeric') if isinstance(source, unicode) else ())
    for name in applicable:
        for form in xrange(3):
            for count in xrange(6):
                args = (arguments(source, name) + (None,) * 6)[:count]
                for keyword_id, kwargs in enumerate(({}, {'unknown': 1}, {u'unknown': 1}, {'maxsplit': 1})):
                    def observe():
                        value = invoke(source, name, form, args, kwargs)
                        if name == '_formatter_parser':
                            value = list(value)
                        elif name == '_formatter_field_name_split':
                            value = value[0], list(value[1])
                        return describe(value, source)
                    emit('method/%d/%s/%d/%d/%d' %
                         (source_id, name, form, count, keyword_id), observe)

fill_owner = bytearray('x')
fills = ('x', '', 'xy', '\xff', u'x', u'\xe9', u'\U0001f600', u'\ud800',
         u'\0', ByteText('x'), UnicodeText(u'x'), bytearray('x'), buffer('x'),
         buffer(u'x'), buffer(fill_owner), memoryview('x'), None, 1, Fill(), u'xy')
assert len(fills) == 20
for source_id, factory in enumerate((str, ByteText, unicode, UnicodeText)):
    for size in xrange(5):
        source = factory('a' * size)
        for name in ('center', 'ljust', 'rjust'):
            for width in xrange(-3, 6):
                for fill_id, fill in enumerate(fills):
                    emit('padding/%d/%d/%s/%d/%d' %
                         (source_id, size, name, width, fill_id),
                         lambda: describe(getattr(source, name)(width, fill), source))


class Width(object):
    def __init__(self, policy, owner):
        self.policy = policy
        self.owner = owner
    def __int__(self):
        events.append('int')
        if self.policy == 0:
            return 5
        if self.policy == 1:
            raise marker
        if self.policy == 2:
            return 'bad'
        if self.policy == 3:
            self.owner[0] = ord('y')
            return 5
        return 1L << 100


class IndexWidth(object):
    def __index__(self):
        events.append('index')
        return 5


class IntWidth(int):
    def __int__(self):
        events.append('int subtype')
        return 3


class LongWidth(long):
    def __int__(self):
        events.append('long subtype')
        return 3


for source_id, source in enumerate(('ab', ByteText('ab'), u'ab', UnicodeText(u'ab'))):
    for name in ('center', 'ljust', 'rjust'):
        for form in xrange(3):
            for policy in xrange(8):
                for fill_id in xrange(3):
                    for handled in xrange(2):
                        owner = bytearray('x')
                        fill = ('x', buffer(u'x'), buffer(owner))[fill_id]
                        width = (IndexWidth() if policy == 5 else IntWidth(5) if policy == 6 else
                                 LongWidth(5) if policy == 7 else Width(policy, owner))
                        def observe_width():
                            def call():
                                try:
                                    value = invoke(source, name, form, (width, fill), {})
                                    outcome = 'value', describe(value, source)
                                except Exception as error:
                                    outcome = 'error', type(error).__name__, str(error), error is marker
                                return outcome, sys.exc_info()[1] is marker, str(owner)
                            if handled:
                                try:
                                    raise marker
                                except ValueError:
                                    return call()
                            return call()
                        emit('width/%d/%s/%d/%d/%d/%d' %
                             (source_id, name, form, policy, fill_id, handled), observe_width)


class JoinSource(object):
    def __init__(self, policy):
        self.policy = policy
    def __iter__(self):
        events.append('iter')
        if self.policy == 0:
            return None
        if self.policy == 1:
            raise TypeError('source type error')
        if self.policy == 2:
            raise marker
        if self.policy == 3:
            return iter(['a', 'b'])
        return self
    def next(self):
        events.append('next')
        raise iteration_marker


class ClassicJoinSource:
    def __init__(self, policy):
        self.policy = policy
    def __iter__(self):
        events.append('iter')
        if self.policy == 0:
            return None
        if self.policy == 1:
            raise TypeError('source type error')
        if self.policy == 2:
            raise marker
        if self.policy == 3:
            return iter(['a', 'b'])
        return self
    def next(self):
        events.append('next')
        raise iteration_marker


for source_id, source in enumerate(('-', ByteText('-'), u'-', UnicodeText(u'-'))):
    for form in xrange(3):
        for kind_id, factory in enumerate((JoinSource, ClassicJoinSource)):
            for policy in xrange(6):
                for handled in xrange(2):
                    items = 1 if policy == 5 else factory(policy)
                    def observe_join():
                        def call():
                            try:
                                value = invoke(source, 'join', form, (items,), {})
                                outcome = 'value', describe(value, source)
                            except Exception as error:
                                outcome = 'error', type(error).__name__, str(error), error is marker, error is iteration_marker
                            return outcome, sys.exc_info()[1] is marker
                        if handled:
                            try:
                                raise marker
                            except ValueError:
                                return call()
                        return call()
                    emit('join/%d/%d/%d/%d/%d' %
                         (source_id, form, kind_id, policy, handled), observe_join)


def view(owner, form):
    if form == 0:
        return buffer(owner)
    if form == 1:
        return buffer(owner, 0, 1)
    if form == 2:
        return buffer(owner, 1, 1)
    return buffer(buffer(owner, 1), 0, 1)


for source_id, source in enumerate(('aba', ByteText('aba'), u'aba', UnicodeText(u'aba'))):
    for owner_id, owner in enumerate(('ab', u'ab', u'a\xe9', bytearray('ab'))):
        for form in xrange(4):
            needle = view(owner, form)
            for name in ('find', 'rfind', 'count', 'split', 'rsplit', 'replace',
                         'partition', 'rpartition', 'startswith', 'endswith'):
                args = (needle, 'x') if name == 'replace' else (needle,)
                emit('character/%d/%d/%d/%s' % (source_id, owner_id, form, name),
                     lambda: describe(getattr(source, name)(*args), needle))

codec_functions = (_codecs.ascii_encode, _codecs.ascii_decode, _codecs.latin_1_encode,
                   _codecs.latin_1_decode, _codecs.utf_8_encode, _codecs.utf_8_decode)
for function in codec_functions:
    for owner_id, owner in enumerate(('ab', u'ab', u'a\xe9', bytearray('ab'))):
        for form in xrange(4):
            source = view(owner, form)
            emit('codec-character/%s/%d/%d' % (function.__name__, owner_id, form),
                 lambda: describe(function(source)))

translation_tables = (None, table, buffer(u'x' * 256), buffer(u'x'))
deletions = ('', 'a', buffer(u'a'), buffer(u'ab', 1, 1))
for source_id, source in enumerate(('abc', ByteText('abc'))):
    for table_id, translation in enumerate(translation_tables):
        for deletion_id, deletion in enumerate(deletions):
            for form in xrange(3):
                emit('translate/%d/%d/%d/%d' % (source_id, table_id, deletion_id, form),
                     lambda: describe(invoke(source, 'translate', form, (translation, deletion), {}), source))


def search(encoding):
    return None


def error_handler(error):
    return u'', error.end


error = UnicodeEncodeError('ascii', u'\xe9', 0, 1, 'test')
entries = [(_codecs.register, (search,)), (_codecs.lookup, ('ascii',)),
           (_codecs.register_error, ('text_arguments_audit', error_handler)),
           (_codecs.lookup_error, ('strict',))]
entries += [(function, ('a', 'strict', 1) if function is _codecs.utf_8_decode else
             ('a', 'strict')) for function in codec_functions]
entries += [(_codecs.encode, ('a', 'ascii', 'strict')),
            (_codecs.decode, ('a', 'ascii', 'strict'))]
entries += [(_codecs.lookup_error(name), (error,)) for name in
            ('strict', 'ignore', 'replace', 'xmlcharrefreplace', 'backslashreplace')]
assert len(entries) == 17
for function_id, (function, base_args) in enumerate(entries):
    for count in xrange(5):
        args = (base_args + (None,) * 5)[:count]
        for keyword_id, kwargs in enumerate(({}, {'other': 1}, {'encoding': 'ascii'})):
            def observe_codec():
                value = function(*args, **kwargs)
                if function is _codecs.lookup:
                    return len(value), callable(value[0]), callable(value[1]), describe(value[0](u'a')), describe(value[1]('a'))
                if function is _codecs.lookup_error:
                    return value.__name__, value.__module__
                return describe(value)
            emit('codec-arguments/%d/%d/%d' % (function_id, count, keyword_id), observe_codec)

for function in (_codecs.lookup, _codecs.lookup_error, _codecs.register_error):
    for name_id, name in enumerate((None, 1, buffer('ascii'), 'a\0', u'\xe9', u'ascii', 'missing_text_arguments_audit')):
        args = (name, error_handler) if function is _codecs.register_error else (name,)
        def observe_name():
            value = function(*args)
            if function is _codecs.lookup:
                return len(value), describe(value[0](u'a'))
            if function is _codecs.lookup_error:
                return value.__name__, value.__module__
            return describe(value)
        emit('codec-name/%s/%d' % (function.__name__, name_id), observe_name)

for function in codec_functions:
    for errors_id, errors in enumerate((None, 1, '', 'strict', u'strict', 'a\0', u'\xe9', buffer('strict'))):
        emit('codec-errors/%s/%d' % (function.__name__, errors_id),
             lambda: describe(function('a', errors)))


class HookSource(object):
    def __unicode__(self):
        events.append('source unicode')
        return u'a'


class ClassicHookSource:
    def __unicode__(self):
        events.append('classic source unicode')
        return u'a'


sources = ('a', ByteText('a'), u'a', UnicodeText(u'a'), buffer('a'), buffer(u'a'),
           buffer(u'\xe9'), buffer(bytearray('a')), None, 1, bytearray('a'),
           memoryview('a'), HookSource(), ClassicHookSource())
assert len(sources) == 14
for function in codec_functions:
    for source_id, source in enumerate(sources):
        for errors_id, errors in enumerate((None, 1, '', 'strict', u'strict', 'a\0', u'\xe9', buffer('strict'))):
            emit('codec-priority/%s/%d/%d' % (function.__name__, source_id, errors_id),
                 lambda: describe(function(source, errors)))

for name in ('strict', 'ignore', 'replace', 'xmlcharrefreplace', 'backslashreplace'):
    function = _codecs.lookup_error(name)
    emit('codec-handler-metadata/' + name, lambda: (function.__name__, function.__module__))


class RegistryName(str):
    def __hash__(self):
        events.append('name hash')
        if self.policy == 1:
            raise marker
        if self.policy == 2:
            return 0
        if self.policy == 3:
            return 'bad'
        return str.__hash__(self)
    def __eq__(self, other):
        events.append('name equal')
        raise marker


class WideRegistryName(unicode):
    def __hash__(self):
        events.append('wide name hash')
        if self.policy == 1:
            raise marker
        if self.policy == 2:
            return 0
        if self.policy == 3:
            return 'bad'
        return unicode.__hash__(self)
    def __eq__(self, other):
        events.append('wide name equal')
        raise marker


def named_handler(error):
    events.append(('named handler', error.start, error.end))
    return u'?', error.end


_codecs.register_error('text_arguments_matrix_named', named_handler)
for name_id, factory in enumerate((RegistryName, WideRegistryName)):
    for policy in xrange(4):
        for operation in xrange(3):
            for handled in xrange(2):
                name = factory('text_arguments_matrix_named')
                name.policy = policy
                def observe_registry():
                    def call():
                        if operation == 0:
                            value = _codecs.register_error(name, named_handler)
                            outcome = value, _codecs.lookup_error('text_arguments_matrix_named') is named_handler
                        elif operation == 1:
                            outcome = _codecs.lookup_error(name) is named_handler
                        else:
                            outcome = u'\xe9'.encode('ascii', name)
                        return outcome, sys.exc_info()[1] is marker
                    if handled:
                        try:
                            raise marker
                        except ValueError:
                            return call()
                    return call()
                emit('codec-registry/%d/%d/%d/%d' %
                     (name_id, policy, operation, handled), observe_registry)

assert len(identities) == 26217, len(identities)
