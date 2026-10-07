#!/usr/bin/env python3
"""Build and verify the supported Python 2.7 contract in four build profiles."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parent / 'conformance'))
from compiler_cases import write_corpus


ROOT = Path(__file__).resolve().parents[1]
PROFILES = {
    'debug': ('Debug', False, False),
    'release': ('Release', False, False),
    'sanitize': ('Debug', True, False),
    'lto': ('Release', False, True),
}
RUNTIME_OUTCOMES = 15040
RUNTIME_MATRICES = [('runtime_matrix.py', RUNTIME_OUTCOMES), ('text_format_matrix.py', 18186),
                    ('compile_matrix.py', 3216), ('text_numeric_edges_matrix.py', 4082),
                    ('control_matrix.py', 222), ('sre_matrix.py', 171200),
                    ('type_slots_matrix.py', 415), ('comparison_matrix.py', 2340),
                    ('compiler_diagnostics_matrix.py', 6432), ('descriptor_matrix.py', 1312),
                    ('iterator_builtin_matrix.py', 1504), ('text_protocol_matrix.py', 726),
                    ('format_fields_matrix.py', 568), ('functional_matrix.py', 499),
                    ('namespace_matrix.py', 511), ('attribute_matrix.py', 1091),
                    ('control_composition_matrix.py', 2048),
                    ('container_callback_matrix.py', 2990),
                    ('text_numeric_callback_matrix.py', 1019),
                    ('object_callback_matrix.py', 808), ('compiler_future_matrix.py', 12288),
                    ('object_state_matrix.py', 1446), ('container_state_matrix.py', 4784),
                    ('operator_dispatch_matrix.py', 11538), ('buffer_state_matrix.py', 2068),
                    ('text_state_matrix.py', 3842), ('execution_state_matrix.py', 622),
                    ('sys_state_matrix.py', 727), ('metadata_arguments_matrix.py', 325), ('numeric_arguments_matrix.py', 995),
                    ('container_arguments_matrix.py', 3756), ('introspection_arguments_matrix.py', 1026),
                    ('buffer_arguments_matrix.py', 7456), ('iterator_metadata_matrix.py', 5232),
                    ('wrapper_metadata_matrix.py', 1556), ('struct_arguments_matrix.py', 5986),
                    ('buffer_lifetime_matrix.py', 7524), ('text_arguments_matrix.py', 26217),
                    ('native_readiness_matrix.py', 913), ('weakref_protocol_matrix.py', 5032)]
STATS = re.compile(r'^tinypy stats: .*outstanding_bytes=0 outstanding_allocations=0\r?\n?$')


def source_fingerprint():
    paths = [ROOT / 'CMakeLists.txt', ROOT / 'SPEC.md']
    for directory in ('src', 'include', 'tests', 'cli', 'tools'):
        paths.extend(path for path in (ROOT / directory).rglob('*')
                     if path.is_file() and (path.suffix in ('.c', '.h', '.inc', '.py', '.json', '.cmake')
                                            or path.name == 'CMakeLists.txt'))
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.relative_to(ROOT).as_posix().encode('utf-8') + b'\0')
        digest.update(path.read_bytes())
        digest.update(b'\0')
    return digest.hexdigest()


def portable_inventory(manifest):
    discovered = {name + '.' + case for name, module in manifest['modules'].items()
                  for case in module['cases']}
    excluded = set(manifest['excluded_cases'])
    if not excluded <= discovered:
        raise RuntimeError('manifest exclusions refer to undiscovered cases')
    return discovered - excluded


def check_coverage_inventory(manifest, inventory):
    modules = manifest['modules']
    local_modules = {name: module for name, module in modules.items() if name.startswith('local.')}
    local_paths = [module['path'] for module in local_modules.values()]
    discovered = {path.relative_to(ROOT / 'tests/upstream').as_posix()
                  for path in (ROOT / 'tests/upstream/local').glob('test_*.py') if path.is_file()}
    registered = set(local_paths)
    if len(local_paths) != len(registered) or registered != discovered:
        raise RuntimeError('local test inventory changed: unregistered=%r missing=%r' %
                           (sorted(discovered - registered), sorted(registered - discovered)))
    for name, module in local_modules.items():
        if module.get('origin') != 'project-authored' or module['path'] != name.replace('.', '/') + '.py':
            raise RuntimeError('local module identity does not match its source: ' + name)
    covered_modules = set()
    covered_paths = set()
    for domain in inventory['domains']:
        for path in domain['tests']:
            if not (ROOT / path).is_file():
                raise RuntimeError('missing coverage input: ' + path)
            covered_paths.add(path)
        for module in domain.get('modules', []):
            if module not in modules:
                raise RuntimeError('missing coverage module: ' + module)
            covered_modules.add(module)
    unmapped = set(local_modules) - covered_modules
    if unmapped:
        raise RuntimeError('local modules missing from coverage: ' + ', '.join(sorted(unmapped)))
    matrix_names = [name for name, _ in RUNTIME_MATRICES]
    if any(type(count) is not int or count <= 0 for _, count in RUNTIME_MATRICES):
        raise RuntimeError('runtime matrix cardinality must be a positive integer')
    registered_matrices = set(matrix_names)
    discovered_matrices = {path.name for path in (ROOT / 'tests/conformance').glob('*_matrix.py') if path.is_file()}
    if len(matrix_names) != len(registered_matrices) or registered_matrices != discovered_matrices:
        raise RuntimeError('runtime matrix inventory changed: unregistered=%r missing=%r' %
                           (sorted(discovered_matrices - registered_matrices),
                            sorted(registered_matrices - discovered_matrices)))
    unmapped_matrices = {'tests/conformance/' + name for name in matrix_names} - covered_paths
    if unmapped_matrices:
        raise RuntimeError('runtime matrices missing from coverage: ' + ', '.join(sorted(unmapped_matrices)))
    return {'local_modules': len(local_modules), 'runtime_matrices': len(matrix_names)}


def check_reference(stdout, stderr):
    info = json.loads(stdout)
    if info['version'] != [2, 7, 18] or info['implementation'] != 'CPython' or stderr:
        raise RuntimeError('reference must be CPython 2.7.18')
    return info


def check_build_info(info, profile):
    debug = PROFILES[profile][0] == 'Debug'
    if bool(info['debug']) != debug or bool(info['cycle_diagnostics']) != debug:
        raise RuntimeError('runtime build flags do not match requested profile')
    return info


def check_balance(stderr):
    if not STATS.fullmatch(stderr.decode('utf-8', errors='replace')):
        raise RuntimeError('unexpected stderr or outstanding allocations')
    return {'outstanding_bytes': 0, 'outstanding_allocations': 0}


def check_ctest_inventory(discovery, native_cases, manifest):
    expected = set(native_cases)
    expected.update('upstream_' + module.replace('.', '_') for module in manifest['modules'])
    expected.add('upstream_adapter_assertions')
    names = [test['name'] for test in discovery['tests']]
    if len(names) != len(set(names)) or set(names) != expected:
        missing = sorted(expected - set(names))
        extra = sorted(set(names) - expected)
        raise RuntimeError('CTest inventory changed: missing=%r extra=%r' % (missing, extra))
    for test in discovery['tests']:
        if any(prop['name'] == 'DISABLED' and prop['value'] for prop in test.get('properties', [])):
            raise RuntimeError('CTest is disabled: ' + test['name'])
    return {'registered': len(names), 'native_and_runtime': len(native_cases),
            'portable_modules': len(manifest['modules']), 'adapter': 1}


def check_ctest_results(path, native_cases):
    tests = ET.parse(path).getroot().findall('.//testcase')
    names = [test.attrib['name'] for test in tests]
    if len(names) != len(set(names)) or set(names) != set(native_cases):
        raise RuntimeError('CTest results contain duplicate, missing or unexpected cases')
    for test in tests:
        if test.find('skipped') is not None or test.find('failure') is not None or test.find('error') is not None:
            raise RuntimeError('CTest did not pass: ' + test.attrib['name'])
        if test.attrib.get('status', 'run') != 'run':
            raise RuntimeError('CTest did not execute: ' + test.attrib['name'])
    return {'PASS': len(tests), 'SKIP': 0}


def check_portable(report, manifest, profile):
    expected = portable_inventory(manifest)
    results = report['results']
    identities = [item['case'] for item in results]
    if len(identities) != len(set(identities)) or set(identities) != expected:
        raise RuntimeError('portable report contains duplicate, missing or unexpected cases')
    cycles = set(manifest['debug_cycle_cases'])
    debug = PROFILES[profile][0] == 'Debug'
    for item in results:
        expected_status = 'SKIP' if item['case'] in cycles and not debug else 'PASS'
        if item['status'] != expected_status:
            raise RuntimeError('unexpected result: %s %s' % (item['case'], item['status']))
        if item['case'] in cycles and debug and item.get('mode') != 'debug-cycle-adaptation':
            raise RuntimeError('cycle case did not exercise the Debug detector')
    counts = {status: sum(item['status'] == status for item in results)
              for status in ('PASS', 'FAIL', 'SKIP', 'DEFER')}
    if report['counts'] != {status: count for status, count in counts.items() if count}:
        raise RuntimeError('portable summary disagrees with individual results')
    return counts


def check_runtime(expected, actual, stderr, expected_count=None):
    expected_count = RUNTIME_OUTCOMES if expected_count is None else expected_count
    if type(expected_count) is not int or expected_count <= 0:
        raise RuntimeError('runtime matrix cardinality must be a positive integer')
    lines = expected.splitlines()
    identities = [line.split(b'\t', 1)[0] for line in lines]
    if len(lines) != expected_count or len(set(identities)) != len(lines):
        raise RuntimeError('runtime product inventory changed: expected %d unique outcomes' % expected_count)
    check_balance(stderr)
    if actual != expected:
        actual_lines = actual.splitlines()
        for index, expected_line in enumerate(lines):
            actual_line = actual_lines[index] if index < len(actual_lines) else b'<missing>'
            if actual_line != expected_line:
                raise RuntimeError('runtime outcome mismatch: expected %r, got %r' % (expected_line, actual_line))
        raise RuntimeError('runtime matrix produced extra output')
    return {'outcomes': len(lines), 'sha256': hashlib.sha256(actual).hexdigest()}


class Validation:
    def __init__(self, arguments):
        self.arguments = arguments
        self.directory = arguments.build_root.resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.report = {'schema': 1, 'reference': str(arguments.reference.resolve()),
                       'requested_profiles': arguments.profile, 'stages': [], 'status': 'RUNNING'}
        self.environment = os.environ.copy()
        self.environment['UBSAN_OPTIONS'] = 'halt_on_error=1'
        # Apple ASan rejects LeakSanitizer; allocator accounting is independent.
        leak_detection = '0' if sys.platform == 'darwin' else '1'
        self.environment['ASAN_OPTIONS'] = 'detect_leaks=' + leak_detection + ':halt_on_error=1'
        self.report['sanitizer_options'] = {name: self.environment[name]
                                           for name in ('ASAN_OPTIONS', 'UBSAN_OPTIONS')}

    def save(self):
        self.arguments.report.parent.mkdir(parents=True, exist_ok=True)
        self.arguments.report.write_text(json.dumps(self.report, indent=2) + '\n', encoding='utf-8')

    def run(self, identity, command):
        print(identity, flush=True)
        started = time.monotonic()
        stage = {'id': identity, 'command': [str(item) for item in command], 'status': 'RUNNING'}
        self.report['stages'].append(stage)
        self.save()
        try:
            timed_out = False
            with subprocess.Popen(stage['command'], cwd=ROOT, env=self.environment,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  start_new_session=os.name == 'posix') as process:
                try:
                    stdout, stderr = process.communicate(timeout=self.arguments.timeout)
                except subprocess.TimeoutExpired:
                    timed_out = True
                    if os.name == 'posix':
                        try:
                            os.killpg(process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    else:
                        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                                       capture_output=True, timeout=10, check=False)
                        if process.poll() is None:
                            process.kill()
                    stdout, stderr = process.communicate(timeout=10)
                result = subprocess.CompletedProcess(stage['command'], process.returncode, stdout, stderr)
            for kind, output in [('stdout', result.stdout), ('stderr', result.stderr)]:
                path = self.directory / 'logs' / (identity + '.' + kind)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(output)
                stage[kind] = str(path)
            stage['returncode'] = result.returncode
            if timed_out:
                raise RuntimeError('%s timed out after %s seconds; process tree stopped' % (identity, self.arguments.timeout))
            if result.returncode:
                raise RuntimeError('%s failed; see %s' % (identity, stage['stdout']))
            stage['status'] = 'PASS'
            return result
        except (OSError, subprocess.TimeoutExpired, RuntimeError) as error:
            stage['status'] = 'FAIL'
            stage['error'] = str(error)
            raise
        finally:
            stage['seconds'] = round(time.monotonic() - started, 3)
            self.save()

    def accept(self, details):
        self.report['stages'][-1]['details'] = details
        self.save()

    def accept_check(self, function, *arguments):
        try:
            self.accept(function(*arguments))
        except (ValueError, KeyError, RuntimeError) as error:
            self.report['stages'][-1]['status'] = 'FAIL'
            self.report['stages'][-1]['error'] = str(error)
            self.save()
            raise

    def execute(self):
        fingerprint = source_fingerprint()
        self.report['source_sha256'] = fingerprint
        reference = self.arguments.reference.resolve()
        version = self.run('reference', [reference, '-E', '-S', '-c',
            "import json,sys; sys.stdout.write(json.dumps({'version':list(sys.version_info[:3]),"
            "'implementation':sys.subversion[0],'maxunicode':sys.maxunicode,'maxint':sys.maxint,'platform':sys.platform}))"])
        self.accept_check(check_reference, version.stdout, version.stderr)
        manifest = json.loads((ROOT / 'tests/upstream/manifest.json').read_text())
        native_cases = json.loads((ROOT / 'tests/conformance/native_cases.json').read_text())
        inventory = json.loads((ROOT / 'tests/conformance/coverage.json').read_text())
        self.report['coverage_inventory'] = check_coverage_inventory(manifest, inventory)
        self.report['coverage'] = inventory
        self.report['selected_cases'] = len(portable_inventory(manifest))
        generated = self.directory / 'compiler-corpus'
        counts = write_corpus(generated)
        self.report['compiler_generated_sources'] = counts
        oracles = {}
        for name, count in RUNTIME_MATRICES:
            oracle = self.run(name[:-3] + '-reference', [reference, '-E', '-S', ROOT / 'tests/conformance' / name])
            if oracle.stderr:
                raise RuntimeError('runtime reference produced stderr')
            oracles[name] = oracle.stdout
        self.run('host-tools', [sys.executable, '-m', 'unittest', 'discover', '-s', 'tests/tools', '-v'])
        for profile in self.arguments.profile:
            build_type, sanitize, lto = PROFILES[profile]
            build = self.directory / profile
            flags = '-fsanitize=address,undefined -fno-omit-frame-pointer' if sanitize else ''
            self.run(profile + '-configure', [self.arguments.cmake, '-S', ROOT, '-B', build,
                '-DBUILD_TESTING=ON', '-DTINYPY_BUILD_CLI=ON', '-DCMAKE_BUILD_TYPE=' + build_type,
                '-DTINYPY_ENABLE_CYCLE_DIAGNOSTICS=ON',
                '-DTINYPY_ENABLE_LTO=' + ('ON' if lto else 'OFF'),
                '-DCMAKE_C_FLAGS=' + flags, '-DCMAKE_C_FLAGS_DEBUG=-g'])
            self.run(profile + '-build', [self.arguments.cmake, '--build', build, '--config', build_type,
                                          '--parallel', str(self.arguments.jobs)])
            executable = build / 'cli/tinypy'
            compiler = build / 'cli/tinypy_compile'
            if os.name == 'nt':
                executable = executable.with_suffix('.exe')
                compiler = compiler.with_suffix('.exe')
                if not executable.is_file():
                    executable = build / 'cli' / build_type / executable.name
                    compiler = build / 'cli' / build_type / compiler.name
            info = json.loads(self.run(profile + '-build-info', [executable, '--build-info']).stdout)
            self.accept_check(check_build_info, info, profile)
            discovery = self.run(profile + '-ctest-inventory', [self.arguments.ctest, '--test-dir', build,
                '-C', build_type, '--show-only=json-v1'])
            self.accept_check(check_ctest_inventory, json.loads(discovery.stdout), native_cases, manifest)
            native_report = build / 'native-results.xml'
            self.run(profile + '-ctest', [self.arguments.ctest, '--test-dir', build, '--output-on-failure',
                '-C', build_type,
                '--no-tests=error', '--timeout', '120', '--parallel', str(self.arguments.jobs),
                '--output-junit', native_report, '-E', '^upstream_'])
            self.accept_check(check_ctest_results, native_report, native_cases)
            portable = build / 'portable.json'
            self.run(profile + '-portable', [sys.executable, ROOT / 'tests/upstream/run_tests.py',
                '--tinypy', executable, '--reference', reference, '--jobs', str(self.arguments.jobs), '--report', portable])
            self.accept_check(check_portable, json.loads(portable.read_text()), manifest, profile)
            for name, count in RUNTIME_MATRICES:
                runtime = self.run(profile + '-' + name[:-3], [executable, '--stats', ROOT / 'tests/conformance' / name])
                self.accept_check(check_runtime, oracles[name], runtime.stdout, runtime.stderr, count)
            adapter = self.run(profile + '-adapter', [executable, '--stats', ROOT / 'tests/upstream/check_adapter.py'])
            self.accept_check(check_balance, adapter.stderr)
            for mode, count in counts.items():
                self.run(profile + '-compiler-' + mode, [sys.executable, ROOT / 'tests/compiler/run_differential.py',
                    '--compiler', compiler, '--reference', reference, '--source-root', generated / mode,
                    '--logical-root', generated, '--mode', mode, '--expected-count', str(count),
                    '--jobs', str(self.arguments.jobs), '--work-dir', build / ('compiler-' + mode)])
                self.accept({'sources': count, 'compilations': count * 3, 'optimize': [0, 1, 2]})
            source_roots = [ROOT / 'tests/upstream/vendor', ROOT / 'tests/upstream/local', ROOT / 'tests/runtime/fixtures']
            source_roots.extend(ROOT / 'tests/conformance' / name for name, _ in RUNTIME_MATRICES)
            source_count = sum(1 if path.is_file() else len(list(path.rglob('*.py'))) for path in source_roots)
            command = [sys.executable, ROOT / 'tests/compiler/run_differential.py', '--compiler', compiler,
                       '--reference', reference, '--logical-root', ROOT, '--expected-count', str(source_count),
                       '--jobs', str(self.arguments.jobs), '--work-dir', build / 'compiler-regressions']
            for source in source_roots:
                command.extend(['--source-root', source])
            self.run(profile + '-compiler-regressions', command)
            self.accept({'sources': source_count, 'compilations': source_count * 3, 'optimize': [0, 1, 2]})
            for subsystem in ('marshal', 'artifact'):
                command = [sys.executable, ROOT / ('tests/' + subsystem + '/run_tests.py'),
                           '--build-dir', build / (subsystem + '-standalone')]
                if sanitize:
                    command.append('--sanitize')
                self.run(profile + '-' + subsystem, command)
            archive = build / 'libtinypy.a'
            if os.name == 'nt':
                archive = build / 'tinypy.lib'
                if not archive.is_file():
                    archive = build / build_type / archive.name
            self.run(profile + '-symbols', [sys.executable, ROOT / 'tools/audit_core_symbols.py', archive])
        if source_fingerprint() != fingerprint:
            raise RuntimeError('source/test inputs changed during validation; rerun the matrix')
        self.report['status'] = 'PASS'
        self.save()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True, help='external CPython 2.7.18 executable')
    parser.add_argument('--build-root', type=Path, default=ROOT / '.temp/validation')
    parser.add_argument('--report', type=Path)
    parser.add_argument('--profile', choices=PROFILES, action='append')
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument('--timeout', type=float, default=900, help='maximum seconds per validation stage')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ctest', default='ctest')
    arguments = parser.parse_args(argv)
    if arguments.jobs < 1 or arguments.timeout <= 0:
        parser.error('--jobs and --timeout must be positive')
    arguments.profile = list(dict.fromkeys(arguments.profile or PROFILES))
    arguments.report = (arguments.report or arguments.build_root / 'report.json').resolve()
    validation = Validation(arguments)
    try:
        validation.execute()
    except (OSError, ValueError, KeyError, RuntimeError, ET.ParseError, subprocess.TimeoutExpired) as error:
        validation.report['status'] = 'FAIL'
        validation.report['error'] = str(error)
        for stage in validation.report['stages']:
            if stage['status'] == 'RUNNING':
                stage['status'] = 'FAIL'
        validation.save()
        print(str(error), file=sys.stderr)
        return 1
    print('validation passed; report: ' + str(arguments.report))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
