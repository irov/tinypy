"""Dynamic compile modes, flags, source forms and exact diagnostic metadata."""


def code_shape(code):
    constants = []
    for value in code.co_consts:
        constants.append(code_shape(value) if type(value) is type(code) else value)
    return (code.co_argcount, code.co_nlocals, code.co_stacksize, code.co_flags,
            code.co_code.encode('hex'), code.co_names, code.co_varnames,
            code.co_freevars, code.co_cellvars, tuple(constants),
            code.co_filename, code.co_name, code.co_firstlineno,
            code.co_lnotab.encode('hex'))


def observe(identity, source, filename, mode, flags=0, dont_inherit=1):
    try:
        result = ('ok', code_shape(compile(source, filename, mode, flags, dont_inherit)))
    except BaseException as error:
        result = ('error', type(error).__name__, error.args,
                  getattr(error, 'msg', None), getattr(error, 'filename', None),
                  getattr(error, 'lineno', None), getattr(error, 'offset', None),
                  getattr(error, 'text', None))
    print identity + '\t' + repr(result)


byte_sources = ['', '\n', '1+2', 'x=1', 'x=1\ny=2', 'if 1:\n pass',
                'def f():\n return 1', 'from __future__ import division\nx=1/2',
                '(', 'x=', 'if 1\n pass', 'if 1:\npass', ' x=1',
                'return 1', 'break', 'continue', 'yield 1',
                'def f(a,a):\n pass', 'def f():\n yield 1\n return 2',
                'def f(a):\n global a', 'x=1\nfrom __future__ import division',
                'from __future__ import made_up', 'a+b=1', 'del f()',
                'f(a=1,a=2)', 'f(a=1,2)', 'try:\n pass',
                'raise ValueError, 1, 2, 3', 'print 1', 'u"\\u20ac"',
                '# coding: utf-8\nx=u"\xc3\xa9"', '\x00', '# coding: made_up\nx=1']
sources = byte_sources + [source.decode('latin-1') for source in byte_sources]
sources.extend([u'x=u"\u00e9\u20ac"', u'"\u00e9\u20ac"'])
# PyCF_ONLY_AST is outside SPEC.md. All remaining flags exercise supported
# futures, obsolete nested-scopes, dedent, unknown bits and C-int overflow.
flags = [0, 16, 512, 8192, 16384, 32768, 65536, 131072,
         -1, 1, 0x1000000, 0x80000000, 2**64]
for source_index, source in enumerate(sources):
    for mode in ['exec', 'eval', 'single']:
        for flag_index, flag in enumerate(flags):
            observe('compile/%d/%s/%d' % (source_index, mode, flag_index),
                    source, '<matrix>', mode, flag)


arguments = [None, 2, [], bytearray('1'), buffer('1'), memoryview('1'),
             '1', u'1', '\x00']
filenames = ['<matrix>', u'<matrix>', u'\u20ac', None, 'bad\x00name']
modes = ['eval', 'bad', u'eval', u'\u20ac', None, 'eval\x00name']
for source_index, source in enumerate(arguments):
    for filename_index, filename in enumerate(filenames):
        for mode_index, mode in enumerate(modes):
            for flag_index, flag in enumerate([0, 2**100]):
                observe('arguments/%d/%d/%d/%d' % (source_index, filename_index,
                                                 mode_index, flag_index),
                        source, filename, mode, flag)


class IntArgument(object):
    def __int__(self):
        return 0


class IndexArgument(object):
    def __index__(self):
        return 0


class BadIntArgument(object):
    def __int__(self):
        return 'bad'


class IntSubtype(int):
    def __int__(self):
        raise AssertionError('stored PyInt value must be used')


class LongSubtype(long):
    def __int__(self):
        return 0


protocol_arguments = [False, True, 0, 0L, 1.5, IntArgument(), IndexArgument(),
                      BadIntArgument(), IntSubtype(0), LongSubtype(2**100),
                      -2**31-1, 2**63]
for index, value in enumerate(protocol_arguments):
    observe('protocol/flags/%d' % index, '1', '<matrix>', 'eval', value)
    observe('protocol/inherit/%d' % index, '1', '<matrix>', 'eval', 0, value)
