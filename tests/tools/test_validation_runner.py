"""Acceptance failures must never become a successful validation report."""

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('validation_runner', ROOT / 'tests/run_validation.py')
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)
COMPILER_SPEC = importlib.util.spec_from_file_location('compiler_runner', ROOT / 'tests/compiler/run_differential.py')
COMPILER = importlib.util.module_from_spec(COMPILER_SPEC)
COMPILER_SPEC.loader.exec_module(COMPILER)


class PortableAcceptance(unittest.TestCase):
    def setUp(self):
        self.manifest = {'modules': {'local.example': {'cases': ['Test.ordinary', 'Test.cycle', 'Test.excluded']}},
                         'excluded_cases': {'local.example.Test.excluded': 'outside contract'},
                         'debug_cycle_cases': {'local.example.Test.cycle': 1}}
        self.report = {'counts': {'PASS': 2}, 'results': [
            {'case': 'local.example.Test.ordinary', 'status': 'PASS'},
            {'case': 'local.example.Test.cycle', 'status': 'PASS', 'mode': 'debug-cycle-adaptation'}]}

    def test_complete_debug_report(self):
        self.assertEqual(RUNNER.check_portable(self.report, self.manifest, 'debug')['PASS'], 2)

    def test_release_requires_skip_before_cycle_execution(self):
        self.report['results'][1] = {'case': 'local.example.Test.cycle', 'status': 'SKIP'}
        self.report['counts'] = {'PASS': 1, 'SKIP': 1}
        self.assertEqual(RUNNER.check_portable(self.report, self.manifest, 'release')['SKIP'], 1)
        with self.assertRaises(RuntimeError):
            RUNNER.check_portable(self.report, self.manifest, 'sanitize')

    def test_ordinary_skip_is_a_failure(self):
        self.report['results'][0]['status'] = 'SKIP'
        with self.assertRaisesRegex(RuntimeError, 'unexpected result'):
            RUNNER.check_portable(self.report, self.manifest, 'release')

    def test_missing_duplicate_and_unexpected_results_fail(self):
        for results in [self.report['results'][:1], self.report['results'] * 2,
                        self.report['results'] + [{'case': 'unknown', 'status': 'PASS'}]]:
            with self.subTest(results=results):
                report = dict(self.report, results=results)
                with self.assertRaisesRegex(RuntimeError, 'duplicate, missing or unexpected'):
                    RUNNER.check_portable(report, self.manifest, 'debug')

    def test_failure_defer_reference_failure_are_rejected(self):
        for status in ['FAIL', 'DEFER', 'REFERENCE_FAIL']:
            report = copy.deepcopy(self.report)
            report['results'][0]['status'] = status
            with self.subTest(status=status), self.assertRaises(RuntimeError):
                RUNNER.check_portable(report, self.manifest, 'debug')

    def test_summary_cannot_hide_failure_or_invent_passes(self):
        self.report['counts']['PASS'] = 50
        with self.assertRaisesRegex(RuntimeError, 'summary disagrees'):
            RUNNER.check_portable(self.report, self.manifest, 'debug')

    def test_cycle_pass_requires_detector_mode(self):
        del self.report['results'][1]['mode']
        with self.assertRaisesRegex(RuntimeError, 'Debug detector'):
            RUNNER.check_portable(self.report, self.manifest, 'debug')


class RuntimeAcceptance(unittest.TestCase):
    def setUp(self):
        self.output = b'a\tint:3\nb\terror:TypeError\n'
        self.stats = b'tinypy stats: peak_heap_bytes=50 outstanding_bytes=0 outstanding_allocations=0\n'

    def check(self, actual=None, stderr=None):
        with patch.object(RUNNER, 'RUNTIME_OUTCOMES', 2):
            return RUNNER.check_runtime(self.output, self.output if actual is None else actual,
                                        self.stats if stderr is None else stderr)

    def test_equal_complete_output_with_zero_balance(self):
        self.assertEqual(self.check()['outcomes'], 2)

    def test_changed_values_exceptions_missing_or_extra_lines_fail(self):
        for actual in [self.output.replace(b'int:3', b'long:3L'),
                       self.output.replace(b'TypeError', b'ValueError'),
                       self.output.splitlines(keepends=True)[0], self.output + b'c\tNone:None\n']:
            with self.subTest(actual=actual), self.assertRaises(RuntimeError):
                self.check(actual=actual)

    def test_leaks_and_unexpected_stderr_fail(self):
        for stderr in [b'', self.stats.replace(b'outstanding_bytes=0', b'outstanding_bytes=8'),
                       self.stats.replace(b'outstanding_allocations=0', b'outstanding_allocations=1'),
                       b'runtime error\n' + self.stats]:
            with self.subTest(stderr=stderr), self.assertRaises(RuntimeError):
                self.check(stderr=stderr)

    def test_duplicate_or_changed_reference_inventory_fails(self):
        self.output = b'a\tint:3\na\tint:4\n'
        with self.assertRaisesRegex(RuntimeError, 'inventory changed'):
            self.check()


class CompilerCorpusAcceptance(unittest.TestCase):
    def test_generated_sources_are_reproducible_and_inventory_is_fixed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            counts = RUNNER.write_corpus(path)
            expected = {source.relative_to(path): source.read_bytes() for source in path.rglob('*.py')}
            self.assertEqual(counts, {'exec': 444, 'eval': 420, 'single': 420})
            self.assertEqual(RUNNER.write_corpus(path), counts)
            self.assertEqual({source.relative_to(path): source.read_bytes() for source in path.rglob('*.py')}, expected)
            (path / 'stale.py').write_text('pass\n')
            with self.assertRaisesRegex(RuntimeError, 'unexpected generated compiler inputs'):
                RUNNER.write_corpus(path)

    def test_compiler_subprocess_timeout_is_enforced(self):
        with patch.object(COMPILER.subprocess, 'run', side_effect=subprocess.TimeoutExpired('compiler', 0.5)) as run:
            with self.assertRaises(subprocess.TimeoutExpired):
                COMPILER.run_command(['compiler'], timeout=0.5)
            self.assertEqual(run.call_args.kwargs['timeout'], 0.5)

    def test_empty_compiler_inventory_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            arguments = type('Args', (), dict(compiler=Path(__file__), reference_script=Path(__file__),
                              reference=Path(__file__), timeout=1, source_root=[path], logical_root=path))()
            with patch.object(COMPILER, 'validate_reference'), self.assertRaisesRegex(RuntimeError, 'corpus is empty'):
                COMPILER.execute(arguments, path)


class CompilerOutputAcceptance(unittest.TestCase):
    def check(self, reference_stdout=b'', reference_stderr=b'', actual_stdout=b'', actual_stderr=b''):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = path / 'case.py'
            source.write_text('pass\n')
            def compile_process(command, timeout):
                reference = command[0] == 'reference'
                output = Path(command[-3] if reference else command[2])
                output.write_bytes(b'byte-identical marshal payload')
                stdout = reference_stdout if reference else actual_stdout
                stderr = reference_stderr if reference else actual_stderr
                return subprocess.CompletedProcess(command, 0, stdout, stderr)
            with patch.object(COMPILER, 'run_command', side_effect=compile_process):
                return COMPILER.compare_case(Path('compiler'), Path('reference'), Path('reference_script'),
                                             path, source, 'case.py', 'exec', 0)

    def test_equal_payloads_with_quiet_children_pass(self):
        self.assertIsNone(self.check())

    def test_successful_tinypy_compiler_output_cannot_hide_behind_equal_payloads(self):
        for output in (b'unexpected diagnostic\n', b'\n'):
            for stream in ('actual_stdout', 'actual_stderr'):
                with self.subTest(stream=stream, output=output):
                    self.assertIn('tinypy compiler produced unexpected output', self.check(**{stream: output}))

    def test_successful_reference_output_is_a_failure(self):
        for stream in ('reference_stdout', 'reference_stderr'):
            with self.subTest(stream=stream):
                self.assertIn('reference compiler produced unexpected output',
                              self.check(**{stream: b'unexpected diagnostic\n'}))

    def test_reference_syntax_warning_is_not_silently_discarded(self):
        warning = b'case.py:1: SyntaxWarning: assertion is always true, perhaps remove parentheses?\n'
        self.assertIn('reference compiler produced unexpected output',
                      self.check(reference_stderr=warning, actual_stderr=warning))


class CoverageInventoryAcceptance(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.local = self.root / 'tests/upstream/local'
        self.conformance = self.root / 'tests/conformance'
        self.local.mkdir(parents=True)
        self.conformance.mkdir(parents=True)
        (self.local / 'test_example.py').write_text('pass\n')
        (self.conformance / 'example_matrix.py').write_text('pass\n')
        self.manifest = {'modules': {'local.test_example': {
            'path': 'local/test_example.py', 'origin': 'project-authored'}}}
        self.inventory = {'domains': [{'name': 'example', 'modules': ['local.test_example'],
                                      'tests': ['tests/upstream/local/test_example.py',
                                                'tests/conformance/example_matrix.py']}]}
        for name, value in [('ROOT', self.root), ('RUNTIME_MATRICES', [('example_matrix.py', 1)])]:
            patcher = patch.object(RUNNER, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def check(self):
        return RUNNER.check_coverage_inventory(self.manifest, self.inventory)

    def test_complete_source_registration_and_coverage_pass(self):
        self.assertEqual(self.check(), {'local_modules': 1, 'runtime_matrices': 1})
        (self.local / 'helper.py').write_text('pass\n')
        (self.conformance / 'compiler_cases.py').write_text('pass\n')
        self.assertEqual(self.check(), {'local_modules': 1, 'runtime_matrices': 1})

    def test_new_unregistered_local_source_fails(self):
        (self.local / 'test_new.py').write_text('pass\n')
        with self.assertRaisesRegex(RuntimeError, 'local test inventory changed.*unregistered=.*test_new'):
            self.check()

    def test_same_count_renamed_source_cannot_replace_registered_test(self):
        (self.local / 'test_example.py').rename(self.local / 'test_new.py')
        with self.assertRaisesRegex(RuntimeError, 'local test inventory changed'):
            self.check()

    def test_duplicate_registered_source_fails(self):
        self.manifest['modules']['local.test_other'] = dict(self.manifest['modules']['local.test_example'])
        with self.assertRaisesRegex(RuntimeError, 'local test inventory changed'):
            self.check()

    def test_module_name_and_origin_must_match_local_source(self):
        for module in ({'path': 'local/test_example.py', 'origin': 'vendor'},
                       {'path': 'local/test_example.py', 'origin': 'project-authored'}):
            name = 'local.test_example' if module['origin'] == 'vendor' else 'local.test_other'
            self.manifest['modules'] = {name: module}
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, 'module identity'):
                self.check()

    def test_registered_local_module_missing_from_coverage_fails(self):
        self.inventory['domains'][0]['modules'] = []
        with self.assertRaisesRegex(RuntimeError, 'local modules missing from coverage'):
            self.check()

    def test_new_unregistered_matrix_fails(self):
        (self.conformance / 'new_matrix.py').write_text('pass\n')
        with self.assertRaisesRegex(RuntimeError, 'runtime matrix inventory changed.*unregistered=.*new_matrix'):
            self.check()

    def test_missing_or_duplicate_declared_matrix_fails(self):
        for matrices in ([('missing_matrix.py', 1)], [('example_matrix.py', 1)] * 2):
            with patch.object(RUNNER, 'RUNTIME_MATRICES', matrices):
                with self.subTest(matrices=matrices), self.assertRaisesRegex(RuntimeError, 'runtime matrix inventory changed'):
                    self.check()

    def test_registered_matrix_missing_from_coverage_fails(self):
        self.inventory['domains'][0]['tests'] = ['tests/upstream/local/test_example.py']
        with self.assertRaisesRegex(RuntimeError, 'runtime matrices missing from coverage'):
            self.check()


class NativeAcceptance(unittest.TestCase):
    def setUp(self):
        self.manifest = {'modules': {'local.example': {}}}
        self.native = ['native_example']
        self.discovery = {'tests': [{'name': name} for name in
                          ['native_example', 'upstream_local_example', 'upstream_adapter_assertions']]}

    def test_exact_inventory(self):
        self.assertEqual(RUNNER.check_ctest_inventory(self.discovery, self.native, self.manifest)['registered'], 3)

    def test_missing_duplicate_or_disabled_registration_fails(self):
        for discovery in [dict(tests=self.discovery['tests'][1:]),
                          dict(tests=self.discovery['tests'] * 2),
                          dict(tests=[{'name': 'native_example', 'properties': [{'name': 'DISABLED', 'value': True}]}]
                               + self.discovery['tests'][1:])]:
            with self.subTest(discovery=discovery), self.assertRaises(RuntimeError):
                RUNNER.check_ctest_inventory(discovery, self.native, self.manifest)

    def check_xml(self, body):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'results.xml'
            path.write_text('<testsuites><testsuite>' + body + '</testsuite></testsuites>')
            return RUNNER.check_ctest_results(path, self.native)

    def test_complete_executed_junit(self):
        self.assertEqual(self.check_xml('<testcase name="native_example" status="run"/>')['PASS'], 1)

    def test_skips_errors_missing_or_duplicate_junit_fail(self):
        for body in ['', '<testcase name="native_example"><skipped/></testcase>',
                     '<testcase name="native_example"><failure/></testcase>',
                     '<testcase name="native_example"><error/></testcase>',
                     '<testcase name="native_example" status="notrun"/>',
                     '<testcase name="native_example"/>' * 2]:
            with self.subTest(body=body), self.assertRaises(RuntimeError):
                self.check_xml(body)


class ProcessAcceptance(unittest.TestCase):
    def test_reference_requires_cpython_version_not_only_version_number(self):
        info = {'version': [2, 7, 18], 'implementation': 'CPython'}
        self.assertEqual(RUNNER.check_reference(json.dumps(info).encode(), b''), info)
        for invalid in [dict(info, implementation='PyPy'), dict(info, version=[2, 7, 17])]:
            with self.subTest(invalid=invalid), self.assertRaises(RuntimeError):
                RUNNER.check_reference(json.dumps(invalid).encode(), b'')

    @unittest.skipUnless(RUNNER.os.name == 'posix', 'POSIX process groups')
    def test_timeout_stops_process_group_and_records_failure_with_output(self):
        with tempfile.TemporaryDirectory() as directory:
            arguments = SimpleNamespace(build_root=Path(directory), report=Path(directory) / 'report.json',
                                        reference=Path('reference'), profile=['debug'], timeout=0.1)
            validation = RUNNER.Validation(arguments)
            with patch.object(RUNNER.subprocess, 'Popen') as popen, patch.object(RUNNER.os, 'killpg') as kill:
                process = popen.return_value.__enter__.return_value
                process.pid = 1234
                process.returncode = -9
                process.communicate.side_effect = [subprocess.TimeoutExpired('stage', 0.1), (b'partial output', b'error')]
                with self.assertRaisesRegex(RuntimeError, 'process tree stopped'):
                    validation.run('timed-out', ['stage'])
                kill.assert_called_once_with(1234, RUNNER.signal.SIGKILL)
                self.assertTrue(popen.call_args.kwargs['start_new_session'])
            stage = validation.report['stages'][-1]
            self.assertEqual(stage['status'], 'FAIL')
            self.assertEqual(Path(stage['stdout']).read_bytes(), b'partial output')


if __name__ == '__main__':
    unittest.main()
