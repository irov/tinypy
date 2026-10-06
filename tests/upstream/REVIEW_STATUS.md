# Runtime review follow-up — 2026-10-06

The supplied review described `62db39e`. This follow-up checks the current
worktree, including the earlier portable-corpus fixes. Its prototypes were
reviewed individually, not applied as one patch. CPython **2.7.18** is the
behavioral oracle; Python 3 runs only the process coordinator.

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
| MRO changes during lookup | Hardened: traverse an independent, owned MRO snapshot and restart after rebasing. MRO storage itself has borrowed entries and cannot be retained with a plain incref. |
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
| Partial state replacement | Hardened: publish all new owned fields before releasing old ones. |
| Native finalizers and debugger callbacks | Hardened: preserve the pending exception around callback entry. |
| Bytearray item/slice/find/in-place addition and memoryview indexing | Hardened: materialize conversions and replacements before rereading current storage; retain exported storage and snapshot overlapping additions. |
| `_struct` offsets | Hardened: convert offsets before acquiring the buffer view. Ordinary supported double formats are covered. |
| Overridden encode result; codec module mutation/search growth | Hardened: validate encode result types; retain a private VM codec registry; search with owned callbacks and live bounds. |
| Negative buffer slice magnitude; Unicode expandtabs | Hardened/fixed: avoid signed negation overflow; account for removed tabs and avoid null-buffer arithmetic. |
| Non-ASCII Unicode format specification | Fixed: require ASCII decoding before builtin format parsing; custom format methods retain their original specification. |
| Reversed SRE group marks | Hardened: normalize a reversed captured span to empty and check group construction failures. Crafted SRE programs were not executed. |
| `__cmp__` sign reversal | Fixed: normalize its result before negation. |
| END_FINALLY / WITH_CLEANUP verifier disagreement | Partly fixed: runtime checks required stack depth before pops. A complete structural verifier model for finally markers remains separate work. |
| MAKE_FUNCTION / MAKE_CLOSURE operands | Hardened: validate code, closure tuple, cells and free-variable count before execution. |

## Python 2 semantics from section 3

| Finding | Status |
| --- | --- |
| Reflected numeric priority, power/invert override dispatch | Fixed; exact builtin storage is used where CPython bypasses user arithmetic overrides. |
| Numeric constructor overrides and exact component types | Fixed; numeric protocol and subtype tests pass. |
| Large sequence indices | Fixed; index errors and search-bound saturation are covered. |
| `float.fromhex` | Fixed for hex grammar, missing prefix, signs, exponent and exact `nan`/`inf`/`infinity` spellings. It still uses `strtod`; locale-independent conversion and exhaustive rounding parity were not established. |
| Classic comparison coercion | Already fixed by the earlier corpus changes; existing callback/error checks pass. |
| NaN three-way `cmp` and comprehensive numeric classification | Remaining compatibility work. Rich comparison behavior is distinct from Python 2's fallback three-way ordering. |
| Complex modular power | Fixed: ValueError rather than TypeError. |
| Classic instance special methods and descriptor protocol lookup | Fixed; instance `__call__`/`__iter__` and type-level descriptor hooks are covered. |
| Initializer after `__new__` returns a subtype; exception `__new__` | Fixed; actual returned type supplies initialization, including exception construction and raise. |
| Winning metaclass in `type()`; default class module/doc | Fixed; delegating `__new__`, copied namespaces and caller module are covered. |
| Builtin-subtype `__class__` / `__dict__` descriptors | Current lookup allows overriding descriptors before synthetic builtin answers. |
| Attribute insertion errors | Fixed: Python-facing writes use checked insertion and propagate failure immediately. |
| Classic class metadata assignments | Fixed: validate and replace actual name/bases/dictionary fields, publish before decrefs, reject inheritance cycles. |
| Deleting an unset slot | Fixed: AttributeError. |
| Slot name colliding with a class variable | The report's ValueError expectation is incorrect for Python 2.7.18. It preserves the class variable and skips that slot descriptor; tinypy now does the same. |
| Custom metaclass `mro()` | Remaining object-model work; type creation/rebasing currently use C3 internally. |
| Classic class string, EnvironmentError string | Fixed and checked against CPython. |
| Printing ignored finalizer/weakref exceptions | Remaining diagnostic work. Pending exceptions are preserved, but CPython's unraisable-reporting behavior is not fully implemented. |
| `sys.exc_clear()` across function return | Fixed through a clear epoch; ordinary nested calls are covered. |
| Defaults longer than argument count | Fixed: use the applicable tail of defaults. |
| Evaluating generator code | Fixed: construct a generator instead of immediately running its body. Full custom-locals behavior is not separately established. |
| Unbound method checks/rebinding | Earlier fixes retained; the binding corpus passes. |
| Import hook dispatch | Fixed; use frame builtins and Python 2's four-argument implicit import convention. |
| Generator throw tuple normalization | Fixed for new-style and classic exceptions. |
| `print >>None`, Unicode whitespace and softspace type | Fixed; output state is integer 0/1 and recognizes Unicode whitespace. |
| Exception matching and metaclass subclass checks | Fixed for successful custom checks, preserving the pending exception. Failed checks remain subject to the unraisable-reporting limitation above. |
| Uncaught diagnostic text | Fixed: render the exception's string, including KeyError quoting and custom string methods. |
| `hasattr` and BaseException | Fixed: propagate interrupts/SystemExit while swallowing ordinary Exception. |
| Module replaces/removes its registration while loading | Fixed; return the published object or raise ImportError. Two normal import fixtures cover it. |
| Native module initializer exception | Preserve an existing exception; only create a generic ImportError when the callback provided none. |
| `range` with long bounds outside int64 | Fixed: use exact integer arithmetic, bounded list allocation and promote/demote each result appropriately. Custom Python 2 `__int__` conversion is preserved. |
| `ord(bytearray(...))` | Fixed. |
| `intern` lifetime/sharing with compiler names | Remaining ownership redesign. VM-lifetime retention is not an allocator leak after VM shutdown, but can retain excessive memory during execution. |
| `filter(..., tuple)` / `tuple(list)` checked allocation | Exact builtin paths check budgets. A complete review of every subtype factory remains separate work. |
| Older VM config layout | Fixed: access appended limits only when `struct_size` includes them. |
| Null artifact release callback | The suspected unconditional null call is not a valid configured state: host validation rejects a resolver without its release callback. |
| Interrupt polling interval | Existing contract: frame entry and backward branches. No finite latency guarantee for a host native callback; not changed here. |
| Dict/set iteration order, growth, presizing, pop slots | Implementation differences; Python 2 does not promise CPython's exact unordered-container iteration order. No table-layout emulation added. |
| Symmetric-difference update and list mutation versions | Fixed: retain backing identity/cached hashes; increment each list's version on swaps. |
| Sticky dictionary iterator size failure | Current implementation marks failure with an impossible expected size, preserving subsequent errors. |
| `slice.indices` with a negative length | Report expectation is inaccurate: CPython 2.7.18 accepts it and yields unusual negative/wrapped indices. Tinypy still rejects negative lengths; this compatibility difference remains. |
| Item-view operand order; partially sorted results after callback failure; enormous repeat exception class | Remaining compatibility audit; exact table order and resource-exhaustion behavior were not reproduced. |
| Mixed byte/Unicode membership | Fixed: enforce ASCII compatibility before comparisons. |
| Percent format with Unicode-returning `__str__` | Fixed: preserve/promote Unicode in percent formatting without changing builtin `str` encoding rules. |
| Unicode-returning `__repr__` in all formatting contexts | Remaining compatibility audit. |
| Encoding-error ranges and standard error-handler ranges | Fixed: group failing characters where the handler sees them; clamp bounds to the retained object before allocating replacement text. |
| Padded literal percent, `.0` float format, bytearray/buffer translation | Fixed; normal formatting/conversion cases pass. |
| Large string find bounds | Current implementation saturates index conversion; now covered by an oracle case. |
| Remaining import/keyword/error-text miscellany | Not claimed resolved: reload/package metadata, invalid globals/empty name components, NUL eval source, mixed mapping-percent arguments, and complete error wording need individual compatibility checks. |
| Public UTF-8 source default | Preserved as the existing compiler API policy. Builtin compile/eval/exec use the Python 2 byte-source path. |
| Claimed full-stdlib compiler run and 1.2M marshal fuzz iterations | Historical evidence, not rerun here. Current differential validation covers 35 sources / 105 compilations. |

## Optimizations from section 4

| Proposal | Outcome |
| --- | --- |
| P1 / P7 attribute caches and slot writes | Implemented: sixteen rows, two load ways, version guards, cached setters and retained callback operands. Classic MRO paths bypass unsafe caches. |
| P2 bound native calls | Implemented: one owned `(self, arguments...)` tuple for the direct positional path and a bounded 64-entry bound-native freelist. Public callback ownership remains unchanged. |
| P3 special-method names | Implemented: VM hash table of precreated special keys instead of linear name matching. |
| P4 truth checks / COMPARE+JUMP | Exact bool/None/int truth checks implemented. Instruction fusion deferred. |
| P5 float arithmetic | Implemented float/int arithmetic fast paths including division; result reuse only for exact float storage. |
| P6 exception/type checks | Implemented lazy diagnostics for Python raises, interned initializer lookup and linear static single-base subtype walks. |
| P8 LOAD_GLOBAL | Implemented code-object cache with unique VM dictionary cache versions. Public mutation counters remain per dictionary; content swaps invalidate both identities. Epoch exhaustion disables caching rather than reusing versions. |
| Lazy subclass version propagation and exc-state saving | Deferred; eager invalidation and owned saved exception references remain. Correctness fixes are independent of these optimizations. |
| Frame cached on a code object | Deferred: affects frame ownership, escaping frame objects and teardown. |
| FOR_ITER | Implemented for the actual exact list/tuple/range iterator types, preserving exhaustion behavior. |
| Float sum | Implemented double accumulator, materialized before returning to user-defined addition. |
| Long multiplication | Shorter outer operand implemented. Additional single-digit/scratch-storage redesign deferred. |
| Timsort minrun/galloping | Deferred; existing sort implementation and mutation/error checks retained. |
| Cached hashes for copies/set operations | Implemented in the corrected paths; broad view-operation optimization remains separate. |
| List shrink | Implemented below half capacity and on clear, retaining the previous buffer if optional realloc fails. |
| Repeated relative-import misses | Implemented registered-miss checks. |
| Marshal intern cache | Implemented open-addressed source-object index instead of a linear scan, including growth overflow guard. |
| Sharing compiler/runtime interned strings | Deferred with the intern ownership redesign. |
| LTO | Not enabled globally; requires target-toolchain/build-policy validation. |

## Validation and measured limits

- Debug, Release and unoptimized Debug ASan/UBSan: **109/109 CTest** each.
- Portable corpus in all three profiles versus CPython 2.7.18:
  **570 discovered / 555 PASS / 15 DEFER**, zero outstanding tinypy allocations
  per active case. The review adds **54 independently authored cases**.
- Compiler differential: **35 sources / 105 compilations**, optimize 0/1/2,
  byte-identical marshal-v2 output.
- Tool/API checks: **34/34**; Debug/Release core symbol audits and strict
  warnings-as-errors builds pass.

Game workload: 2,000 frames, five alternating runs, median CPU seconds:

| Runtime | CPU seconds | Peak host allocator bytes |
| --- | --- | --- |
| Corrected tinypy before these optimizations | 0.666283 | 1,711,835 |
| Final Release tinypy | 0.490974 | 1,754,563 |
| CPython 2.7.18 | 0.321246 | Not measured with the tinypy allocator |

This is **1.36x faster** than the corrected pre-optimization build and
**1.53x CPython CPU time** on this workload. The report's 1.27x comparison was
not reproduced. Peak heap increases approximately 2.5% with the larger caches.
Tinypy measures its own CLI CPU time; CPython uses child-process user+system
CPU time, so small startup/accounting differences remain. Outputs agree;
both tinypy builds return allocator accounting to zero. The local benchmark
explicitly clears entity/component ownership cycles. This is one workload,
not a general speed guarantee, and not the report's 20,000-frame experiment.

Primary semantic references:
[CPython type construction](https://github.com/python/cpython/blob/v2.7.18/Objects/typeobject.c),
[exception matching](https://github.com/python/cpython/blob/v2.7.18/Python/errors.c),
[codec handlers](https://github.com/python/cpython/blob/v2.7.18/Python/codecs.c),
[Unicode exception range accessors](https://github.com/python/cpython/blob/v2.7.18/Objects/exceptions.c),
[module execution/registration](https://github.com/python/cpython/blob/v2.7.18/Python/import.c).
