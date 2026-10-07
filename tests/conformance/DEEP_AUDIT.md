# Deep conformance audit — 2026-10-07

The supported contract is [SPEC.md](../../SPEC.md), with external CPython
2.7.18 as the observable-behavior oracle. Six audit passes added 565 independently
authored portable cases: the initial 109, 84 boundary/callback cases,
and 92 comparison/type/generator/regex cases (37 comparisons, 27 types,
13 control flow and 15 SRE), followed by 87 constructor/iterator/descriptor/
decoder cases (24 text protocols, 33 builtins, 17 descriptors and 13 compiler
diagnostics), followed by 110 formatting/function/namespace/introspection cases
(24 brace-formatting, 30 functional consumers, 25 namespace and 31 attribute
protocol cases), followed by 83 direct-slot and stateful callback cases
(37 containers, 30 numeric/codecs and 16 attributes/descriptors).
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
| Legacy direct slices | `__getslice__`, `__setslice__` and `__delslice__` parse C-integer bounds in order, invoke the appropriate Python 2 `__int__` hooks, reject unsupported values and clamp negative bounds to zero |
| Dictionary callback state | Equality rehashes lookup keys and suppresses lookup errors, rereads current storage after callbacks, permits value replacement and releases retained key/value in oracle order; item-view membership suppresses lookup errors, key-view membership propagates them; view comparisons use size-aware iterators; proxy copying preserves cached hashes |
| Set consumers | Generic intersection/difference stream their inputs without constructing an intermediate set; cached-hash set/dict paths preserve equality orientation, duplicate/error callbacks and the first-versus-later argument rules |
| Numeric payload methods | Long `__getnewargs__`, float `__trunc__` and compatible `__coerce__` promotions read stored subtype payloads instead of constructor overrides; same-kind coercion retains identity, rounding/overflow and bool result types remain observable |
| Codec callback lifetime | One conversion retains its resolved handler and reusable exception; callbacks use Python 2 integer position conversion, preserve original input bounds and updated error metadata, release tuple/position in oracle order and raise the original error for unencodable replacements; builtin handlers read native UnicodeError payloads without attribute overrides |
| Attribute protocol dispatch | Exact-string metadata fast paths preserve string-subclass hash/equality lookup; function data descriptors precede dictionary shadows, generic lookup errors are suppressed without stale pending state; types canonicalize mutation keys after custom metaclass hooks and publish names before releasing old values; Python call attributes use bound wrappers/class/proxy lookup while the native embedding fallback remains unchanged |
| Diagnostics and native fields | Shared invalid-int-result errors, complex conversion errors, list index/pop, codec handler/position, join/SRE and type metadata mutation diagnostics match; UnicodeTranslateError exposes its encoding descriptor with None |

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

The sixth pass additionally verifies the supported native-instance call-attribute
fallback through the existing embedding fixture. Its owned reference, identity,
call result and allocator balance are checked; Python-facing functions, methods,
builtin functions, partials, classic classes, weakrefs and callable proxies use
their normal descriptor or attribute paths. The new tests were independently
reviewed and rerun against fresh strict Debug builds before aggregate validation.

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
| Legacy direct slots, dictionary/view/proxy callbacks and set consumers | 2,990 |
| Numeric subtype payloads, codec handler state/positions, join and SRE callbacks | 1,019 |
| Attribute-name protocols, function descriptor shadows, type mutation and call wrappers | 808 |
| Ordered nested statement/action pairs, branch/suppression states and generator drivers | 2,048 |
| **Total unique runtime outcomes** | **234,209** |

Operands include integer widening, long, bool, float, complex, infinity, NaN,
subnormal and maximum finite doubles. All twenty matrices compare complete stdout to
the oracle and verify unique identities, exact cardinality and zero outstanding
tinypy allocations. They compare exception classes; protocol regressions also
assert callback order, mutation, identity and selected exception messages.

The generated compiler corpus contains 813 `exec`, 420 `eval` and 420 `single`
sources. It includes 320 ordered control-wrapper/action tuples, 25 nested
closure/argument tuples and 24 line-gap/instruction-run tuples. An independent
host assertion checks every tuple's filename; a separate bounded runtime product
checks control execution and suspended unwind traces.
Each source is checked at optimize 0/1/2. Another 138 checked-in vendor, local,
runtime and matrix sources undergo three-level `exec` comparison. This gives
5,373 byte-identical marshal-v2 comparisons per profile, 21,492 across all four.
No compiler mismatch was found in these inputs.

Three explicitly identified attribute-product rows coalesce consecutive identical
unsuccessful `module.__dict__` equality probes with a pure False result. Dictionary
perturb probing can revisit a bucket; differing builtin type-dictionary sizes
change that repeat count. These rows still compare hash calls, callback occurrence,
result/error payloads and every other event. Successful, mutating and raising
callbacks retain exact traces. No runtime lookup change or general trace
normalization was introduced for this implementation detail.

Dynamic compiler diagnostics use logical filenames with no filesystem source
lookup. Semantic-error text is None, matching the oracle when that named file
is unavailable. Core filesystem access remains outside the embedding contract.

## VM name presets

The VM registry now contains 729 persistent names, including class/module
metadata, fixed protocol names, keyword parameters, codecs and compiler labels,
module-local methods, builtin type/exception names and `__future__` features.
Every fixed production name is created at VM startup and uses an `internal_`
field. The preset lookup table has 2,048 entries with a compile-time load bound.
Byte-name adapters reuse those objects through an owned-reference factory;
fixed metadata lookups borrow their VM fields directly. The shared registry
defines fields, initialization, initial interning policy and shutdown roots.
Compiler comprehension labels retain their non-interned marshal representation.
The lookup table keeps spare capacity with a compile-time registry bound.

The 24 indexed operator names now borrow registry fields through an offset
table and `tinypy_internal_object_special_operator_key(vm, index)`. Their
duplicate VM array, initialization and owned references were removed. The 81
native wrapper slots also use registry offsets and pointer/content comparison.
Core/runtime fixed semantic-name comparisons use VM presets, including codec
aliases with their existing normalization and Python 2 NUL-prefix quirks.
AST name checks reuse VM keys; raw parser tokens and standalone C input APIs
retain bytes without creating Python name objects. Native checks cover borrowed
operator references and preset/raw/Unicode wrapper classification with full-span
NUL rejection. Source guards enforce indexed tables and fixed comparisons.

The native `intern_lifetime` case checks all registry entries and operator keys
for cached identity, owned-reference balance and allocation-free reuse. It also
checks independent VM ownership, initial interning flags, exact byte spans,
uncached fallback and zero allocator balance after shutdown. It also verifies
owned singleton accessors and single evaluation of object and VM expressions.
The full four-profile
acceptance matrix below was rerun after this change; the portable inventory
remains unchanged.

`TINYPY_RET(value)` returns the same non-null object with one added reference,
using a portable inline helper so the expression is evaluated once. Its VM
accessors cover None, True, False, NotImplemented, Ellipsis, the empty tuple and
the empty byte string. Runtime/compiler ownership pairs use this shared
primitive; the previous object-specific helper was removed. Public C getters
retain their existing ABI and ownership contract.

Ordinary and checked byte-string constructors now reuse an existing interned
string from the same VM, adding one owned reference. Empty and one-byte strings
keep their existing constant caches; misses create a normal string without
automatically interning it. The lookup shares the existing seeded byte hash and
skips hashing strings longer than the table's entry-length upper bound. Entries
remain borrowed and lookup is disabled during VM destruction.

Class metadata and special-slot comparisons use `TINYPY_NAME_EQ`: pointer
equality returns immediately, while distinct byte strings, Unicode and string
subclasses retain the existing content comparison without invoking `__eq__`.
Native method, classmethod, staticmethod, property and module-function
registration now share common helpers accepting borrowed string keys. Fixed
names, module-local methods and operator tables use named VM presets.
`TINYPY_INTERNAL_STRING` remains available to extensions and tests and pins
interned keys until VM shutdown. It
computes literal sizes, including embedded NUL, and evaluates VM once. Ordinary
runtime strings never enter this owned literal dictionary. The SRE keyword
adapters are selected explicitly at registration without scanning string names;
bytearray bridges retain static callback specs and borrow keys by VM field
offset. Shared helpers preserve descriptor kinds, binding, finalizers and
module metadata. Public type attribute get/set support both byte names and
borrowed string keys, with identical descriptor behavior.
Core C sources cannot use lazy literal factories: the host guard checks this
alongside prefixed, unique registry fields and lookup-table capacity. Native
coverage checks that the lazy dictionary is absent after startup, every preset
is reused without allocation, and byte/Unicode function-code aliases agree.
`func_code` and `__code__` retain distinct keys for the same Python 2 attribute;
pointer comparison still has a content fallback for incoming noncanonical keys.

Builtin attribute lookup now uses immutable string metadata for preset IDs
and function tables for the object-kind/name pair. All VM preset strings
borrow process-lifetime metadata; zero IDs designate names without a getter.
The 66-entry registry maps to ten sparse kind tables, 65 registered getters and
71 handler slots including aliases. The tables occupy 5,776 bytes of shared
pointer storage on macOS arm64, plus 67 bytes for the metadata records.
Canonical `__class__` lookup checks its VM pointer before reading name bytes;
raw/Unicode names share the same class getter through the content fallback.
Common dunder getters, function-code aliases and module dictionary access
retain direct paths. Raw/Unicode names now borrow presets from the existing
2,048-slot internal-name cache and read their metadata. The duplicate 256-slot
builtin-name table and its maximum-length field were removed, saving another
2,056 bytes per VM. Full-span comparison avoids subtype callbacks and preserves
the incoming key without retaining or inserting it. The metadata pointer adds
eight bytes to every allocated byte string; its introduction had previously
saved 248 bytes per VM by replacing the parallel ID array.
No per-name allocation, Python reference or new string type is introduced.
Boolean attribute-lookup flags consistently use `TINYPY_TRUE`/`TINYPY_FALSE`;
integer statuses and bit masks retain their numeric representation.
Protocol and compiler operations load their local VM before accessing its
internal keys. The 73 nested `TINYPY_VALUE_VM(...)->internal_*` expressions
were replaced; existing VM locals are reused, and nullable results/referents
are checked before their VM is accessed. Raw compiler/marshal strings and
immutable string-subtype copies have no preset metadata, while direct Unicode
keys have no metadata field; they reuse the shared cache instead of a separate
builtin-name lookup. A public `getattr(code, Name('co_varnames'))` check, where
`Name` subclasses `str`, matches CPython 2.7, including NUL-suffixed rejection.
Native regressions check shared metadata across VMs, allocation-free
raw/Unicode metadata lookup, borrowed key ownership and occupied-bucket
collisions using embedded-NUL names. Immutable subtype/plain copies clear
metadata and growable strings cannot retain an obsolete ID. A host guard
verifies unique dispatch names/IDs, eager interning, table capacity and handler
coverage for every registered name.
Descriptor and custom-hook priority remain covered by the complete runtime matrices.

The subsequent full name audit migrated module values, object/instance/type
attributes, special-method dispatch, constructor/keyword tables, imports and
type/module creation to value keys. Fixed lookup tables borrow VM fields;
static SRE callback specs store field offsets instead of byte names and lengths.
Public byte-name APIs remain adapters to the key implementations. Module and
type key constructors retain the exact input string without implicitly
interning ordinary names. Direct instance key access retains its dictionary
semantics, separate from Python descriptor binding. Native coverage checks
these ownership rules, embedded-NUL keys, byte interoperability, allocation-free
replacement, imports and `sys.stdout`. A host source guard rejects internal
byte-name API calls and fixed literals passed to the owned-name byte factory.
Bootstrap C type names, parser tokens, diagnostics and dynamic host import
paths retain their required byte representation.
Meta/preprocessor AST identifier predicates also use ready interned keys and
pointer/content comparison; the source guard covers fixed meta name arguments.

Compiler literals, logical filenames and raw marshal strings bypass intern
lookup. Folded interned results are copied before changing their serialization
policy, preserving VM presets. Native coverage verifies raw filename flags and
byte-identical dump/load/dump in a VM with matching presets; the generated
compiler corpus also compares preset literals and folded results with CPython.
Dynamic lookup coverage includes embedded NUL, no extra allocation, balanced
references and removal after the last owner releases the string.
Ordinary live generated strings never grow the table. AST and meta identifiers
are inserted at creation rather than merely receiving an interned flag. They
remain separate from non-interned compiler labels with the same spelling.
Native tests check script identifier insertion, label flags, key retention,
byte/key API interoperability and method descriptor ownership. Literal tests
check pinning an existing weak intern entry, allocation-free repeated lookup,
independent VMs, embedded NUL and raw compiler labels. Registration tests invoke
every binding kind, verify module metadata and check seven finalizers run
exactly once before verifying complete allocator balance at VM shutdown.

A bounded Release comparison for the name-preset change against the fifth-audit
accepted build used 20,000
calls to the classic class factory or `type('Generated', (), {})`, with seven
interleaved runs per build. Median CPU time fell from 0.007086 s to 0.006178 s
for classic classes (12.8%) and from 0.028990 s to 0.024488 s for new-style
classes (15.5%). Output matched and allocator balance was zero in every run.
These are local workload measurements, not a general runtime speed guarantee;
the recorded results are in `.temp/preset-benchmark.json`.

A separate Release `-O3`, LTO-off comparison uses the standalone
[attribute benchmark](../../cli/tests/attribute_dispatch_benchmark.c) against
the preceding accepted preset-name build. Seven interleaved runs measure
5,000,000 direct dispatches and 500,000 complete C attribute lookups per case,
excluding VM creation and compilation. Complete-lookup median CPU seconds
fell from 0.010041 to 0.005445 for `func_dict`, 0.006803 to 0.003672 for
`co_varnames`, 0.009979 to 0.003900 for `co_lnotab`, and 0.009701 to 0.004805
for `__module__` (1.84–2.56 times faster). Common `list.append` lookup was
essentially unchanged (0.012158 versus 0.012190); `__class__` was 2.6% slower
(0.002665 versus 0.002733). The getter validates hit/miss counts in every run.
These short local microbenchmarks do not establish application performance;
samples and medians are in `.temp/builtin-attribute-benchmark.json`.

A subsequent metadata/function-table comparison uses that accepted hash-table
runtime as its baseline (`f47e5188d22f5e209b098daf5dd8c218567df65c4eca399dbc1f4ebe019373f4`).
The same benchmark is compiled with `TINYPY_ATTRIBUTE_BENCHMARK_SCALE=10`:
seven interleaved runs, 50,000,000 direct dispatches and 5,000,000 complete
lookups per case, Release `-O3` without LTO. Complete-lookup median CPU seconds
fell from 0.042539 to 0.035800 for `func_dict`, 0.029181 to 0.026394 for
`co_varnames`, 0.029368 to 0.027044 for `co_lnotab`, and 0.042289 to 0.036232
for `__module__` (7.9–15.8% lower). `__class__` fell from 0.023601 to 0.020038
(15.1% lower); direct metadata dispatch fell by 16.3–23.0% for the four named
fields. The raw unknown-name direct miss was 1.6% slower. Hits/misses matched
in every run. Results and all samples are in `.temp/builtin-metadata-benchmark.json`;
they describe these local lookup workloads, not application performance.

## Final validation

The default [matrix runner](../run_validation.py) completed all 158 stages on
macOS arm64. C and C99 standalone builds used strict warnings-as-errors.

| Profile | Native/runtime CTest | Portable oracle cases | Runtime outcomes | Compiler comparisons |
| --- | --- | --- | --- | --- |
| Debug, detector on | 89 PASS | 1,235 PASS | 234,209 identical | 5,373 identical |
| Release | 89 PASS | 1,231 PASS / 4 diagnostic SKIP | 234,209 identical | 5,373 identical |
| Unoptimized Debug, ASan/UBSan | 89 PASS | 1,235 PASS | 234,209 identical | 5,373 identical |
| Release, LTO | 89 PASS | 1,231 PASS / 4 diagnostic SKIP | 234,209 identical | 5,373 identical |

All 140 CTest registrations were checked against the exact inventory. Portable
cases are run separately with the external oracle; their CTest wrappers are
not counted again as executed native tests. Host/API/runner acceptance checks
passed 89/89. Standalone marshal/artifact and core symbol audits passed in all
profiles. No ASan/UBSan diagnostics occurred. Apple ASan lacks LeakSanitizer;
macOS records `detect_leaks=0`, while allocator balance is checked independently.

Logs, per-case portable results, JUnit results and the aggregate source/test
SHA-256 are in `.temp/validation-sixth-audit-accepted/report.json` and its linked artifacts. The
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
`e8bb0fc9efaa60edf5d53ba258b450542d511e45c18f81df3abbe1e799c191e7`.
The post-run fingerprint matched; all 158 recorded stages have PASS status.
The object-dispatch, native-wrapper classification, exception-registration and
VM `_WIN32` branches also passed
standalone strict C11 syntax checking on macOS; this does not constitute a
Windows runtime check.

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
supported-scope discrepancies from all six audit passes are fixed; the one explicit
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
