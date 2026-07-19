#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, default=root / "build/runtime-import-fixtures")
    arguments = parser.parse_args()
    arguments.build_dir.mkdir(parents=True, exist_ok=True)
    fixture_root = root / "tests/runtime/fixtures/imports"
    modules = [
        ("main", fixture_root / "main.py"),
        ("helper", fixture_root / "helper.py"),
        ("package", fixture_root / "package/__init__.py"),
        ("package.child", fixture_root / "package/child.py"),
        ("package.fallback", fixture_root / "package/fallback.py"),
        ("package.failure", fixture_root / "package/failure.py"),
        ("package.implicit", fixture_root / "package/implicit.py"),
        ("package.sibling", fixture_root / "package/sibling.py"),
        ("package.broken_target", fixture_root / "package/broken_target.py"),
        ("absolute_only", fixture_root / "absolute_only.py"),
        ("broken_target", fixture_root / "broken_target.py"),
        ("star_module", fixture_root / "star_module.py"),
    ]
    artifacts = []
    for module_name, source in modules:
        artifact = arguments.build_dir / (module_name.replace(".", "_") + ".marshal")
        subprocess.run([str(arguments.compiler), str(source), str(artifact), str(source.relative_to(root)), "exec", "0"], check=True)
        artifacts.append(artifact)
    subprocess.run([str(arguments.runner), "--eval-import", *(str(artifact) for artifact in artifacts)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
