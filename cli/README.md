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
build/cli/cli/tinypy -tt script.py
```

The same build produces `build/cli/cli/tinypy_compile` for source-to-marshal
differential tests; it compiles each file as the string `compile()` reads,
like the reference script.

## Host behaviour

The CLI runs a program the way `python2.7` does:

- a script and the modules it imports are files, compiled with the file
  tokenizer rules of CPython (ASCII unless a PEP 263 cookie or a BOM declares
  the encoding); `-c` compiles its command as a string, and `-` or piped
  standard input is the file `<stdin>`;
- `sys.path[0]` is the resolved directory of the script, or `''` for a
  command and standard input; `__main__` has `__package__` `None`,
  `__builtins__` the `__builtin__` module and `__file__` only for a file;
- `-t` warns once per file about inconsistent tabs and spaces in indentation
  and `-tt` makes them a TabError, as in CPython; the default checks nothing;
  the flags apply to the main program;
- `sys.excepthook` and `sys.__excepthook__` are the host's PyErr_Display:
  an uncaught exception goes to `sys.excepthook` with the fallbacks of
  PyErr_PrintEx (`Error in sys.excepthook:`, `sys.excepthook is missing`),
  the report is written to the `sys.stderr` object (`lost sys.stderr` when it
  is None), honours `sys.tracebacklimit`, prints the location of a
  SyntaxError from its attributes after the traceback, and reports a failed
  `str()` as `<exception str() failed>`;
- an uncaught SystemExit ends the program with its int or long code, or
  writes any other code to `sys.stderr` and exits with 1;
- at exit the CLI runs `sys.exitfunc`, then releases the module namespaces in
  the order of PyImport_Cleanup (`__main__` first, `sys` last, values set to
  None) so finalizers and generator `finally` blocks run and `Exception ...
  ignored` reports print, then destroys the VM. Class objects and values that
  other references keep alive stay reachable for the VM sweep: without a
  cyclic collector the cycles they anchor would otherwise be reported as
  allocator leaks.

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
