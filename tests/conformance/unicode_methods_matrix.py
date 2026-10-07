"""Bounded Unicode translation and built-in codec callback products."""

import _codecs
import sys

identities = set()
events = []
marker = ValueError('matrix callback')
outer = RuntimeError('matrix outer')


class Wide(unicode):
    def __unicode__(self):
        raise AssertionError('stored Unicode payload required')


class Integer(int):
    def __int__(self):
        raise AssertionError('stored integer payload required')


def describe(value):
    if isinstance(value, tuple):
        return tuple(describe(item) for item in value)
    return type(value).__name__, repr(value)


def capture(operation):
    try:
        return 'ok', describe(operation())
    except Exception as error:
        return 'error', type(error).__name__, str(error), error is marker


def emit(identity, operation):
    assert identity not in identities, identity
    identities.add(identity)
    events[:] = []
    try:
        raise outer
    except RuntimeError:
        outcome = capture(operation)
        result = outcome, events[:], sys.exc_info()[1] is outer
    print identity + '\t' + repr(result)


answers = (u'X', Wide(u'X\0Y'), u'', 88, Integer(88), None,
           'byte', 88L, -1, 0x110000)


class Mapping(object):
    def __init__(self, deleted, answer, behavior):
        self.deleted, self.answer, self.behavior = deleted, answer, behavior
        self.hits = {}

    def __getitem__(self, key):
        events.append(('item', key))
        if key == self.deleted:
            return None
        count = self.hits.get(key, 0) + 1
        self.hits[key] = count
        if self.behavior == 1 and count > 1:
            return u'changed'
        if self.behavior == 2 and count > 1:
            raise marker
        if self.behavior == 3:
            raise LookupError('absent')
        if self.behavior == 4 and count == 1:
            return None
        return self.answer


class Classic:
    __init__ = Mapping.__dict__['__init__']
    __getitem__ = Mapping.__dict__['__getitem__']


class Dictionary(dict):
    __init__ = Mapping.__dict__['__init__']
    __getitem__ = Mapping.__dict__['__getitem__']


contents = (u'', u'a', u'ab', u'aaab', u'abab', u'abba', u'abc',
            u'a\0b', u'a\U00010428b', u'ba')
for factory_id, factory in enumerate((unicode, Wide)):
    for content_id, content in enumerate(contents):
        source = factory(content)
        for table_id, table_type in enumerate((Mapping, Classic, Dictionary)):
            for deletion_id, deleted in enumerate((97, 98, 0)):
                for answer_id, answer in enumerate(answers):
                    for behavior in range(5):
                        for form in range(2):
                            def operation():
                                table = table_type(deleted, answer, behavior)
                                return (source.translate(table) if form == 0
                                        else unicode.translate(source, table))
                            emit('translate/%d/%d/%d/%d/%d/%d/%d' %
                                 (factory_id, content_id, table_id, deletion_id,
                                  answer_id, behavior, form), operation)


class Encode(UnicodeEncodeError):
    pass


class Decode(UnicodeDecodeError):
    pass


class Translate(UnicodeTranslateError):
    pass


error_types = (UnicodeEncodeError, Encode, UnicodeDecodeError, Decode,
               UnicodeTranslateError, Translate)
for factory_id, factory in enumerate(error_types):
    decode = issubclass(factory, UnicodeDecodeError)
    translate = issubclass(factory, UnicodeTranslateError)
    for size in (0, 1, 3):
        text = 'abc'[:size] if decode else u'abc'[:size]
        for start in (0, 1, 2, 3):
            for end in (0, 1, 2, 3, 5):
                for mode in (('ignore', 'replace') if decode else ('ignore',)):
                    def operation():
                        error = (factory(text, start, end, 'bad') if translate
                                 else factory('ascii', text, start, end, 'bad'))
                        return _codecs.lookup_error(mode)(error)
                    emit('end/%d/%d/%d/%d/%s' % (factory_id, size, start, end, mode), operation)


class ReportedName(object):
    def __init__(self, name, failure):
        self.name, self.failure = name, failure

    def __str__(self):
        events.append('str')
        if self.failure == 3:
            raise marker
        return self.name


class Label(object):
    def __init__(self, name, failure):
        self.name, self.failure = name, failure

    @property
    def __name__(self):
        events.append('name')
        if self.failure == 2:
            raise marker
        return ReportedName(self.name, self.failure)


class Wrong(object):
    def __init__(self, label, failure):
        self.label, self.failure = label, failure

    @property
    def __class__(self):
        events.append('class')
        if self.failure == 1:
            raise marker
        return self.label


for mode in ('ignore', 'replace', 'xmlcharrefreplace', 'backslashreplace'):
    for name_id, name in enumerate(('', 'Reported', 'A\0B', 'A' * 399, 'A' * 400, 'A' * 401)):
        for failure in range(4):
            def operation():
                return _codecs.lookup_error(mode)(Wrong(Label(name, failure), failure))
            emit('diagnostic/%s/%d/%d' % (mode, name_id, failure), operation)

for transform_id, transform in enumerate((_codecs.encode, _codecs.decode)):
    for source_id, source in enumerate(('', 'ab', 'ff', '\xff', u'', u'ab', u'ff', u'\xff', Wide(u'ab'), object())):
        for errors_id, errors in enumerate(('ignore', 'replace', 'unknown')):
            def operation():
                try:
                    return transform(source, 'hex', errors)
                except AssertionError as error:
                    return type(error).__name__, error.args
            emit('hex/%d/%d/%d' % (transform_id, source_id, errors_id), operation)

assert len(identities) == 18636, len(identities)
