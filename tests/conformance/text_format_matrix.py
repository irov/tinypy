"""Bounded Python 2.7 formatting domains; compare stdout with CPython."""


def emit(identity, function, *arguments):
    try:
        value = function(*arguments)
        outcome = type(value).__name__ + ':' + repr(value)
    except Exception as error:
        outcome = 'error:' + type(error).__name__
    print identity + '\t' + outcome


numbers = [0, 1, -1, 15, 15L, -15L, 2 ** 63, 2 ** 100,
           0.0, -0.0, 1.25, -1.25, float('inf'), float('-inf'),
           float('nan'), 1 + 2j]
values = numbers + ['x', u'x', u'\u20ac', None, True]
codes = ['', 's', 'd', 'b', 'o', 'x', 'X', 'n', 'c',
         'e', 'E', 'f', 'F', 'g', 'G', '%']
flags = ['', '#', '+', ' ', '0', '<', '>', '=', '^', ',', '_', '0,', '+0,']
widths_and_precisions = ['', '8', '.2', '8.2']
for value_index, value in enumerate(values):
    for code_index, code in enumerate(codes):
        for flags_index, flag in enumerate(flags):
            for width_index, width in enumerate(widths_and_precisions):
                identity = 'format/builtin/%d/%d/%d/%d' % (value_index, code_index,
                                                          flags_index, width_index)
                emit(identity, format, value, flag + width + code)


def percent(format_string, value):
    return format_string % value


templates = ['%c', '%d', '%i', '%u', '%o', '%x', '%X', '%s', '%r', '%e',
             '%f', '%g', '%#o', '%08x', '%+08.2f', '%.0d', '%#.0x']
for template_index, template in enumerate(templates):
    for value_index, value in enumerate(values):
        for text_index, text in enumerate([template, unicode(template)]):
            identity = 'format/percent/%d/%d/%d' % (template_index, value_index, text_index)
            emit(identity, percent, text, value)
