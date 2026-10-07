# Portable corpus validation

macOS arm64 validation on 2026-10-07, after runtime compatibility fixes:

- 20 byte-for-byte unchanged CPython 2.7.18 modules: 221 discovered cases, including inherited methods; 210 selected.
- Nineteen project-authored Python 2.7 modules: 745 discovered and selected cases.
- All 951 ordinary active cases pass on tinypy and external CPython 2.7.18.
- Four ownership-sensitive cases use Debug detector adaptations and are skipped in Release.
- Seven tuple-cache identity variants are excluded; four CPython-specific originals have local replacements.
- Total: 966 discovered, 11 excluded originals, 955 selected cases; Debug has 955 PASS, Release has 951 PASS / 4 SKIP. No DEFER remains.
- Every tinypy case runs independently with zero outstanding allocator memory.
- The assertion adapter passes its positive and negative checks under both interpreters.
- No expected-failure annotations or relaxed runtime expectations were added.

These are case counts, not individual assertions or distinct bugs. Type matrices
check multiple manifestations of a behavior, and seven enumerate cases exercise
the same inherited malformed-iterator check.

## Coverage

| Module | Cases | PASS | Checks |
| --- | --- | --- | --- |
| `local.test_binding` | 55 | 55 | Receiver validation, error wording, call forms, method identity, descriptors |
| `local.test_numeric_protocols` | 111 | 111 | Numeric slot/type matrices, modular power, widening, division and zero divisors |
| `local.test_object_protocols` | 29 | 29 | Truth protocols, short circuiting, attributes, properties, inheritance, reflected operations |
| `local.test_percent_format` | 77 | 77 | Formatting vectors, Unicode, flags, bad arguments, mapping lookup, conversion protocols |
| `local.test_runtime_regressions` | 23 | 23 | Recursion, exception cleanup, classic coercion and callback errors, augmented assignment, implicit static `__new__` |
| `local.test_review_regressions` | 109 | 109 | Long conversion, custom MRO, intern sharing/lifetime, caches, finalizers, subtype factories, imports, builtins namespaces, numeric/codec/container protocols, sorting, finally, exception state, deletion/reversed and length-hint protocols |
| `local.test_evaluation_semantics` | 25 | 25 | Evaluation order, scope, decorators, control-flow unwinding, context managers and generators |
| `local.test_container_semantics` | 31 | 31 | Iteration, list/set/bytearray partial updates and reinitialization, constructor keywords, subclass representation, slices, mappings and properties |
| `local.test_container_deep` | 41 | 41 | Integer conversion, callback mutation, self-extension, set atomicity, one-lookup setdefault, direct/operator dispatch and reversed metadata |
| `local.test_numeric_text_deep` | 29 | 29 | Text conversion, codec buffers/keywords/incremental decoding, formatting, float-format state and supported struct argument/buffer protocols |
| `local.test_object_eval_deep` | 39 | 39 | Descriptors, super/proxies/subclasses, Unicode names, generator exception triples, adaptive exception construction and reinitialization |
| `local.test_compiler_edges` | 7 | 7 | Compile argument conversion/order, legacy source buffers, mode normalization and exact syntax metadata |
| `local.test_container_edges` | 30 | 30 | Buffer/slice conversion order, callback mutation, dict-values membership, iterator state and memoryview exports |
| `local.test_text_numeric_edges` | 21 | 21 | Float overrides, format-spec Unicode, character buffers, partition identity, search/count ordering and join iteration |
| `local.test_object_edges` | 26 | 26 | Keyword equality/hash, code/default snapshots, star argument order/keys protocol, native UnicodeError state/format/lifetime |
| `local.test_comparison_deep` | 37 | 37 | Three-way/rich dispatch, NotImplemented retries, child-result identity, list mutation, buffer comparisons, hash/truth/length conversion and native-slot expectation |
| `local.test_type_deep` | 27 | 27 | Generic/canonical slots, descriptor names, namespace/base callbacks and identity, metaclass constructor forms, variable-layout rejection, weakrefs and slotnames |
| `local.test_control_deep` | 13 | 13 | Explicit StopIteration type/payload, throw constructor failures, close/reentry and locals mapping protocols |
| `local.test_sre_deep` | 15 | 15 | Bounds/counts, keywords, conversion order, buffer/slice types, groups, scanner metadata and host helper forwarding |

The local modules use independently authored test bodies and operand vectors,
with all expectations checked on CPython 2.7.18.

## Runtime changes

The initial corpus had 401 PASS, 76 FAIL and 16 DEFER out of 493 cases.
All 76 failures are now fixed in the runtime:

- Built-in descriptors and unbound methods validate their receiver; unbound-method errors follow Python 2 wording. Function-valued `__new__` becomes an implicit staticmethod.
- Integer modular-power slots return NotImplemented for unsupported operand types before entering the calculation.
- Percent formatting handles empty mapping keys, byte/Unicode character conversion and integer/long fallback. Conversion failures release owned operands and preserve the appropriate Python 2 exception behavior.
- Classic comparisons run coercion before `__cmp__`, distinguish missing descriptors from exceptions raised by callbacks, and honor classic hashability rules. Classic simple slices normalize omitted bounds before passing a slice to item hooks.
- Classic `__iter__` results are accepted at iterator creation; missing `next` is rejected when consumed, matching Python 2.
- `isinstance` and `issubclass` preserve exceptions from `__bases__` and guard recursive class-info tuples. A failing classic initializer descriptor returns its exception instead of calling a null result.
- Native callable dispatch shares the recursion guard. A separate native-stack byte budget prevents C-stack exhaustion before the logical depth limit, including under unoptimized sanitizers. RuntimeError construction avoids recursively entering its own failing guard.

The host may set `tinypy_vm_config_t.max_stack_bytes`; zero or an older config
selects the 1 MiB default. GCC, Clang and MSVC support native-stack measurement;
other compilers retain the logical depth guard. Set the budget to fit the
embedding thread, allowing additional room for callbacks, runtime call frames
and error unwinding. Native stack checks may stop recursion before
`sys.getrecursionlimit()`, depending on frame size and build instrumentation.

The dict-comprehension assignment error check is now active; its literal
`assertRaisesRegexp` patterns are supported by the assertion adapter. The 23
new regression cases cover the fixes and adjacent error paths.

## Selected replacements and cycle adaptations

Seven `test_tuple_reuse` variants are excluded from the selected corpus.
`ClassTests.testDelItem`, `TestReversed.test_len`, `test_bug1229429` and
`test_xrange_optimization` have independently authored replacements under
`local.test_review_regressions`: Python deletion/error propagation, direct
length hints, weakref lifetime after invalid reversed calls, and reversed
xrange values/exhaustion. These replacements pass on CPython 2.7.18 and tinypy;
they do not claim CPython's C ABI, refcount inspection or concrete iterator type.
All eleven originals remain in the vendor/discovery inventory and are excluded
from execution, with reasons in `manifest.json` and reports. Discovery still
validates them, so exclusions cannot silently hide a missing source case.

Four ownership-sensitive cases use `cycle_cases.py`
in Debug with `TINYPY_ENABLE_CYCLE_DIAGNOSTICS=ON`:

- `ClassTests.testDel`: acyclic finalization and zero detector false positives.
- `TestReversed.test_gc`: one sequence/reversed cycle.
- `AugAssignTest.testCustomMethods1`: two class/method/closure cycles, retaining identity/rebinding assertions.
- `ClassTests.testSFBug532646`: one class/instance cycle, retaining the RuntimeError recursion check.

Weak references allow cleanup without rooting the cycles. Each adaptation
first checks that reachable cycles are not reported, then checks unreachable
detection, breaks its owning edges and checks zero remaining cycles;
the process must also return its allocator balance to zero. The coordinator
checks diagnostic edges and source locations and records an explicit
`debug-cycle-adaptation` mode. Release skips all four before fixture execution.
Vendor sources remain unchanged; adaptation PASS is not an original GC test PASS.

The runtime still uses reference counting with explicit ownership-cycle cleanup;
this change does not add cyclic GC or the CPython `_testcapi` ABI.

## Completed checks

| Profile | Active cases | Skipped cases | Native/runtime CTest |
| --- | --- | --- | --- |
| Debug with cycle diagnostics | 955 PASS | 0 | 89/89 PASS |
| Release | 951 PASS | 4 | 89/89 PASS |
| Debug with cycle diagnostics, ASan and UBSan | 955 PASS | 0 | 89/89 PASS |
| Release with LTO | 951 PASS | 4 | 89/89 PASS |

All 129 CTest registrations are validated against an exact inventory in each
profile: 89 native/runtime tests, 39 portable-module wrappers and the assertion
adapter. The matrix runs native/runtime CTest and executes the portable corpus
separately with the external oracle, plus the adapter. Native marshal, artifact,
opcode and bytecode-verifier tests now participate in all profiles.
The native-stack-budget test checks an explicit byte budget,
default/older-config behavior, RuntimeError,
recovery and zero outstanding allocations. No AddressSanitizer or
UndefinedBehaviorSanitizer diagnostics occurred. UBSan used
`UBSAN_OPTIONS=halt_on_error=1`; sanitizer Debug remains unoptimized. Apple
ASan does not support LeakSanitizer, so macOS uses `detect_leaks=0`; this is
recorded in the report. Per-case tinypy allocator balance is checked independently.

The earlier runtime closure's compiler differential passed for 35 Python 2 vendor, local and adapter sources
at optimize levels 0, 1 and 2: 105 byte-identical marshal-v2 outputs against
CPython 2.7.18. The current tool/API and coordinator suite passed 78/78, including
exclusion/discovery validation and rejection of explicitly requested excluded
cases. Runtime builds completed
with the repository's strict warnings-as-errors flags.

The complete latest audit compares 214,701 unique runtime outcomes and 4,197
marshal-v2 outputs per profile (16,788 total), including dynamic compile error
metadata. [DEEP_AUDIT.md](../conformance/DEEP_AUDIT.md) records fixes and the
explicit extra-keyword pending-C-exception boundary; that tinypy policy is
tested in the native runtime fixture rather than claimed as an oracle match.

These checks cover the listed corpus and native tests; they do not prove full
Python 2 compatibility or acceptance of an embedded application. Rerun the
coordinator with `--report` for current case-by-case results.

The review module now has 109 functional cases and import fixtures also cover
reload/package behavior. Source-audit findings, closure validation, measured
performance and profile boundaries are listed in [REVIEW_STATUS.md](REVIEW_STATUS.md).

The third pass adds module/minimal/missing builtins namespaces, inherited eval
builtins and tuple concatenation subtype/identity checks. Independent matrices
also cover 4,476 text/Unicode outcomes, 47 frame/eval outcomes and 66 tuple
outcomes. The redundant finally/with stack-shape diagnostics compile only in
Debug; one-time structural verification and semantic exceptions remain active
in Release.

The fourth pass compares 484 deterministic aggregate-function and iterator
outcomes against CPython 2.7.18 in all four profiles. The five new local cases
cover filter's immutable-subtype item protocol and storage length, indexed
errors, immediate text-item validation, bool truth callbacks, and map's shallow
copy/subtype-iteration behavior. Semantic checks remain active in Release.

The fifth pass adds checked Python 2 length hints for sequence consumers. Its
differential covers callback order, `__len__` fallback, classic instances,
negative hints and propagated callback errors. Subclasses of `reversed` also
retain Python 2's ignored-keyword behavior. The general multi-sequence `map`
path also retains Python 2.7's observable ignored-hint-error behavior. These
semantic callbacks and errors remain active in Release.

The external-suite review adds 56 project-authored cases: 25 evaluation/control
flow checks and 31 container/iterator checks. Every ordinary case also passes
on external CPython 2.7.18. General list extension now appends as the source
yields. Reinitializing list, set and bytearray clears first and retains partial
progress on failure; set difference-update streams general iterables. Sequence
and bytearray constructor keywords and set-subclass representation follow
Python 2.7. Slice replacement retains its separate atomic collection semantics.
Selection and pinned upstream references are documented in
[EXTERNAL_TEST_REVIEW.md](EXTERNAL_TEST_REVIEW.md).
Both new source files additionally produce byte-identical marshal-v2 against
CPython 2.7.18 at optimize levels 0, 1 and 2 (six compilations).
