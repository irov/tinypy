# TinyPy CLI library

`tinypy_cli` is an optional library built on the public `tinypy::tinypy` C ABI.
It owns filesystem access, terminal I/O, source-module resolution, process
arguments and command-line allocator statistics. None of its sources are part
of the embedding `tinypy` target.

Enable it explicitly:

```sh
cmake -S . -B build/cli \
    -DCMAKE_BUILD_TYPE=Release \
    -DTINYPY_BUILD_CLI=ON
cmake --build build/cli -j

build/cli/cli/tinypy
build/cli/cli/tinypy -c 'print 6 * 7'
build/cli/cli/tinypy --stats script.py argument
```

The same build produces `build/cli/cli/tinypy_compile` for source-to-marshal
differential tests.

The executable `main` files only forward to `tinypy_cli_run` and
`tinypy_cli_compile_run`; the implementation remains in `cli/src/`.

Compare the Release CLI with Python 2.7.18 using deterministic runtime,
function-call, attribute/method, compiler, allocation-churn and retained-memory
workloads:

```sh
python3 cli/tests/run_benchmark.py --tinypy build/cli/cli/tinypy
```

The runner verifies identical output, reports median wall time and peak RSS,
and requires TinyPy allocator accounting to return to zero after every run.
