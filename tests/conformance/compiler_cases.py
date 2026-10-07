"""Project-authored, deterministic valid-source compiler product domains."""

from pathlib import Path


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
        '"module doc"\nx=1\nassert x, "assert message"\n',
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
