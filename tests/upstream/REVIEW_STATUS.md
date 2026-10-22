# Runtime review closure — 2026-10-08

The subsequent [deep conformance audit](../conformance/DEEP_AUDIT.md) adds 1,134
portable regressions across twelve passes and a repeatable four-profile matrix.
The twelfth-pass inventory requires 1,804 selected cases, 43 finite runtime
matrices with 361,017 outcomes per profile, and 11,628 compiler comparisons per
profile (46,512 across four). All 273 stages passed on macOS arm64 in Debug,
Release, ASan/UBSan and Release LTO, with independent source reviews and a
matching post-run source/test/SPEC fingerprint. Previous accepted reports retain
their historical counts. The future variant and acceptance plan is in
[TEST_PLAN.md](../conformance/TEST_PLAN.md). The explicit CPython pending-C-exception
boundary is recorded in the audit. Original findings and historical evidence
remain below; bounded acceptance does not close the broader continuing audit.

The supplied review described `62db39e`. This follow-up checks the current
worktree, including the earlier portable-corpus fixes. Its prototypes were
reviewed individually, not applied as one patch. CPython **2.7.18** is the
behavioral oracle; Python 3 runs only the process coordinator. All actionable
implementation items in this review have been addressed. Existing profile
contracts and evidence limits are identified explicitly below. The eleventh pass
also corrects the cold-start metadata observation from the tenth pass; this statement
about the supplied review is not a claim of arbitrary-program equivalence.

**Fixed** below means the relevant implementation changed or the correction
was already present. Ordinary regression tests and the existing native suite
validate the resulting worktree. **Hardened** means an ownership or boundary
correction was checked in source; the historical crash itself was not
reproduced. The supplied crashing scripts, custom nopool build and crafted
bytecode/regex programs were not run. A passing sanitizer suite does not
establish equivalence to that separate historical investigation.

## Twelfth-pass follow-up — accepted

Three independently authored modules add 56 portable witnesses: 24
[bytearray construction and state](local/test_sequence_constructor_audit.py), 15
[Unicode and codec callbacks](local/test_unicode_methods_audit.py) and 17
[direct descriptor calls](local/test_descriptor_call_audit.py). Their three guarded
products add 23,475 outcomes (4,158 + 18,636 + 681). Focused strict Debug checks
and independent peer reviews preserve CPython results, error identity, handled
exception state, callback order and zero outstanding allocations.

| Family | Concrete correction and observation |
| --- | --- |
| Bytearray | Extension obtains the iterator before reading a source hint; negative length versus hint errors and bound/unbound diagnostics remain distinct. Constructors/reinit fall back to iteration only after index TypeError. Byte-string encoding still invokes the codec and retains its temporary while copying the original bytes. Append/insert use the entry length before conversion callbacks, checked by defined growth witnesses. |
| Unicode translation | Deleted runs look ahead and recheck the first retained character. Discarded lookup values are validated and released before retry; second results, lookup failures, mapping mutation and prior handled exceptions remain observable. |
| Codec handlers | Ignore and decode replacement consume the clamped end without reading start. Wrong-type errors read reported class/name and str, including callbacks, NUL/truncation and user failures. Hex codecs compare errors to strict before source conversion and raise an empty-args AssertionError when false. In the seventh pass `strict` raises a classic instance, which `PyExceptionInstance_Check` accepts, and the UnicodeError of a str or unicode subclass instance holds the exact text the codec read. In the twelfth pass compile() calls the strict handler wherever Python 2's compiler decodes: the escape decoders of a unicode literal share one exception per literal and go on at the returned position, an invalid UTF-8 run of a literal takes the replacement as escapes, a string or file whose cookie names a codec without a fast path tokenizes the decoded text and an ASCII cookie encodes byte literals back strictly; only a UnicodeError becomes the `(unicode error)` SyntaxError, and one whose str() fails reads `unknown error` or `unknown decode error`. Covered by `local.test_compiler_edges` and `local.test_compiler_diagnostics`. In the thirteenth pass a literal decodes into room sized by its prepared escapes, so the replacement of an invalid UTF-8 run decodes whole however much longer than the literal it is, where CPython 2.7 writes its escapes past a buffer sized by the literal; covered by `cli_runtime_exception_codec_runtime`. In the fourteenth pass a lone final backslash that a handler's position leaves a unicode literal goes to the handler as `\ at end of string` (the raw decoder keeps it), and the diagnostic of a source with an ASCII cookie restores the characters of a replacement to `?` with an offset in characters, as `PyTokenizer_RestoreEncoding` does. |
| Descriptors | Direct get/set/delete/repr methods reuse canonical native argument styles and receiver diagnostics. Omitted/None method owners remain None; classic/new-style rebinding and metaclass subclass checks preserve identity and errors. Instance/subclass consumers reuse one existing algorithm through a single internal helper. |

The frozen inventory contains 52 project-authored modules with 1,594 cases plus
210 selected vendor cases: 1,804 Debug/sanitize PASS, or 1,800 Release/LTO PASS
with the existing four detector SKIP. Every profile also passes 89 native/runtime
CTest groups, 43 products with 361,017 unique outcomes, and 11,628 compiler
comparisons at optimize 0/1/2. All 90 host/API/runner checks pass. CTest registers
162 entries; checked-in compiler inputs number 183, generated inputs 3,693,
and eager VM presets remain 741.

All 273 stages pass in 303.7 recorded stage seconds. The accepted report is
`.temp/validation-twelfth-audit-accepted/report.json`; its source/test/SPEC SHA-256
matches the post-run worktree:
`3ed361ccb150ab16c3d24f09b2f978c3a3b869f0fd8dd19cbb50343d37ab0357`.
Strict warnings-as-errors, standalone marshal/artifact, core symbols, allocator
accounting and ASan/UBSan checks pass. Apple ASan uses `detect_leaks=0` with
independent allocator accounting. No new skip, DEFER, expected failure, detector
adaptation or SPEC boundary was added. Python-visible validation remains active
in Release. These are finite local results on macOS arm64.

The root-builtins observations originally left outside this prepared checkpoint
were covered by later permanent regressions. Subsequent changes corrected generic
keyword rejection and min/max diagnostics. Later follow-ups release a losing
`min(iterable, key=...)` item before its temporary key, the prior `sum` total
before the current item, and temporary `map` arguments plus `zip` rows and
iterators in CPython's right-to-left tuple order. Partially created `map`
iterators are released separately in CPython's left-to-right descriptor order.
The original CPython/Tiny witnesses remain in `.temp/twelfth-builtins/probe.py`
as historical evidence and were never exclusions from the supported contract.

The cleanup follow-up adds nine portable regressions. Successful min/max release
the best key before the iterator; key, comparison and iteration failures release
the candidate key, candidate, best key and best item in the applicable CPython
order before the iterator and key function. Filter, generic sum and tuple/star
conversion release their partial result before a failing iterator. List and
single-input map(None, ...) retain their distinct iterator-first cleanup. A
native iter(list.pop, None) witness avoids the intentional traceback/fast-locals
lifetime difference. Unpacking releases an extra item and the extracted items
before the iterator, including when the final exhaustion probe raises. Existing
SPEC boundaries and detector adaptations are unchanged.

The next follow-up adds 19 portable regressions for five audited families.
Set item failures release the iterator before the failing key; intersection
also releases its temporary result before that key. Mapping lookup and insertion
failures keep their distinct iterator/key/value order. Invalid dictionary pairs
release the original pair before its converted sequence. String, Unicode and
bytearray join, and list slice collection, release partial items before a failing
iterator, while list construction retains its separate order.

In the twelfth pass a `zip` whose later iterator fails releases the result
before the partial row, as `builtin_zip` does, while `map` keeps its row-first
order. min/max hand each candidate reference to the comparison step, which
releases a losing candidate before its key without returning the key. A list
subclass extending itself collects its own iterator as `PySequence_Fast` does:
a missing iterator reads "argument must be iterable" and the length hint comes
from that iterator.

In the thirteenth pass the error of a length hint that `map` keeps pending, as
`builtin_map` leaves it the current exception, is gone once any later exception
is raised or an optional attribute lookup misses (getattr with a default,
hasattr, the keys probe), and an iterator ending normally clears a pending
StopIteration as `PyIter_Next` does. `dict(x)`, `update` and `dict.__init__`
probe `keys` on every argument but an exact dict before `PyDict_Merge` takes a
dict subclass directly; copies, class namespaces and partial keywords merge
without the probe. The `__cmp__` wrappers of dict, int and long order their
operands with their own slot rather than the rich comparisons, slice's orders
the fields with `PyObject_Cmp`, and the wrappers of these types and of set,
dictproxy, cell and code take only an operand whose type shares the slot or
derives from the receiver's type, as `wrap_cmpfunc` does.

In the fourteenth pass finalizers (`__del__`, weakref callbacks, the close of
an abandoned generator, host finalizers), `PyDict_GetItem` lookups and the
optional lookups of `dir()` restore that pending error with the exception
they set aside, as `PyErr_Fetch` and `PyErr_Restore` do around the first two
and `_dir_object` raises a pending exception before the last. Remaining
differences: CPython raises the pending exception itself from some operations
inside the mapped function (creating a classic instance without `__init__`,
`len()` or an attribute load of a classic instance, `dir()`), while tinypy
reports it when the iteration ends; a lookup miss of a classic instance moves
it, though `instance_getattr1` raises no AttributeError over a pending
exception, so the special methods of a classic instance (`bool()`, `repr()`,
comparisons), `getattr()` with a default and `dir()` of a classic instance
return where CPython raises the pending error; and where CPython raises and
clears an exception internally without tinypy raising one, tinypy keeps the
error pending and reports it while CPython returns the mapped list: the
generic lookup before a `__getattr__` hook, the TypeError of a missing
`__len__` in a length hint (`list()`, `tuple()`, `sorted()` or a join of a
generator, `enumerate`, `reversed`, a call or list iterator), the IndexError
ending a str, unicode or buffer iterator, and the `__trunc__` probe of `int()`
and `long()` over text. Dict ordering reads a failed key comparison of the other
dictionary as a missing key, as `PyDict_GetItem` does; cells and code objects order their contents
with the three-way comparison, which `cmp()` calls directly as their
`tp_compare`; and code equality compares constants by `_PyCode_ConstantKey`:
after `==`, a constant other than an exact int, long, bool, str, unicode,
float, complex, tuple, frozenset or code object is equal only to itself
(frozenset constants still compare their items pairwise). A
`__getattribute__` slot wrapper of the generic lookup assigned to a class
with `__getattr__` runs the generic lookup whatever type owns the wrapper, as
`slot_tp_getattr_hook` does; `BaseException.__getitem__` called directly
counts a negative index from a subclass `__len__`; and `str()` of a dictproxy
is `str()` of its dictionary.

In the fifteenth pass a subscript of an exception adds the length of a
subclass `__len__` to a negative index once and reads the item from the
arguments, as `PySequence_GetItem` and `BaseException_getitem` do; a
`__getitem__` that the `__len__` gives the class takes the adjusted index.
Code equality compares the keys of the items of tuple and frozenset constants
in one pass, pairing frozenset items by their stored hashes, so nested code
constants no longer double the work at every level. CPython compares such a
container with `==` before the keys of its items, so in hand-built constants
the `__eq__` of an object that is no exact builtin runs only against an item
of its own type: its calls, and an error it raises, can differ. The
instance-dictionary probe of an attribute load, classic class and instance
lookups, the MRO walks of types and `super`, the keyword lookup of
`str.format` and the namespace probe of string concatenation keep the pending
length-hint error of `map` as `PyDict_GetItem` does; a key `__eq__` that
raises and catches an exception during the instance-dictionary probe of an
attribute load or a classic lookup still moves it, since only a failed probe
steps back over the raise of the error it clears. The builtin error handlers
report a deleted `object` of a UnicodeError as not set.

In the fifteenth pass the bundled `array` module follows `arraymodule.c` of
CPython 2.7.18, so the BIGCHARSET encoding of `sre_compile` (a unicode class
with more than two runs outside Latin-1) compiles. Its item sizes are those
of the LP64 UCS-4 build on every target. Its sequence slots concatenate and
repeat after the number slots of the other operand, a subclass keeps
`array_inplace_concat` as `nb_inplace_add`, conversions run twice where
`ins1` runs them, a subclass instance hashes by identity while an array is
unhashable, and buffers, `_struct`, the `_codecs` decoders and `fromstring`
read its items through the old buffer protocol while the character buffer
coercions refuse them. Remaining differences, where CPython 2.7 reads or
writes released or uninitialized storage: a conversion or `__index__`
callback that resizes the array stores only into an item the array still
has, `fromlist` converts before the array grows, `fromstring` of a view of
the array itself reads after the resize, and a resize while `_struct` holds
the items raises BufferError. A 'u' item beyond U+10FFFF raises ValueError
when read, file methods always refuse their file, and `_sre` refuses an
array subject. Covered by `array_matrix.py` (12,795 outcomes),
`local.test_array_audit`, `cli_runtime_array_runtime` and
`runtime_array_heap_budget`.

ASCII and UTF-8 decoding consult registered handlers for every policy name,
including default strict. ASCII/Latin-1 encoding retains its four builtin
policies and looks up backslashreplace. Implicit Unicode conversion uses the
actual replacement in concatenation, comparisons, containment, joins, search,
replacement, splitting and formatting. Search bounds are parsed once before
codec callbacks and adjusted to the converted length. Unicode joins re-read list
size after item/separator callbacks; percent formatting reparses a converted
remainder and converts its earlier byte output afterwards. Standard handlers
are restored by every regression that overrides them. The selected inventory
is 2,403 cases, with the existing four Release/LTO detector adaptations.

The four-profile follow-up passes all 273 validation stages on macOS arm64:
2,403 portable PASS in Debug/sanitize, and 2,399 PASS plus the four declared
detector SKIP in Release/LTO. Strict builds, 90 host/API/tool checks, native
tests, compiler comparisons, symbol audits and allocator accounting pass.
The final source/test/SPEC SHA-256 is
`bb40b1aeebfb10902b3913bf9874c00aeaf4b0d60f32a8798bde379d141acb69`.
An additional 816-outcome callback/cleanup product matches CPython in each
profile with zero outstanding allocations and clean diagnostics.

The next correction adds 14 portable regressions for three audited families.
ASCII/UTF-8 decoders read current unread bytes after handler and position
callbacks over bytearray, legacy buffer and memoryview input. The exception
retains its original snapshot; subsequent ranges/reasons use current input,
and direct bytearray exports remain held until success or failure cleanup.
Byte percent formatting always restarts the Unicode parser when promoted,
including ASCII-only remainders, repeating mapping lookups with Unicode keys
and preserving their errors and temporary-release order. Adjacent mixed
literals use actual default-codec replacements, retain non-UnicodeError
identity, and wrap UnicodeError as a compiler diagnostic. Large literal runs
still concatenate in linear time. The selected inventory is now 2,417 cases,
with the existing four Release/LTO detector adaptations.

This three-family correction passes all 273 validation stages on macOS arm64
in 388.6 recorded stage seconds: 2,417 portable PASS in Debug/sanitize and
2,413 PASS plus four declared detector SKIP in Release/LTO. All 141 native
groups, 90 host/API/tool checks, 43 runtime matrices and 11,679 compiler
comparisons per profile pass, together with strict builds, symbol audits and
allocator accounting. The post-run source/test/SPEC SHA-256 matches
`cf4c0188f7f9387809414dba2ce294c25230e2c31b2b1a46635fd3cf27987090`.
The accepted report is `/private/tmp/tinypy-three-fix-validation/report.json`.
An additional deterministic 662-outcome callback product matches CPython
in each profile (2,648 comparisons), with zero outstanding allocations and
clean diagnostics; its report is
`/private/tmp/tinypy-three-fix-final-probes/report.json`.

The next correction adds 16 portable regressions for four audited families.
Weak proxies unwrap both operands of subscription, and every operand of
ternary power, including a proxy used only as exponent or modulus. Direct
proxy `__getattribute__` unwraps its name before the attribute slot validates
it; ordinary `getattr` retains its earlier name validation. Assignment,
deletion and membership continue to pass their raw key/value operands.
The classic `instance` type now exposes its native protocol wrappers through
its existing operator, coercion, comparison, conversion and item algorithms.
Direct legacy slice wrappers preserve signed bounds without calling `__len__`.
The selected inventory is now 2,433 cases, with the existing four Release/LTO
detector adaptations.

CPython 2.7.18 exposes `instance.__ipow__` through `wrap_binaryfunc`, although
`instance_ipow` has a ternary C signature. Successful direct calls can use an
uninitialized modulus and crash the macOS arm64 oracle. The portable tests
exercise inplace power through its ordinary operator and check the direct
wrapper's arity; TinyPy gives the two-operand wrapper a defined None modulus.
The external direct-wrapper product excludes that undefined oracle call.

This four-family correction passes all 273 validation stages on macOS arm64
in 321.1 recorded stage seconds: 2,433 portable PASS in Debug/sanitize and
2,429 PASS plus the existing four detector SKIP in Release/LTO. Every profile
passes all 141 native groups, 43 runtime matrices with 361,017 outcomes and
11,679 compiler comparisons. All 90 host/API/tool checks, strict builds,
symbol audits, allocator accounting and sanitizer checks pass. The post-run
source/test/SPEC SHA-256 matches
`5b3993bc5e91340473beea35ab1baa1772bc0ef6ddaa2a90f5eb9f77be490066`.
The accepted report is `/private/tmp/tinypy-four-fix-validation/report.json`.
An additional 2,838-outcome product combines the original audit witnesses,
1,008 direct classic-wrapper observations and the previous callback products.
It matches CPython in each profile (11,352 comparisons), with zero outstanding
allocations and clean diagnostics. Script hashes and the oracle exclusions
are recorded in `/private/tmp/tinypy-four-fix-final-probes/report.json`.

The next correction adds 13 portable regressions for four super/reduction
findings. After the target MRO misses, super reuses generic attribute lookup,
including its own instance dictionary and descriptor precedence. Its target
MRO also decides `__dict__`, before any builtin dictionary shortcut or own-type
key-comparison callbacks; explicit
`object.__getattribute__` still accesses the wrapper dictionary. Protocol 2+
reduction rejects variable-sized values when constructor arguments and custom
state are unavailable, while retaining list/dict iterator state and CPython's
fixed-size Unicode behavior. Reduction failure releases arguments before
state, then its iterators, preserving callback order and the original error.
At that pass, the selected inventory contained 2,446 cases, with the existing four Release/LTO
detector adaptations.

This super/reduction correction passes all 273 validation stages on macOS
arm64 in 352.3 recorded stage seconds: 2,446 portable PASS in Debug/sanitize
and 2,442 PASS plus the four declared detector SKIP in Release/LTO. All 141
native groups, 90 host/API/tool checks, 43 runtime matrices with 361,017
outcomes per profile and 11,679 compiler comparisons per profile pass, together
with strict builds, symbol audits, allocator accounting and sanitizer checks.
The post-run source/test/SPEC SHA-256 matches
`2ff9de0c78e0efad4bcaa51b61951b511c799a2d696c3517ed1b7179736b856a`.
The accepted report is `/private/tmp/tinypy-super-reduce-validation/report.json`.
An additional 2,561-outcome product per profile repeats the original audit
witnesses and checks the super own-type key-comparison boundary. All 10,244
comparisons match CPython, with zero outstanding allocations and clean
diagnostics. The script hashes and results are recorded in
`/private/tmp/tinypy-super-reduce-final-probes/report.json`.

Primary references:
[bytearrayobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/bytearrayobject.c),
[unicodeobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/unicodeobject.c),
[codecs.c](https://github.com/python/cpython/blob/v2.7.18/Python/codecs.c),
[hex_codec.py](https://github.com/python/cpython/blob/v2.7.18/Lib/encodings/hex_codec.py),
[classobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/classobject.c),
[descrobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/descrobject.c),
[weakrefobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/weakrefobject.c),
and [typeobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/typeobject.c).

## Slot-name reduction and copy_reg follow-up — accepted

Four portable regressions cover transient and cached `__slotnames__` finalizers,
dynamic slot-name comparisons and methods, metaclass-visible slot metadata,
and cache writes through metaclass `__setattr__`. Protocol 2+ reduction retains
its names and slots through result construction and releases them in CPython's
cleanup order. `copy_reg._slotnames` uses the public attribute, comparison and
string-method protocols for its reads and writes; rejected cache writes remain
nonfatal. Since the twelfth pass the other functions reach their arguments
through the protocols `copy_reg.py` uses, without type checks of their own:
`_reduce_ex` asserts `proto < 2`, `__newobj__`, `_reconstructor`,
`pickle_complex` and `_slotnames` take any object and read its attributes, and
`add_extension` converts the code with `int()`, compares registrations with
`==` and `!=` and reports conflicts with the messages of `copy_reg.py`. In the
thirteenth pass `pickle` and `constructor` test `hasattr(obj, '__call__')`,
`pickle` registers the reducer before it calls the module's `constructor`,
`_reduce_ex` reads `_HEAPTYPE` and, after the truth test of the state,
`_reconstructor` as globals, and protocol 2 refuses type objects, which are
variable-sized in CPython, and skips slot names that are not strings before
any hook. In the fourteenth pass the `hasattr` probes of `_reduce_ex` and
`_slotnames` let errors that are not `Exception` subclasses through, `pickle`
reads `_ClassType` as a global, `_reduce_ex` %-formats the name it cannot
pickle, `object.__reduce_ex__` imports `copy_reg` through the running
`__import__`, and the functions read the globals of the module the VM keeps.
In the fifteenth pass that `__import__` is the one `PyImport_Import` finds:
an item or attribute of the `__builtins__` of the running globals, read at
call time, or of the `__builtin__` module without running code, and `_sre`
imports `re` for its template helpers the same way; covered by
`local.test_object_protocols`, `local.test_sre_deep` and
`runtime_reduce_import_without_frame`.
The source oracle is CPython 2.7.18
[`typeobject.c`](https://github.com/python/cpython/blob/v2.7.18/Objects/typeobject.c)
and [`copy_reg.py`](https://github.com/python/cpython/blob/v2.7.18/Lib/copy_reg.py).

The selected inventory is 2,450 cases: 2,240 project-authored cases across 58
modules and 210 selected vendor cases. All 273 validation stages pass on macOS
arm64: Debug and ASan/UBSan each pass 2,450 portable cases; Release and LTO
each pass 2,446 plus the four declared detector SKIP. Strict builds, 141 native
groups, 43 runtime matrices with 361,017 outcomes per profile, 11,679 compiler
comparisons per profile, symbol audits and allocator checks pass. The final
source/test/SPEC SHA-256 is
`5349b3ec49129ab46d819a64fd9d0b1ec80d0b89c756d6db4e68c9b21c2a9d27`.
The accepted report is
`/private/tmp/tinypy-slotnames-fix-final-validation/report.json`.
Five supplemental scripts compare 1,364 scenario rows per profile (5,456
comparisons total), including the 41 new audit cases and prior reduction and
cleanup matrices. All outputs match CPython, sanitizer diagnostics are clean,
and each TinyPy process ends with zero outstanding allocations; see
`/private/tmp/tinypy-slotnames-fix-probes/report.json`.

## Historical eleventh-pass follow-up

Four independently authored modules add 129 portable witnesses: 27
[buffer lifetime](local/test_buffer_lifetime_audit.py), 34
[text arguments](local/test_text_arguments_audit.py), 30
[native readiness](local/test_native_readiness_audit.py) and 38
[weakref protocols](local/test_weakref_protocol_audit.py). Their four guarded
products add 39,686 outcomes (7,524 + 26,217 + 913 + 5,032). Focused strict Debug
comparisons match CPython 2.7.18 exactly, with quiet reference streams and zero
outstanding tinypy allocations. Independent peer reviews check the final
ownership, parser order and exception-state paths.

| Family | Concrete correction and observation |
| --- | --- |
| Buffer lifetime | Requested bounds survive owner resize; nested views flatten to the retained root owner. Complete repr checks owner/self roles, and hashing retains the first computed value. Raw Unicode bytes remain separate from the encoded legacy character-buffer view. Numeric buffer conversions retain int/long/float parser and NUL/explicit-base error priority. In the seventh pass `tinypy_buffer_from_object` of a unicode returns NULL with MemoryError raised when the memory of its code points is refused, rather than a buffer without bytes. In the twelfth pass nested offsets that add up past `PTRDIFF_MAX` raise ValueError, as the `Py_ssize_t` sum of CPython turns negative; simple-slice assignment and deletion convert both offsets before refusing the read-only buffer; memoryview `itemsize` and `ndim` are long; and `bytearray.join` copies the separator size it took from storage that stayed in place. |
| Text arguments | Shared native argument styles preserve receiver/count/keyword errors. Unicode padding converts character buffers, validates one character and retains width-before-fill order; byte padding accepts one byte only. Subtype center preserves separately clamped signed margins. Join translates only iterator-acquisition TypeError, while iteration and other callback errors retain identity. Private formatter spans preserve fresh result identity. |
| Codec arguments | Guaranteed encoder/decoder entries preserve nullable errors, typed/NUL name diagnostics, builtin handler metadata and source/error parser priority. Parsed registry names use exact string keys without subtype hash/equality hooks. Encoders consume character-buffer views; decoders retain raw bytes. The product observes the existing guaranteed codec entries and does not claim standard-library stream reader/writer classes. |
| Native readiness | Cold/warm metadata errors track first safe attribute access and remain shared by instances of the actual type. Native C descriptor metadata and numeric real/imag/numerator/denominator fields use physical member/getset descriptors, canonical values, receiver validation and readonly error priority. In the eleventh pass `object.__class__`, the members of `slice` and the softspace of the output streams are such descriptors too, read without the recursion level of a property getter's call. In the twelfth pass the softspace keeps the int assigned to it, as the `f_softspace` member of a file does. |
| Weakrefs | Constructor/new/init/call/module parsers preserve class/referent/keyword/count priority. Basic-reference/proxy caches retain list/callback order across callability changes. Repr and Unicode proxy dispatch preserve reference lookup behavior; dead-reference deletion performs one lookup and suppresses only KeyError. Function/method/generator/set weakref slots, generator callback-before-finally order and exact empty-frozenset identity retain lifetime semantics. In the twelfth pass a proxy passes `__class__` and every other attribute through `proxy_getattr` and `proxy_setattr`, as the getset of `object.__class__` no longer answers before the hook of a proxy or a host native type; its `__getattribute__`, `__setattr__` and `__delattr__` wrappers check the name after the referent as `PyObject_GetAttr` and `PyObject_SetAttr` do; `__str__` and `__cmp__`, which takes proxies alone while other operands get the default ordering that counts a proxy as a number, wrap its own slots; abs, len and next of the referent follow the builtins; the slice wrappers read C long bounds; object's `__setattr__` and `__delattr__` refuse a proxy; and a classic instance whose `__getattr__` provides `__call__` gets a callable proxy. The proxy type still holds `__hash__ = None` where CPython inherits the wrapper of object, and its `__index__` gives an int for a bool result. In the thirteenth pass `reversed()` refuses a proxy, which has no `sq_item` (in the fifteenth pass `apply()` shares its `PySequence_Check` and refuses a proxy and a dictproxy too), and a simple slice of a proxy whose referent has neither the slice nor the subscript slot reports what `PySequence_GetSlice` and `PySequence_SetSlice` report. Covered by `local.test_weakref_protocol_audit` and `core_native_attribute_hook_class`. |

The frozen inventory requires 49 project-authored modules with 1,538 cases plus
210 selected vendor cases: 1,748 Debug/sanitize PASS, or 1,744 Release/LTO PASS
with the existing four detector SKIP. It declares 89 native/runtime groups,
159 CTest registrations, 177 checked-in compiler inputs, 3,693 generated compiler
inputs and 741 VM name presets. The complete aggregate passed all 258 stages
in 288.3 recorded stage seconds. The report is
`.temp/validation-eleventh-audit-accepted/report.json`; its source/test/SPEC
SHA-256 matches the post-run worktree:
`ce236d1dd1fd8dd64333947c4f5bab72a65b55fe84a91558478d245f214cf4b0`.
All 90 host/API/runner checks pass. No compiler warnings or ASan/UBSan diagnostics
occurred; Apple ASan uses `detect_leaks=0`, with independent per-process
allocator accounting. This is bounded local acceptance on macOS arm64.

The existing native embedding group also compares ten weakref descriptor
arity/keyword/control failures with an explicit error output and with
`out_error == NULL`, checking the VM exception, successful recovery and allocator
balance. A parser failure now returns before receiver fallback can replace it.

Address-bearing buffer/weakref representations are checked against the
referent/owner/self objects in the same process; only those address roles are
substituted for comparison. Buffer and weakref hash observations retain their within-process
relationships rather than imposing a shared interpreter seed. Native readiness
products use ordinary safe operations and safe scalar/nullable direct fields;
they do not claim coverage of CPython getter paths requiring a ready type.
No new skip, expected failure, detector adaptation or SPEC boundary is added.
Python-visible validation and callback behavior remain active in Release.

Primary references:
[bufferobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/bufferobject.c),
[stringobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/stringobject.c),
[unicodeobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/unicodeobject.c),
[getargs.c](https://github.com/python/cpython/blob/v2.7.18/Python/getargs.c),
[codecs.c](https://github.com/python/cpython/blob/v2.7.18/Python/codecs.c),
[descrobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/descrobject.c),
[object.c](https://github.com/python/cpython/blob/v2.7.18/Objects/object.c) and
[weakrefobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/weakrefobject.c).

## Historical tenth-pass follow-up

Four independently authored modules add 110 portable witnesses: 25 wrapper
metadata, 27 iterator metadata, 28 buffer arguments and 30 Struct arguments/state.
The four guarded products add 20,230 outcomes (1,556 + 5,232 + 7,456 + 5,986).
Corrections cover native metadata/binding/hash/comparison/free-list ownership,
bytearray iterator taxonomy and generic text-subtype item/length protocols,
canonical buffer/bytearray parser diagnostics and supported Struct compiled
payload/cache/format/keyword/reinitialization behavior. A positive public C API
witness covers native hash callback failure with an omitted error output.
Independent source review also corrects original Unicode-buffer ownership
through codec/offset callbacks; no unsafe lifetime reproducer was executed.

The complete four-profile run passed all 238 stages in 287.5 recorded stage
seconds. Debug/sanitize pass 1,619 portable cases; Release/LTO pass 1,615 with
four expected detector SKIP. Every profile passes 89 native/runtime tests,
297,856 runtime observations and 11,586 compiler comparisons. Inventories cover
155 CTest registrations, 169 checked-in compiler sources and 736 VM names;
all 90 host checks pass. No DEFER or new skip was added. The accepted source/test
SHA-256 is `55397ff2e2b6829ad2ee857c45d18d565b0ca8183997eb02a8de8570975864ed`, matching the post-run
fingerprint. Report: `.temp/validation-tenth-audit-accepted/report.json`.
ASan/UBSan emit no diagnostics; Apple ASan uses detect_leaks=0 with independent
zero allocator balance. Explicit physical/hash/address/NotImplemented fallback
invariants and the remaining cold first-write metadata observation are detailed
in [DEEP_AUDIT.md](../conformance/DEEP_AUDIT.md). Python-visible semantic checks
remain active in Release; finite local coverage does not prove arbitrary-program
or device/other-ABI equivalence.

## Historical ninth-pass follow-up

Four independently authored modules add 96 portable cases: 14
[metadata arguments](local/test_metadata_arguments_audit.py), 25
[numeric arguments](local/test_numeric_arguments_audit.py), 31
[container arguments](local/test_container_arguments_audit.py) and 26
[introspection arguments](local/test_introspection_arguments_audit.py).
Their four finite products add 6,102 outcomes (325 + 995 + 3,756 + 1,026).
Focused strict Debug comparisons match quiet CPython 2.7.18 streams with zero
outstanding tinypy allocations. Independent source reviews check shared parser
styles, descriptor routing, argument ownership and saved exception-state transfer.

The complete four-profile run passed all 218 stages in 312.7 recorded stage
seconds. Debug and ASan/UBSan pass 1,509 portable cases; Release/LTO pass 1,505
with the four expected detector skips. Every profile passes all 89 native/runtime
tests, 277,626 runtime outcomes and 11,562 compiler comparisons. Inventory checks
cover 151 CTest registrations, 161 checked-in compiler sources and 734 VM presets;
all 90 host checks pass. No DEFER or new skip was added. The accepted source/test
SHA-256 is `c31bf6d38c1362bf36040274701fd38926be9065f3e5c5b67145fc8fba79f3e1`,
matching the post-run fingerprint. The report is
`.temp/validation-ninth-audit-accepted/report.json`. ASan/UBSan emit no diagnostics;
Apple ASan uses `detect_leaks=0` with independent tinypy allocator-balance checks.

| Family | Concrete correction |
| --- | --- |
| Metadata | Native C names split into short __name__ and qualified __module__; Python heap names retain their original object; classic metadata deletion/type/NUL diagnostics and modern empty-base errors follow the reference. Native partial/scanner representations retain qualified names. |
| Native arguments | One canonical method parser handles native/wrapper arity styles, implicit receivers and keyword rejection. Container operators, set constructors, dictionary/view comparison/hash wrappers and iterator constructors preserve their own operand errors and conversion priority. |
| Numeric/text | New-argument tuples use fresh base payload snapshots; direct numeric formatting and public format distinguish Unicode specification conversion/result types; numeric class methods retain exact argument diagnostics. |
| Functions/reduction | FunctionType retains typed argument pins and parser order, suppressed globals metadata lookup and NUL-prefix keyword behavior. Reduction preserves optional lookup/call failures, reported classes and cached slot-name validation. |
| Frames/descriptors | f_exc_* getsets expose writable saved caller state. Setters clear the displaced field before finalizers; stopped-frame witnesses observe None before publication of the new value. Frame exit transfers/releases saved state once. Read-only frame/generator fields retain descriptor errors. |

Size comparisons verify protocol, type, nonnegative values and relative storage
relationships; physical CPython object sizes are not asserted. Frame publication
tests use stopped frames with read-only finalizers. All Python-visible argument
checks remain active in Release. These changes add no detector adaptation, skip
or SPEC exclusion. The eighth-pass SRE short-name and classic class NUL-name
observations are corrected here. Primary references include
[typeobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/typeobject.c),
[classobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/classobject.c)
and [frameobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/frameobject.c).

## Historical eighth-pass follow-up

Four independently authored modules add 91 portable cases: 20
[buffer state](local/test_buffer_state_audit.py), 28
[text/codec/SRE state](local/test_text_state_audit.py), 24
[execution state](local/test_execution_state_audit.py) and 19
[sys state](local/test_sys_state_audit.py). Their four finite products add
7,259 outcomes (2,068 + 3,842 + 622 + 727). Focused CPython 2.7.18 and fresh
strict Debug comparisons pass, with quiet reference streams and zero outstanding
tinypy allocations; scoped ownership and error-state changes received independent
source review. All 198 stages of the complete four-profile run passed in
458.8 recorded stage seconds. The accepted source/test SHA-256 is
`640ba0ae6184abd44ddec7b38eace07e39236da9bfc0b8fd7ef0aff91073b678`; the post-run fingerprint matched.
The detailed report is `.temp/validation-eighth-audit-accepted/report.json`.

| Family | Concrete correction |
| --- | --- |
| Buffers/bytearrays | Legacy Unicode buffers expose native wide-character bytes and retain owners through child views; bound/count callbacks precede current storage snapshots; join drains and pins items before validation and reads the current separator; memoryview and fromhex diagnostics retain tested precedence. |
| Text/codecs | No-op and partition identities follow the reference; character-buffer diagnostics, search/count conversions and parser order match; generic codec callbacks distinguish omitted from explicit errors and suppress keyword lookup failures without losing handled state. |
| SRE | Keyword validation precedes missing subjects; bounds precede current subject-length callbacks; callable replacements precede template detection; supported group/result slices propagate errors and recover. Products use ordinary valid regex programs. |
| Import/execution | Fromlist and __all__ use indexed access; cached nonmodules expose generic attributes/namespaces; star publication keeps partial state; loading precedes fromlist truth; cached absolute imports avoid unnecessary metadata reads; ordered __import__ parsing retains arguments and uses Python 2 C-int conversion. |
| Print/generators | Softspace exchanges before conversion and suppresses attribute failures; an owned writer is resolved before str; original text controls trailing whitespace state. Generator native argument errors preserve reusable frames and caller exception state. |
| Sys | Frame depth and recursion limits use the reference integer protocols/ranges; getsizeof uses type special-method binding and a TypeError-only default; displayhook preserves softspace/repr/writer/underscore order and missing-stream behavior. |

Primary references for the latter families include
[CPython sysmodule.c](https://github.com/python/cpython/blob/v2.7.18/Python/sysmodule.c),
[fileobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/fileobject.c),
[intobject.c](https://github.com/python/cpython/blob/v2.7.18/Objects/intobject.c),
[ceval.c](https://github.com/python/cpython/blob/v2.7.18/Python/ceval.c) and
[import.c](https://github.com/python/cpython/blob/v2.7.18/Python/import.c).
The accepted full run checked 147 CTest registrations, 153 checked-in compiler
sources and 730 VM name presets. Release/LTO retain 1,409 portable passes and
four detector skips; Debug requires all 1,413 cases.

## Historical cold metadata observation — corrected in the eleventh pass

The tenth pass corrected method-wrapper and bytearray_iterator type identity;
both are directly tested. Its isolated cold first metadata write observation
reflects CPython's lazy type readiness: wrapper __name__ assignment changes
from generic TypeError before any metadata read to getset AttributeError after
it. Method-descriptor errors likewise change after readiness. The tenth-pass
runtime eagerly initialized these types and reported the ready descriptor error
in both states. Its warmed product states that precondition and does not prove
the cold behavior. The eleventh-pass native-readiness cases now retain both
states and their transitions without changing the historical result. See
[DEEP_AUDIT.md](../conformance/DEEP_AUDIT.md), with the primary object.c and
descrobject.c paths. No new exclusion, skip or SPEC boundary was added.
Bounded ordinary programs and stable buffer mutations provide finite evidence;
arbitrary callbacks/programs, malformed inputs and other host ABIs remain outside
that evidence.

## Critical items from section 1

| Original item | Current status | Implementation |
| --- | --- | --- |
| 1: long-to-double accumulation | Fixed | `long.c` bounds its significand accumulator, preserves sticky bits, rounds ties to even and reports overflow. Ordinary powers, rounding and overflow cases agree with CPython. |
| 2: implicit relative imports | Fixed | `import.c` tries a relative parent only when already registered; explicit missing parents fail. Missing-relative markers also avoid repeated failed loads. In the eighth pass a relative import continues from the package it takes from `sys.modules`, as `get_parent` and `load_next` do, so a missing or None ancestor is not imported again. In the ninth pass the miss is marked as soon as the absolute head imported, as `load_next` does, the empty package a leading dot leaves is not loaded, and the submodules of a package that is None in `sys.modules` are searched as top-level modules under its name. |
| 3: heap-budget MemoryError recursion | Hardened | A reserved MemoryError instance avoids recursive construction; checked pool allocations can reuse storage without external growth. Existing allocation and recovery tests pass. |
| 4: unbound builtin receiver validation | Already fixed by the earlier corpus changes | Native dispatch validates the receiver before a callback accesses its storage. The binding corpus remains active. |
| 5: recursive `type.__call__` dispatch | Fixed | Calls the internal type constructor directly. Normal metaclass delegation is covered. |
| 6: finalizer cache updated before the dictionary | Fixed | Publish dictionary changes and invalidate caches before releasing displaced objects. Finalizer assignment and removal are covered. |
| 7: classic classes in new-style MRO | Fixed | Types containing classic MRO entries keep their cache versions only until a classic class dictionary changes or is replaced, which withdraws the versions of all of them. |
| 8: recursive release and representation | Hardened | Allocation-free deferred release limits recursive container teardown; representation uses the logical/native stack guard. Existing deep-release tests and ordinary representations pass. |
| 9: base representations using custom `__format__` | Fixed | `oct`, `hex` and `bin` use builtin numeric formatting; normal subtype override cases agree with CPython. |

## Ownership and boundary findings from section 2

| Finding group | Current status and correction |
| --- | --- |
| Attribute/descriptors across callbacks; eval attribute caches | Hardened: own attributes, owners and descriptors through calls; borrowed cache entries require current versions. Lookup hints never invoke arbitrary equality. |
| MRO changes during lookup | Hardened: hold the type and its MRO snapshot and finish the walk on them, as `_PyType_Lookup` never restarts; a value only the walk still owns is not found. A type a key comparison released is freed after the lookup, so callers that keep using it hold it, and calls hold their callable. Internal MRO storage borrows its own type and owns every other entry, including classes inserted by custom `mro()`. Its wrapper cannot be retained with a plain incref. Rebase transactions guard overlapping recursive assignments and invalidate caches on publication/rollback. |
| Weakrefs created by finalization; `__class__` changes in finalizers | Hardened: refresh the actual type after finalization and clear newly created weakrefs before teardown. In the sixth pass an instance of a type, set, frozenset or partial subclass keeps its weak references through `__del__`, whose base clears them after it with the callbacks, those of references `__del__` made included, as `subtype_dealloc` leaves a base's own weak list to the base deallocator. |
| Subclass weakref userdata | Hardened: retain the base for the lifetime of its weakref callback. |
| Member/getset descriptors outliving a type | Hardened: weak owner references are refreshed before use. This retains tinypy's detached-descriptor behavior; CPython's strong owner lifetime is not emulated. |
| Classic descriptor binding failures | Fixed: propagate binding failures and never call a null result. `__getattr__` consumes AttributeError only when a fallback exists. |
| Recursive `isinstance` / `issubclass` / classic hierarchy walks | Hardened: logical and native stack guards cover these walks. Classic attribute lookups, hook resolution and `PyClass_IsSubclass` through the bases, which CPython leaves unguarded, take no recursion level: only the native stack guard stops them, also from host calls at depth 0 (the recursion limit where the compiler gives no stack address); the subclass check follows the last base in a loop, and a failure is reported (RuntimeError, unraisable in `except` matching) rather than read as a miss. In the eleventh pass `except` matching takes no level for a tuple of candidates either, nested tuples recurse under the native stack guard alone (a nesting too deep for it is unraisable and no match), and `dir()` merges the dictionaries of the bases the same way, raising the guard's error instead of omitting names. Covered by `local.test_exception_parity`, `local.test_builtin_edges`, `cli_runtime_old_class_runtime`, `runtime_class_subclass_native_guard` and `runtime_exception_contracts`. |
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
| Native finalizers and debugger callbacks | Hardened: preserve the pending exception around callback entry. Live-VM native finalizers report ignored errors while the object and its children are retained; destruction proceeds only after the callback and any resurrection check. In the tenth pass the debugger reports a RETURN for every frame that leaves, by yield or by an exception too, and an EXCEPTION for one thrown into a generator, as the trace function of CPython does, so CALL and RETURN events pair. In the eleventh pass LINE follows `maybe_call_line_trace` (first instruction of an lnotab line range or a backward jump), EXCEPTION comes after the frame joined the traceback and carries the raised type, a host interrupt that a callback's code received stays pending for the program, and `tinypy_debugger_frame_sync` is `PyFrame_LocalsToFast(frame, 0)`; event streams equal `sys.settrace` on 300 generated flow programs. In the twelfth pass the pending interrupt stays within its execution (dropped when the outermost frame ends or the debugger is removed), the sync and `tinypy_debugger_frame_get`/`set`/`delete` hold what they use across lookups whose key comparisons can finish the generator of the frame, a cell binds a captured argument over its argument slot as in `PyFrame_FastToLocals`, and class-body free variables are not locals. In the fourteenth pass the cell binds a captured name of a sublist parameter, which follows the arguments among the locals, over its slot as well. |
| Bytearray item/slice/find/in-place addition and memoryview indexing | Hardened: materialize conversions and replacements before rereading current storage; retain exported storage and snapshot overlapping additions. |
| `_struct` offsets | Superseded by the deep audit: acquire and retain the buffer export before offset/numeric callbacks, matching CPython's resize blocking and observation of in-place changes; release it on every exit. Supported offset protocols and error ordering have oracle regressions. |
| Overridden encode result; codec module mutation/search growth | Hardened: validate encode result types; retain a private VM codec registry; search with owned callbacks and live bounds. |
| Negative buffer slice magnitude; Unicode expandtabs | Hardened/fixed: avoid signed negation overflow; account for removed tabs and avoid null-buffer arithmetic. |
| Non-ASCII Unicode format specification | Fixed: require ASCII decoding before builtin format parsing; custom format methods retain their original specification. In the thirteenth pass a str or unicode subclass specification reaches a builtin str, unicode or number as its `__format__` reads it: `unicode.__format__` through `PyObject_Unicode`, `str.__format__` through `PyObject_Str`, and a number's `__format__` a unicode specification through `PyObject_Str` and a str one as its bytes, which then format as a byte specification whose str result `format()` decodes for a unicode specification; such bytes never become the UTF-8 of a unicode object. Covered by `local.test_format_fields`. |
| Reversed SRE group marks | Hardened: normalize a reversed captured span to empty and check group construction failures. Crafted SRE programs were not executed. |
| `__cmp__` sign reversal | Fixed: normalize its result before negation. |
| END_FINALLY / WITH_CLEANUP verifier disagreement | Fixed: the verifier tracks normal, exception, return, break and continue markers with their resume depths. Like CPython's stack depth computation it models every pending finally reason three values wide, and the reasons that enter one handler from one context share a marker, so nested finally clauses do not multiply the verifier's states. Normal cleanup requires a proven None constant. Consuming or rewriting the values of an except clause invalidates that proof; only END_FINALLY and WITH_CLEANUP consume a finally reason, code never reads or pops below the level of its block, to which an exception unwinds, and untyped cleanup paths are rejected. Redundant runtime depth/shape diagnostics compile only in Debug; structural verification remains active in Release. |
| MAKE_FUNCTION / MAKE_CLOSURE operands | Hardened: validate code, closure tuple, cells and free-variable count before execution. |

## Python 2 semantics from section 3

| Finding | Status |
| --- | --- |
| Reflected numeric priority, power/invert override dispatch | Fixed; exact builtin storage is used where CPython bypasses user arithmetic overrides. |
| Numeric constructor overrides and exact component types | Fixed; numeric protocol and subtype tests pass. |
| Large sequence indices | Fixed; index errors and search-bound saturation are covered. |
| `float.fromhex` | Fixed: an ASCII parser converts hexadecimal significands directly, rounds guard/sticky bits to even and handles subnormal values, overflow and signed zero independently of the host locale. Boundary tests, a comma-decimal locale check and 1,012 ordinary vectors match CPython; this is not an exhaustive enumeration of all possible strings. In the seventh pass an overflow is reported before the characters after the number are checked, as `float_fromhex` does. |
| Classic comparison coercion | Already fixed by the earlier corpus changes; existing callback/error checks pass. |
| NaN three-way `cmp` and comprehensive numeric classification | Fixed: Python 2 three-way comparison uses default ordering when numeric comparisons all return false, rather than equating NaN with another value. Same-type fallback follows object addresses and mixed numeric fallback follows type addresses, so the sign may differ between processes while preserving CPython's invariant. Numeric classification includes classic instances and `__int__`/`__float__` protocols. Rich comparisons retain their separate NaN behavior. |
| Complex modular power | Fixed: ValueError rather than TypeError. In the seventh pass the complex slot converts the base and then the exponent before it rejects the modulus, as `complex_pow` does: an operand it cannot convert declines the call, and a long too large for a float raises OverflowError. |
| Classic instance special methods and descriptor protocol lookup | Fixed; instance `__call__`/`__iter__` and type-level descriptor hooks are covered. |
| Initializer after `__new__` returns a subtype; exception `__new__` | Fixed; actual returned type supplies initialization, including exception construction and raise. |
| Winning metaclass in `type()`; default class module/doc | Fixed; delegating `__new__`, copied namespaces and caller module are covered. |
| Builtin-subtype `__class__` / `__dict__` descriptors | Current lookup allows overriding descriptors before synthetic builtin answers. |
| Attribute insertion errors | Fixed: Python-facing writes use checked insertion and propagate failure immediately. |
| Classic class metadata assignments | Fixed: validate and replace actual name/bases/dictionary fields, publish before decrefs, reject inheritance cycles. |
| Deleting an unset slot | Fixed: AttributeError. |
| Slot name colliding with a class variable | The report's ValueError expectation is incorrect for Python 2.7.18. It preserves the class variable and skips that slot descriptor; tinypy now does the same. |
| Custom metaclass `mro()` | Fixed: creation and descendant rebasing call the metaclass hook, accept iterable results, validate class entries and solid layouts, retain extra MRO classes and roll back failed rebases. `type.mro(cls)` independently computes the default C3 result. Ordinary creation/introspection/rebase/error cases match CPython. A rebase calls each descendant's hook once, breadth-first after its rebased bases, where `mro_subclasses` walks depth-first and may call a diamond descendant's hook twice; SPEC records the difference and the type protocol runtime fixture pins the order. |
| Classic class string, EnvironmentError string | Fixed and checked against CPython. |
| Printing ignored finalizer/weakref exceptions | Fixed: ignored errors are written to owned `sys.stderr` as Exception/type/value/object/ignored diagnostics. Failures in repr/write are consumed and the previous pending exception is restored. A failed `__module__` lookup or repr is cleared before its replacement text is written, so a Python `write` method never runs with it pending; on a stream object, unlike a file, Python 2.7 drops `<unknown>`, the class name and `: ` while that error is pending, which the output runtime fixture records. Finalizer and weakref diagnostics match the tested CPython behavior. |
| `sys.exc_clear()` across function return | Fixed through a clear epoch; ordinary nested calls are covered. |
| Defaults longer than argument count | Fixed: use the applicable tail of defaults. |
| Evaluating generator code | Fixed: construct a generator instead of immediately running its body. An oracle case also verifies that generator name lookup uses globals rather than the supplied custom locals. |
| Unbound method checks/rebinding | Earlier fixes retained; the binding corpus passes. |
| Import hook dispatch | Fixed; use frame builtins and Python 2's four-argument implicit import convention. In the eighth pass `sys.meta_path` finders come before the host finder, both with the PEP 302 protocol of `import_submodule` (an ImportError of `find_module` means not found, a loader returning None reports the module missing); `sys.path_hooks` and `sys.path_importer_cache` are gone, since the host resolver owns path entries. The VM saves the namespaces of its modules, which an import re-creates once they leave `sys.modules` and `reload` restores; a reload of a module being reloaded returns it and keeps its `__builtins__`. SPEC records where the finder contract differs from CPython's. In the ninth pass the length of `sys.meta_path` is read once and a finder is held only while it is asked, as in `find_module`; fromlists and `__all__` index classic instances through `__getattr__`, and a host resolver fails an import with the exception it raises. In the tenth pass `find_module` and `load_module` are called as `PyObject_CallMethod` calls them, which names an attribute that is not callable by its type. |
| Generator throw tuple normalization | Fixed for new-style and classic exceptions. As in `PyErr_NormalizeException`, the exception that a failed normalization raises instead is normalized in turn, up to the recursion limit, for `raise`, `except`, `throw` and hosts. |
| `print >>None`, Unicode whitespace and softspace type | Fixed; output state is integer 0/1 and recognizes Unicode whitespace. In the fifth pass `write` and `writelines` of the standard streams reset it once they accept their argument, as `file_write` does, and `print()` writes the `str()` of every item, separator and ending that is not unicode, a str subtype included, as `PyFile_WriteObject` with `Py_PRINT_RAW`. In the sixth pass `print()` looks the `write` method of the file up before it converts each value, as `PyFile_WriteObject` does. |
| Exception matching and metaclass subclass checks | Fixed for successful custom checks, preserving the pending exception. Failed checks report an unraisable error and continue matching with the original pending exception; both paths are covered. A metaclass that inherits the `__instancecheck__` or `__subclasscheck__` of `type` gets the default rules without a call, as those hooks are the default rules in CPython; copied into a class that is not a metaclass, they still bind to the classinfo and raise TypeError. |
| Uncaught diagnostic text | Fixed: render the exception's string, including KeyError quoting and custom string methods. An exception class whose `__str__` is the one of object returns what its `__repr__` returns, a str subtype or unicode included, as `object_str` does; `str()` of any value keeps what its string slot returns, as `PyObject_Str` does, so a weak proxy keeps the result of its referent. A classic exception whose `__str__` lookup fails reads as `<exception str() failed>`: `tinypy_object_str` reports the failure through its result, also without an `out_error`. In the sixth pass a built-in `__str__` or `__repr__` that applies to the value runs as the C slot it stands for, such as `BaseException_str`, without a method to bind, so a refused allocation no longer turns `str()` of a built-in exception into its repr. In the seventh pass the `__str__` and `__repr__` of int render a bool as the int it is, and its `__and__`, `__or__` and `__xor__` give two bools an int. |
| `hasattr` and BaseException | Fixed: propagate interrupts/SystemExit while swallowing ordinary Exception. |
| Module replaces/removes its registration while loading | Fixed; return the published object or raise ImportError. Two normal import fixtures cover it. In the sixth pass a submodule that cannot be bound to its parent fails the import with that error and stays in `sys.modules`, as `add_submodule` does; a module parent gets it in its namespace whatever its `__setattr__` does. |
| Native module initializer exception | Preserve an existing exception; only create a generic ImportError when the callback provided none. An exception left raised fails the import even when the initializer reports success, as `_PyImport_LoadDynamicModule` checks `PyErr_Occurred()`; so does one that the resolver raises with an artifact. |
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
| Mixed byte/Unicode membership | Fixed: enforce ASCII compatibility before comparisons. In the seventh pass only a str against a unicode needs ASCII: a str or unicode subclass instance holding non-ASCII text equals the same text of its base or a sibling subclass. In the twelfth pass an ASCII str, whose decoding no error handler sees, takes part in ordering, membership, equality, the search, split, strip, replace and partition methods and unicode formatting as the UTF-8 it already holds, and its implicit conversion builds the unicode directly. A TypeError raised while a non-ASCII str decodes makes the unicode rich comparison return NotImplemented, so the pair takes the default ordering (`cmp()` decodes for ==, < and > in turn), and `startswith`/`endswith` report it for a single argument as an argument of the wrong type, as CPython does. When both rich comparisons of a str or unicode subclass instance decline that way, the built-in comparison that follows decodes the str once more, so such a handler runs twice where CPython runs it once. Covered by `local.test_unicode_methods_audit`. |
| Percent format with Unicode-returning `__str__` | Fixed: preserve/promote Unicode in percent formatting without changing builtin `str` encoding rules. In the fifth pass a unicode operand restarts a byte-string format as `PyString_Format` does: the rest of the format from that specifier decodes first, the output so far only after the rest is formatted, so later formatting errors take precedence and decode positions count from each decoded string; `str.replace` decodes a byte-string receiver before a unicode argument and its replacement. In the sixth pass `unicode.replace` decodes `old` before it converts `new`, as `PyUnicode_Replace` does. In the seventh pass a unicode float conversion of a classic instance propagates the failure of its `__float__` lookup, as `PyFloat_AsDouble` does. In the twelfth pass the restart takes an ASCII rest of the format and an ASCII output so far, whose decoding reaches no handler, as the UTF-8 they already are: it formats the rest without a decoded byte-string copy and joins the output directly, or returns the formatted rest when nothing preceded it. Covered by `local.test_percent_format`. |
| Release order of `__del__` | Fixed in the fifth pass: lists and tuples release their items from the last one, and an instance of a subclass releases its `__slots__` members and then its dictionary before the contents of a builtin base, as `subtype_dealloc` and the base deallocators do. Slots of several class levels are released in their storage order rather than from the most derived class, which keeps the release of slotted instances as fast as before. In the sixth pass deleting a dict entry releases the value before the key, as `PyDict_DelItem` does. |
| Classic `__format__` lookup | Fixed in the fifth pass: `format()` of a classic instance formats its `str()` or `unicode()` whatever looking its `__format__` up raised, as `PyObject_Format` does. |
| SRE capture marks and type surface | Fixed in the sixth pass: the matcher saves and restores marks as Python 2.7's `_sre.c` does. MIN_REPEAT_ONE counts the item past its maximum and restores the last mark before it fails, MIN_UNTIL keeps only the last mark and index around its tail, and the mark stack of BRANCH and MAX_UNTIL holds the marks up to the last mark only when it is above 0, so the groups a failed attempt leaves agree with CPython; a differential of 28,000 generated patterns over CPython's `sre_compile` programs shows no difference. The data attributes of patterns, matches and scanners are readonly members and getsets of their types, patterns and matches refuse `__copy__` and `__deepcopy__`, `_sre.compile` parses its arguments as `"OiO!|nOO"`, and the `_sre` types are neither acceptable bases nor mutable. In the seventh pass the matcher sets every mark validation accepts (up to `2 * groups + 1`, of 100 groups for a program compiled without groups), `lastgroup` reads `indexgroup` with sequence item access, and group names follow the mapping protocol of any `groupindex`: subscription with its failure cleared, and `keys()` for `groupdict`. |
| `str.format` field names; `c` under a unicode spec | Fixed in the sixth pass: a brace inside a field name is part of the name, and a field holding any nested brace expands its format spec, as the `MarkupIterator` of Python 2.7 does; `format(int, u'<width>c')` decodes the padded byte field, as `int.__format__` does. |
| Unicode-returning `__repr__` in all formatting contexts | Fixed: Unicode returned by `__repr__` must encode as ASCII for repr and `%r`, including nested list/tuple/dict and Unicode format contexts; non-ASCII results raise UnicodeEncodeError. |
| Encoding-error ranges and standard error-handler ranges | Fixed: group failing characters where the handler sees them; clamp bounds to the retained object before allocating replacement text. |
| Padded literal percent, `.0` float format, bytearray/buffer translation | Fixed; normal formatting/conversion cases pass. |
| Large string find bounds | Current implementation saturates index conversion; now covered by an oracle case. |
| Import/keyword/error-text miscellany | Fixed for the concrete reported cases: reload requires a registered parent and preserves its attribute and package metadata; packages infer `__package__` lazily; non-dict import globals are ignored; trailing empty name components are accepted and leading/middle empty components raise ValueError; NUL eval source raises TypeError; mapping-percent argument consumption and int/long x/base keywords match the oracle. Enumerate already promotes at the integer boundary. A second differential pass also aligned missing-value `list.index` repr/errors, `slice` arity, non-iterator type names and index-sized overflow diagnostics. Error checks cover these specified diagnostics, without claiming every CPython message is identical. In the fifth pass the attribute, name, import, call and type-name messages show program-supplied names with the precisions of the `PyErr_Format` calls of CPython, such as `'%.50s' object has no attribute '%.400s'`, `name '%.200s' is not defined` and `No module named %.200s`, which also bounds what raising them allocates. In the sixth pass so do the argument types of the getargs conversions (`%.50s`), the unknown error handler (`%.400s`), the descriptor call messages (`%.100s`) and the code and generator reprs. In the seventh pass the unsupported format character message of a byte string keeps a NUL and shows a character read after width or precision digits unsigned, as `Py_CHARMASK` does, `str.format` shows an unknown conversion character of a byte string sign-extended, and `int()` and `long()` of a str holding a NUL byte repeat the repr of the argument itself. In the eleventh pass every PyArg-style parser rejects the keywords its parameters left through one pass, as `vgetargskeywords` does: a key that is not a str, unicode included, raises `keywords must be strings` before it is read as text, and a str names a parameter by its text up to an embedded NUL, so such a key passes unused; the `copy_reg` and hex codec functions, Python functions in CPython, bind keywords as `PyEval_EvalCodeEx` does. Covered by `local.test_keyword_arguments_audit`. In the twelfth pass attribute messages show the repr of a native key a host passes, a key missing from the dictionary it is deleted from raises AttributeError with the key itself, and the attribute wrappers of classic classes and instances take str names alone, as `class_getattr` and `instance_setattr` do; covered by `local.test_attribute_edges` and `core_attribute_native_key_errors`. |
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
| P4 truth checks / COMPARE+JUMP | Implemented: exact bool/None/int truth checks and Release-only exact-bool COMPARE + POP_JUMP fusion. Debug keeps separate instruction events; both paths pass the same oracle corpus. Membership tests call the containment protocol directly, as `cmp_outcome` does, outside the recursion level of a comparison. |
| P5 float arithmetic | Implemented float/int arithmetic fast paths including division; result reuse only for exact float storage. |
| P6 exception/type checks | Implemented lazy diagnostics for Python raises, interned initializer lookup and linear static single-base subtype walks. |
| P8 LOAD_GLOBAL | Implemented code-object cache with unique VM dictionary cache versions. Public mutation counters remain per dictionary; content swaps invalidate both identities. Epoch exhaustion disables caching rather than reusing versions. |
| Lazy subclass version propagation and exc-state saving | Implemented: per-type versions as CPython's `tp_version_tag`. A type gets one only after every type of its MRO, so a change withdraws the versions of the type and of its subclasses and stops at types already without one; an MRO computed while a `__bases__` assignment ran Python code, or merged from what the iteration of a tuple subclass of the bases gave, gets one only while every type of it is in the MRO of a base; builtin subtypes, which are not registered subclasses, follow a builtin type's change. Caches compare the versions of the types they depend on before dereferencing; exhaustion of the counter disables caching. Finalizer presence refreshes lazily. Frames save handled exception state only when entering a handler and respect the `exc_clear` epoch. |
| Frame cached on a code object | Implemented: each code object can retain one cleared frame of at most 1,024 pointer slots, with no owned code/globals/locals references. Only unescaped evaluator frames enter this cache; escaping frame identity/code and teardown are tested. The existing bounded VM freelist remains a fallback. |
| FOR_ITER | Implemented for the actual exact list/tuple/range iterator types, preserving exhaustion behavior. |
| Float sum | Implemented double accumulator, materialized before returning to user-defined addition. |
| String building | Implemented as CPython's `string_concatenate`: `s + t` and `s += t` on exact byte strings grow `s` in place when the stack holds its only reference, as a temporary or once the variable that the following `STORE_FAST`, `STORE_DEREF` or `STORE_NAME` rebinds is unbound. Like `string_concatenate`, the evaluator peeks at that store inline and compares a fast local by identity, so additions whose result goes elsewhere stay inline; a namespace is probed by identity and compares keys only past a colliding key. Namespace probe errors are discarded and the reference count and interning are checked after the probes; a string that something else took or interned during them is concatenated as usual and, as in `string_concatenate`, the variable stays unbound until the store binds the result. A growth that runs out of memory rebinds the variable, a name only when a probe by identity finds no key to compare. |
| Long multiplication | Implemented: shorter outer operand, single-digit carry path and direct result-digit storage for schoolbook multiplication. Separate capacity tracks trimmed logical digits for correct teardown. Karatsuba retains the scratch space needed by its recursion. In the eleventh pass multiplication, division and modular powers run on CPython's 30-bit digits, packed from the stored 15-bit digits at the kernel boundary: a factor of up to two such digits scales the result digits directly, squares take every cross product once, Karatsuba splits at cutoffs measured for these kernels, division is the normalized loop of `x_divrem`, plain powers square from the top bit and `pow()` with a modulus reduces every product in place by the normalized modulus, with a 5-ary window past 240 exponent bits as in `long_pow`. In the twelfth pass a plain power of an int base takes its steps in int64 until one would overflow. |
| Timsort minrun/galloping | Implemented: computed minrun, stable binary insertion and exponential/binary galloping in both merge directions. Mutation/version checks, stable ties and cleanup after comparison/key errors remain active. |
| Keyed-sort scratch storage | Implemented: generated key references live only in the sort-item array; the redundant ownership array is gone. This removes one pointer (8 bytes on the measured arm64 build) per input item while retaining partial-result and key-error cleanup. In the tenth pass the items sort in the list's own storage, as listsort's sortslice, and an array of keys moves with them only for a key function, so a sort needs no memory without merges and at most half the items for them. |
| Tuple construction and bound native arguments | Third pass: concatenate tuples and construct `(self, arguments...)` directly in checked owned storage through the common tuple-item builder, avoiding a fill with None followed by replacements. Tuple subtype conversion, empty/nonempty result identity and callback ownership are retained. |
| Filter and map collection paths | Fourth pass: generic `filter(bool, iterable)` tests truth directly, exact tuples avoid a filter iterator, and single-input `map(None, list/tuple)` copies through checked bulk storage. Subclass iteration, shallow-copy identity, callback errors and allocator cleanup remain covered. |
| Repeated finally/with stack diagnostics | Third pass: stack-depth/shape error checks compile only without NDEBUG. The one-time CFG/stack/block verifier remains active in every profile. Dynamic operand checks, mutation guards, numeric overflow and Python-visible exceptions remain active. The removed diagnostic string is absent from both Release and LTO binaries and present in Debug. |
| Cached hashes for copies/set operations | Implemented in copy/set paths and keys-view subset comparisons with set/frozenset/key views. Exact primitive builtin keys reuse stored hashes; custom-key callback behavior and collision equality are retained. |
| List shrink | Implemented below half capacity and on clear, retaining the previous buffer if optional realloc fails. In the tenth pass a shrinking slice assignment moves the kept items into the smaller storage, and the storage it leaves holds the removed ones until they are released, saving the transient array and the copy; storage of the host allocator moves into the pool only below half its largest block, so a list oscillating around 64 items no longer reallocates on every truncation; a length-hint reservation is cut back after the iteration. |
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
  return as required by the existing ownership-cycle policy, and since the
  sixth pass so does the `f_locals` snapshot built from them. Frame caching
  does not introduce cyclic GC or change that policy. Since the seventh pass a
  sync of the snapshot that finishes the generator through a finalizer stops,
  and the frame keeps the snapshot.
- Since the seventh pass finalizers nested eight deep, rather than the fifty
  levels at which CPython's trashcan unwinds, leave the objects with a
  generator to close or a `__del__` they release to the outermost one, which
  keeps the nesting within the native stack guard of instrumented builds, so
  every finalizer of a long chain runs. Native finalizers still run as the
  last reference goes, since a host may hand the value out until then.
- Since the eighth pass a heap type's `__getattr__` is fetched before its own
  `__getattribute__` runs, as `slot_tp_getattr_hook` does. Since the tenth
  pass the generic lookup fetches it before its descriptors run too, and the
  evaluator's attribute cache (since the ninth pass also for `__getattr__`
  types) before the code of a descriptor, so a descriptor that replaces or
  removes `__getattr__` or reassigns `__class__` leaves the hook fetched
  before; the dispatch memo spares the lookup for types without one. A key
  comparison of the instance dictionary that does the same on a lookup the
  evaluator serves still changes the hook it calls.
- Since the tenth pass a slot wrapper of a type outside the MRO, stored under
  the name of its own slot, leaves the inherited C slot as `update_one_slot`
  does when another name of that slot still finds the wrapper of the base:
  the other operand of a binary operator, the other rich comparisons, item
  or attribute assignment with deletion, since the eleventh pass in
  subclasses of exceptions and native types too. Such a wrapper under a name
  of `sq_concat`, `sq_repeat` or their in-place forms in a subclass of a
  built-in sequence fills no slot of that name, as those slots have no
  method of their own: the operator reports unsupported operands or tries
  the reflected method, and `+=` falls back to concatenation. The rule
  judges by the built-in base, not by the slots an intermediate Python class
  left. Built-in types hold the wrappers of their own slots only, so a
  wrapper that CPython gives a type for an inherited `tp_getattro`, such as
  `dictproxy.__getattribute__`, is object's in tinypy and binds anywhere.
  Since the thirteenth pass BaseException owns the wrappers of its generic
  `tp_getattro` and `tp_setattro`, which refuse other receivers; a slot
  wrapper over the C function of the built-in base that applies to the value
  overrides nothing, as `update_one_slot` keeps its `d_wrapped`. A
  `__new__` written in Python makes the `tp_new` of a class
  `slot_tp_new`, which it and its subclasses keep after the method is
  removed. Since the eleventh pass the dictionary of a
  built-in type holds its `__new__` as a builtin function, as CPython holds
  `tp_new_wrapper`, which keeps the `tp_new` a class inherits wherever it is
  stored; a static method made of one, or a native function the host sets
  as `__new__`, makes it `slot_tp_new`. The function is still represented as
  `<built-in function __new__>`, with a `__self__` of None.
- Restricted execution is not implemented: a `__builtins__` other than the
  VM's builtins only replaces the builtin names a frame looks up, and since
  the eighth pass `f_restricted` is always False rather than reporting a
  restriction that nothing enforces.
- The SystemError of a `__length_hint__` returning -1 without an error
  names the call that reports the empty result, as CPython's evaluation loop
  or `PyObject_Call` does, since the ninth pass. A built-in cannot tell every
  path apart: `list.extend(items, source)`, run unbound, gets the wording of
  the method-call fast path, and `tuple.__new__(cls, source)` called directly
  gets the `PyObject_Call` wording that `tuple(source)`, `T(source)` and
  `apply` report.
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

## Historical validation and measured limits

Earlier review snapshot on macOS arm64 with Apple Clang. The counts and
measurements below are retained as historical evidence; current eleventh-pass
requirements and accepted aggregate results are recorded above.

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
