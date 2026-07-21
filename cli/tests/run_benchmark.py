#!/usr/bin/env python3

# CLI benchmark runner; this file is not part of the embedding runtime.

import argparse
import os
from pathlib import Path
import re
import statistics
import subprocess
import time


RSS_PATTERN = re.compile(r"([0-9]+)\s+maximum resident set size")
HEAP_PATTERN = re.compile(r"peak_heap_bytes=([0-9]+)")
OUTSTANDING_PATTERN = re.compile(r"outstanding_bytes=([0-9]+) outstanding_allocations=([0-9]+)")
PEAK_ALLOCATIONS_PATTERN = re.compile(r"peak_allocations=([0-9]+)")
TOTAL_ALLOCATIONS_PATTERN = re.compile(r"total_allocations=([0-9]+)")


def format_megabytes(value):
    if value is None:
        return "-"
    return "%.2f" % (value / (1024.0 * 1024.0))


def run_once(command, require_allocator_stats):
    environment = dict(os.environ)
    environment["LC_ALL"] = "C"
    timed_command = ["/usr/bin/time", "-l"] + [str(item) for item in command]
    begin = time.perf_counter()
    completed = subprocess.run(timed_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=environment)
    elapsed = time.perf_counter() - begin
    if completed.returncode != 0:
        raise RuntimeError("command failed (%d): %s\n%s" % (completed.returncode, " ".join(timed_command), completed.stderr))

    rss_match = RSS_PATTERN.search(completed.stderr)
    heap_match = HEAP_PATTERN.search(completed.stderr)
    outstanding_match = OUTSTANDING_PATTERN.search(completed.stderr)
    peak_allocations_match = PEAK_ALLOCATIONS_PATTERN.search(completed.stderr)
    total_allocations_match = TOTAL_ALLOCATIONS_PATTERN.search(completed.stderr)
    if rss_match is None:
        raise RuntimeError("unable to read maximum RSS from /usr/bin/time output:\n%s" % completed.stderr)
    if require_allocator_stats and (heap_match is None or outstanding_match is None or peak_allocations_match is None or total_allocations_match is None):
        raise RuntimeError("TinyPy allocator statistics are missing:\n%s" % completed.stderr)
    if require_allocator_stats and (outstanding_match.group(1) != "0" or outstanding_match.group(2) != "0"):
        raise RuntimeError("TinyPy allocator did not return to zero:\n%s" % completed.stderr)

    return {
        "seconds": elapsed,
        "rss": int(rss_match.group(1)),
        "heap": int(heap_match.group(1)) if heap_match is not None else None,
        "peak_allocations": int(peak_allocations_match.group(1)) if peak_allocations_match is not None else None,
        "total_allocations": int(total_allocations_match.group(1)) if total_allocations_match is not None else None,
        "stdout": completed.stdout.strip(),
    }


def benchmark(command, repetitions, warmups, require_allocator_stats):
    for _ in range(warmups):
        run_once(command, require_allocator_stats)

    samples = [run_once(command, require_allocator_stats) for _ in range(repetitions)]
    outputs = {sample["stdout"] for sample in samples}
    if len(outputs) != 1:
        raise RuntimeError("non-deterministic output: %r" % sorted(outputs))

    return {
        "seconds": statistics.median(sample["seconds"] for sample in samples),
        "rss": int(statistics.median(sample["rss"] for sample in samples)),
        "heap": int(statistics.median(sample["heap"] for sample in samples if sample["heap"] is not None)) if require_allocator_stats else None,
        "peak_allocations": int(statistics.median(sample["peak_allocations"] for sample in samples if sample["peak_allocations"] is not None)) if require_allocator_stats else None,
        "total_allocations": int(statistics.median(sample["total_allocations"] for sample in samples if sample["total_allocations"] is not None)) if require_allocator_stats else None,
        "stdout": samples[0]["stdout"],
    }


def main():
    cli_root = Path(__file__).resolve().parents[1]
    default_python = Path.home() / ".pyenv/versions/2.7.18/bin/python2.7"
    parser = argparse.ArgumentParser(description="Compare TinyPy and Python 2.7 using deterministic runtime, compiler and heap stress workloads.")
    parser.add_argument("--tinypy", type=Path, required=True)
    parser.add_argument("--python27", type=Path, default=default_python)
    parser.add_argument("--script", type=Path, default=cli_root / "tests/runtime_stress.py")
    parser.add_argument("--speed-count", type=int, default=1000000)
    parser.add_argument("--call-count", type=int, default=500000)
    parser.add_argument("--attribute-count", type=int, default=500000)
    parser.add_argument("--compile-count", type=int, default=20000)
    parser.add_argument("--churn-count", type=int, default=1000)
    parser.add_argument("--memory-count", type=int, default=100000)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    arguments = parser.parse_args()

    if arguments.repetitions < 1 or arguments.warmups < 0:
        parser.error("repetitions must be positive and warmups must be non-negative")
    if min(arguments.speed_count, arguments.call_count, arguments.attribute_count, arguments.compile_count, arguments.churn_count, arguments.memory_count) < 0:
        parser.error("workload counts must be non-negative")
    for executable in (arguments.tinypy, arguments.python27):
        if executable.is_file() is False:
            parser.error("executable does not exist: %s" % executable)
    if arguments.script.is_file() is False:
        parser.error("stress script does not exist: %s" % arguments.script)

    results = {}
    workloads = (
        ("speed", arguments.speed_count),
        ("calls", arguments.call_count),
        ("attributes", arguments.attribute_count),
        ("compiler", arguments.compile_count),
        ("churn", arguments.churn_count),
        ("memory", arguments.memory_count),
    )
    for workload, count in workloads:
        tinypy_command = [arguments.tinypy, "--stats", arguments.script, workload, str(count)]
        python_command = [arguments.python27, arguments.script, workload, str(count)]
        results[(workload, "TinyPy")] = benchmark(tinypy_command, arguments.repetitions, arguments.warmups, True)
        results[(workload, "Python 2.7")] = benchmark(python_command, arguments.repetitions, arguments.warmups, False)
        if results[(workload, "TinyPy")]["stdout"] != results[(workload, "Python 2.7")]["stdout"]:
            raise RuntimeError("result mismatch for %s:\nTinyPy: %s\nPython 2.7: %s" % (workload, results[(workload, "TinyPy")]["stdout"], results[(workload, "Python 2.7")]["stdout"]))

    print("workload   count     runtime      median_s  peak_rss_mib  tinypy_heap_mib  allocations_peak/total")
    for workload, count in workloads:
        for runtime in ("TinyPy", "Python 2.7"):
            result = results[(workload, runtime)]
            allocation_text = "%d/%d" % (result["peak_allocations"], result["total_allocations"]) if result["total_allocations"] is not None else "-"
            print("%-10s %9d %-12s %8.4f %13s %16s %23s" % (workload, count, runtime, result["seconds"], format_megabytes(result["rss"]), format_megabytes(result["heap"]), allocation_text))
        tinypy_result = results[(workload, "TinyPy")]
        python_result = results[(workload, "Python 2.7")]
        print("%-10s           ratio        %8.2fx %13.2fx" % (workload, tinypy_result["seconds"] / python_result["seconds"], tinypy_result["rss"] / float(python_result["rss"])))
        print("%-10s           output       %s" % (workload, tinypy_result["stdout"]))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
