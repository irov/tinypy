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
    parser.add_argument("--build-dir", type=Path, default=root / "build/runtime-fixtures")
    arguments = parser.parse_args()
    arguments.build_dir.mkdir(parents=True, exist_ok=True)
    source = root / "tests/runtime/fixtures/class_method.py"
    artifact = arguments.build_dir / "class_method.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/class_method.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/with_statement.py"
    artifact = arguments.build_dir / "with_statement.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/with_statement.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/generator.py"
    artifact = arguments.build_dir / "generator.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/generator.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/set_runtime.py"
    artifact = arguments.build_dir / "set_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/set_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/output_runtime.py"
    artifact = arguments.build_dir / "output_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/output_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-output", str(artifact), "alpha 42\ntail continued\n", "error 7\n"], check=True)
    source = root / "tests/runtime/fixtures/constructors_runtime.py"
    artifact = arguments.build_dir / "constructors_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/constructors_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/container_methods.py"
    artifact = arguments.build_dir / "container_methods.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/container_methods.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/string_methods.py"
    artifact = arguments.build_dir / "string_methods.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/string_methods.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/metaclass_runtime.py"
    artifact = arguments.build_dir / "metaclass_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/metaclass_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/buffer_runtime.py"
    artifact = arguments.build_dir / "buffer_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/buffer_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/old_class_runtime.py"
    artifact = arguments.build_dir / "old_class_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/old_class_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/slots_runtime.py"
    artifact = arguments.build_dir / "slots_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/slots_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/multislots_runtime.py"
    artifact = arguments.build_dir / "multislots_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/multislots_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/bytearray_runtime.py"
    artifact = arguments.build_dir / "bytearray_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/bytearray_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/weakref_runtime.py"
    artifact = arguments.build_dir / "weakref_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/weakref_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/tuple_subclass_runtime.py"
    artifact = arguments.build_dir / "tuple_subclass_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/tuple_subclass_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/dict_view_runtime.py"
    artifact = arguments.build_dir / "dict_view_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/dict_view_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/codecs_runtime.py"
    artifact = arguments.build_dir / "codecs_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/codecs_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/metaclass_checks_runtime.py"
    artifact = arguments.build_dir / "metaclass_checks_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/metaclass_checks_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/finalizer_runtime.py"
    artifact = arguments.build_dir / "finalizer_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/finalizer_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/functools_runtime.py"
    artifact = arguments.build_dir / "functools_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/functools_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/struct_runtime.py"
    artifact = arguments.build_dir / "struct_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/struct_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    source = root / "tests/runtime/fixtures/sys_runtime.py"
    artifact = arguments.build_dir / "sys_runtime.marshal"
    subprocess.run([str(arguments.compiler), str(source), str(artifact), "tests/runtime/fixtures/sys_runtime.py", "exec", "0"], check=True)
    subprocess.run([str(arguments.runner), "--eval-any", str(artifact)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
