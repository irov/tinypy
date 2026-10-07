"""Project-authored, deterministic valid-source compiler product domains."""

from pathlib import Path
from itertools import product


CONTROL_WRAPPERS = ('plain', 'if', 'while', 'for', 'except', 'finally', 'with', 'multiwith')
CONTROL_ACTIONS = ('return result\n', 'break\n', 'continue\n',
                   'raise ValueError, result\n', 'yield result\n')


def indent(source):
    return ''.join(' ' + line if line.strip() else line
                   for line in source.splitlines(True))


def wrap_control(body, kind):
    if kind == 'plain':
        return body
    if kind == 'if':
        return 'if flag:\n' + indent(body) + 'else:\n pass\n'
    if kind == 'while':
        return 'while flag:\n' + indent(body) + 'else:\n flag=0\n'
    if kind == 'for':
        return 'for item in items:\n' + indent(body) + 'else:\n flag=0\n'
    if kind == 'except':
        return ('try:\n' + indent(body) +
                'except ValueError, error:\n flag=error\nelse:\n flag=1\n')
    if kind == 'finally':
        return 'try:\n' + indent(body) + 'finally:\n cleanup()\n'
    if kind == 'with':
        return 'with manager as entry:\n' + indent(body)
    if kind == 'multiwith':
        return 'with manager as entry, other as second:\n' + indent(body)
    raise AssertionError(kind)


def composition_cases():
    result = {}
    for outer, inner, action in product(CONTROL_WRAPPERS, CONTROL_WRAPPERS,
                                        CONTROL_ACTIONS):
        body = wrap_control(wrap_control(action, inner), outer)
        source = ('def f(flag,items,manager,other,result):\n for value in items:\n' +
                  indent(indent(body)) + ' return\n')
        name = 'exec/control_%s_%s_%s.py' % (outer, inner, action.split()[0])
        result[name] = source
    signatures = ('a,z,b', 'z,a,b', '(z,a),b', 'a,(b,z)', 'a,b=2,*z,**kw')
    expressions = ('lambda: (a,b,z)', '[lambda: (a,b,z) for b in items]',
                   '(lambda: (a,b,z) for b in items)',
                   '{b:lambda: (a,b,z) for b in items}', '{b for b in (a,z)}')
    for signature_index, signature in enumerate(signatures):
        for expression_index, expression in enumerate(expressions):
            source = ('def outer(%s):\n items=[1,2]\n def middle():\n'
                      '  class C(object):\n   a=2\n   def inner(self):\n'
                      '    return %s\n  return C\n return middle\n') % (signature, expression)
            result['exec/closure_%d_%d.py' % (signature_index, expression_index)] = source
    for gap, length in product((0, 127, 128, 255, 256, 511), (1, 21, 86, 260)):
        source = ('def f(flag):\n if flag:\n' + '  x=1\n' * length +
                  '\n' * gap + '  return x\n return -1\n')
        result['exec/line_%d_%d.py' % (gap, length)] = source
    return result


def cases():
    expressions = []
    literals = ['-3', '0', '2', '5L', '1.25', '(1+2j)']
    for left in literals:
        for right in literals:
            for operation in ['+', '-', '*', '/', '//', '%', '==', '<', '&', '|', '^']:
                expressions.append('(%s) %s (%s)' % (left, operation, right))
    expressions.extend([
        'not []', '~5L', '-0.0', '2**65', '1 < 2 < 3',
        '(1 and 2) or 3', '5 if 0 else 8',
        '[x*x for x in range(4) if x % 2]',
        '{x*x for x in range(4)}', '{x:x*x for x in range(4)}',
        '(x*x for x in range(4))', 'lambda (a,b), c=2: a+b+c',
        '(1,)', '()', '{}', '[1,2][::-1]', 'u"a\\u20ac"',
        '"a" "b"', 'r"\\n"', '0xffL', '077', '`(1,2)`',
        'f(1, a=2, *args, **kwargs)', 'obj.attr[1:4:2]',
    ])
    result = {}
    for index, expression in enumerate(expressions):
        name = 'expression_%04d.py' % index
        result['eval/' + name] = expression + '\n'
        result['single/' + name] = expression + '\n'
        result['exec/' + name] = 'result = ' + expression + '\n'
    statements = [
        '"module doc"\nx=1\nassert x, "assert message"\n'
        'raw="<lambda>"\nfolded="<"+"lambda>"\nformatted="<%s>" % "lambda"\n'
        'joined="<" "module>"\nidentifier="__doc__"\n'
        'top=genexpr=setcomp=dictcomp=1\n',
        'from __future__ import division\nx=5/2\n',
        'from __future__ import unicode_literals\nx="value"\n',
        'from __future__ import print_function\nprint(1,2,sep=":")\n',
        'from __future__ import absolute_import\nfrom .pkg import member\n',
        'def f(a, (b,c), *args, **kwargs):\n "doc"\n return a+b+c\n',
        'def outer(a):\n def inner(b):\n  return a+b\n return inner\n',
        'def f():\n for x in range(4):\n  yield x\n',
        '@decorate(1)\n@other\ndef f(a=2):\n return a\n',
        'class C(Base):\n "doc"\n __slots__=("x",)\n def f(self):\n  return self.x\n',
        'for x in source:\n if x: continue\n else: break\nelse:\n y=2\n',
        'while condition:\n if other: break\nelse:\n pass\n',
        'try:\n x=call()\nexcept ValueError, error:\n x=error\nelse:\n x=2\nfinally:\n cleanup()\n',
        'def f():\n try:\n  return 3\n finally:\n  cleanup()\n',
        'with a() as x, b() as y:\n result=x+y\n',
        'exec source in global_namespace, local_namespace\n',
        'print >>output, value,\n',
        'a,(b,c)=source\ndel a, obj.attr, source[1:3]\n',
        'global value\nvalue += 2\n',
        'raise ValueError, "message", traceback\n',
        'try:\n try:\n  work()\n finally:\n  cleanup()\nexcept:\n raise\n',
        'def f():\n return [x+y for x in left for y in right if x < y]\n',
        'def outer():\n x=0\n def middle():\n  def inner():\n   return x\n  return inner\n return middle\n',
        '# coding: utf-8\nx=u"é€"\n',
    ]
    for index, source in enumerate(statements):
        result['exec/statement_%04d.py' % index] = source
    result.update(composition_cases())
    return result


def write_corpus(directory: Path):
    sources = cases()
    # Refuse stale generated inputs: the directory is a persistent audit artifact.
    if directory.exists():
        existing = {path.relative_to(directory).as_posix()
                    for path in directory.rglob('*.py')}
        unknown = existing - set(sources)
        if unknown:
            raise RuntimeError('unexpected generated compiler inputs: ' + ', '.join(sorted(unknown)))
    for name, source in sources.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding='utf-8')
    return {mode: sum(name.startswith(mode + '/') for name in sources)
            for mode in ('exec', 'eval', 'single')}
