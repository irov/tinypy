"""Compiler phase, context, token and diagnostic product domains."""


def shape(code):
    constants = tuple(shape(value) if type(value) is type(code) else value
                      for value in code.co_consts)
    return (code.co_argcount, code.co_nlocals, code.co_stacksize, code.co_flags,
            code.co_code.encode('hex'), code.co_names, code.co_varnames,
            code.co_freevars, code.co_cellvars, constants, code.co_filename,
            code.co_name, code.co_firstlineno, code.co_lnotab.encode('hex'))


sources = []
targets = ['None', 'True', '1', '1L', '1.0', '"s"', '()', '[x for x in y]',
           '(x for x in y)', '{x for x in y}', '{x:x for x in y}', 'f()',
           'a+b', 'lambda: 1', 'a if b else c', '`a`']
for target in targets:
    sources.extend([target + '=1', target + '+=1', 'del ' + target])
parameters = ['a,a', 'a,(b,a)', '(a,(b,a))', 'a=1,b', '(a,b)=1,c',
              '*a,*b', '**a,b', '*a=1', '(a,1)', '(a,)', '()',
              '(a,b),(c,d)', 'a=1,*args,**kwargs', '(a,b),c=1']
for parameters_source in parameters:
    sources.extend(['def f(' + parameters_source + '):\n pass\n',
                    'lambda ' + parameters_source + ': 1'])
sources.extend([
    'continue', 'break', 'yield 1', 'return 1',
    'def f():\n yield 1\n return 2',
    'def f():\n yield 1\n return',
    'def f():\n def inner():\n  yield 1\n return 2',
    'while 1:\n try:\n  pass\n finally:\n  continue',
    'while 1:\n try:\n  pass\n finally:\n  break',
    'while 1:\n def f():\n  break',
    'while 1:\n class C:\n  continue',
    'def f(a):\n global a',
    'def f():\n exec "x=1"\n def inner():\n  return x',
    'from __future__ import division as d\nx=1/2',
    'from __future__ import braces',
    'from __future__ import *',
    'from __future__ import division, made_up',
    '"doc"\nfrom __future__ import division\nx=1/2',
    '"doc"\n"other"\nfrom __future__ import division',
    'if 1:\n from __future__ import division',
    'def f():\n from __future__ import division',
    'from __future__ import print_function\nprint 1',
    'from __future__ import unicode_literals\nx=b"a"',
    'f(a=1,a=2)', 'f(a=1,2)', 'f(*a,2)', 'f(**a,**b)',
    'f(a.b=1)', 'f(1=2)', 'f(a==1)', 'f(a for a in b, 1)',
    'try:\n pass', 'try:\n pass\nelse:\n pass',
    'try:\n pass\nexcept:\n pass\nexcept ValueError:\n pass',
    'with a as (b,c):\n pass', 'with a as 1:\n pass',
    'raise ValueError, 1, 2, 3', 'raise',
    'if 1:\npass', ' x=1', 'if 1:\n pass\n  pass',
    'if 1:\n  pass\n pass', 'if 1\n pass',
    '"unterminated', '"""unterminated', "u'\\x'", "u'\\u123'",
    "u'\\Uffffffff'", "r'\\'", '09', '0b2', '0x', '1e+',
    '1.2L', '123abc', 'a=(', 'a=[1,2', 'a={1:2', 'a=1\\',
    'a=1\r\nb=2\r\n', 'a=1\rb=2\r', '# comment\r\na=1',
    '# coding: latin-1\nx=u"\xe9"',
    '# coding: utf-8\nx=u"\xe9"',
    '# coding: utf-8\nx=u"\xc3\xa9"',
    '# coding: ascii\nx="\xe9"',
    '# coding: made_up\nx=',
    'x=1\n\x00y=2', '# comment\x00',
])

# Decoder failures are ordinary source syntax, never manufactured bytecode.
for prefix in ['u', 'ur', 'U', 'Ur', '']:
    for contents in ['\\x', '\\x0', '\\xg0', 'a\\x0q', '\\u123',
                     '\\u12q4', '\\U12', '\\Uffffffff', '\\N', '\\N{',
                     '\\N{}', '\\N{MADE UP}', 'a\\N{MADE UP}',
                     '\\ud800', '\\U0010ffff', '\\N{LATIN SMALL LETTER A}']:
        # A terminating non-hex tail makes raw decoder diagnostics stable.
        if prefix.lower() == 'ur':
            contents += 'zzzzzzzz'
        sources.append(prefix + "'" + contents + "'")
for encoding, encoded in [('latin-1', '\xe9'), ('utf-8', '\xc3\xa9'),
                          ('utf-8', '\xe9'), ('utf-8', '\xc3\xa9\xe9')]:
    for body in ["u'%s\\xq'", "ur'%s\\u12zzzzzzzz'", "'%s'", "# %s\nx=1"]:
        sources.append('# coding: ' + encoding + '\n' + body % encoded)
sources.extend(["'\\xff' u''", "u'' '\\xff'", "u'a' '\\xff' u'b'",
                "x='\xe9'", "u'\xe9\\xq'", "ur'\xe9\\u12zzzzzzzz'"])
for newline in ['\n', '\r', '\r\n']:
    for body in ['x="\xe9"', '# \xe9\nx=1', '"\xe9"']:
        sources.append(('# coding: ascii\n' + body + '\n').replace('\n', newline))
for cookie, encoded in [('', '\xe9'), ('# coding: latin-1\n', '\xe9'),
                        ('# coding: utf-8\n', '\xc3\xa9')]:
    for body in ["u'%s'+", "x=u'%s'+", "u'%s' x", "del u'%s'"]:
        sources.append(cookie + body % encoded)

for source_index, raw in enumerate(sources):
    for form_index, source in enumerate((raw, raw.decode('latin-1'))):
        for mode in ('exec', 'eval', 'single'):
            for flags_index, flags in enumerate((0, 512, 8192, 65536)):
                try:
                    result = ('ok', shape(compile(source, '<compiler-matrix>', mode, flags, 1)))
                except BaseException as error:
                    result = ('error', type(error).__name__, error.args,
                              getattr(error, 'msg', None), getattr(error, 'filename', None),
                              getattr(error, 'lineno', None), getattr(error, 'offset', None),
                              getattr(error, 'text', None))
                print 'diagnostic/%d/%d/%s/%d\t%s' % (source_index, form_index, mode, flags_index, repr(result))
