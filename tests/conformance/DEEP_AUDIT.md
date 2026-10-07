# Deep conformance audit — 2026-10-07

The supported contract is [SPEC.md](../../SPEC.md), with external CPython
2.7.18 as the observable-behavior oracle. Five audit passes added 482 independently
authored portable cases: the initial 109, 84 boundary/callback cases,
and 92 comparison/type/generator/regex cases (37 comparisons, 27 types,
13 control flow and 15 SRE), followed by 87 constructor/iterator/descriptor/
decoder cases (24 text protocols, 33 builtins, 17 descriptors and 13 compiler
diagnostics), followed by 110 formatting/function/namespace/introspection cases
(24 brace-formatting, 30 functional consumers, 25 namespace and 31 attribute
protocol cases).
Every ordinary selected case passes on both runtimes.
These counts are regression witnesses, not a count of distinct defects.

## Corrected behavior

| Area | Confirmed corrections |
| --- | --- |
| Numeric arguments | Python 2 `__int__` parsing for list/bytearray mutation and text methods; separate `__index__` search bounds and direct sequence repetition; C-int overflow for sort/splitlines/codec flags; distinct long-subclass and float offset rules in struct |
| Lists | Callback-sensitive index bounds; self-subclass extension snapshots before mutation; direct multiplication versus operator/reflected dispatch; receiver and multiplier selection |
| Dictionaries | `setdefault` hashes and resolves a collision once, then inserts into the resolved position; lookup remains safe when callbacks clear or modify the dictionary |
| Sets | `isdisjoint` short-circuits, respects subclass iteration and compares in the smaller-set orientation; intersection updates compute before swapping; difference-update rejects unhashable mutable-set elements |
| Bytearrays and iterators | Byte range/error classes; reverse list physical storage; tuple-subclass sequence hooks; terminal reversed errors; `listreverseiterator`/`rangeiterator` metadata and overflow-free extreme range stepping |
| Text and formatting | Codec method keywords, ASCII/NUL name validation, Unicode translation result types, percent star operand types and Unicode character-conversion errors |
| Codecs | Supported buffer inputs; subtype override bypass; low-level arity; UTF-8 partial prefixes, `final`, consumed counts and callback order; registered UTF-8 decoder finalization |
| Float format and marshal | Per-VM format state; native versus standard struct policy; binary marshal float/complex nonfinite failures and signed-zero behavior; legacy text float loading retains its separate policy |
| Struct buffers | Numeric conversion callbacks, partial writes, raw buffer writes, export lifetime across offset/float callbacks, cleanup on failure, Unicode/legacy bounds and unpack-from keywords |
| Super and descriptors | Unbound None, reported-class proxies, strict-subclass rebinding through its constructor, validation before mutation, arbitrary descriptor owner handling and None-owner rules |
| Attributes | Unicode names encode before get/set/delete hooks and hasattr suppression; custom lookup hooks also receive their own method names |
| Generators and exceptions | Throwing an existing instance with None; requested exception type/value/traceback preserved separately, including adaptive constructors and normalization before handlers/bare raise; correct reinitialization of message, errno/filename, exit code and syntax fields |
| Compiler arguments and errors | Python 2 C-int conversion, filename/mode Unicode conversion and error order; supported legacy source buffers observed after flag callbacks; mode-specific newline handling; parser/AST/symbol/codegen error args and location fields; Unicode encoding-declaration precedence and unknown-encoding messages |
| Slices and legacy buffers | Python 2 constructor conversions, current owner bounds after callbacks, empty-buffer concatenation identity; list bounds before replacement iteration and length hints; bytearray index/element order, repeated slice conversion and numeric-source rejection |
| Views and iterator state | Dict-values membership follows iterator size checks and early-match behavior; dict/set length hints and sticky set errors; memoryview key/error ordering, read-only behavior, overlaps and child-export lifetime |
| Text boundaries | Round honors integer-subclass float overrides; Unicode format specs/default ASCII conversion; character buffers, partition identity, search/split/replace conversion order; join exhausts input before validation and follows subclass iteration/length hints |
| Function binding | Formal keyword matching compares without hashing; extra keywords hash when inserted; code/default snapshots survive callback mutation; mapping keys materialize before star iteration, including keys-only objects and sequence subclasses |
| Unicode errors | Native fields cannot be shadowed by instance dict; parser/reinitialization/setter partial state and failures; exact hex escapes, blank Unicode result, metadata conversion and NUL rendering; reference traversal/release follows physical storage ancestry after custom MRO changes |
| Comparison and hash | `cmp` invokes the three-way protocol once; Python 2 same-type/reflected rich-slot retry order; arbitrary child rich-result identity; list ordering reacquires current elements and sizes after equality callbacks; classic/new-style result conversion and errors; native rich-slot call count |
| Truth and lengths | Classic negative nonzero and long length result rejection; new-style length numeric conversion/overflow; inherited numeric nonzero remains ahead of a subclass length method |
| Buffer comparisons | Byte strings and buffers retain Python 2 type ordering; Unicode comparisons decode supported legacy buffers |
| Type construction and slots | Query/creation APIs and original metaclass kwargs; original base-tuple identity and subtype protocols; generic slot iteration, validation and error order; namespace snapshot after callbacks; exact descriptor names; canonical immutable physical slot names and class reassignment; nonempty variable-size builtin/metaclass slot rejection and weakref policy |
| Generator control and namespaces | Explicit StopIteration type/payload survives Python-visible next/send; native next still consumes exhaustion; throw constructor failure enters the suspended generator; close tests requested exception type; classic locals require getitem and bytearray/memoryview mappings are accepted |
| Regular expressions | Negative update counts; numeric conversion/order and signed C-int getlower; keyword/alias/default handling; supported buffer subject/result types and subtype slicing; reversed independent bounds, including zero-minimum repeats; original scanner/finditer/sub callback position metadata |
| Numeric constructors | String/Unicode subtype conversion overrides; numeric legacy buffers; explicit-base conversion and error order; decimal Unicode error fields and ranges; keyword arguments and complex second-operand float conversion |
| Native keyword parsing | Dictionary equality callbacks distinguish supplied values from defaults; ignored lookup exceptions preserve the prior exception state; positional/keyword conflicts and per-parameter conversion order |
| Aggregate parsing and allocation | `sorted` checks and converts arguments before materializing its input, then repeats reverse conversion through `list.sort`; length hints use the correct stored integer-subtype values and numeric conversions; `dict.fromkeys` accepts alternative writable constructor results and transfers cached hashes for exact builtin sources |
| Translation and text construction | Unicode-table promotion for byte strings; legacy character-buffer acceptance and delete-character error order; no-op exact-string identity; Unicode/str constructor keywords and builtin decode versus subtype hooks |
| Property and callable descriptors | None-copy preserves accessors; getter-doc Exception suppression versus BaseException propagation; published state before doc callbacks; subtype doc storage; callable-descriptor argument diagnostics and precedence |
| Weak references | Noncallable callback construction; callback release after target death and batch cleanup order; `ref.__new__` keyword policy; cross-subtype equality |
| Source decoding and literal diagnostics | Late UTF-8 Unicode-literal decoding versus byte literals/comments; early ASCII decoding including Unicode inputs; byte escape messages; named/hex/raw Unicode escape spans; adjacent literal decoding; parser text restored by source encoding and mode |
| Brace formatting | Conversion before nested specification callbacks; recursion limits and shared automatic numbering; converted text-subclass formatting; original format-spec identity; Unicode numeric fields and mapping keys; field-path validation/error order and formatter iterator recovery |
| Functional state | Partial tuple-subclass canonicalization and dictionary-subclass copying; cached keyword hashes; sequential publication before each displaced field's finalizer; merge errors prevent target execution |
| Iterator consumers | Reduce initial-iterator error translation and accumulator lifetime through exhaustion; reusable two-item call tuples with fresh allocation when retained by a native callback; enumerate counter types/promotion and keyword precedence; classic versus new-style reversed lengths; xrange integer parsing; general map iterator-error translation |
| Execution dictionaries | Exact local/builtin lookup suppression versus checked interned-name global lookup; callback-sensitive global caches including misses and lookup restarts; global writes bypass dictionary-subclass hooks; single-lookup deletion errors; exec NUL validation after namespace/builtins handling |
| Module lifetime | Allocation-only exact/subclass modules expose None until lazy namespace creation; nullable descriptor/C API fields; checked teardown snapshots and private-first clearing without recreating deleted keys; subtype slots precede namespace release; diagnostics traverse references without clearing them |
| Introspection and class binding | Vars reads __dict__ once and translates lookup errors; dir follows generic dictionary/members/methods/class/base protocols and classic strict lengths; classic callable lookup and classobj construction; unbound receivers use reported classes/metaclass checks, including the second class lookup when formatting a rejection |
| Import and reload | Package fromlists use the canonical request name even when module names differ or were assigned after allocation; allocation-only star import reports missing namespace keys; reload returns the loader's replacement without merging or modifying the original namespace, including nonmodule replacements |

Python-visible conversion failures, error ordering and callbacks remain active
in Release. This work does not add cyclic GC or change the documented ownership
boundaries. The existing four detector adaptations still run only in Debug.
The verifier unit expectation for an absent finally marker was updated to its
existing structural rejection reason; the verifier was not weakened.

The marshal state regression uses the supported code-object C API in
`tests/runtime/test_load_code.c`: 14 float/complex rows and separate native and
unknown-format VMs. A Python `marshal` module is not bundled.

The native rich-comparison retry expectation is source-led: CPython 2.7's
same-type fast call is followed by both operands after NotImplemented.
The native tinypy fixture checks three calls; independent Python-level cases
check the corresponding six method calls and strict-subtype order. This does
not claim execution of the CPython extension ABI. Primary reference:
[CPython object.c](https://github.com/python/cpython/blob/v2.7.18/Objects/object.c).

The fifth pass adds native witnesses for retained reduce call tuples,
allocation-only module access through the public C API, read-only module traversal
by the cycle detector, and module-finder reload replacement identity. Their groups
remain part of the existing 89 native/runtime tests. Python-facing lifecycle,
introspection and namespace cases use independent CPython expectations. The
allocation-only integration guards were reviewed in source before exercising
the corrected valid states. Primary references:
[moduleobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/moduleobject.c),
[classobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/classobject.c),
[ceval.c](https://github.com/python/cpython/blob/v2.7.18/Python/ceval.c) and
[import.c](https://github.com/python/cpython/blob/v2.7.18/Python/import.c).

SRE products use ordinary valid programs generated from project-authored
patterns by CPython 2.7.18's regex compiler. Replacement-template parsing and
Match.expand require host `re._subx`/`re._expand`; the portable helper stub
checks forwarding/order rather than the external parser. Error classes are
compared throughout the products; exact diagnostic wording is asserted only
where a portable regression explicitly specifies it. Primary reference:
[CPython _sre.c](https://github.com/python/cpython/blob/v2.7.18/Modules/_sre.c).

One CPython 2.7.18 error-state defect is explicitly excluded from emulation:
`PyEval_EvalCodeEx` ignores insertion errors while binding extra keywords and
can return a value with a pending C exception. Tinypy propagates that hash or
comparison failure before executing the function body. This policy and recovery
are tested in `call_semantics_runtime.py` in every profile, rather than reported
as an oracle match. [SPEC.md](../../SPEC.md#14-errors) records this boundary.

## Permanent finite domains

| Fixture | Enumerated outcomes |
| --- | --- |
| Numeric binary operations: 20 operands × 20 operands × 17 operations | 6,800 |
| Bounded power/shifts: 20 operands × 6 counts × 3 operations | 360 |
| Unary operators/conversions/hash: 20 operands × 10 operations | 200 |
| Sequence slices: 10 sequences × 8 starts × 8 stops × 6 steps | 3,840 |
| Mutable slice deletion and replacement | 3,840 |
| Builtin formatting: 21 values × 16 conversions × 13 flags × 4 width/precision forms | 17,472 |
| Byte/Unicode percent formatting: 21 values × 17 templates × 2 text forms | 714 |
| Numeric parsing/conversion boundaries | 783 |
| Text operands, subtypes and legacy buffers | 2,820 |
| Extended formatting boundaries | 479 |
| Dynamic compile sources/modes/flags and argument diagnostics | 3,216 |
| Generator throw/close/StopIteration and locals-mapping protocols | 222 |
| Type slot declarations × builtin bases and constructor protocols | 415 |
| Comparisons: 16 builtin operands × 16 operands × 7 operations | 1,792 |
| Rich dispatch across class styles, return profiles and subtype direction | 378 |
| Classic/new-style comparison/hash/truth/length return conversions | 170 |
| Regex: 32 patterns × 50 subjects × 13 bound pairs × 5 methods | 104,000 |
| Regex updates: 32 patterns × 50 subjects × 7 counts × 6 operations | 67,200 |
| Compiler diagnostic contexts, source forms, encodings, modes and flags | 6,432 |
| Property/callable descriptor copying, initialization, binding and weakrefs | 1,312 |
| Iterator consumers, lengths, aggregate arguments and cached-hash transfer | 1,504 |
| Text conversion protocols, constructor arguments, decimal and translation errors | 726 |
| Brace fields, conversion/specification protocols and formatter iterators | 568 |
| Partial state/calls, reduce, counters, lengths and aggregate parsing | 499 |
| Execution mappings, dictionary callbacks/caches and nullable module/package imports | 511 |
| Module/class construction, directory protocols, receiver binding and finalizers | 1,091 |
| **Total unique runtime outcomes** | **227,344** |

Operands include integer widening, long, bool, float, complex, infinity, NaN,
subnormal and maximum finite doubles. All sixteen matrices compare complete stdout to
the oracle and verify unique identities, exact cardinality and zero outstanding
tinypy allocations. They compare exception classes; protocol regressions also
assert callback order, mutation, identity and selected exception messages.

The generated compiler corpus contains 444 `exec`, 420 `eval` and 420 `single`
sources. Each is checked at optimize 0/1/2. Another 131 checked-in vendor, local,
runtime and matrix sources undergo three-level `exec` comparison. This gives
4,245 byte-identical marshal-v2 comparisons per profile, 16,980 across all four.
No compiler mismatch was found in these inputs.

Dynamic compiler diagnostics use logical filenames with no filesystem source
lookup. Semantic-error text is None, matching the oracle when that named file
is unavailable. Core filesystem access remains outside the embedding contract.

## Final validation

The default [matrix runner](../run_validation.py) completed all 138 stages on
macOS arm64. C and C99 standalone builds used strict warnings-as-errors.

| Profile | Native/runtime CTest | Portable oracle cases | Runtime outcomes | Compiler comparisons |
| --- | --- | --- | --- | --- |
| Debug, detector on | 89 PASS | 1,152 PASS | 227,344 identical | 4,245 identical |
| Release | 89 PASS | 1,148 PASS / 4 diagnostic SKIP | 227,344 identical | 4,245 identical |
| Unoptimized Debug, ASan/UBSan | 89 PASS | 1,152 PASS | 227,344 identical | 4,245 identical |
| Release, LTO | 89 PASS | 1,148 PASS / 4 diagnostic SKIP | 227,344 identical | 4,245 identical |

All 137 CTest registrations were checked against the exact inventory. Portable
cases are run separately with the external oracle; their CTest wrappers are
not counted again as executed native tests. Host/API/runner acceptance checks
passed 82/82. Standalone marshal/artifact and core symbol audits passed in all
profiles. No ASan/UBSan diagnostics occurred. Apple ASan lacks LeakSanitizer;
macOS records `detect_leaks=0`, while allocator balance is checked independently.

Logs, per-case portable results, JUnit results and the aggregate source/test
SHA-256 are in `.temp/validation-fifth-accepted/report.json` and its linked artifacts. The
report rejects incomplete discovery, native/portable skips outside the four
allowed Release adaptations, inconsistent summaries, empty compiler corpora,
changed inputs and timeouts. The input fingerprint includes the normative
`SPEC.md`; matrix cardinalities must be positive integers. Reverse inventories also reject unregistered local
test files, uncovered local modules or unregistered runtime matrices. Compiler
product children must have quiet stdout/stderr, including the oracle; matching
marshal payloads cannot hide diagnostics. Intentional Python SyntaxWarning
semantics have separate runtime fixtures, including `sys_runtime.py`. Timed-out
stage process trees are stopped.

The final accepted source/test SHA-256 is
`5a15dd86f8ec358ce48e7c9382b68fab3ab0b436ee59df1d9c53e007dee7bb5f`.
The post-run fingerprint matched; all 138 recorded stages have PASS status.

An earlier scoped Debug measurement for 100,000 `pack_into('<d', ...)` calls showed a
median CPU time of 0.074736 s before the direct-buffer change and 0.049017 s
after it, across five interleaved runs (about 34% lower). Bytes and allocator
balance match. Existing pools keep the external allocation count unchanged;
this is a local microbenchmark, not an application performance guarantee.

## Continuing the audit

[coverage.json](coverage.json) maps 16 semantic domains and their permanent
fixtures; [TEST_PLAN.md](TEST_PLAN.md) defines applicable variant axes, callback
states, lifetime checks and future acceptance gates. [tests/README.md](../README.md)
describes adding regression witnesses and running the full matrix. Confirmed
supported-scope discrepancies from all five audit passes are fixed; the one explicit
CPython pending-error boundary is described above. The finite products are exhaustive
for their operand lists; arbitrary Python programs, callbacks, host extensions
and other ABIs remain unbounded. Declared SPEC limitations stay explicit.

Builtin documentation strings remain abbreviated; these products verify
descriptor/documentation behavior with authored values and do not assert full
CPython documentation-text parity. The aggregate matrix enumerates stable
ordinary callbacks. Earlier local regressions retain Python 2 general `map`'s
ignored length-hint-error behavior; arbitrary pending-C-exception paths are
not established by that bounded coverage.

The decoder diagnostic phase and keyword-parser behavior were checked against
ordinary source/callback witnesses and independently reviewed against
[CPython ast.c](https://github.com/python/cpython/blob/v2.7.18/Python/ast.c),
[tokenizer.c](https://github.com/python/cpython/blob/v2.7.18/Parser/tokenizer.c)
and [getargs.c](https://github.com/python/cpython/blob/v2.7.18/Python/getargs.c).
