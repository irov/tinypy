# Conformance test plan

The contract is [SPEC.md](../../SPEC.md). External CPython 2.7.18 supplies the
expected Python-visible behavior. Every confirmed supported-scope discrepancy
gets a small local regression and, where relevant, a permanent finite product
domain. [coverage.json](coverage.json) assigns the existing fixtures to domains;
[the coordinator](../run_validation.py) verifies their inventories and runs all
acceptance layers together.

## Choosing variants

For each operation, enumerate the applicable rows below. A row may contain
several operands. Products cover bounded independent axes; separate stateful
tests cover callbacks that affect later observations.

| Axis | Required variants when applicable |
| --- | --- |
| Entry point | Constructor, reinitialization, bound/unbound method, direct descriptor/slot, operator, reflected operator, augmented assignment |
| Receiver | Exact builtin, builtin subclass without override, overriding subclass, classic class, new-style class, invalid receiver |
| Operand | Exact accepted type, subtype, protocol-only object, missing protocol, wrong protocol result, raising protocol, NotImplemented |
| Arguments | Omitted/default, positional, keyword, mixed, duplicate, missing, extra, Unicode names, names containing NUL, unknown name |
| Numeric boundary | Bool, zero, signed zero, positive/negative, C-int bounds, index-size bounds, widened long, nonfinite/subnormal/maximum finite double |
| Text | Empty/ASCII/non-ASCII, byte/Unicode combinations, subtype identity, valid/invalid codec names, embedded NUL, legacy buffers |
| Sequence | Empty/singleton/many, exact/subclass iterable, self/overlapping input, forward/reverse/exhausted iterator, slices with omitted/negative/huge/zero-step bounds |
| Mapping/set | Empty/many, collisions, equality orientation, unchanged size versus changed size, views, existing/missing key, early match/failure |
| Comparisons and hash | Direct builtin rich method versus operator/cmp, strict subclass/reflected order, NotImplemented, arbitrary rich-result identity, classic versus new-style return conversion, truth and hash errors |
| Type layout | One/three-argument type APIs, metaclass winner and original kwargs, iterable/string/Unicode slots, duplicate and special slots, namespace mutation during slot iteration, canonical physical layout and class reassignment |
| Generator control | New/paused/finished/running, next/send/throw/close, explicit StopIteration payload, requested exception type, constructor failure, handlers/with/finally and post-failure recovery |
| Regular expressions | Valid byte/Unicode patterns, empty matches, captures/names, branches/repeats/lookarounds, exact/subclass/legacy-buffer subjects, independent bounds, scanner progress/metadata, negative/zero/positive counts and replacement callbacks |
| Buffer | Read-only/writable, bytearray/legacy buffer/memoryview where accepted, outstanding export, child view, overlap, offset and size boundaries |
| Compiler | exec/eval/single, optimize 0/1/2, explicit/inherited futures, bytes/Unicode, newline/CRLF, valid syntax and parser/AST/symbol/codegen errors |
| Source decoders | Default byte encoding, ASCII/Latin-1/UTF-8 cookie and BOM, bytes/Unicode input, LF/CR/CRLF, byte versus Unicode/raw literal, escape spans before/after non-ASCII text, decoder-versus-parser error precedence |
| Native keyword parser | String/Unicode/subtype names, equality True/False/raising Exception/BaseException, prior handled exception, duplicate/missing/unknown names, per-parameter conversion and raw-name validation order |
| Descriptor construction | Accessor None/omitted/replacement, exact/subtype property, doc callback sees published state, Exception/BaseException doc lookup, callable-descriptor binding and argument-error priority |
| State | Fresh/reinitialized object, partial progress, failed operation followed by recovery, per-VM state and separate VM isolation |
| Lifetime | Retained input across callback, removed input, finalizer/weakref notification, borrowed versus owned reference, zero allocator balance |

Test Python 2 conversion protocols separately: `__int__`, `__index__`,
`__float__`, truth and length conversion are not interchangeable. Stored builtin
subtype values sometimes bypass overrides, while other entry points invoke
them. Direct slot calls and operator dispatch also need separate witnesses.

## Stateful callback scenarios

Use small, bounded callbacks and observable event lists. Cover successful,
invalid-result and raising callbacks at each applicable conversion/iteration/
comparison/descriptor/finalizer boundary. Observe the first error, callback
order and object state after failure.

Mutation cases include replacing an existing value, clearing a container,
adding/removing entries, changing the callback method or function defaults,
reinitializing an object, and modifying a writable exported buffer. Verify
that iterator exhaustion/errors are sticky where CPython specifies it. For
buffer exports, check resize blocking, in-place observations and cleanup after
errors. For multi-step updates, check the oracle's atomicity or partial progress.

Function binding tests retain the original code/defaults while callbacks alter
the function. Keyword tests distinguish exact strings from string subclasses
and compare equality/hash calls. Exception tests inspect type, value identity,
args, native attributes and pending exception state, including generator throw,
bare raise, nested handlers, finally and context-manager exit.

Do not compare process addresses or unspecified mapping/set iteration order.
Canonicalize values when order is unspecified; preserve order when the Python
contract makes it observable. Keep signed zero, result type and long suffixes
visible in numeric observations. Compare syntax-error args and location fields,
not merely the fact that compilation failed.

## Acceptance layers

| Layer | Acceptance |
| --- | --- |
| Portable regressions | Every selected identity discovered and executed independently on both runtimes; exact expectations, matching output/stderr, tinypy allocator balance zero |
| Runtime products | Exact unique identity count, byte-identical observations against CPython, no unexpected stderr, allocator balance zero |
| Compiler products | Exact generated inventory and source modes; marshal-v2 byte equality at all three optimization levels; quiet compiler/oracle stdout and stderr; mismatch payloads retained |
| Native/runtime CTest | Exact registrations, every selected test executed, no skips or failures; valid C API contracts, verifier, marshal/artifact, VM limits and recovery |
| Infrastructure | Acceptance rejects missing/duplicate results, ordinary skips, inconsistent summaries, empty compiler inputs, unexpected stderr, changed sources and timeouts |
| Build profiles | Debug with detector, Release, unoptimized Debug ASan/UBSan, Release LTO; strict warnings-as-errors and symbol audits |

Python-visible errors, callbacks and validation order remain in Release.
Diagnostic invariant checks may compile out. The four declared ownership-cycle
adaptations execute the detector in Debug and skip before fixture execution in
Release. No other skip or DEFER can pass the default coordinator.

Test documented valid C API inputs and explicit fallible/resource-limit
paths. Invalid native pointer/type/direct-accessor preconditions are outside
the contract. Cyclic collection, detached descriptor ownership, code-object
invariants and escaped fast-local behavior retain their explicit SPEC bounds.
Bundled module/codec coverage is limited to the declared surface; host modules
need fixtures supplied by the host.

For generators, test Python-visible preservation of StopIteration separately
from the C iterator API's consumed-exhaustion contract. Regex products must use
valid compiler-generated programs from bounded ordinary patterns. Compare
match group types, callback slicing, pos/endpos metadata and independent
bounds. Host re helper stubs test template/expand forwarding and error order;
they do not establish the external template parser's behavior.

Length-hint tests distinguish classic strict-int length results, stored new-style
integer/long subtype payloads, float/custom numeric conversion, unavailable length,
fallback hint, ignored versus propagated errors and exhausted iterators. Native
argument parsers may suppress keyword lookup errors as CPython does; checked
function-binding dictionary insertion retains its separately documented policy.
For `dict.fromkeys`, distinguish exact dict/set cached hashes from subclass
iteration and alternative writable objects returned by the constructor.

Compiler diagnostics compare the complete args/location payload and codec error
spans in the decoder's byte domain. Use a terminating non-hex tail for raw escape
failure products so the oracle's reported positions remain deterministic.
Builtin documentation text is abbreviated; use authored doc values when testing
property/doc dispatch rather than treating external documentation prose as a
semantic oracle.

## Adding and maintaining coverage

1. Confirm a compact witness on CPython 2.7.18 and record the differing
   observation before changing the runtime.
2. Add a project-authored case to the appropriate local module. Include
   adjacent successful/error/recovery variants that share the cause.
3. Extend a finite domain for operand products, keeping unique stable IDs.
   Add its exact cardinality to `RUNTIME_MATRICES`. Add the relevant fixture
   and module to the coverage map.
4. Refresh the module's discovered cases in the portable manifest and assign
   the module to its coverage domain. Reverse inventories reject unregistered
   local files or matrices and uncovered local modules/matrices. Do not rewrite
   or silently remove unchanged vendor inputs.
5. Run focused oracle cases during iteration, then the default four-profile
   coordinator for final acceptance. Freeze source inputs for that run.
6. Preserve failure logs, compiler payloads, portable results, JUnit results
   and the source/test/SPEC hash in the report. Record material platform/contract
   boundaries in the audit report.

Finite matrices exhaust their declared operand lists. Arbitrary callbacks and
Python programs remain unbounded; deterministic bounded fuzz tests and new
regression witnesses extend that boundary over time. A new feature must update
the applicable axes and coverage map rather than relying on an old PASS count.

Run the same acceptance matrix on each supported target ABI/toolchain. The
report records the reference's integer width, Unicode width and platform;
macOS arm64 acceptance does not establish another platform's acceptance.
Apple ASan lacks LeakSanitizer: the coordinator records that choice and retains
independent tinypy allocator accounting.

Compiler products intentionally use sources without compile-time warnings.
Warning semantics need a dedicated runtime/host callback fixture with exact
expected diagnostics, rather than a stderr allowlist in the product comparison.
CPython's extra-keyword insertion path can return a result with a pending C
exception. Tinypy propagates that error immediately as declared in SPEC.md;
the separate runtime fixture verifies failure-before-body and recovery in every
profile. Do not mark this observation as an oracle match.
