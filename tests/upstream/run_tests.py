#!/usr/bin/env python3
"""Run the pinned portable corpus without installing upstream test frameworks."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parent
ACCOUNTING = re.compile(r"outstanding_bytes=(\d+) outstanding_allocations=(\d+)")
STATS_LINE = re.compile(r"^tinypy stats:.*(?:\n|$)", re.MULTILINE)
CYCLE_HEADER = re.compile(r"^\[tinypy cycle\] cycle \d+ contains \d+ unreachable objects?; break one owning edge listed below$", re.MULTILINE)
CYCLE_LINE = re.compile(
    r"^(?:\[tinypy cycle\] .*|  object #\d+: .*|    created at .*|"
    r"    owning edge: .*|      candidate break site at .*)(?:\n|$)",
    re.MULTILINE,
)


def invoke(command, timeout):
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        return -1, "", "test timed out after %s seconds" % timeout


def run_case(case, arguments, manifest):
    name, path = case
    identity = name + "." + path
    cycle_count = manifest.get("debug_cycle_cases", {}).get(identity)
    if cycle_count is not None:
        if not arguments.build_info["debug"]:
            return {"case": identity, "status": "SKIP", "detail": "Cycle diagnostic adaptation runs only in Debug."}
        if not arguments.build_info["cycle_diagnostics"]:
            return {"case": identity, "status": "FAIL", "detail": "Debug cycle tests require TINYPY_ENABLE_CYCLE_DIAGNOSTICS=ON."}
    runner = str(ROOT / "run_case.py")
    expected_output = None
    expected_error = ""
    if arguments.reference is not None:
        code, output, error = invoke(
            [str(arguments.reference), "-E", "-S", runner, "reference", name, path],
            arguments.timeout,
        )
        if code != 0:
            return {"case": identity, "status": "REFERENCE_FAIL", "detail": error or output}
        expected_output = output
        expected_error = error
    command = [str(arguments.tinypy), "--stats"]
    if cycle_count is not None:
        command.append("--cycle-diagnostics")
    command.extend([runner, "tinypy-cycle" if cycle_count is not None else "tinypy", name, path])
    code, output, error = invoke(
        command,
        arguments.timeout,
    )
    if code != 0:
        return {"case": identity, "status": "FAIL", "detail": "exit status %d\n%s%s" % (code, output, error)}
    accounting = ACCOUNTING.search(error)
    if accounting is None or accounting.groups() != ("0", "0"):
        return {"case": identity, "status": "FAIL", "detail": "allocator did not return to zero:\n" + error}
    diagnostic = STATS_LINE.sub("", error)
    if cycle_count is not None:
        if len(CYCLE_HEADER.findall(diagnostic)) != cycle_count:
            return {"case": identity, "status": "FAIL", "detail": "unexpected cycle diagnostic count:\n" + diagnostic}
        if cycle_count and ("owning edge:" not in diagnostic or "cycle_cases.py:" not in diagnostic):
            return {"case": identity, "status": "FAIL", "detail": "cycle report lacks owning edges or source locations:\n" + diagnostic}
        diagnostic = CYCLE_LINE.sub("", diagnostic)
    if diagnostic != expected_error:
        return {"case": identity, "status": "FAIL", "detail": "unexpected stderr:\n" + diagnostic}
    if expected_output is not None and output != expected_output:
        return {"case": identity, "status": "FAIL", "detail": "stdout differs: tinypy=%r reference=%r" % (output, expected_output)}
    result = {"case": identity, "status": "PASS", "detail": ""}
    if cycle_count is not None:
        result["mode"] = "debug-cycle-adaptation"
        result["detail"] = "Debug adaptation: %d detected cycles; explicit cleanup; zero allocator balance." % cycle_count
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tinypy", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--module", action="append")
    parser.add_argument("--case", help="exact module.Class.method or module.test_function")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--list-modules", action="store_true")
    arguments = parser.parse_args()
    manifest = json.loads((ROOT / "manifest.json").read_text())
    if arguments.list_modules:
        print("\n".join(manifest["modules"]))
        return 0
    if arguments.tinypy is None:
        parser.error("--tinypy is required")
    if arguments.jobs < 1 or arguments.timeout <= 0:
        parser.error("--jobs and --timeout must be positive")
    arguments.tinypy = arguments.tinypy.resolve()
    code, output, error = invoke([str(arguments.tinypy), "--build-info"], arguments.timeout)
    if code != 0:
        raise RuntimeError("cannot query tinypy build capabilities:\n" + (error or output))
    arguments.build_info = json.loads(output)
    if arguments.reference is not None:
        arguments.reference = arguments.reference.resolve()
        result = subprocess.run(
            [str(arguments.reference), "-E", "-S", "-c", "import sys; print sys.version_info[:3]"],
            capture_output=True, text=True, timeout=arguments.timeout,
        )
        if result.returncode != 0 or result.stdout.strip() != "(2, 7, 18)":
            raise RuntimeError("the reference must be CPython 2.7.18")
    modules = arguments.module or list(manifest["modules"])
    unknown = set(modules) - set(manifest["modules"])
    if unknown:
        parser.error("unknown modules: " + ", ".join(sorted(unknown)))
    cases = []
    identities = {
        name + "." + path
        for name, specification in manifest["modules"].items()
        for path in specification["cases"]
    }
    excluded_cases = manifest.get("excluded_cases", {})
    stale_excluded = set(excluded_cases) - identities
    if stale_excluded:
        raise RuntimeError("unknown excluded cases: " + ", ".join(sorted(stale_excluded)))
    cycle_cases = set(manifest.get("debug_cycle_cases", {}))
    if cycle_cases - identities or cycle_cases & set(excluded_cases):
        raise RuntimeError("invalid debug cycle case manifest")
    for name in modules:
        specification = manifest["modules"][name]
        source = ROOT / specification["path"]
        if specification.get("origin") == "project-authored":
            if not name.startswith("local.") or source.parent != ROOT / "local":
                raise RuntimeError("invalid project-authored module: " + name)
        elif hashlib.sha256(source.read_bytes()).hexdigest() != specification["sha256"]:
            raise RuntimeError("upstream source changed: " + name)
        if not specification["cases"]:
            raise RuntimeError("empty upstream module: " + name)
        code, output, error = invoke(
            [str(arguments.tinypy), str(ROOT / "run_case.py"), "tinypy", name, "--list"],
            arguments.timeout,
        )
        if code != 0 or output.splitlines() != specification["cases"]:
            raise RuntimeError("upstream discovery changed: %s\n%s" % (name, error or output))
        cases.extend((name, path) for path in specification["cases"] if name + "." + path not in excluded_cases)
    if arguments.case is not None:
        if arguments.case in excluded_cases:
            parser.error("case outside the tinypy corpus: " + excluded_cases[arguments.case])
        cases = [case for case in cases if ".".join(case) == arguments.case]
        if not cases:
            parser.error("unknown case: " + arguments.case)
    with ThreadPoolExecutor(max_workers=arguments.jobs) as pool:
        results = list(pool.map(lambda case: run_case(case, arguments, manifest), cases))
    counts = {}
    for result in results:
        status = result["status"]
        counts[status] = counts.get(status, 0) + 1
        print("%s %s" % (status, result["case"]))
        if result["detail"]:
            print(result["detail"].rstrip())
    print("upstream: %d cases; %s" % (len(results), ", ".join("%s=%d" % item for item in sorted(counts.items()))))
    if arguments.report is not None:
        arguments.report.parent.mkdir(parents=True, exist_ok=True)
        arguments.report.write_text(json.dumps({"counts": counts, "results": results, "excluded_cases": excluded_cases}, indent=2) + "\n")
    return int(any(result["status"] not in ("PASS", "SKIP") for result in results))


if __name__ == "__main__":
    sys.exit(main())
