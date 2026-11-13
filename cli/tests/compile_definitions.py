#!/usr/bin/env python3
"""Exercise the real command line, output preservation and marshal payload."""

import argparse
from pathlib import Path
import subprocess


def run(arguments):
    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    source = arguments.work_dir / 'input with spaces.py'
    output = arguments.work_dir / 'output with spaces.bin'

    def compile_source(text, definitions, optimize, separated=False):
        source.write_text(text, encoding='utf-8')
        command = [str(arguments.compiler), str(source), str(output), '<defines>', 'exec', str(optimize)]
        for definition in definitions:
            command.extend(['-D', definition] if separated else ['-D' + definition])
        result = subprocess.run(command, capture_output=True, timeout=30)
        assert result.returncode == 0, (command, result.stdout, result.stderr)
        assert not result.stdout and not result.stderr, (result.stdout, result.stderr)
        return output.read_bytes()

    observations = 0
    literals = ['False', 'True', 'None', '0', '-7', '+13', '0.5', '-0.5', ' \tFalse',
                '123456789012345678901234567890L', "'a=b'", "u'\\u041f\\u0440'",
                "u'\\ud83d\\ude00\\udc00'", "(17)"]
    for optimize in [0, 1, 2]:
        plain = compile_source('value = 42\n', [], optimize)
        supplied = compile_source('value = CONTROL\n', ['CONTROL=42'], optimize)
        assert plain == supplied
        runtime_names = compile_source('value = (_DEVELOPMENT, __NDEBUG__)\n', [], optimize)
        assert b'_DEVELOPMENT' in runtime_names and b'__NDEBUG__' in runtime_names
        observations += 2
        for literal in literals:
            expected = compile_source('value = CONTROL\n', ['CONTROL=' + literal], optimize)
            for name in ['_DEVELOPMENT', 'featureEnabled', '__NAME__', '__NDEBUG__']:
                actual = compile_source('value = ' + name + '\n', [name + '=' + literal], optimize,
                                        separated=observations % 2 == 0)
                assert actual == expected, (optimize, name, literal)
                if arguments.reference:
                    # Execute the emitted Python 2.7 code in the external oracle.
                    # Its expected constant also passes through marshal, which
                    # joins a surrogate pair as CPython's own .pyc would.
                    payload = arguments.work_dir / 'payload.marshal'
                    payload.write_bytes(actual)
                    reference = subprocess.run([str(arguments.reference), '-E', '-S', '-c',
                        "import marshal,sys; ns={}; exec marshal.load(open(sys.argv[1],'rb')) in ns; "
                        "expected=marshal.loads(marshal.dumps(eval(sys.argv[2]))); value=ns['value']; "
                        "assert type(value) is type(expected) and value == expected",
                        str(payload), literal], capture_output=True, timeout=30)
                    assert reference.returncode == 0, (literal, reference.stdout, reference.stderr)
                    assert not reference.stdout and not reference.stderr
                observations += 1

        guarded = ('events = []\n'
                   'def probe():\n    events.append(1)\n    return True\n'
                   'if FLAG:\n    events.append(2)\n'
                   'elif probe() and FLAG:\n    events.append(3)\n'
                   'else:\n    events.append(4)\n'
                   'if not FLAG:\n    events.append(5)\n'
                   'value = FLAG\n')
        for literal in ('True', 'False'):
            actual = compile_source(guarded, ['FLAG=' + literal], optimize)
            expected = compile_source(guarded.replace('FLAG', 'CONTROL'), ['CONTROL=' + literal], optimize)
            assert actual == expected
            observations += 1

    invalid = [['FLAG'], ['FLAG='], ['2FLAG=True'], ['__debug__=False'],
               ['FLAG=unknown()'], ['FLAG=1 // 0'], ['FLAG=[1]'], ['FLAG=False', 'FLAG=True'],
               ['FLAG=()'], ['FLAG=(1,)'], ['FLAG=1, 2'], ['FLAG=(1, (2, 3))'], ['FLAG=tuple()']]
    source.write_text('value = 42\n', encoding='utf-8')
    for definitions in invalid:
        output.write_bytes(b'preserved output')
        command = [str(arguments.compiler), str(source), str(output), '<defines>', 'exec', '1']
        result = subprocess.run(command + ['-D' + item for item in definitions], capture_output=True, timeout=30)
        # The diagnostic quotes the definition that failed.
        assert result.returncode == 1 and not result.stdout, definitions
        assert ("'%s'" % definitions[-1]).encode() in result.stderr, (definitions, result.stderr)
        assert output.read_bytes() == b'preserved output', definitions
        observations += 1
    print('compile definitions: %d checks passed' % observations)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    run(parser.parse_args())
