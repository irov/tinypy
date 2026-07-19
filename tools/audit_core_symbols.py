#!/usr/bin/env python3
"""Reject forbidden host/runtime dependencies in the TinyPy C core.

The runtime is deliberately memory-only.  This check operates on the linked
static archive (or object files) so indirect uses introduced by later source
changes cannot hide behind wrappers or macros.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Sequence, Set


FORBIDDEN_EXACT = frozenset(
    {
        # Allocation must go through tinypy_allocator_t.
        "malloc",
        "calloc",
        "realloc",
        "free",
        "aligned_alloc",
        "posix_memalign",
        "valloc",
        # No direct filesystem or stdio surface.
        "fopen",
        "fopen64",
        "freopen",
        "fclose",
        "fread",
        "fwrite",
        "fprintf",
        "printf",
        "puts",
        "putchar",
        "scanf",
        "open",
        "open64",
        "close",
        "read",
        "write",
        "pread",
        "pwrite",
        "lseek",
        "stat",
        "lstat",
        "fstat",
        "access",
        "unlink",
        "rename",
        "opendir",
        "readdir",
        "closedir",
        # No ambient process, environment, locale, or termination API.
        "getenv",
        "setenv",
        "unsetenv",
        "putenv",
        "setlocale",
        "localeconv",
        "exit",
        "_exit",
        "quick_exit",
        "abort",
        "system",
        "popen",
        "pclose",
        "fork",
        "execve",
        # No hidden non-local control flow.
        "setjmp",
        "longjmp",
        "sigsetjmp",
        "siglongjmp",
    }
)

FORBIDDEN_PREFIXES = (
    "pthread_",
    "thrd_",
    "mtx_",
    "cnd_",
    "sem_",
)

FORBIDDEN_DEFINED_PREFIXES = (
    "py_",
    "__py_",
    "Py",
    "_Py",
)


def normalize_symbol(symbol: str) -> str:
    """Normalize Mach-O's leading underscore without masking C `_exit`."""

    if symbol.startswith("___"):
        # Compiler/runtime helper such as ___memcpy_chk -> __memcpy_chk.
        return symbol[1:]
    if symbol.startswith("_") and not symbol.startswith("__"):
        return symbol[1:]
    return symbol


def parse_nm_undefined(output: str) -> Set[str]:
    symbols: Set[str] = set()
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped or stripped.endswith(":"):
            continue

        # GNU/LLVM/BSD nm all put `U` immediately before the symbol for an
        # undefined reference; optional addresses/file prefixes may precede it.
        match = re.search(r"(?:^|\s)U\s+([^\s]+)$", stripped)
        if match is not None:
            symbols.add(normalize_symbol(match.group(1)))
    return symbols


def parse_nm_defined(output: str) -> Set[str]:
    symbols: Set[str] = set()
    for line in output.splitlines():
        stripped = line.strip()
        if not stripped or stripped.endswith(":"):
            continue
        match = re.search(
            r"(?:^|\s)([A-TV-Za-tv-z])\s+([^\s]+)$", stripped
        )
        if match is not None and match.group(1).upper() != "U":
            symbols.add(normalize_symbol(match.group(2)))
    return symbols


def run_nm(nm: str, paths: Sequence[Path], undefined_only: bool) -> str:
    arguments = [nm]
    if undefined_only:
        arguments.append("-u")
    else:
        arguments.append("-g")
    arguments.extend(str(path) for path in paths)

    completed = subprocess.run(
        arguments,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError("nm failed: {}".format(detail))
    return completed.stdout


def forbidden_symbols(symbols: Iterable[str]) -> list[str]:
    rejected = []
    for symbol in symbols:
        if symbol in FORBIDDEN_EXACT or symbol.startswith(FORBIDDEN_PREFIXES):
            rejected.append(symbol)
    return sorted(set(rejected))


def forbidden_defined_symbols(symbols: Iterable[str]) -> list[str]:
    """Reject compiler implementation symbols inherited from CPython."""

    return sorted(
        symbol
        for symbol in set(symbols)
        if symbol.startswith(FORBIDDEN_DEFINED_PREFIXES)
    )


def audit(nm: str, paths: Sequence[Path]) -> list[str]:
    undefined = parse_nm_undefined(run_nm(nm, paths, True))
    defined = parse_nm_defined(run_nm(nm, paths, False))
    external = undefined.difference(defined)
    return sorted(
        set(forbidden_symbols(external)).union(forbidden_defined_symbols(defined))
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", nargs="+", type=Path)
    parser.add_argument("--nm", default="nm", help="nm-compatible executable")
    arguments = parser.parse_args(argv)

    missing = [path for path in arguments.artifacts if not path.is_file()]
    if missing:
        for path in missing:
            print("missing artifact: {}".format(path), file=sys.stderr)
        return 2

    try:
        rejected = audit(arguments.nm, arguments.artifacts)
    except (OSError, RuntimeError) as exception:
        print(str(exception), file=sys.stderr)
        return 2

    if rejected:
        print("forbidden TinyPy core symbols:", file=sys.stderr)
        for symbol in rejected:
            print("  {}".format(symbol), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
