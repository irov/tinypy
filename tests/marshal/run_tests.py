#!/usr/bin/env python3
"""Build and run the standalone C99 marshal reader tests.

This intentionally does not modify the root CMake graph while the marshal
subsystem is developed independently. All generated files stay below build/.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys


STRICT_FLAGS = (
    "-std=c99",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Wshadow",
    "-Wstrict-prototypes",
    "-Wmissing-prototypes",
    "-Werror",
)


def run(command: list[str], *, capture: bool = False) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )
    if completed.returncode != 0:
        if capture:
            if completed.stdout:
                sys.stderr.write(completed.stdout)
            if completed.stderr:
                sys.stderr.write(completed.stderr)
        raise RuntimeError("command failed: {}".format(" ".join(command)))
    return completed


def compile_artifacts(
    root: Path,
    build: Path,
    compiler: str,
    sanitize: bool,
) -> tuple[Path, Path, Path]:
    build.mkdir(parents=True, exist_ok=True)
    source = root / "src/marshal/marshal.c"
    test_source = root / "tests/marshal/test_marshal.c"
    validator_source = root / "tests/marshal/validate_marshal.c"
    include = root / "include"
    object_file = build / "tinypy_marshal.o"
    test_binary = build / "tinypy_marshal_tests"
    validator_binary = build / "tinypy_validate_marshal"
    flags = list(STRICT_FLAGS)
    if sanitize:
        flags.extend(("-fsanitize=address,undefined", "-fno-omit-frame-pointer"))

    run([compiler, *flags, "-I", os.fspath(include), "-c", os.fspath(source), "-o", os.fspath(object_file)])
    run(
        [
            compiler,
            *flags,
            "-I",
            os.fspath(include),
            os.fspath(source),
            os.fspath(test_source),
            "-o",
            os.fspath(test_binary),
        ]
    )
    run(
        [
            compiler,
            *flags,
            "-I",
            os.fspath(include),
            os.fspath(source),
            os.fspath(validator_source),
            "-o",
            os.fspath(validator_binary),
        ]
    )
    return object_file, test_binary, validator_binary


def validate_corpus(
    validator: Path,
    corpus: Path,
    expected_count: int | None,
    expected_sha256: str | None,
) -> None:
    files = sorted(path for path in corpus.rglob("*.marshal") if path.is_file())
    if expected_count is not None and len(files) != expected_count:
        raise RuntimeError(
            "expected {} marshal files, found {} below {}".format(
                expected_count,
                len(files),
                corpus,
            )
        )

    chunk_size = 128
    for start in range(0, len(files), chunk_size):
        chunk = files[start:start + chunk_size]
        run([os.fspath(validator), *(os.fspath(path) for path in chunk)], capture=True)

    digest = hashlib.sha256()
    for path in files:
        relative = path.relative_to(corpus).as_posix().encode("utf-8")
        digest.update(relative)
        digest.update(b"\0")
        with path.open("rb") as stream:
            while True:
                block = stream.read(1024 * 1024)
                if not block:
                    break
                digest.update(block)
        digest.update(b"\n")
    actual_sha256 = digest.hexdigest()
    if expected_sha256 is not None and actual_sha256 != expected_sha256:
        raise RuntimeError(
            "expected corpus SHA-256 {}, got {}".format(
                expected_sha256,
                actual_sha256,
            )
        )
    print(
        "exactly round-tripped {} corpus marshal-v2 code objects; "
        "aggregate_sha256={}".format(len(files), actual_sha256)
    )


def main(argv: list[str] | None = None) -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--build-dir", type=Path, default=root / "build/marshal-tests")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--corpus", type=Path)
    parser.add_argument("--expected-corpus-count", type=int)
    parser.add_argument("--expected-corpus-sha256")
    arguments = parser.parse_args(argv)

    try:
        object_file, test_binary, validator = compile_artifacts(
            root,
            arguments.build_dir,
            arguments.cc,
            arguments.sanitize,
        )
        run([os.fspath(test_binary)])
        run(
            [
                sys.executable,
                os.fspath(root / "tools/audit_core_symbols.py"),
                os.fspath(object_file),
            ]
        )
        if arguments.corpus is not None:
            validate_corpus(
                validator,
                arguments.corpus.resolve(),
                arguments.expected_corpus_count,
                arguments.expected_corpus_sha256,
            )
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
