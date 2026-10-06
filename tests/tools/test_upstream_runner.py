"""Acceptance boundaries for corpus selection and Debug cycle adaptations."""

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("upstream_runner", ROOT / "tests/upstream/run_tests.py")
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)
CASE = ("cpython.test_enumerate", "TestReversed.test_gc")
IDENTITY = ".".join(CASE)
STATS = "tinypy stats: outstanding_bytes=0 outstanding_allocations=0\n"
DIAGNOSTIC = (
    "[tinypy cycle] cycle 1 contains 2 unreachable objects; break one owning edge listed below\n"
    "  object #1: instance, refcount=1\n"
    "    created at cycle_cases.py:20 in create\n"
    "    owning edge: object #1 -> object #2\n"
)


class UpstreamCycleAcceptance(unittest.TestCase):
    def setUp(self):
        self.arguments = SimpleNamespace(
            tinypy=Path("tinypy"), reference=None, timeout=1,
            build_info={"debug": True, "cycle_diagnostics": True},
        )
        self.manifest = {"excluded_cases": {}, "debug_cycle_cases": {IDENTITY: 1}}

    def run_result(self, code=0, output="", error=DIAGNOSTIC + STATS):
        with patch.object(RUNNER, "invoke", return_value=(code, output, error)) as invoke:
            result = RUNNER.run_case(CASE, self.arguments, self.manifest)
            return result, invoke

    def test_release_skips_without_executing_fixture(self):
        self.arguments.build_info = {"debug": False, "cycle_diagnostics": False}
        result, invoke = self.run_result()
        self.assertEqual(result["status"], "SKIP")
        invoke.assert_not_called()

    def test_debug_without_detector_fails_without_executing_fixture(self):
        self.arguments.build_info["cycle_diagnostics"] = False
        result, invoke = self.run_result()
        self.assertEqual(result["status"], "FAIL")
        invoke.assert_not_called()

    def test_detected_cycle_with_zero_balance_passes_as_adaptation(self):
        result, invoke = self.run_result()
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["mode"], "debug-cycle-adaptation")
        self.assertIn("--cycle-diagnostics", invoke.call_args[0][0])
        self.assertIn("tinypy-cycle", invoke.call_args[0][0])

    def test_missing_detection_fails(self):
        result, unused = self.run_result(error=STATS)
        self.assertEqual(result["status"], "FAIL")

    def test_missing_edges_or_source_location_fails(self):
        for error in (DIAGNOSTIC.replace("owning edge:", "unknown edge:"),
                      DIAGNOSTIC.replace("cycle_cases.py:", "unknown.py:")):
            with self.subTest(error=error):
                result, unused = self.run_result(error=error + STATS)
                self.assertEqual(result["status"], "FAIL")

    def test_allocator_leak_is_not_accepted_as_a_detected_cycle(self):
        result, unused = self.run_result(error=DIAGNOSTIC + STATS.replace("outstanding_bytes=0", "outstanding_bytes=128"))
        self.assertEqual(result["status"], "FAIL")

    def test_extra_stderr_and_nonzero_exit_fail(self):
        for code, error in ((0, DIAGNOSTIC + STATS + "unexpected warning\n"),
                            (1, DIAGNOSTIC + STATS)):
            with self.subTest(code=code):
                result, unused = self.run_result(code=code, error=error)
                self.assertEqual(result["status"], "FAIL")


class UpstreamCorpusSelection(unittest.TestCase):
    def setUp(self):
        self.module = "local.selection"
        self.excluded = self.module + ".Selection.test_excluded"
        self.manifest = {
            "modules": {self.module: {
                "path": "local/selection.py", "origin": "project-authored",
                "cases": ["Selection.test_excluded", "Selection.test_kept"],
            }},
            "excluded_cases": {self.excluded: "CPython implementation detail."},
        }
        self.build_info = json.dumps({"debug": False, "cycle_diagnostics": False})
        self.discovery = "Selection.test_excluded\nSelection.test_kept\n"

    def invoke_main(self, results, extra_arguments=()):
        self.output = io.StringIO()
        with patch("sys.argv", ["run_tests.py", "--tinypy", "tinypy", "--jobs", "1"] + list(extra_arguments)), \
                patch.object(Path, "read_text", return_value=json.dumps(self.manifest)), \
                patch.object(RUNNER, "invoke", side_effect=results) as invoke, \
                redirect_stdout(self.output), redirect_stderr(io.StringIO()):
            result = RUNNER.main()
        return result, invoke

    def test_excluded_original_is_not_run_or_reported_as_deferred(self):
        result, invoke = self.invoke_main([
            (0, self.build_info, ""), (0, self.discovery, ""), (0, "", STATS),
        ])
        self.assertEqual(result, 0)
        self.assertEqual(invoke.call_count, 3)
        self.assertEqual(invoke.call_args[0][0][-1], "Selection.test_kept")
        self.assertNotIn(self.excluded, self.output.getvalue())
        self.assertIn("1 cases; PASS=1", self.output.getvalue())

    def test_exclusion_does_not_hide_missing_discovery(self):
        with self.assertRaisesRegex(RuntimeError, "discovery changed"):
            self.invoke_main([(0, self.build_info, ""), (0, "Selection.test_kept\n", "")])

    def test_unknown_exclusion_fails(self):
        self.manifest["excluded_cases"][self.module + ".Selection.test_unknown"] = "Unknown."
        with self.assertRaisesRegex(RuntimeError, "unknown excluded cases"):
            self.invoke_main([(0, self.build_info, "")])

    def test_explicit_excluded_case_is_rejected(self):
        with self.assertRaises(SystemExit) as error:
            self.invoke_main([(0, self.build_info, ""), (0, self.discovery, "")], ["--case", self.excluded])
        self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
