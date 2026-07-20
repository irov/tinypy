#!/usr/bin/env python3
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Iterable, List, Optional, Sequence, Tuple


REFERENCE_VERSION = (2, 7, 18)


def reference_command(reference: Path, optimize: int, script: Path) -> List[str]:
    command = [str(reference)]
    if optimize == 1:
        command.append("-O")
    elif optimize == 2:
        command.append("-OO")
    command.extend(["-E", "-S", str(script)])
    return command


def run_command(command: Sequence[str]) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def validate_reference(reference: Path) -> None:
    command = [
        str(reference),
        "-E",
        "-S",
        "-c",
        "import sys; sys.stdout.write('%d.%d.%d' % sys.version_info[:3])",
    ]
    result = run_command(command)
    expected = ".".join(str(part) for part in REFERENCE_VERSION).encode("ascii")
    if result.returncode != 0:
        raise RuntimeError(
            "reference interpreter failed: "
            + result.stderr.decode("utf-8", errors="replace").strip()
        )
    if result.stdout != expected:
        raise RuntimeError(
            "reference interpreter must be %s, got %s"
            % (
                expected.decode("ascii"),
                result.stdout.decode("ascii", errors="replace"),
            )
        )


def collect_sources(roots: Iterable[Path], logical_root: Path) -> List[Tuple[Path, str]]:
    collected = {}
    logical_root = logical_root.resolve()
    for root in roots:
        root = root.resolve()
        if not root.exists():
            raise RuntimeError("source root does not exist: %s" % root)
        if root.is_file():
            candidates = [root]
        else:
            candidates = root.rglob("*.py")
        for source in candidates:
            source = source.resolve()
            try:
                logical = source.relative_to(logical_root).as_posix()
            except ValueError as exception:
                raise RuntimeError(
                    "source %s is outside logical root %s" % (source, logical_root)
                ) from exception
            collected[source] = logical
    return sorted(collected.items(), key=lambda item: item[1])


def decode_process_error(result: subprocess.CompletedProcess[bytes]) -> str:
    output = result.stderr if result.stderr else result.stdout
    return output.decode("utf-8", errors="replace").strip()


def compare_case(
    compiler: Path,
    reference: Path,
    reference_script: Path,
    work_dir: Path,
    source: Path,
    logical: str,
    mode: str,
    optimize: int,
) -> Optional[str]:
    identity = hashlib.sha256(
        ("%d\0%s\0%s" % (optimize, mode, logical)).encode("utf-8")
    ).hexdigest()
    expected_path = work_dir / (identity + ".expected")
    actual_path = work_dir / (identity + ".actual")
    expected_command = reference_command(reference, optimize, reference_script)
    expected_command.extend(
        ["compile", str(source), str(expected_path), logical, mode]
    )
    expected_result = run_command(expected_command)
    if expected_result.returncode != 0:
        return "mode=%s optimize=%d %s: reference compile failed: %s" % (
            mode,
            optimize,
            logical,
            decode_process_error(expected_result),
        )
    actual_result = run_command(
        [
            str(compiler),
            str(source),
            str(actual_path),
            logical,
            mode,
            str(optimize),
        ]
    )
    if actual_result.returncode != 0:
        return "mode=%s optimize=%d %s: TinyPy compile failed: %s" % (
            mode,
            optimize,
            logical,
            decode_process_error(actual_result),
        )
    if expected_path.read_bytes() == actual_path.read_bytes():
        expected_path.unlink()
        actual_path.unlink()
        return None
    compare_result = run_command(
        reference_command(reference, optimize, reference_script)
        + ["compare", str(expected_path), str(actual_path)]
    )
    difference = decode_process_error(compare_result)
    if compare_result.stdout:
        difference = compare_result.stdout.decode("utf-8", errors="replace").strip()
    if not difference:
        difference = "unable to describe marshal mismatch"
    return "mode=%s optimize=%d %s: %s" % (mode, optimize, logical, difference)


def execute(arguments: argparse.Namespace, work_dir: Path) -> int:
    if not arguments.compiler.is_file():
        raise RuntimeError("TinyPy compiler does not exist: %s" % arguments.compiler)
    if not arguments.reference_script.is_file():
        raise RuntimeError(
            "reference compiler script does not exist: %s"
            % arguments.reference_script
        )
    validate_reference(arguments.reference)
    sources = collect_sources(arguments.source_root, arguments.logical_root)
    if arguments.expected_count is not None and len(sources) != arguments.expected_count:
        raise RuntimeError(
            "expected %d source files, found %d"
            % (arguments.expected_count, len(sources))
        )
    cases = [
        (source, logical, mode, optimize)
        for optimize in arguments.optimize
        for mode in arguments.mode
        for source, logical in sources
    ]
    failures = []
    completed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=arguments.jobs) as executor:
        futures = [
            executor.submit(
                compare_case,
                arguments.compiler,
                arguments.reference,
                arguments.reference_script,
                work_dir,
                source,
                logical,
                mode,
                optimize,
            )
            for source, logical, mode, optimize in cases
        ]
        for future in concurrent.futures.as_completed(futures):
            failure = future.result()
            completed += 1
            if failure is not None:
                failures.append(failure)
                print("FAIL " + failure, flush=True)
            elif completed % arguments.progress_every == 0:
                print("checked %d/%d" % (completed, len(cases)), flush=True)
    if failures:
        print(
            "differential compiler check failed: %d/%d mismatches"
            % (len(failures), len(cases))
        )
        return 1
    print(
        "differential compiler check passed: %d sources, %d compilations"
        % (len(sources), len(cases))
    )
    return 0


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare TinyPy marshal-v2 output with Python 2.7.18"
    )
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument(
        "--reference-script",
        type=Path,
        default=Path(__file__).with_name("reference_compiler.py"),
    )
    parser.add_argument("--source-root", type=Path, action="append", required=True)
    parser.add_argument("--logical-root", type=Path, required=True)
    parser.add_argument("--expected-count", type=int)
    parser.add_argument(
        "--mode",
        choices=("exec", "eval", "single"),
        action="append",
        default=None,
    )
    parser.add_argument(
        "--optimize",
        type=int,
        choices=(0, 1, 2),
        action="append",
        default=None,
    )
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--progress-every", type=int, default=100)
    parser.add_argument("--work-dir", type=Path)
    arguments = parser.parse_args()
    if arguments.optimize is None:
        arguments.optimize = [0, 1, 2]
    if arguments.mode is None:
        arguments.mode = ["exec"]
    if arguments.jobs < 1:
        parser.error("--jobs must be at least 1")
    if arguments.progress_every < 1:
        parser.error("--progress-every must be at least 1")
    return arguments


def main() -> int:
    arguments = parse_arguments()
    try:
        if arguments.work_dir is not None:
            arguments.work_dir.mkdir(parents=True, exist_ok=True)
            return execute(arguments, arguments.work_dir)
        with tempfile.TemporaryDirectory(prefix="tinypy-differential-") as directory:
            return execute(arguments, Path(directory))
    except (OSError, RuntimeError) as exception:
        print("differential compiler check failed: %s" % exception, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
