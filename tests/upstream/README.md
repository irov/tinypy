# Portable Python 2 tests

This corpus vendors 20 complete CPython 2.7.18 modules with 221 discovered
cases, including inherited TestCase methods, and adds six project-authored
modules with 349 cases. Upstream files are byte-for-byte unchanged.
`manifest.json` records source commits, paths, SHA-256 hashes, case names and
explicit reasons for deferring individual cases. Locally authored modules
have an explicit origin marker and a complete case list; their source is
editable without updating a vendor checksum. Tests require no network or
installed third-party test framework.

Sources:

- CPython `v2.7.18`, commit `8d21aa21f2cbc6d50aab3f420bb23be1d081dac4`:
  <https://github.com/python/cpython/tree/v2.7.18/Lib/test>.
- Project-authored modules under `local/` check numeric slot dispatch,
  receiver binding, truth and object protocols, percent formatting and runtime
  regressions.
  Their vectors and expectations are validated on CPython 2.7.18.
- CPython license and copyright notices: `LICENSES/PSF-2.0.txt`
  relative to the repository root, or
  [the third-party notice](../../LICENSES/README.md).

## Running

Enable the existing optional CLI and testing targets:

```sh
cmake -S . -B build/upstream -DCMAKE_BUILD_TYPE=Debug \
    -DTINYPY_BUILD_CLI=ON -DBUILD_TESTING=ON
cmake --build build/upstream -j
ctest --test-dir build/upstream -L upstream --output-on-failure
```

Python 3.7 or newer runs only the host coordinator that launches processes,
collects results and enforces timeouts. The actual test programs are Python 2.7
and execute inside tinypy. This optional build-time dependency does not change
the runtime language or the library's dependencies. CTest registers one check
per module and a separate assertion-adapter check. Every case
runs in its own tinypy process, with a timeout and allocator accounting; a
crash cannot prevent the remaining cases from running. Nonzero exit codes,
undeclared skips and nonzero outstanding allocations fail the check.

The optional reference mode also requires every active case to pass on an
external CPython 2.7.18 interpreter and compares its stdout with tinypy:

```sh
python3 tests/upstream/run_tests.py \
    --tinypy build/upstream/cli/tinypy \
    --reference /path/to/python2.7 \
    --report build/upstream/upstream-results.json
```

Use `--module cpython.test_class`, or a full case identifier such as
`--case cpython.test_class.ClassTests.testHashStuff`, to narrow a run.
Reference Python is not downloaded, embedded or required by CTest.
Discovery must match the manifest exactly; changing a vendor file or silently
losing a test makes the coordinator fail.

## Adapter boundaries

On tinypy, `assertions.py` supplies only the TestCase assertions, exception
contexts and skip decorators used by these modules. `support.py` supplies
Unicode availability and the normal, non-`-3` Py3k warning context. This does
not implement warning validation or a general unittest replacement. The
`assertRaisesRegexp` adapter accepts literal substring patterns only; regex
metacharacters explicitly raise an unsupported-operation error. Missing
APIs raise errors instead of accepting a test. `check_adapter.py` checks both
successful assertions and rejected assertions independently of the corpus.

The CPython reference uses its real standard-library unittest and test support.
Neither the adapters
nor the vendor files are part of the runtime library or its installation.

## Deferred work

15 individual cases remain present in their unchanged CPython modules but are
listed as `DEFER` with reasons in `manifest.json`. They require cyclic GC or
CPython implementation details. Two original fixtures create owning cycles;
their behavior is also checked by local equivalents with explicit cleanup or
without a closure cycle. Their unchanged originals remain deferred. A deferred
case is not reported as passing.

Whole modules needing additional libraries or source cleanup were not copied:

| Next candidate | What must be supplied or adapted first |
| --- | --- |
| CPython list/tuple tests | `list_tests`, `seq_tests`, itertools, os and GC dependencies |
| CPython bool/int/long/float/complex tests | os, math, random, operator and related helpers |
| CPython dict/set/bytes tests | mapping helpers, UserDict, copy, pickle, GC and other imports |
| CPython str/unicode tests | string helpers, codecs, struct and UserList |
| CPython property tests | optimize decorators depend on `sys.flags`, which tinypy does not expose |
| CPython scope/grammar/compiler tests | warning validation, tracing, library imports, `_ast` or subprocess helpers |
| CPython generators/genexps/setcomps/unpack | A Python 2 doctest runner and its shared-state/output handling |
| CPython module, weakref and descriptor tests | GC-dependent cases and ownership-cycle cleanup |
| IronPython tests | iptest and CLR-specific infrastructure |
| MicroPython tests | Select and validate Python 2-compatible cases from a Python 3 corpus |

All 555 active cases currently pass on tinypy and the CPython 2.7.18 reference.
Failures remain failures, without expected-failure annotations. Runtime fixes
and validation results are recorded in [VALIDATION.md](VALIDATION.md). Passing
compiler checks or the portable subset does not establish complete Python 2
compatibility.

The October runtime review, optimization measurements and remaining work are
tracked in [REVIEW_STATUS.md](REVIEW_STATUS.md).
