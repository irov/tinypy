# Portable Python 2 tests

This corpus vendors 20 complete CPython 2.7.18 modules with 221 discovered
cases, of which 210 are selected, and adds twenty-seven project-authored
modules with 942 cases. Upstream files are byte-for-byte unchanged.
`manifest.json` records source commits, paths, SHA-256 hashes, case names and
explicit reasons for excluding originals outside the tinypy corpus. Locally authored modules
have an explicit origin marker and a complete case list; their source is
editable without updating a vendor checksum. Tests require no network or
installed third-party test framework.

Sources:

- CPython `v2.7.18`, commit `8d21aa21f2cbc6d50aab3f420bb23be1d081dac4`:
  <https://github.com/python/cpython/tree/v2.7.18/Lib/test>.
- Project-authored modules under `local/` check numeric slot dispatch,
  receiver binding, truth and object protocols, percent formatting and runtime
  regressions, evaluation order, control flow and container semantics.
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

For an optional Release LTO build, configure with
`-DCMAKE_BUILD_TYPE=Release -DTINYPY_ENABLE_LTO=ON`. CMake verifies toolchain
support; the option defaults to OFF.

Python 3.7 or newer runs only the host coordinator that launches processes,
collects results and enforces timeouts. The actual test programs are Python 2.7
and execute inside tinypy. This optional build-time dependency does not change
the runtime language or the library's dependencies. CTest registers one check
per module and a separate assertion-adapter check. Every case
runs in its own tinypy process, with a timeout and allocator accounting; a
crash cannot prevent the remaining cases from running. Nonzero exit codes,
undeclared skips and nonzero outstanding allocations fail the check.
The coordinator queries `tinypy --build-info`; four ownership-sensitive cases
use Debug diagnostic adaptations and receive `SKIP` in Release. Debug builds
compile cycle diagnostics by default (`TINYPY_ENABLE_CYCLE_DIAGNOSTICS=ON`);
explicitly disabling the option makes those checks fail. Release never compiles
the detector, including with the option enabled. Existing CMake caches that
retain OFF must be reconfigured with `-DTINYPY_ENABLE_CYCLE_DIAGNOSTICS=ON`.
The CLI's `--cycle-diagnostics` switch enables tracking and supplies
`__tinypy_report_cycles__()` in the script's globals as a bridge to the public
`tinypy_vm_report_cycles` API. It is available only with diagnostic support.

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
losing a test makes the coordinator fail, including for excluded originals.
`excluded_cases` removes those originals from execution and PASS/SKIP counts;
reports retain their exclusion reasons separately. An explicit `--case` request
for an excluded original fails with its reason.

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

## Corpus selection

No selected case remains DEFER. Seven inherited `test_tuple_reuse` variants
are excluded because they require CPython's particular tuple-cache identity.
Ordinary enumerate value/subclass tests remain active. Four other originals
are replaced by independently authored cases in `local.test_review_regressions`:

| Original | Selected replacement |
| --- | --- |
| `ClassTests.testDelItem` | Python deletion dispatch and KeyError propagation, without the `_testcapi` C ABI |
| `TestReversed.test_len` | Direct `__length_hint__()` calls, remaining length, exhaustion and callback errors |
| `TestReversed.test_bug1229429` | TypeError for noncallable `__reversed__`, weakref lifetime and zero allocator balance |
| `TestReversed.test_xrange_optimization` | Reversed values, iterator identity and exhaustion, without concrete CPython type identity |

The eleven originals remain only in the pinned vendor sources and discovery
inventory; they are not pending tests or reported as passing. Four
ownership-sensitive cases use project-authored adaptations in
`cycle_cases.py`, selected by `debug_cycle_cases` in the manifest. Debug checks
the exact number of unreachable owning cycles, diagnostic edges/source
locations, explicit cleanup and a zero allocator balance; `testDel` checks an
acyclic destructor and zero false positives. Release skips these four before
running their fixtures. Reports identify Debug adaptation results explicitly;
they do not claim that tinypy executes the unchanged GC-dependent originals.

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

All 1,038 ordinary active cases currently pass on tinypy and the CPython 2.7.18
reference. Debug additionally passes four diagnostic adaptations (1,042 PASS);
Release reports 1,038 PASS and 4 SKIP. The full build/oracle/compiler matrix has
one entry point documented in [tests/README.md](../README.md); its finite
coverage model and latest audit are under [conformance](../conformance/DEEP_AUDIT.md).
Failures remain failures, without expected-failure annotations. Runtime fixes
and validation results are recorded in [VALIDATION.md](VALIDATION.md). Passing
compiler checks or the portable subset does not establish complete Python 2
compatibility.

The October runtime review closure and optimization measurements are
tracked in [REVIEW_STATUS.md](REVIEW_STATUS.md).

The review of RustPython snippets and python-spec-test-suite contributed 56
independently written Python 2.7 checks in `local.test_evaluation_semantics` and
`local.test_container_semantics`. Selection, pinned source references and
Python 2/3 differences are recorded in [EXTERNAL_TEST_REVIEW.md](EXTERNAL_TEST_REVIEW.md).
