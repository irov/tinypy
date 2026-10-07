"""Finite Python 2 brace-field parsing, lookup and callback outcomes."""

events = []


def emit(identity, operation):
    events[:] = []
    try:
        value = operation()
        result = ('ok', type(value).__name__, repr(value), events[:])
    except Exception as error:
        detail = None
        if isinstance(error, UnicodeError):
            detail = (error.encoding, repr(error.object), error.start,
                      error.end, error.reason)
        result = ('error', type(error).__name__, str(error), detail, events[:])
    print identity + '\t' + repr(result)


class StringFormat(str):
    def __str__(self):
        raise AssertionError('format receiver uses its payload')
    def __unicode__(self):
        raise AssertionError('format receiver uses its payload')


class UnicodeFormat(unicode):
    __str__ = StringFormat.__dict__['__str__']
    __unicode__ = StringFormat.__dict__['__unicode__']


receivers = (str, unicode, StringFormat, UnicodeFormat)


class Value(object):
    def __init__(self, name):
        self.name = name
    def __getitem__(self, key):
        events.append(('item', self.name, type(key).__name__, repr(key)))
        return Value('item')
    def __getattr__(self, key):
        events.append(('attr', self.name, type(key).__name__, repr(key)))
        return Value('attr')
    def __format__(self, spec):
        events.append(('format', self.name, type(spec).__name__, repr(spec)))
        return 'v'
    def __str__(self):
        events.append(('str', self.name))
        return 's'
    def __unicode__(self):
        events.append(('unicode', self.name))
        return u'u'
    def __repr__(self):
        events.append(('repr', self.name))
        return 'r'


source = Value('first')
other = Value('second')
formats = ('{0}', '{}', '{first}', '{missing}', '{9}', '{0.foo}',
           '{0[foo]}', '{0[2]}', '{0[!]}', '{0[:]}', '{0[a:b]}',
           '{0[a!b]}', '{0[]}', '{0.}', '{0[2]x}', '{0[2].}',
           '{0!s}', '{0!r}', '{0!a}', '{0!}', '{0!sr}',
           '{0!s:{1}}', '{0!r:{1}}', '{0:{1}}', '{0:{1!s}}',
           '{0:{1!r}}', '{0:{1:}}', '{0:{1:{2}}}', '{0:{1.foo}}',
           '{0!s:{1.foo}}', '{0[!]:{1}}', '{0:\0}', '{0!\0}',
           '{0!s}{1}', '{0!r}{1}', '{0}{1}', '{0}{missing}',
           '{0}{}', '{}{0}', '{{{0}}}', '{{}}', '{', '}', '{0',
           '{0:{1}', '{0:{}.{}}', '{0[2]}', u'{0[\xe9]}',
           u'{caf\xe9}', u'{\u0660}', u'{0[\u0662]}', u'{0.\xe9}',
           u'{0!s:{1}}')
for i, pattern in enumerate(formats):
    for j, receiver in enumerate(receivers):
        if receiver in (str, StringFormat) and isinstance(pattern, unicode):
            if any(ord(character) > 127 for character in pattern):
                continue
        text = receiver(pattern)
        emit('field-%d-%d' % (i, j),
             lambda: text.format(source, other, '', first=source,
                                 **{u'caf\xe9': source}))


answers = ('bytes', u'unicode', u'\xe9', '\xff', None, 1,
           bytearray('abc'), buffer('abc'))
for i, answer in enumerate(answers):
    class Output(object):
        def __format__(self, spec):
            events.append(('format', type(spec).__name__, repr(spec)))
            return answer
    for j, receiver in enumerate(receivers):
        text = receiver('{0}')
        emit('return-%d-%d' % (i, j), lambda: text.format(Output()))


class ConvertedString(str):
    def __format__(self, spec):
        events.append(('subformat', type(spec).__name__, repr(spec)))
        return 'custom'


converted_string = ConvertedString('converted')


class Converted(object):
    def __str__(self):
        events.append('str')
        return converted_string
    def __repr__(self):
        events.append('repr')
        return converted_string


for i, pattern in enumerate(('{0!s}', '{0!r}', '{0!s:>20}',
                              '{0!r:>20}', '{0!s:{1}}', '{0!r:{1}}')):
    for j, receiver in enumerate(receivers):
        text = receiver(pattern)
        emit('converted-subtype-%d-%d' % (i, j),
             lambda: text.format(Converted(), Value('spec')))


class StringSpec(str):
    pass


class UnicodeSpec(unicode):
    pass


specs = ('', '>4', u'', u'>4', StringSpec(''), StringSpec('>4'),
         UnicodeSpec(u''), UnicodeSpec(u'>4'), None, 1,
         bytearray(''), buffer(''))
for i, spec in enumerate(specs):
    class FormatValue(object):
        def __format__(self, received):
            events.append(('format', type(received).__name__,
                           repr(received), received is spec))
            return 'v'
    for j, value in enumerate((FormatValue(), Converted(), 'a', u'a')):
        emit('builtin-spec-%d-%d' % (i, j), lambda: format(value, spec))


class Mapping(dict):
    def __getitem__(self, key):
        events.append(('mapping', type(key).__name__, repr(key)))
        return 'mapped'


for i, receiver in enumerate(receivers):
    for j, pattern in enumerate(('{0[2]}', '{0[02]}', '{0[abc]}')):
        text = receiver(pattern)
        emit('mapping-key-%d-%d' % (i, j), lambda: text.format(Mapping()))


def collect(iterator):
    result = []
    for step in range(6):
        try:
            result.append(('value', next(iterator)))
        except StopIteration:
            result.append(('stop',))
        except Exception as error:
            result.append(('error', type(error).__name__, str(error)))
    return result


parser_sources = ('', 'literal', '{0}', '{}', '{{x}}', 'a{0!r:b}c',
                  'a{0!sr}tail', 'a}tail{0}', 'a{', '{0', '{0[2]x}',
                  '0', '01', '99999999999999999999', '0.foo',
                  '0[2].foo', '0[]tail', '0[2]tail.foo', '0[foo',
                  u'\u0660', u'\u0660[\u0662]', u'0[\xe9]')
for i, pattern in enumerate(parser_sources):
    for j, receiver in enumerate(receivers):
        if receiver in (str, StringFormat) and isinstance(pattern, unicode):
            continue
        text = receiver(pattern)
        emit('parser-%d-%d' % (i, j),
             lambda: collect(text._formatter_parser()))
        def split():
            head, iterator = text._formatter_field_name_split()
            return head, collect(iterator)
        emit('split-%d-%d' % (i, j), split)


for i, convert in enumerate((str, repr)):
    def conversion_identity():
        value = convert(Converted())
        return type(value).__name__, value is converted_string
    emit('conversion-identity-%d' % i, conversion_identity)


for i, spec in enumerate(('', '>4', u'', u'>4')):
    emit('object-format-converted-%d' % i,
         lambda: object.__format__(Converted(), spec))


class RenderedString(str):
    def __str__(self):
        events.append('render-str')
        return 'byte-result'
    def __unicode__(self):
        events.append('render-unicode')
        return u'unicode-result'


class Rendered(object):
    def __format__(self, spec):
        events.append(('format', type(spec).__name__, repr(spec)))
        return RenderedString('original')


for i, receiver in enumerate(receivers):
    text = receiver('{0}')
    emit('render-result-subtype-%d' % i, lambda: text.format(Rendered()))


for i, answer in enumerate((u'ascii', u'\xe9')):
    class UnicodeReturn(object):
        def __str__(self):
            events.append('str')
            return answer
        def __repr__(self):
            events.append('repr')
            return answer
    for j, convert in enumerate((str, repr)):
        emit('unicode-return-%d-%d' % (i, j), lambda: convert(UnicodeReturn()))


for i, pattern in enumerate(('{0!s:{1}}', '{0!r:{1}}', '{0:{1}}')):
    class FailingSpec(object):
        def __format__(self, spec):
            events.append(('failing-spec', repr(spec)))
            raise ValueError('spec callback')
    for j, receiver in enumerate(receivers):
        text = receiver(pattern)
        emit('nested-error-order-%d-%d' % (i, j),
             lambda: text.format(Value('first'), FailingSpec()))


for i, value in enumerate((buffer('abc'), buffer('\xff'),
                           bytearray('abc'), bytearray('\xff'))):
    for j, pattern in enumerate(('{0}', '{0!s}', '{0:>4}')):
        for k, receiver in enumerate(receivers):
            text = receiver(pattern)
            emit('legacy-buffer-%d-%d-%d' % (i, j, k),
                 lambda: text.format(value))


class Classic:
    def __format__(self, spec):
        events.append(('classic-format', type(spec).__name__, repr(spec)))
        return 'classic'
    def __str__(self):
        events.append('classic-str')
        return 's'
    def __unicode__(self):
        events.append('classic-unicode')
        return u'u'


for i, pattern in enumerate(('{0}', '{0!s}', '{0:>4}')):
    for j, receiver in enumerate(receivers):
        text = receiver(pattern)
        emit('classic-format-%d-%d' % (i, j), lambda: text.format(Classic()))
