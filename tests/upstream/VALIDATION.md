# Portable corpus validation

macOS arm64 validation on 2026-10-06, after runtime compatibility fixes:

- 20 byte-for-byte unchanged CPython 2.7.18 modules: 221 discovered cases, including inherited methods.
- Six project-authored Python 2.7 modules: 349 discovered cases.
- All 555 active cases pass on tinypy and external CPython 2.7.18.
- 15 original CPython cases remain explicitly deferred for the reasons in `manifest.json`.
- Total: 570 cases, 555 PASS, 15 DEFER, no active failures or signal exits.
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
| `local.test_review_regressions` | 54 | 54 | Long conversion, caches, finalizers, metaclasses, imports, numeric/codec/container protocols, output and exception state |

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

## Deferred originals

The remaining 15 originals require cyclic GC, CPython extension/iterator details,
refcount inspection or ownership cleanup. `AugAssignTest.testCustomMethods1`
captures its own class in closures. `ClassTests.testSFBug532646` creates a
class/instance ownership cycle; its initial native crash was fixed by guarding
native callable recursion. Local equivalents now check augmented-assignment
identity/rebinding and recursive calls with explicit cycle cleanup. The
unchanged originals remain deferred and are not counted as passing.

The runtime still uses reference counting with explicit ownership-cycle cleanup;
this change does not add cyclic GC or the CPython `_testcapi` ABI.

## Completed checks

| Profile | Active cases | Deferred cases | CTest |
| --- | --- | --- | --- |
| Debug | 555 PASS | 15 | 109/109 PASS |
| Release | 555 PASS | 15 | 109/109 PASS |
| Debug with ASan and UBSan | 555 PASS | 15 | 109/109 PASS |

All 81 pre-existing CTests pass in each profile. New registrations comprise 26
corpus modules, the assertion adapter and a native-stack-budget test. The latter
checks an explicit byte budget, default/older-config behavior, RuntimeError,
recovery and zero outstanding allocations. No AddressSanitizer or
UndefinedBehaviorSanitizer diagnostics occurred. UBSan used
`UBSAN_OPTIONS=halt_on_error=1`; sanitizer Debug remains unoptimized.

Compiler differential passed for 35 Python 2 vendor, local and adapter sources
at optimize levels 0, 1 and 2: 105 byte-identical marshal-v2 outputs against
CPython 2.7.18. The existing tool/API suite passed 34/34. All builds completed
with the repository's strict warnings-as-errors flags.

These checks cover the listed corpus and native tests; they do not prove full
Python 2 compatibility or acceptance of an embedded application. Rerun the
coordinator with `--report` for current case-by-case results.

The current review adds 54 functional cases and two import fixtures. Its
source-audit findings, measured performance, primary references and unresolved
compatibility/optimization work are listed in [REVIEW_STATUS.md](REVIEW_STATUS.md).
