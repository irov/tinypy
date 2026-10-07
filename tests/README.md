# Validation of the supported Python 2.7 contract

Run the complete repeatable matrix from the repository root:

```sh
python3 tests/run_validation.py --reference /path/to/python2.7.18
```

Python 3 coordinates build tools and subprocesses. All language fixtures and
the external oracle execute Python 2.7.18 semantics. The oracle executable is
required; a run without it cannot be reported as differential acceptance.

The default run builds Debug, Release, unoptimized Debug with ASan/UBSan, and
Release with LTO. Use `--profile debug` for a focused iteration, repeat
`--profile` to select several profiles, and `--jobs` to control concurrency.
`--build-root` chooses the artifact directory; the default is
`.temp/validation`. Every validation stage has a timeout. Logs and an incremental
JSON report remain there on success or failure. Only requested profiles appear
as tested. Compiler mismatches retain both marshal payloads. The report includes
an aggregate source/test/SPEC SHA-256 and refuses acceptance if inputs change during
the run. This coordinator needs CMake/CTest 3.26 or newer, a C compiler, `nm`
and a toolchain that supports the chosen sanitizer/LTO profiles.
Apple ASan does not support LeakSanitizer, so `detect_leaks=0` is selected on
macOS and recorded in the report. ASan/UBSan errors remain fatal, and tinypy's
per-process outstanding-allocation checks still run independently.

The run includes:

1. Host tool/API/runner acceptance checks.
2. Strict warnings-as-errors builds and native/runtime CTests, including the
   previously standalone opcode and bytecode-verifier tests.
3. Every selected portable case against external CPython 2.7.18, with discovery
   validation, exact source hashes for unchanged vendor modules, and zero
   allocator balance for each tinypy process.
4. A fixed matrix of 15,040 numeric and sequence outcomes. It compares values,
   result types and exception classes, including division, reflected builtin
   numeric combinations, infinities, NaN, subnormal/maximum finite doubles,
   bounded powers/shifts, conversions, slicing and
   mutable slice assignment/deletion. A second matrix covers 18,186 byte/Unicode
   builtin/percent-format outcomes across conversion, flag, width, precision and operand
   domains. The boundary matrix adds 4,082 numeric/parsing/text/format outcomes,
   and dynamic `compile()` adds 3,216 source/mode/flag/argument/diagnostic
   outcomes. Further products cover 222 generator/namespace observations,
   415 type/slot observations and 2,340 comparison/hash/truth observations
   including method-call traces. The regex matrix covers 171,200 observations
   across 32 valid patterns, 50 subjects, independent bounds, groups, scanner
   metadata and replacement/split counts. Additional products cover 6,432 compiler
   diagnostics, 1,312 descriptor/weakref observations, 1,504 iterator/aggregate
   observations and 726 text/constructor protocols. New products cover 568 brace-format
   observations, 499 functional consumers, 511 execution/import namespaces and
   1,091 module/class/introspection observations. Total: 227,344 unique outcomes per
   profile across sixteen matrices. Every identity must be unique, with a positive
   declared cardinality.
5. 1,284 generated valid source inputs: 444 `exec`, 420 `eval`, 420 `single`.
   Every input is compiled at optimization levels 0, 1 and 2 and must produce
   byte-identical marshal-v2 to CPython. The inputs cover literal/operator
   products, scopes/closures, comprehensions, decorators, futures, classes,
   exception/loop/context-manager blocks and Python 2 syntax. All vendor,
   local, runtime and product-matrix fixture sources also undergo the three-level compiler
   comparison in `exec` mode.
   These compiler product sources must compile with quiet stdout and stderr on
   both interpreters; matching payloads cannot hide warnings or other diagnostics.
   Warning behavior is tested separately in runtime callback fixtures.
6. Standalone marshal/artifact tests, sanitizer checks and symbol audits of
   those components and the core archive.

The portable report is rejected for missing/duplicate cases, ordinary skips,
DEFER, reference failures, or inconsistent counts. Only the four declared
ownership-cycle adaptations may skip in Release; Debug must exercise the
detector. Python-visible conversion errors, callbacks and error ordering remain
part of Release acceptance. Diagnostic invariant checks can compile out.
The CTest inventory is also exact, recorded in `conformance/native_cases.json`;
JUnit results must contain every selected test and no skips. Timeouts stop the
stage's process tree, preserving captured output and a failed report.
Reverse inventories require every local test file and runtime matrix to be
registered and assigned to a coverage domain.

[TEST_PLAN.md](conformance/TEST_PLAN.md) specifies entry points, operand and
argument variants, mutation/lifetime scenarios and acceptance gates for future
changes. [coverage.json](conformance/coverage.json) maps semantic domains to permanent
fixtures and lists test axes and declared boundaries from [SPEC.md](../SPEC.md).
This is a coverage model and an extension point, not proof that every combination
of arbitrary Python programs was tested. The product domains are complete for
their listed finite operands; protocol tests use explicit regression witnesses.
Platform-specific behavior needs the same run on the target platform.

For a new discrepancy, add a small project-authored case to the relevant local
module, first confirm its expectations on CPython 2.7.18, and update the exact
case inventory in `tests/upstream/manifest.json`. Extend a product domain when
the failure depends on operand combinations. Keep the observation responsible
for the failure: returned type/value, exception, callback order, mutation or
lifetime. Run the affected case during iteration and the full matrix before
accepting a runtime change. Record material limitations in the coverage model.

The existing compiler differential runner can also check a separately curated
stdlib/application corpus with `--source-root`, `--logical-root` and
`--expected-count`. It rejects empty corpora. Host-supplied modules and codecs
must be available when running application semantics.
