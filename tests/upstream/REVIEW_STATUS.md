# Runtime review closure — 2026-10-07

The subsequent [deep conformance audit](../conformance/DEEP_AUDIT.md) adds 652
portable regressions and a repeatable four-profile matrix. Its final corpus
has 1,322 selected cases; finite runtime matrices cover 264,265 outcomes, and
compiler comparison covers 46,056 outputs across the profiles. The future
variant and acceptance plan is in [TEST_PLAN.md](../conformance/TEST_PLAN.md).
The explicit CPython pending-C-exception boundary is recorded in the audit.
The original
review findings and their historical evidence are retained below.

The supplied review described `62db39e`. This follow-up checks the current
worktree, including the earlier portable-corpus fixes. Its prototypes were
reviewed individually, not applied as one patch. CPython **2.7.18** is the
behavioral oracle; Python 3 runs only the process coordinator. All actionable
implementation items in this review have been addressed. Existing profile
contracts and evidence limits are identified explicitly below.

**Fixed** below means the relevant implementation changed or the correction
was already present. Ordinary regression tests and the existing native suite
validate the resulting worktree. **Hardened** means an ownership or boundary
correction was checked in source; the historical crash itself was not
reproduced. The supplied crashing scripts, custom nopool build and crafted
bytecode/regex programs were not run. A passing sanitizer suite does not
establish equivalence to that separate historical investigation.

## Critical items from section 1

| Original item | Current status | Implementation |
| --- | --- | --- |
| 1: long-to-double accumulation | Fixed | `long.c` bounds its significand accumulator, preserves sticky bits, rounds ties to even and reports overflow. Ordinary powers, rounding and overflow cases agree with CPython. |
| 2: implicit relative imports | Fixed | `import.c` tries a relative parent only when already registered; explicit missing parents fail. Missing-relative markers also avoid repeated failed loads. |
| 3: heap-budget MemoryError recursion | Hardened | A reserved MemoryError instance avoids recursive construction; checked pool allocations can reuse storage without external growth. Existing allocation and recovery tests pass. |
| 4: unbound builtin receiver validation | Already fixed by the earlier corpus changes | Native dispatch validates the receiver before a callback accesses its storage. The binding corpus remains active. |
| 5: recursive `type.__call__` dispatch | Fixed | Calls the internal type constructor directly. Normal metaclass delegation is covered. |
| 6: finalizer cache updated before the dictionary | Fixed | Publish dictionary changes and invalidate caches before releasing displaced objects. Finalizer assignment and removal are covered. |
| 7: classic classes in new-style MRO | Fixed | Types containing classic MRO entries bypass caches that cannot observe classic dictionary changes. |
| 8: recursive release and representation | Hardened | Allocation-free deferred release limits recursive container teardown; representation uses the logical/native stack guard. Existing deep-release tests and ordinary representations pass. |
| 9: base representations using custom `__format__` | Fixed | `oct`, `hex` and `bin` use builtin numeric formatting; normal subtype override cases agree with CPython. |

## Ownership and boundary findings from section 2

| Finding group | Current status and correction |
| --- | --- |
| Attribute/descriptors across callbacks; eval attribute caches | Hardened: own attributes, owners and descriptors through calls; borrowed cache entries require current versions. Lookup hints never invoke arbitrary equality. |
| MRO changes during lookup | Hardened: traverse an independent, owned MRO snapshot and restart after rebasing. Internal MRO storage borrows its own type and owns every other entry, including classes inserted by custom `mro()`. Its wrapper cannot be retained with a plain incref. Rebase transactions guard overlapping recursive assignments and invalidate caches on publication/rollback. |
| Weakrefs created by finalization; `__class__` changes in finalizers | Hardened: refresh the actual type after finalization and clear newly created weakrefs before teardown. |
| Subclass weakref userdata | Hardened: retain the base for the lifetime of its weakref callback. |
| Member/getset descriptors outliving a type | Hardened: weak owner references are refreshed before use. This retains tinypy's detached-descriptor behavior; CPython's strong owner lifetime is not emulated. |
| Classic descriptor binding failures | Fixed: propagate binding failures and never call a null result. `__getattr__` consumes AttributeError only when a fallback exists. |
| Recursive `isinstance` / `issubclass` / classic hierarchy walks | Hardened: logical and native stack guards cover these walks. Existing recursion/recovery tests pass. |
| Set initialization and in-place XOR | Hardened: swap contents of the backing dictionary, preserving its identity. Set iterators consequently see the current storage. |
| List index/slice callbacks and clear/repeat | Hardened: resolve conversions before reading current bounds; detach storage before decrefs. |
| Dictionary copy/update and remembered lookup slots | Hardened: preserve keys/values across callbacks; reuse cached hashes; restart when a table/key or remembered dummy slot changes. Public copy still performs collision equality, as CPython does. |
| In-place set intersection/difference; set XOR; failed intersection copy | Hardened: check versions after decrefs, retain operands, use cached hashes and check copy failure. |
| List ordering and container repr callbacks | Hardened: reread live list bounds; own entries during callbacks and reacquire table positions. |
| Call argument binding and `**mapping` merge | Hardened: use private owned dictionaries; propagate checked lookup/insertion errors. |
| Function code/closure mismatch | Hardened: validate free-variable count against the closure, including an absent closure. |
| Code stack size and frame allocation | Hardened: reject negative sizes in constructors/marshal and check frame allocation bounds. |
| Deleted `sys.stdout`; builtin print target | Fixed/hardened: missing output raises RuntimeError; output targets are retained across writes. |
| Star import, mutable `__all__`, fromlist names and host importer names | Hardened: snapshot keys or iterate an owned `__all__`; retain text used by subsequent calls. |
| Empty package import name | Fixed: avoid indexing an empty name and return the package for an allowed relative empty import. |
| Partial state replacement | Superseded by the deep audit: validate and canonicalize first, then replace each field before releasing its old value, in CPython's func/args/keywords/attributes order. Finalizers observe the new current field and the still-old later fields. |
| Native finalizers and debugger callbacks | Hardened: preserve the pending exception around callback entry. Live-VM native finalizers report ignored errors while the object and its children are retained; destruction proceeds only after the callback and any resurrection check. |
| Bytearray item/slice/find/in-place addition and memoryview indexing | Hardened: materialize conversions and replacements before rereading current storage; retain exported storage and snapshot overlapping additions. |
| `_struct` offsets | Superseded by the deep audit: acquire and retain the buffer export before offset/numeric callbacks, matching CPython's resize blocking and observation of in-place changes; release it on every exit. Supported offset protocols and error ordering have oracle regressions. |
| Overridden encode result; codec module mutation/search growth | Hardened: validate encode result types; retain a private VM codec registry; search with owned callbacks and live bounds. |
| Negative buffer slice magnitude; Unicode expandtabs | Hardened/fixed: avoid signed negation overflow; account for removed tabs and avoid null-buffer arithmetic. |
| Non-ASCII Unicode format specification | Fixed: require ASCII decoding before builtin format parsing; custom format methods retain their original specification. |
| Reversed SRE group marks | Hardened: normalize a reversed captured span to empty and check group construction failures. Crafted SRE programs were not executed. |
| `__cmp__` sign reversal | Fixed: normalize its result before negation. |
| END_FINALLY / WITH_CLEANUP verifier disagreement | Fixed: the verifier tracks normal, exception, return, break and continue markers with their exact widths/resume depths. Normal cleanup requires a proven None constant. Consuming or rewriting a marker invalidates that proof; untyped cleanup paths are rejected. Redundant runtime depth/shape diagnostics compile only in Debug; structural verification remains active in Release. |
| MAKE_FUNCTION / MAKE_CLOSURE operands | Hardened: validate code, closure tuple, cells and free-variable count before execution. |

## Python 2 semantics from section 3

| Finding | Status |
| --- | --- |
| Reflected numeric priority, power/invert override dispatch | Fixed; exact builtin storage is used where CPython bypasses user arithmetic overrides. |
| Numeric constructor overrides and exact component types | Fixed; numeric protocol and subtype tests pass. |
| Large sequence indices | Fixed; index errors and search-bound saturation are covered. |
| `float.fromhex` | Fixed: an ASCII parser converts hexadecimal significands directly, rounds guard/sticky bits to even and handles subnormal values, overflow and signed zero independently of the host locale. Boundary tests, a comma-decimal locale check and 1,012 ordinary vectors match CPython; this is not an exhaustive enumeration of all possible strings. |
| Classic comparison coercion | Already fixed by the earlier corpus changes; existing callback/error checks pass. |
| NaN three-way `cmp` and comprehensive numeric classification | Fixed: Python 2 three-way comparison uses default ordering when numeric comparisons all return false, rather than equating NaN with another value. Same-type fallback follows object addresses and mixed numeric fallback follows type addresses, so the sign may differ between processes while preserving CPython's invariant. Numeric classification includes classic instances and `__int__`/`__float__` protocols. Rich comparisons retain their separate NaN behavior. |
| Complex modular power | Fixed: ValueError rather than TypeError. |
| Classic instance special methods and descriptor protocol lookup | Fixed; instance `__call__`/`__iter__` and type-level descriptor hooks are covered. |
| Initializer after `__new__` returns a subtype; exception `__new__` | Fixed; actual returned type supplies initialization, including exception construction and raise. |
| Winning metaclass in `type()`; default class module/doc | Fixed; delegating `__new__`, copied namespaces and caller module are covered. |
| Builtin-subtype `__class__` / `__dict__` descriptors | Current lookup allows overriding descriptors before synthetic builtin answers. |
| Attribute insertion errors | Fixed: Python-facing writes use checked insertion and propagate failure immediately. |
| Classic class metadata assignments | Fixed: validate and replace actual name/bases/dictionary fields, publish before decrefs, reject inheritance cycles. |
| Deleting an unset slot | Fixed: AttributeError. |
| Slot name colliding with a class variable | The report's ValueError expectation is incorrect for Python 2.7.18. It preserves the class variable and skips that slot descriptor; tinypy now does the same. |
| Custom metaclass `mro()` | Fixed: creation and descendant rebasing call the metaclass hook, accept iterable results, validate class entries and solid layouts, retain extra MRO classes and roll back failed rebases. `type.mro(cls)` independently computes the default C3 result. Ordinary creation/introspection/rebase/error cases match CPython. |
| Classic class string, EnvironmentError string | Fixed and checked against CPython. |
| Printing ignored finalizer/weakref exceptions | Fixed: ignored errors are written to owned `sys.stderr` as Exception/type/value/object/ignored diagnostics. Failures in repr/write are consumed and the previous pending exception is restored. Finalizer and weakref diagnostics match the tested CPython behavior. |
| `sys.exc_clear()` across function return | Fixed through a clear epoch; ordinary nested calls are covered. |
| Defaults longer than argument count | Fixed: use the applicable tail of defaults. |
| Evaluating generator code | Fixed: construct a generator instead of immediately running its body. An oracle case also verifies that generator name lookup uses globals rather than the supplied custom locals. |
| Unbound method checks/rebinding | Earlier fixes retained; the binding corpus passes. |
| Import hook dispatch | Fixed; use frame builtins and Python 2's four-argument implicit import convention. |
| Generator throw tuple normalization | Fixed for new-style and classic exceptions. |
| `print >>None`, Unicode whitespace and softspace type | Fixed; output state is integer 0/1 and recognizes Unicode whitespace. |
| Exception matching and metaclass subclass checks | Fixed for successful custom checks, preserving the pending exception. Failed checks report an unraisable error and continue matching with the original pending exception; both paths are covered. |
| Uncaught diagnostic text | Fixed: render the exception's string, including KeyError quoting and custom string methods. |
| `hasattr` and BaseException | Fixed: propagate interrupts/SystemExit while swallowing ordinary Exception. |
| Module replaces/removes its registration while loading | Fixed; return the published object or raise ImportError. Two normal import fixtures cover it. |
| Native module initializer exception | Preserve an existing exception; only create a generic ImportError when the callback provided none. |
| `range` with long bounds outside int64 | Fixed: use exact integer arithmetic, bounded list allocation and promote/demote each result appropriately. Custom Python 2 `__int__` conversion is preserved. |
| `ord(bytearray(...))` | Fixed. |
| `intern` lifetime/sharing with compiler names | Fixed: a weak VM table shares runtime strings, compiler names/identifier constants and marshal-interned strings. Zero-ref strings leave the table before deferred teardown; insertion compacts/shrinks tombstone-heavy tables. Repeated transient batches have bounded live-VM memory and zero outstanding allocations at shutdown. |
| `filter(..., tuple)` / `tuple(list)` checked allocation | Fixed: tuple subtype storage and immutable str/unicode/int/long/float/complex subtype copies use checked allocation with partial-construction cleanup. Python-facing object/type/list/dict/set/frozenset/weakref construction also uses checked initial allocation. Fault-injected str/unicode/long/tuple subtype factories raise MemoryError and leave zero outstanding VM allocations. Bootstrap and public C allocation contracts are unchanged. |
| Older VM config layout | Fixed: access appended limits only when `struct_size` includes them. |
| Null artifact release callback | The suspected unconditional null call is not a valid configured state: host validation rejects a resolver without its release callback. |
| Interrupt polling interval | Existing contract: frame entry and backward branches. No finite latency guarantee for a host native callback; not changed here. |
| Dict/set iteration order, growth, presizing, pop slots | Implementation differences; Python 2 does not promise CPython's exact unordered-container iteration order. No table-layout emulation added. |
| Symmetric-difference update and list mutation versions | Fixed: retain backing identity/cached hashes; increment each list's version on swaps. |
| Sticky dictionary iterator size failure | Current implementation marks failure with an impossible expected size, preserving subsequent errors. |
| `slice.indices` with a negative length | Fixed against the actual Python 2 behavior: negative lengths are accepted and unusual negative/wrapped indices are calculated without signed-overflow UB. Ordinary negative-length and boundary vectors match CPython. |
| Item-view operand order; partially sorted results after callback failure; enormous repeat exception class | Fixed: items-view compares candidate with stored value; sort publishes its partial permutation after comparison failure and restores unmerged items. Small partial-order vectors match CPython and larger cases retain every item. Oversized list/tuple repeat storage raises MemoryError, verified in source against Python 2; existing bounded allocation-failure tests pass. Exact hash-table order remains outside the language contract. |
| Mixed byte/Unicode membership | Fixed: enforce ASCII compatibility before comparisons. |
| Percent format with Unicode-returning `__str__` | Fixed: preserve/promote Unicode in percent formatting without changing builtin `str` encoding rules. |
| Unicode-returning `__repr__` in all formatting contexts | Fixed: Unicode returned by `__repr__` must encode as ASCII for repr and `%r`, including nested list/tuple/dict and Unicode format contexts; non-ASCII results raise UnicodeEncodeError. |
| Encoding-error ranges and standard error-handler ranges | Fixed: group failing characters where the handler sees them; clamp bounds to the retained object before allocating replacement text. |
| Padded literal percent, `.0` float format, bytearray/buffer translation | Fixed; normal formatting/conversion cases pass. |
| Large string find bounds | Current implementation saturates index conversion; now covered by an oracle case. |
| Import/keyword/error-text miscellany | Fixed for the concrete reported cases: reload requires a registered parent and preserves its attribute and package metadata; packages infer `__package__` lazily; non-dict import globals are ignored; trailing empty name components are accepted and leading/middle empty components raise ValueError; NUL eval source raises TypeError; mapping-percent argument consumption and int/long x/base keywords match the oracle. Enumerate already promotes at the integer boundary. A second differential pass also aligned missing-value `list.index` repr/errors, `slice` arity, non-iterator type names and index-sized overflow diagnostics. Error checks cover these specified diagnostics, without claiming every CPython message is identical. |
| Frame and eval builtins namespaces | Fixed in the third pass: a module supplies its dictionary, an invalid namespace or a missing function namespace supplies the minimal None dictionary, and eval inserts the current frame's builtins into a new globals dictionary. Same-globals frames retain their shared builtins. Module-dictionary mutations invalidate existing global caches. The public embedding API supplies and records its default builtins for an absent key on a top-level frame, so later function calls retain that environment. |
| Filter immutable-subtype protocols | Fixed in the fourth pass: tuple/str/unicode subtypes use their item protocol and underlying storage length. Overridden `__iter__` and `__len__` are bypassed as in Python 2. IndexError/StopIteration from item access propagate. Text with a None predicate bypasses item truth testing and validates accepted item types immediately, preserving callback/error order. |
| Public UTF-8 source default | Preserved as the existing compiler API policy. Builtin compile/eval/exec use the Python 2 byte-source path. |
| Claimed full-stdlib compiler run and 1.2M marshal fuzz iterations | Revalidated compiler evidence: 1,909 supported stdlib sources / 5,727 compilations and the portable 35 / 105 are byte-identical. Twelve intentionally invalid stdlib fixtures are rejected by both compilers; one KOI8-R source is outside the documented encoding profile. The historical 1.2M marshal fuzz claim is retired from current acceptance evidence; existing bounded native loader/verifier tests are the current checks. |

## Optimizations from section 4

| Proposal | Outcome |
| --- | --- |
| P1 / P7 attribute caches and slot writes | Implemented: sixteen rows, two load ways, version guards, cached setters and retained callback operands. Classic MRO paths bypass unsafe caches. |
| P2 bound native calls | Implemented: one owned `(self, arguments...)` tuple for the direct positional path and a bounded 64-entry bound-native freelist. Public callback ownership remains unchanged. |
| P3 special-method names | Implemented: VM hash table of precreated special keys instead of linear name matching. |
| P4 truth checks / COMPARE+JUMP | Implemented: exact bool/None/int truth checks and Release-only exact-bool COMPARE + POP_JUMP fusion. Debug keeps separate instruction events; both paths pass the same oracle corpus. |
| P5 float arithmetic | Implemented float/int arithmetic fast paths including division; result reuse only for exact float storage. |
| P6 exception/type checks | Implemented lazy diagnostics for Python raises, interned initializer lookup and linear static single-base subtype walks. |
| P8 LOAD_GLOBAL | Implemented code-object cache with unique VM dictionary cache versions. Public mutation counters remain per dictionary; content swaps invalidate both identities. Epoch exhaustion disables caching rather than reusing versions. |
| Lazy subclass version propagation and exc-state saving | Implemented: O(1) VM cache-generation invalidation replaces eager descendant walks. Borrowed caches check the generation before dereferencing; wraparound disables caching. Finalizer presence refreshes lazily. Frames save handled exception state only when entering a handler and respect the `exc_clear` epoch. |
| Frame cached on a code object | Implemented: each code object can retain one cleared frame of at most 1,024 pointer slots, with no owned code/globals/locals references. Only unescaped evaluator frames enter this cache; escaping frame identity/code and teardown are tested. The existing bounded VM freelist remains a fallback. |
| FOR_ITER | Implemented for the actual exact list/tuple/range iterator types, preserving exhaustion behavior. |
| Float sum | Implemented double accumulator, materialized before returning to user-defined addition. |
| Long multiplication | Implemented: shorter outer operand, single-digit carry path and direct result-digit storage for schoolbook multiplication. Separate capacity tracks trimmed logical digits for correct teardown. Karatsuba retains the scratch space needed by its recursion. |
| Timsort minrun/galloping | Implemented: computed minrun, stable binary insertion and exponential/binary galloping in both merge directions. Mutation/version checks, stable ties and cleanup after comparison/key errors remain active. |
| Keyed-sort scratch storage | Implemented: generated key references live only in the sort-item array; the redundant ownership array is gone. This removes one pointer (8 bytes on the measured arm64 build) per input item while retaining partial-result and key-error cleanup. |
| Tuple construction and bound native arguments | Third pass: concatenate tuples and construct `(self, arguments...)` directly in checked owned storage through the common tuple-item builder, avoiding a fill with None followed by replacements. Tuple subtype conversion, empty/nonempty result identity and callback ownership are retained. |
| Filter and map collection paths | Fourth pass: generic `filter(bool, iterable)` tests truth directly, exact tuples avoid a filter iterator, and single-input `map(None, list/tuple)` copies through checked bulk storage. Subclass iteration, shallow-copy identity, callback errors and allocator cleanup remain covered. |
| Repeated finally/with stack diagnostics | Third pass: stack-depth/shape error checks compile only without NDEBUG. The one-time CFG/stack/block verifier remains active in every profile. Dynamic operand checks, mutation guards, numeric overflow and Python-visible exceptions remain active. The removed diagnostic string is absent from both Release and LTO binaries and present in Debug. |
| Cached hashes for copies/set operations | Implemented in copy/set paths and keys-view subset comparisons with set/frozenset/key views. Exact primitive builtin keys reuse stored hashes; custom-key callback behavior and collision equality are retained. |
| List shrink | Implemented below half capacity and on clear, retaining the previous buffer if optional realloc fails. |
| Repeated relative-import misses | Implemented registered-miss checks. |
| Marshal intern cache | Implemented open-addressed source-object index instead of a linear scan, including growth overflow guard. |
| Sharing compiler/runtime interned strings | Implemented through the shared weak intern table, including special keys and marshal-loaded interned strings. |
| LTO | Implemented as opt-in `TINYPY_ENABLE_LTO=ON` for Release. CMake checks toolchain IPO support and fails clearly when unavailable. Apple Clang Release LTO passes the native suite and full oracle corpus; the default stays OFF. |

## Profile contracts

These are explicit scope decisions, rather than pending implementation items:

- The runtime uses reference counting and explicit cycle cleanup. Seven
  CPython tuple-cache identity cases are excluded; four other CPython-specific
  originals have portable local replacements for deletion, length hints,
  reversed-call lifetime and reversed xrange values. No selected case is DEFER.
  Four ownership-sensitive
  cases use Debug detector adaptations and are skipped in Release. Three
  create owning cycles; the destructor case checks zero false positives.
  These adapted passes are explicitly marked and do not claim cyclic GC.
- Internal MRO borrows self and owns other entries. Exposed `__mro__` is an
  owning copy. Detached descriptors remain safe with weak owners. Overlapping
  reentrant `__bases__` transactions are rejected with RuntimeError, a stricter
  defensive rule than CPython; ordinary custom hooks have oracle coverage.
- Escaped frames keep their identity and code; fast locals still clear on
  return as required by the existing ownership-cycle policy. Frame caching
  does not introduce cyclic GC or change that policy.
- Verification proves reachable CFG stack/block/finally structure. It is not
  a static type proof for arbitrary Python values; code/closure/cell operands
  also have runtime validation, and the trusted-code artifact policy remains.
- The source decoder supports ASCII, UTF-8 and Latin-1. Public C compilation
  retains its UTF-8 default; Python compile/eval/exec retain their byte-source
  behavior. KOI8-R source is outside this documented profile.
- Unordered dict/set iteration layout and a latency bound inside a host native
  callback are not Python 2 language guarantees or added runtime contracts.
  Complete equality of every CPython diagnostic string is not claimed.
- Historical crash reproducers, the custom nopool investigation and the
  historical marshal fuzz count are not current acceptance evidence. Source
  hardening and the ordinary sanitizer/allocator suites are labeled separately.

## Validation and measured limits

macOS arm64, Apple Clang, current worktree:

- Debug, Release, unoptimized Debug ASan/UBSan and Release LTO:
  **114/114 CTest** in each profile, including debugger, loader/verifier,
  bounded allocator, intern lifetime and locale checks.
- Portable corpus in all four profiles versus CPython 2.7.18:
  **681 discovered / 670 selected / 11 excluded originals**: Debug with cycle
  diagnostics has **670 PASS**; Release and LTO have **666 PASS / 4 SKIP**.
  Every active tinypy case returns allocator accounting to zero. The review
  module contains **109 independently authored cases**, including 55 added
  since the initial review corpus.
- A second differential matrix compared 13,497 deterministic numeric,
  sequence, slice, sort, mapping, set and builtin outcomes. Another 30 iterator,
  slice and subtype protocol outcomes and eight sort mutation/error outcomes
  were compared independently. After the fixes above, all deterministic output
  agrees with CPython. NaN default-order signs are process-address-dependent;
  separate tests compare them with the object/type address rule instead.
- A third differential pass compares 4,476 text/Unicode outcomes, 47 frame/eval
  outcomes and 66 tuple concatenation/repeat outcomes against CPython 2.7.18.
  After the builtins fixes their deterministic outputs agree.
- The fourth pass compares 484 aggregate-function and iterator outcomes in
  Debug, Release, ASan/UBSan and LTO. Its 24 differing outcomes before the fix
  belonged to the filter immutable-subtype protocol cases; all now agree.
  Five local regression cases retain protocol, error-order, ownership and
  shallow-copy checks. No semantic checks were disabled in Release.
- The fifth pass compares 56 length-hint callback/error combinations and 42
  negative/non-integer/classic-instance edges. Sequence consumers now follow
  CPython's callback order and `__len__` to `__length_hint__` fallback rules;
  `zip` resolves hints before creating iterators. Three local cases retain the
  behavior, including `reversed` subclass keyword handling and Python 2.7's
  general-`map` ignored-hint-error quirk. Python-visible checks and callback
  exceptions remain active in Release.
- The external-suite review adds **56 independently written cases** covering
  evaluation order, Python 2 scope behavior, decorators, exception/context
  unwinding, generators, iterable fallbacks and container protocols. It exposed
  incremental container behavior. List extension and list/set/bytearray
  reinitialization retain yielded items if iteration fails; set
  difference-update streams general iterables. Exact list/tuple and set inputs
  keep their checked bulk paths. Constructor keywords and set-subclass repr
  now match Python 2.7. General iterables no longer need a temporary list. The
  selection is recorded in [EXTERNAL_TEST_REVIEW.md](EXTERNAL_TEST_REVIEW.md).
- Earlier runtime-closure compiler differential: **35 portable sources / 105 compilations** and
  **1,909 supported stdlib sources / 5,727 compilations**, optimize 0/1/2,
  byte-identical marshal-v2 output. Of 1,922 stdlib sources inspected,
  12 deliberately invalid fixtures are rejected by both compilers and one
  (`test/test_source_encoding.py`) uses the unsupported KOI8-R source encoding.
  Invalid fixtures are `lib2to3/tests/data/py3_test_grammar.py`,
  `test/bad_coding.py`, `test/bad_coding2.py`, `test/bad_coding3.py`,
  `test/badsyntax_future3.py` through `test/badsyntax_future9.py`, and
  `test/badsyntax_nocaret.py`.
- Float hexadecimal conversion: **1,012 oracle vectors** in Debug, Release and
  ASan/UBSan, covering signed zero, subnormals, rounding ties and overflow;
  the native comma-decimal locale test also passes.
- Tool/API and coordinator checks: **45/45**; Debug/Release core symbol audits and strict
  warnings-as-errors builds pass. No sanitizer diagnostics occurred;
  UBSan uses `halt_on_error=1`.

The first two passes measured HEAD `535dfa4` (which already contains P1-P8 and
earlier fixes) against the worktree before the third pass. Each workload uses five alternating runs and median
CPU seconds; all compared outputs agree and tinypy outstanding allocations
return to zero.

Game workload, 2,000 frames:

| Runtime | CPU seconds | Peak host allocator bytes |
| --- | --- | --- |
| HEAD 535dfa4 Release | 0.465084 | 1,754,563 |
| Worktree before third pass, Release | 0.479626 | 1,753,995 |
| Worktree before third pass, Release LTO | 0.458849 | 1,753,995 |
| CPython 2.7.18 | 0.305368 | Not measured with the tinypy allocator |

The ordinary Release median is about 3.1% slower than HEAD on this game;
LTO is about 1.4% faster than HEAD and 4.3% faster than that Release worktree.
No overall game speedup is claimed for the new optimizations. Peak heap is
568 bytes lower. Tinypy reports CLI CPU time; CPython uses child-process
user+system CPU time, with possible startup/accounting differences. The game
explicitly clears entity/component ownership cycles.

Specific CPU workloads:

| Workload | HEAD Release | Pre-third-pass Release | Pre-third-pass LTO | HEAD/worktree speedup |
| --- | --- | --- | --- | --- |
| 1,500,000 short calls | 0.097201 | 0.095992 | 0.097548 | 1.01x |
| 5,000,000 iterations with two comparisons | 0.358871 | 0.351899 | 0.359275 | 1.02x |
| 200,000 2,048-bit long x single-digit products | 0.035571 | 0.026418 | 0.026813 | 1.35x |
| 15,000 sorts of structured 1,024-element lists | 0.323053 | 0.220420 | 0.214422 | 1.47x |

The long and sorting gains apply to these measured shapes. The small call and
branch differences do not establish a general improvement, and LTO gains
vary by workload. These are local CLI measurements, not embedded application
or device acceptance.

The second pass separately measured ten keyed sorts of a 500,000-element list
whose key results are all equal. Removing the duplicate key-owner array reduced
peak allocator bytes from **21,954,896** to **17,954,896**, exactly eight bytes
per input item. Seven alternating runs produced median CPU times of 0.215727
and 0.214787 seconds respectively; that roughly 0.4% difference is within the
noise of this local measurement, so no keyed-sort speedup is claimed.

The third pass compared the frozen pre-third-pass Release binary with its
completed Release build. Seven alternating runs per workload use median CLI CPU seconds;
stdout matches CPython 2.7.18 and allocator balances return to zero:

| Workload | Before | After | Before/after speedup |
| --- | --- | --- | --- |
| 10,000 concatenations of two 4,000-element tuples | 0.166717 | 0.103243 | 1.61x |
| 3,000,000 bound native `dict.get` calls | 0.274122 | 0.265757 | 1.03x |
| 3,000,000 try/finally iterations | 0.212235 | 0.215851 | 0.98x |

Only the tuple workload shows a substantial repeatable gain. Native-call and
finally differences change with local measurement noise; neither establishes
a speedup. Removing the redundant Release diagnostics preserves the profile
contract independently of these timings. No overall application gain is claimed.

The fourth pass measured a frozen pre-fourth-pass Release binary against the
completed Release build. Each workload performs 200 operations on 40,000
elements (`[0, 1, 2, 0] * 10000`, converted to a tuple where applicable).
Seven alternating runs produce the following median CLI CPU seconds; outputs
match CPython 2.7.18 and all tinypy allocator balances return to zero:

| Workload | Before | After | Before/after speedup |
| --- | --- | --- | --- |
| `filter(bool, list)` | 0.200870 | 0.047146 | 4.26x |
| `filter(None, tuple)` | 0.050172 | 0.043455 | 1.15x |
| `map(None, list)` | 0.062202 | 0.017022 | 3.65x |
| `map(None, tuple)` | 0.060759 | 0.017173 | 3.54x |

These gains apply to the specified exact-builtin workloads. Custom subtype
callbacks and predicates retain their general dispatch and error semantics;
these measurements do not establish an overall application speedup.

Primary semantic references:
[CPython type construction](https://github.com/python/cpython/blob/v2.7.18/Objects/typeobject.c),
[numeric/default comparison](https://github.com/python/cpython/blob/v2.7.18/Objects/object.c),
[hexadecimal float conversion](https://github.com/python/cpython/blob/v2.7.18/Objects/floatobject.c),
[list allocation and sort](https://github.com/python/cpython/blob/v2.7.18/Objects/listobject.c),
[tuple allocation](https://github.com/python/cpython/blob/v2.7.18/Objects/tupleobject.c),
[frame builtins](https://github.com/python/cpython/blob/v2.7.18/Objects/frameobject.c),
[eval builtins inheritance](https://github.com/python/cpython/blob/v2.7.18/Python/bltinmodule.c),
[exception matching/reporting](https://github.com/python/cpython/blob/v2.7.18/Python/errors.c),
[codec handlers](https://github.com/python/cpython/blob/v2.7.18/Python/codecs.c),
[Unicode exception ranges](https://github.com/python/cpython/blob/v2.7.18/Objects/exceptions.c),
[module execution/registration](https://github.com/python/cpython/blob/v2.7.18/Python/import.c).
