#!/usr/bin/env python3
"""Build, sanitize, and symbol-audit the standalone artifact codec."""

from __future__ import annotations

import argparse
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


def run(command: list[str]) -> None:
    completed = subprocess.run(command, check=False)
    if completed.returncode != 0:
        raise RuntimeError("command failed: {}".format(" ".join(command)))


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--build-dir", type=Path, default=root / "build/artifact-tests")
    parser.add_argument("--sanitize", action="store_true")
    arguments = parser.parse_args()
    build = arguments.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    flags = list(STRICT_FLAGS)
    if arguments.sanitize:
        flags.extend(("-fsanitize=address,undefined", "-fno-omit-frame-pointer"))

    include_flags = [
        "-I",
        str(root / "include"),
        "-I",
        str(root / "src/artifact"),
    ]
    sources = [
        root / "src/artifact/sha256.c",
        root / "src/artifact/artifact.c",
    ]
    objects = []
    try:
        for source in sources:
            output = build / (source.stem + ".o")
            run(
                [
                    arguments.cc,
                    *flags,
                    *include_flags,
                    "-c",
                    str(source),
                    "-o",
                    str(output),
                ]
            )
            objects.append(output)

        binary = build / "tinypy_artifact_tests"
        run(
            [
                arguments.cc,
                *flags,
                *include_flags,
                *(str(source) for source in sources),
                str(root / "tests/artifact/test_artifact.c"),
                "-o",
                str(binary),
            ]
        )
        run([str(binary)])
        run(
            [
                sys.executable,
                str(root / "tools/audit_core_symbols.py"),
                *(str(item) for item in objects),
            ]
        )
    except (OSError, RuntimeError) as exception:
        print(exception, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
