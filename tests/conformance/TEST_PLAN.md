# Conformance test plan

The contract is [SPEC.md](../../SPEC.md). External CPython 2.7.18 supplies the
expected Python-visible behavior. Every confirmed supported-scope discrepancy
gets a small local regression and, where relevant, a permanent finite product
domain. [coverage.json](coverage.json) assigns the existing fixtures to domains;
[the coordinator](../run_validation.py) verifies their inventories and runs all
acceptance layers together.

The eleventh-audit inventory contains 40 finite runtime matrices with 337,542
outcomes per profile. The ninth-pass additions cover metadata/receiver arguments
(325), numeric arguments and snapshots (995), container arguments and error
phases (3,756), and function construction/reduction/saved frames (1,026).
The tenth pass adds buffer arguments (7,456), iterator metadata (5,232),
native wrappers (1,556) and supported Struct parser/state/cache behavior (5,986).
The eleventh pass adds live-buffer lifetime/character views (7,524), text
arguments/padding/codecs (26,217), native readiness (913) and weakref protocols
(5,032). The portable inventory contains 49 project-authored modules with 1,538
cases and 210 selected vendor cases, totaling 1,748. These are declared coverage
counts; completed profile acceptance comes from the coordinator's report.

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
| Buffer lifetime | Requested versus current bounds, shrink/grow/regrow, nested root ownership, raw versus encoded Unicode view, cached first hash, owner/self repr roles, numeric valid/invalid prefixes before NUL |
| Native readiness | Isolated cold first write/delete, nonwarming operations, successful/failed first reads, shared type state across instances, safe scalar/nullable direct fields, physical member/getset kind |
| Weakrefs | Exact/subtype construction, explicit new/init/call, basic ref/proxy reuse, callability changes, callback/list order, supported referent kinds, live/dead lookup, cached empty frozenset, generator callback-before-finally |
| Compiler | exec/eval/single, optimize 0/1/2, explicit/inherited futures, bytes/Unicode, newline/CRLF, valid syntax and parser/AST/symbol/codegen errors |
| Compiler folding | Mixed numeric constants, signed zero, widened long, complex and extreme finite float values; byte/Unicode/tuple operations, invalid folds, indexing and shifts, repetition around the 20-item fold threshold, constant type separation and all supported future combinations |
| Future inheritance | All 32 caller future combinations × 32 explicit combinations × inherit/dont-inherit × exec/eval/single × byte/Unicode source; compare complete code shape and executed value/result types |
| Source decoders | Default byte encoding, ASCII/Latin-1/UTF-8 cookie and BOM, bytes/Unicode input, LF/CR/CRLF, byte versus Unicode/raw literal, escape spans before/after non-ASCII text, decoder-versus-parser error precedence |
| Native keyword parser | String/Unicode/subtype names, equality True/False/raising Exception/BaseException, prior handled exception, duplicate/missing/unknown names, per-parameter conversion and raw-name validation order |
| Native method arguments | No-argument/single-argument methods, parsed/unpacked tuples and slot wrappers; implicit receiver excluded from supplied counts, bound/unbound/explicit descriptor binding, missing/wrong receiver, keyword-versus-count-versus-conversion priority, named/anonymous diagnostics and post-failure recovery |
| Type metadata | Exact/subtype byte-name identity, classic/heap/native types, writable versus read-only name/module fields, embedded NUL validation, short versus qualified names in type/object/descriptor representations, builtin and extension-module names |
| Function construction | FunctionType code/globals/name/defaults/closure variants, omitted/duplicate/unknown arguments, NUL-terminated keyword validation versus dictionary equality lookup, optional keyword lookup suppression and prior handled exception, original name/default/closure identity and namespace metadata |
| Reduction protocols | Direct __reduce__/__reduce_ex__, optional protocol conversion before overrides, stored int-subtype protocol, old/new protocol lookup counts, getnewargs/state/dictionary/slot access, cached __slotnames__ versus copy_reg._slotnames, result type/identity and callback error recovery |
| Saved frame exception fields | Stopped/suspended/active frame f_exc_type/f_exc_value/f_exc_traceback getsets, descriptor metadata, read/write/delete, saved versus currently handled exception, field clearing before old-value finalizers, incoming-value publication and handler restoration |
| Descriptor construction | Accessor None/omitted/replacement, exact/subtype property, doc callback sees published state, Exception/BaseException doc lookup, callable-descriptor binding and argument-error priority |
| Brace formatting | Byte/Unicode receiver, automatic/manual numbering, attribute/item paths, decimal indices, conversion before nested specs, converted text subclass format hook, original builtin format-spec identity, parser error and iterator recovery |
| Execution namespaces | eval/exec source/code/tuple forms, globals dict subtype versus custom locals, builtin insertion/error precedence, LOAD_NAME suppression versus LOAD_GLOBAL propagation, cold/warm name cache and collision callbacks, global versus local assignment/deletion |
| Functional consumers | Partial construction/call/state conversion and replacement order, stored keyword hashes, enumerate/xrange numeric conversion, reversed length and terminal states, reduce accumulator lifetime and retained call-argument tuples |
| Stateful operator dispatch | Classic left/right coercion, replaced operands, same-type/strict-subtype direction, successful/NotImplemented/raising inplace hooks, binary versus ternary power, reflected method descriptors and ignored lookup errors |
| Iterable materialization | Negative hints -1/-2/-3, constructor/reinitialization versus method error boundaries, direct versus syntax assignment, modern versus legacy slice bounds before/after callbacks, input errors distinguished from collector-owned errors |
| Modules and introspection | Allocation-only module state, initialization/reinitialization keywords, lazy builtins dictionary, vars/dir protocol reads, spoofed class and metaclass instance checks, retained module dictionary and teardown callbacks |
| State | Fresh/reinitialized object, partial progress, failed operation followed by recovery, per-VM state and separate VM isolation |
| Lifetime | Retained input across callback, removed input, finalizer/weakref notification, borrowed versus owned reference, zero allocator balance |
| VM name presets | Every registry entry, dispatch operator and singleton accessor reuses its VM object without allocation; eager initialization of all fixed core names, no lazy dictionary after startup, unique registry fields with `internal_` prefix and bounded lookup capacity; ordinary/checked constructors, no insertion of ordinary generated strings, script identifier insertion, byte/key attribute registration and descriptor ownership, owned-reference balance, single argument evaluation, full byte-span matching, embedded-NUL intern entry lifetime, pointer/content protocol comparison, byte/Unicode function-code aliases, independent VMs, compiler/marshal interning flags and shutdown allocator balance; extension/test C-literal pinning and repeated allocation-free lookup; shared method/wrapper/classmethod/staticmethod/property registration, module metadata and exactly-once finalizers; exact borrowed module/type names, module/direct-instance value keys, descriptor versus direct-dictionary semantics, embedded-NUL byte interoperability and allocation-free replacement, key imports and source guard against internal byte-name and lazy literal calls; indexed operator/wrapper-slot tables reuse registry offsets, operator access has no extra roots or references; AUTO wrapper classification covers preset/raw/Unicode keys and embedded-NUL rejection; guard against fixed literal comparisons in core/runtime |
| Builtin attribute dispatch | Shared immutable string metadata supplies preset IDs without hashing; function tables retain descriptor/custom-hook priority and alias semantics; zero IDs and absent handlers, subtype/plain copies and growing strings clear metadata; raw/Unicode metadata keys, allocation-free reference reuse, full-span embedded-NUL collision probes, no dynamic key insertion, independent VM ownership and eager dispatch registry/capacity/handler coverage checks; standalone Release benchmark compares direct dispatch and complete C attribute lookup |
| Nested control | Ordered pairs of plain/if/while/for/except/finally/with/multiple-with wrappers, record/return/break/continue/raise/yield actions, true/false branch inputs, suppressing/propagating context managers, generator drain/throw/close and final frame state |
| Codec callback state | Exact/subtype tuple and Unicode replacement, independent int/index/float position protocols, handler registry replacement, cached exception identity, mutation of error metadata/object, replacement encoding failure, return-tuple/position finalizers and failure recovery |
| Text method state | Bounds and scalar conversions before needle validation, contextual character-buffer errors, subtype identity on unchanged results, native versus Python callback slicing of captured groups |
| Codec invocation | Encoding omitted/default/explicit, errors omitted versus explicit, one versus two callback arguments, keyword parser ordering, ASCII normalization of Unicode names and exact byte-string arguments |
| Import sequence protocol | Indexed fromlist/__all__ versus iterator, ignored __path__/member lookup errors versus propagated __all__ errors, byte versus Unicode names, star-prefix recursion, missing-module error before fromlist truth, lazy host importer metadata |
| Sys and output state | Negative frame depths, int versus index conversion, C-int bounds, special __sizeof__ lookup and numeric results, TypeError-only default, softspace getter/setter exchange, write lookup before str/repr, replaced stdout and builtin module, initial/final newline and prior handled exception |

Test Python 2 conversion protocols separately: `__int__`, `__index__`,
`__float__`, truth and length conversion are not interchangeable. Stored builtin
subtype values sometimes bypass overrides, while other entry points invoke
them. Direct slot calls and operator dispatch also need separate witnesses.
Classic binary operators must cover the left coercion/hook before the right
coercion/hook, replacement operands, reflected ordering and inplace fallback.
For builtin subclasses, distinguish a rejected numeric slot from a sequence
concatenation/repetition fallback, and observe reflected-method descriptor
lookup and comparison. Returning NotImplemented from an inplace hook must
not repeat that hook.
Binary and ternary power need separate products: an explicit None modulus uses
binary dispatch, while an actual third operand follows Python 2 coercion and
promotion rules. Check descriptor lookup errors without replacing a prior
handled exception or a user-raised exception that has the same diagnostic text.

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

Native method products distinguish the C calling conventions rather than
sharing one generic argument diagnostic. Count user arguments separately from
the implicit receiver, including bound descriptor clones and explicit
__get__ binding. Verify missing/wrong receiver priority, keyword rejection
before arity and conversions, and parsed versus unpacked versus wrapper
messages. Use valid receivers for conversion and mutation callbacks; after
failure, inspect state and perform an independent successful operation.
Native C API calls must preserve the same VM exception with an explicit error
output and with `out_error == NULL`; a receiver fallback must not replace a
preceding parser error. Exercise arity and keyword failures and then recovery.
For slice replacement, distinguish failure while obtaining an iterator from
failure while consuming it, preserving user exception identity where required.

Metadata tests compare retained name objects by identity and observe both short
and qualified representations. Test classic assignment separately from heap
type construction and builtin read-only metadata; Unicode/NUL validation and
subtype names must retain their own error and identity behavior. Avoid comparing
process addresses when checking native type or descriptor representations.

Native metadata products distinguish method-wrapper, builtin C methods,
method_descriptor and wrapper_descriptor. Enumerate their member/getset fields,
read/write/delete and direct descriptor receiver checks, binding, call arity,
comparison/hash callbacks, handled-error recovery and alternating free-list
reuse. Compare address/hash invariants within each process. Record cold first
writes separately from namespace-ready writes: CPython's lazy type readiness
can change the exact exception class/message, and a warmed product cannot prove
that cold behavior.
Schedule nonwarming rows before the first read in a fresh process, then observe
the shared type transition. Keep safe direct-field checks distinct from ordinary
attribute lookup. Numeric field descriptors require physical member/getset kind,
metadata, readonly and wrong-receiver checks as well as returned values.

Iterator metadata products enumerate exact/subtype bytearray/list/tuple/text,
classic and new-style indexed sequences, missing/raising/wrong-result length
hooks, active/exhausted states, bounded shrink/grow and hint-callback reentry.
Direct bytearray hints can be negative after shrink; structural consumers clamp
their allocation hint. Check str/unicode subtype item/length hooks and the long
result of listreverseiterator's direct hint separately.

Buffer argument products cover bound and unbound methods, keywords/counts,
readonly versus writable views, legacy slice conversion, bytearray scalar and
buffer validation order, lazy tuple-prefix validation and post-failure state.
Physical capacity and seeded hashes use stated invariants. Buffer lifetime rows
compare complete repr grammar and within-process owner/self address roles,
preserving offset and requested size. Resize a retained bytearray owner below
and above those bounds; nested views must observe the root without losing their
requested ranges. Distinguish cached first hash from current bytes, raw Unicode
code units from encoded character views, implicit versus explicit numeric base,
and valid/invalid prefixes before an embedded NUL. Parser/conversion errors and
callback identity remain exact.

Weakref products separate base ref, subtype ref and callable/noncallable proxy
construction and explicit __new__/__init__/__call__. Test class validation before
referent arity, keyword rejection phases, cached basics and newest-first callback
references. Cover supported function/method/generator/set/frozenset lifetimes,
fresh subtype values and retained exact empty frozensets. A repr callback can
change the reference state: compare the state reread after lookup and preserve
the handled exception. Dead-reference dictionary removal performs one lookup;
KeyError/subclasses alone are suppressed, while other errors propagate and a
subsequent successful operation confirms recovery.

Struct products stay within the supported d/repeat/prefix formats. Cover module
cache versus compiled-instance payload, byte subtype identity, Unicode ASCII
conversion, NUL suffixes, omitted/duplicate/unknown keywords and buffer/offset
conversion priority. Verify failed reinitialization state, readonly format/size
getsets, immutable base type and writable subtype instance dictionaries, and
weakref release. Allocation-only __new__ witnesses inspect metadata and then
initialize before pack/unpack; they never execute uninitialized compiled state.
The exact _struct.error class follows its declared ValueError alias; preserve
other exception classes and callback traces. Physical __sizeof__ bytes use the
stated result-type/positive-size invariant.

FunctionType tests separate dictionary equality lookup from final C-string
keyword validation, including embedded NUL names and False/raising equality
callbacks. Retain original code, defaults, closure and namespace objects while
callbacks execute. Reduction products distinguish optional attribute lookup
suppression from errors raised by the called method. Observe protocol conversion
before an override, the old/new protocol lookup sequence, and cached slot names
versus a replaceable copy_reg._slotnames callback. Compare tuple/state identity,
slot values, partial failure and a successful recovery operation.

Saved-frame getset tests retain the frame and prior values while replacing or
deleting f_exc_type, f_exc_value and f_exc_traceback. For replacement, old-value
finalizers observe the cleared field before the incoming value is published.
Test stopped frames separately from active handlers and suspended generators.
Distinguish the currently handled exception from the saved triple and record
how clearing, deleting or replacing saved fields affects the exception restored
when a called frame returns.

Do not compare process addresses or unspecified mapping/set iteration order.
Canonicalize values when order is unspecified; preserve order when the Python
contract makes it observable. Keep signed zero, result type and long suffixes
visible in numeric observations. Compare syntax-error args and location fields,
not merely the fact that compilation failed.

Unsuccessful dictionary probes can revisit a bucket; exact probe repetition
depends on physical table capacity and layout. A pure False equality callback
may coalesce consecutive identical probe events only in an explicitly identified
product row. Hash calls, callback occurrence, results and errors stay visible.
Keep successful, mutating and raising callback traces exact; do not use general
trace normalization to conceal distinct semantic lookups.

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
Apply the same exact-versus-subclass distinction to set construction/update:
cached-hash traversal is independent of the generic iterator contract. Retain
keys across comparison callbacks and observe the current source storage after
bounded mutation. Test duplicate/equality/hash failures and partial progress.
Repeat these cases for subset ordering and all set algebra entry points; equal
sizes and reversed operands change the probe direction. Partial keyword copying
must keep collision callbacks, source ownership and callback-visible state
replacement observable. Avoid unrelated colliding keys whose bucket order would
make the trace depend on an unspecified physical dictionary layout.

Negative length hints have different Python 2 error boundaries for constructors,
methods and direct list slice slots. Check their exception classes, selected
stable messages, mutation and recovery independently. For internal diagnostics
that embed a CPython C source filename and line, assert the reason and callback
state without requiring that build-specific location. Legacy list slicing
normalizes bounds after replacement materialization; modern slice assignment
normalizes before it. Cover omitted, negative, large and stepped bounds with
callbacks that grow the destination.

Warm a name cache before repeating callback-sensitive lookups. Colliding custom
keys must still compare on every lookup, including misses that fall back to
builtins. Check native global dictionary writes separately from local mapping
hooks. Frame/builtin lookup suppresses errors where Python 2 uses PyDict_GetItem;
LOAD_GLOBAL's ordinary interned-name path propagates them. Follow both paths
with an independent successful operation and inspect the prior handled exception.

Formatting tests record conversion, nested-spec and final format callbacks in
order. Inspect returned text subclasses independently with str/repr/format, then
through brace formatting. Formatter iterator tests continue after a parsing
failure to observe terminal state. Module teardown tests retain the namespace
dictionary and record value finalizers; read-only traversal must not clear it.
Include deletion of a later namespace key by an earlier finalizer. Test
allocation-only modules and heap subtypes before and after lazy dictionary
creation, then packages with an assigned name/path and recursive __all__.
Import requests use the canonical request name independently of the module's
stored name. A native loader replacing a module during reload must preserve
replacement identity and leave the original namespace intact; test both a
new module and an ordinary nonmodule result.
For reduce, retain previous intermediate values with weak references while the
next item is requested; a user callback retaining its call tuple requires a fresh
tuple for the next call.

Compiler diagnostics compare the complete args/location payload and codec error
spans in the decoder's byte domain. Use a terminating non-hex tail for raw escape
failure products so the oracle's reported positions remain deterministic.
Builtin documentation text is abbreviated; use authored doc values when testing
property/doc dispatch rather than treating external documentation prose as a
semantic oracle.

Legacy Unicode buffers expose native code-unit bytes rather than UTF-8 bytes.
Cover ASCII, non-ASCII, non-BMP and surrogate code units, Unicode subtypes,
offsets and sizes. Record the reference Unicode width and endianness. Retain
owners across exported views and verify lazy storage is reclaimed at shutdown.
For bytearray text methods, record scalar conversions before the receiver and
argument snapshots. Join must exhaust its iterable before copying the current
separator and items; use bounded same-length callback mutations.

For custom codecs, distinguish omitted errors from explicit errors: the callback
receives one argument in the former case. Test parser validation before registry
search and callback invocation. For SRE capture results, observe each actual
subject slice rather than only the returned text. Returned text identity needs
separate byte/Unicode and exact/subtype cases, including canonical empty Unicode.
Encoder character-buffer coercion and decoder raw-buffer acquisition have
different error-parser priorities. Use nullable, subtype, Unicode and NUL errors
names, builtin handler metadata and exact registry keys that bypass subtype
hash/equality hooks. Padding checks width before fill, byte versus Unicode fill
conversion, one character versus one byte, and subtype signed-margin behavior.

For sys output, use event lists to observe softspace reads and writes, writer
lookup, repr/str and the initial/final newline. A callback can replace stdout or
the writer, so check which object is pinned and which subsequent operation reads
the current registry. Suppressed softspace errors must preserve an outer handler.
Physical allocation sizes and GC headers are implementation dependent; getsizeof
products compare special lookup, conversion, relative custom sizes, defaults and
errors instead of requiring CPython's object layout.

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

The compiler corpus also enumerates 8 outer wrappers × 8 inner wrappers ×
5 control actions, 5 argument layouts × 5 nested closure/comprehension forms,
and 6 line gaps × 4 instruction-run lengths. The host inventory check verifies
every tuple's filename, not only the aggregate count. These 369 sources compare
marshal-v2 bytes at optimize 0/1/2; the separate runtime composition product
uses bounded loops and compares 2,048 executions, callback traces and suspended
generator unwind states. Neither compiler byte equality nor execution equality
substitutes for the other layer.

The optimizer corpus adds 2,040 valid sources: 680 bounded numeric-operation
tuples, 432 sequence-operation tuples, 96 subscript tuples, 240 repetition
tuples, 120 shift tuples, 88 unary tuples, 256 constant-type pairs and 128
future/body tuples. An independent host inventory checks every filename.
Invalid operand combinations remain syntactically valid: a failed fold must
retain the runtime operation. Powers are bounded to avoid creating enormous
constants. Future inheritance has a separate 12,288-row execution matrix;
generated source compilation alone cannot test a caller's inherited flags.

Callback-sensitive products must identify which entry points read a builtin
subtype's stored payload and which invoke its conversion hooks. Record cached
versus recomputed hashes separately for dictionary equality, copies, views and
set operations. Name lookup must distinguish canonical preset strings from
raw strings and string subclasses; type mutation has its own canonicalization
rule. Run the same observations with cold and warm caches, including data
descriptor priority, instance-dictionary shadows and a successful operation
after a callback raises. A fast path may remove Python-visible work only when
the oracle demonstrates that the corresponding callbacks do not run.

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
