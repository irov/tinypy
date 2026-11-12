# tinypy: runtime and compiler specification

## 1. Purpose

tinypy is an embedded Python 2.7 implementation for executing trusted and
restricted code from memory buffers. The runtime, compiler, and data formats
are implemented in C11 and do not require an installed Python interpreter.

The core does not directly access:

- the filesystem or standard streams;
- environment or process APIs;
- locale;
- sockets;
- thread APIs or TLS;
- the system allocator.

The host provides memory, modules, output, diagnostics, and interrupt polling
through C callbacks.

## 2. Implementation scope

Supported:

- Python 2.7 source semantics;
- Python 2.7 bytecode and code objects;
- marshal v2;
- the `exec`, `eval`, and `single` compilation modes;
- dynamic `compile`, `eval`, and string-based `exec`;
- host-resolved source and bytecode modules;
- independent VMs without a shared runtime lock.

Not supported:

- Python 3;
- the CPython binary extension ABI;
- loading shared libraries from the core;
- Python-visible `_ast` and `compile(ast, ...)`;
- concurrent execution of one VM by multiple threads;
- automatic cyclic GC;
- CPython 2.7 restricted execution mode: a `__builtins__` entry in globals
  that differs from the VM's builtins only replaces the frame's builtins
  namespace. Restrictions on `func_globals`, `func_code`, class and instance
  `__dict__`, `__class__` assignment, and other operations are not enforced,
  and `f_restricted` is always `False`.

## 3. C ABI

Public namespace:

```text
tinypy_          functions, types and values
TINYPY_          constants and enum values
tinypy_internal_ cross-unit implementation helpers
__tinypy_        private implementation functions and tables
```

Naming rules:

- struct, union, and scalar typedefs end in `_t`;
- enum typedefs end in `_e`;
- declarations in public headers occupy a single line;
- `tinypy.h` aggregates the entire C API;
- `tinypy.hpp` is the only C++ proxy header;
- public C headers do not contain `extern "C"` or `__cplusplus`.

ABI-extensible structures start with:

```c
uint32_t abi_version;
uint32_t struct_size;
```

A smaller known `struct_size` means that fields appended later are absent.
Required pointers, ownership, the correct direct-accessor type, and indices
are C preconditions. Violating them has undefined behavior in every build
type; the public contract promises neither a runtime check nor a debug assertion.

## 4. VMs and concurrency

`tinypy_vm_t` owns all mutable state:

- builtins and modules;
- interned values and cached constants;
- frames and exception state;
- compiler context;
- import state;
- recursion and instruction counters;
- the allocator and host callbacks.

There are no process-wide mutable globals, GIL, or TLS. Immutable opcode,
grammar, Unicode, and parser tables may be shared by all VMs.

A VM has one owner thread. Synchronous reentrant entry from a native callback
is allowed. Different VMs may execute and compile concurrently.

## 5. Memory and lifetime

Reference cycles must be broken explicitly; there is no cyclic GC. Compiling
source to bytecode rejects a local function that captures its own binding:
`def`, an assigned `lambda`, and direct local aliases, including transitive
aliases. The check is lexical and conservative: branches and later
reassignments do not lift the restriction. The `SyntaxError` identifies the
function, captured binding, and definition line. Recursion through a global
name, a method, or an explicitly passed argument, and ordinary closures are allowed.

This is an intentional difference from CPython 2.7. The check runs after
scope resolution and before bytecode generation, including for `compile`,
`eval`, `exec`, and source imports.
Precompiled bytecode loading, cycles created dynamically through the C API
or `FunctionType`, indirect bindings through containers, and mutual captures
between different functions are not analyzed; their owners must break them
explicitly.

Every runtime object starts with a universal header:

```c
struct tinypy_value_t {
    tinypy_ref_t ref;
    tinypy_type_t *type;
};
```

Variable-size objects add a signed size and type-specific storage. There is
no shared payload union or allocation prefix.

All external memory is requested exclusively through `tinypy_allocator_t`,
with a size, alignment, and user data. NULL is recovered as `MemoryError`
only in explicitly fallible operations with a computed size; all other valid
allocation requests must return non-NULL.

A VM-local pool allocator handles runtime allocations of up to 512 bytes:

- size classes are multiples of the native alignment;
- pools are 4 KiB;
- arenas are 256 KiB;
- partially free arenas are ordered from most occupied to least occupied;
- a completely free arena is immediately returned to the host allocator,
  except for the last arena in that order: as in CPython 3.9, one completely
  free arena is retained for reuse and freed no later than VM destruction;
  when `max_heap_bytes` is set, none is retained;
- an emptied pool remains in use if its size class has no other empty pool:
  otherwise, a loop that allocates and frees blocks at the boundary between
  pools of that class would take a pool from its arena and return it on every
  iteration. This bounds empty pools to one per size class, or at most
  512 bytes / native alignment per VM (256 KiB with alignment 8, 128 KiB with
  16). Such a pool remains only while its arena contains an occupied pool
  that is not itself retained this way: an arena containing only these
  retained pools reclaims them and then follows the completely free arena
  rule. They retain no arenas: after objects are freed, the host allocator
  holds only arenas with live blocks and one completely free arena.
  These pools return to their arenas no later than VM destruction; when
  `max_heap_bytes` is set, they return immediately, as do any remaining from
  VM bootstrap;
- allocations larger than 512 bytes go directly to the host allocator.

Pool state belongs to a specific `tinypy_vm_t`; it uses no process globals,
TLS, or locks. The host sees actual external allocations, rather than every
logical pooled object. During budget preflight, `max_heap_bytes` includes the
VM object, arena table, full reserved arena sizes, and all direct allocations;
requests rejected by the host are not counted. `tinypy_error_t`, together
with the message rendered into it on first read, is also a direct VM
allocation: it is counted until the host frees it or the VM is destroyed,
but the budget does not reject it, so raising an exception does not depend
on the remaining budget. Computed `long`, text, buffer, list, and tuple
results are rejected with `MemoryError` if they exceed the remaining budget.
Removing list elements does not depend on the budget: a list losing all its
elements frees its storage, as in `list_clear`; a shrinking list moves to
smaller storage, while the old storage retains removed elements until they
are released. Other removed elements wait on the stack or in a temporary
array that the budget counts but does not reject.
`list.sort` sorts the list's own storage: a key array is needed only with a
key function, and merging needs at most half the elements, as in listsort.

VM bootstrap completes before its budget is checked: if it uses more than
`max_heap_bytes`, `tinypy_vm_create` frees everything and returns NULL. It
does the same if the host allocator returns NULL for the VM object itself
or for a bootstrap request after which bootstrap can continue; other
bootstrap requests follow the general non-NULL rule. Like bootstrap, the
first import of `array` creates the module's type and namespace without a
budget check.
C API functions without an error result, such as `tinypy_dict_set` and
`tinypy_list_append`, are not fallible: their growth ignores the budget and
always completes. The compiler and runtime report `MemoryError` on budget
exhaustion without losing data. A length hint that does not fit the address
space of a 32-bit host also produces `MemoryError`, as does an unallocatable
size on a 64-bit host. Like `Py_ssize_t` in 64-bit CPython, the shift count
is int64 on every host: a right shift beyond all bits produces 0 or -1,
while a left shift or power whose result does not fit the host's address
space produces `MemoryError`.

Like `list_resize`, a list that uses less than half its storage after
deletion releases that storage and shrinks to a size with `list_resize`'s
spare capacity. Like `listextend`, a list also shrinks storage reserved by a
length hint beyond the elements actually added. Storage moves from the host
allocator to a pool only when its capacity is at most half the largest pool
block, so a list fluctuating around that size does not reallocate at every
change. An emptied list retains storage for one append, so a one-element
stack does not reallocate it; a list slice with step 1 occupies exactly its
elements, as in `list_slice`.
Like CPython, a bytearray stores a trailing NUL byte after its bytes, and
every resize path follows `PyByteArray_Resize` and
`PyByteArray_FromStringAndSize`; `+=`, `*=`, and `__init__` from an iterator
preserve any result that fits the current storage in place, even with
exports. Consequently, `__sizeof__` and `__alloc__` match CPython wherever
those rules and the object header size match, and `BufferError` occurs in
the same cases.

Lifetime is governed by reference counting. Acyclic objects are destroyed
immediately. The host must release owned references and break owning cycles
before `tinypy_vm_destroy`.

When a module is destroyed, an externally retained dictionary of that module
keeps its keys; byte-string key values are replaced with `None` in two
passes: names with a single leading `_` first, then all others except
`__builtins__`. Non-string and Unicode keys are preserved. Cleanup walks
the table by index and allocates no memory, so it also works when the budget
is exhausted. Cycle diagnostic traversal only reads references and does not
clear the dictionary. Python's `module.__new__` creates an object without a
name or dictionary until initialization; C accessors return borrowed NULL
for those missing fields. Adding a value or using the module as builtins
creates its dictionary lazily.

This is a fundamental rule, rather than a missing feature: the runtime does
not maintain a registry of live values, and `tinypy_vm_destroy` does not
sweep unreachable objects. A value still alive at that point is a host or
runtime error that must be fixed where the reference leaks; opt-in cycle
diagnostics help locate such errors.

`tinypy_vm_destroy` first runs finalizers of host native values reachable
from the VM while all values are alive: host native type instances first,
then native functions, since an instance finalizer may still call a method
whose finalizer releases the call's user data. The runtime's own native
values (memoryview, dictproxy, exception instances, struct sequences,
`struct.Struct`, and its native functions) retain no host objects: these
passes do not finalize them, and they remain available to Python until
their memory is freed. An instance whose last reference is dropped by a
finalizer is released normally: its `__del__` runs before its finalizer,
while native functions have not yet been finalized; `__del__` does not run
for an instance released after its finalizer.
If finalizers, values they release, or diagnostic rendering at the end of a
pass create new native values with finalizers, or if `__del__` resurrects an
instance before its finalizer, the pass repeats for them, at most 8 times
in total; values created by the last pass are freed without finalizers.
Only then is the memory of reachable objects freed, without invoking
finalizers. Each finalizer runs exactly once; calling a native function
whose finalizer has already run raises RuntimeError, while a bound method
reads its descriptor's user data.
Likewise, slots of a native instance whose finalizer has already run,
methods registered on its type, and native accessors of its properties raise
RuntimeError instead of calling callbacks with the payload of a destroyed
host object. The instance remains accessible while its finalizer runs: the
finalizer may pass it to Python. An error left by a finalizer is reported
as unraisable, identifying the instance as `object.__repr__` would
(`<T object at 0x...>`): after the finalizer, host type callbacks are no
longer called.

The VM owns a reference to the `sys` module: `sys.stdout`,
`sys.displayhook`, and `sys.exc_*` are read and written directly in its
dictionary, and replacing `sys.modules['sys']` does not affect the runtime.
The `__mro__` attribute returns an owning copy of the internal MRO tuple:
without a cyclic collector, a type cannot own a tuple that refers back to it.

The VM retains its own constant values:

- `None`, `True`, `False`;
- integers from `-1023` to `1024`;
- positive float zero;
- the empty byte string;
- the empty tuple;
- an empty exact `frozenset`, retained lazily after the first Python construction.

Python's `frozenset()` and construction from an empty iterable reuse this
object; subtypes and the C API `tinypy_frozenset_new` create separate values.
The C API functions `tinypy_set_add`, `tinypy_set_discard`, and
`tinypy_set_clear` mutate a frozenset only as an embedding-only builder:
a fresh `tinypy_frozenset_new` result before its first hash and before it
is exposed to Python code.

These values have ordinary reference counts and a base owned reference held
by the VM itself; there is no special immortal refcount sentinel.

## 6. Object model

The runtime implements:

- `None`, bool, integer, long, float, and complex;
- byte `str` and `unicode`;
- tuple, list, dict, set, frozenset, bytearray, and buffer;
- slices and sequence/mapping protocols;
- functions, bound methods, closures, and cells;
- iterators, comprehensions, and generators;
- exceptions, frames, and tracebacks;
- old-style and new-style classes, including classic classes in the
  `__bases__` and `__mro__` of a new-style class (`tinypy_type_mro_at` and
  `tinypy_type_base_at` return NULL for classic entries);
- `type`, metaclasses, C3 MRO, custom `mro()`, and `super`;
- descriptors, properties, class/static methods, and `__slots__`;
- weak references and explicit finalization behavior.

Dict and set reproduce CPython 2.7's tables: growth after insertion;
presizing for merge, copy, and dict displays, after which dict-to-dict and
set-to-set merges do not grow the table; lookup that finds the slot holding
the key object itself regardless of its stored hash; and a finger in slot 0
for `popitem` and `set.pop`. Iteration order therefore matches CPython 2.7.
An insertion that leaves insufficient memory to grow the table raises
`MemoryError` and leaves the container unchanged. `list.sort` reproduces
CPython 2.7's listsort, including comparison counts and the partially sorted
order after a comparison raises an exception.

Functions, bound Python methods, code objects, generators, set, and frozenset
support weak references. A generator's weakref callbacks run before
`finally` when it is closed during destruction. Basic ref/proxy caches
preserve reference and callback order; changing a referent's callable
protocol does not create a second basic proxy. `_remove_dead_weakref`
performs its lookup once and suppresses only `KeyError`, including
subclasses. As in `subtype_dealloc`, weak references of an instance whose
class adds its weak list are cleared before its `__del__`; any created by
that `__del__`, unless it resurrects the object, are cleared without calling
their callbacks. The weak lists of `type`, `set`, `frozenset`, and `partial`
belong to those types themselves: as in their CPython deallocators, these
lists are cleared after a subclass's `__del__`, so `__del__` sees live weak
references, and their callbacks, including those of references it creates,
run afterward. This lets a class retain the weak references through which
its bases enumerate it and its descriptors find it until the end of its
metaclass's `__del__`; a resurrected class remains a subclass of its bases.
Instances of native type subclasses, including `_struct.Struct`, clear weak
references before `__del__` and the native finalizer, like instances of
classes that add a weak list; for `_struct.Struct`, this differs from CPython.
Objects released directly by a finalizer are not deferred by the release
queue, so a deep chain calls each `__del__` once.
Like CPython's trashcan, an object with an unfinished generator or `__del__`
that is released within eight nested finalizers waits for the outer
finalizer: it releases such objects in sequence before releasing its own
object. A long chain in which each finalizer releases the next object can
therefore run all finalizers without exhausting the native stack.
Whether a type has `__del__` is determined by its latest lookup during class
creation, base replacement, or finalization, so a `__del__` that changes a
class attribute does not break the chain. An object whose type's latest
lookup found no `__del__`, or an object still referenced by weak references
in its weak list, is finalized immediately. A native value with a finalizer
never waits: its host finalizer runs when the last reference is released,
before that release returns. The host can therefore release a Python
instance whose borrowed pointer it clears in the finalizer.
A weak reference being destroyed detaches from its referent before
releasing its own `__dict__` and slots, so its own callback is not invoked
even if only those fields were retaining the referent.

Like `proxy_getattr` and `proxy_setattr`, a weak proxy forwards every
attribute name to its referent, including `__class__`: `isinstance`,
`super`, and pickling see the referent's class; assigning `__class__`
changes the referent's class; and a dead proxy raises `ReferenceError`.
After checking the referent, the proxy type's direct `__getattribute__`,
`__setattr__`, and `__delattr__` wrappers reject names that are neither str
nor unicode with TypeError, like `PyObject_GetAttr` and `PyObject_SetAttr`.
A proxy's `__cmp__` accepts only proxies; against other operands, a proxy
uses default ordering as a number.

Like `Py_buffer`, a memoryview over a bytearray holds an export and sees the
bytes it exported: its length does not follow in-place array resizing, and
bytes beyond the current size expose the trailing NUL byte and the previous
memory contents, as in CPython. While an export exists, the bytearray's
storage is neither reallocated nor freed.

A legacy `buffer` retains the requested offset and size, clamping access to
its owner's current length. Shrinking and later growing a bytearray owner
does not lose the original bounds; nested views retain the same root owner.
The first hash is cached. A raw Unicode buffer contains native code units,
which the first buffer caches on the unicode owner; if their allocation is
rejected, `tinypy_buffer_from_object` returns NULL with `MemoryError` raised.
Like `PyBuffer_FromObject`, `tinypy_buffer_from_object` rejects owners other
than str, unicode, bytearray, `array.array`, or buffer: it returns NULL with
`TypeError: buffer object expected` raised.
Character-buffer conversion uses the default encoding and applies bounds
separately to those bytes, retaining the owner during codec callbacks.
Offsets and sizes above `PTRDIFF_MAX` (possible only on 32-bit hosts) are
stored as `PTRDIFF_MAX`: an owner cannot be longer, so the selected bytes
do not change. A buffer constructed from another buffer adds its offset to
the source buffer's offset; a sum above `PTRDIFF_MAX` is rejected with
`ValueError: offset must be zero or positive`, like a negative `Py_ssize_t`
sum in CPython.

Type slots return their semantic result directly. Invalid slot input
violates a C precondition and has undefined behavior. Python exceptions
are used only for actual runtime errors.

A type's internal MRO borrows the reference to the type itself and owns the
other entries, including classes added outside `__bases__` by a custom
`mro()`. Without cyclic GC, an owning reference to the type itself would
create an uncollectable cycle.
MRO attribute lookup and `super` retain the type and its internal tuple
during traversal: if a key comparison assigns `__bases__` during traversal,
the replaced tuple becomes an ordinary tuple and owns the type itself until
traversal ends; a type whose last instance has its `__class__` changed by a
comparison is released only after lookup. Like `_PyType_Lookup`, traversal
does not restart: it finishes reading the old MRO and the dictionary it was
checking, even if a comparison replaces a classic class's or instance's
`__dict__`. A value then owned only by the traversal itself is treated as
not found. Code that still uses an object's type after lookup (memo
dispatch, attribute assignment, or constructor memoization) retains the
type itself, while `tinypy_call` retains the callable:
the caller may pass a borrowed field, such as a property's getter, which
comparison during `__call__` lookup releases. While a custom `mro()` is
computing a new class's MRO, lookup in that class finds nothing, as in CPython.
Every access to `__mro__` returns a new tuple with owning references, and
`C.__mro__ is C.__mro__` is false, unlike CPython. The hook runs during type
creation and when descendant MROs are recomputed after a base change; its
result must contain classes with compatible solid layouts. After
`__bases__` assignment, the type's MRO and each descendant's MRO are computed
once: descendants are visited breadth-first in subclass order, each after
its bases that are being recomputed, so the hook sees their new MROs.
CPython's `mro_subclasses` traverses `tp_subclasses` depth-first and invokes
the hook for a descendant reachable through multiple paths on each path,
initially with another base's old MRO still in place; tinypy intentionally
invokes the hook once in deterministic order. Overlapping reentrant
`__bases__` assignments during the hook are rejected with RuntimeError;
a failed recomputation rolls back MROs and bases. Classes that Python code
(the hook or a key comparison) creates during recomputation from the MRO
being replaced have their MROs recomputed, as in CPython's `tp_subclasses`
traversal in `mro_subclasses`. After rollback, such a class retains its MRO,
as in CPython 2.7, but lookup caches do not retain answers for an MRO whose
classes no longer appear in its bases' MROs.
Like CPython 2.7's `mro_implementation`, the default C3 MRO merges the
results of iterating `__bases__`, so a tuple subclass with its own
`__iter__` can add types outside the bases to the MRO; changes to those types
are immediately visible, and lookup caches do not retain answers for such
an MRO. CPython inserts a non-class object into the MRO and then reads it as
a type; tinypy rejects it during class creation, `__bases__` assignment, and
`type.mro()` calls with TypeError "bases must be types". Likewise, CPython
accepts a type added by iteration whose instance layout is not the layout
of a class base, and then reads the class's instances as instances of that
type; tinypy rejects such a type with TypeError "mro() returned base with
unsuitable layout", as it does for a type returned by a custom `mro()`.
Like CPython's `class_name`, the "duplicate base class" message identifies
the first repeated element by `__name__`, or otherwise by repr, displaying
"?" if the result is not str.
Member and getset descriptors that outlive their type remain safe: repr
shows `<deleted type>`, and applying them to an object raises TypeError.

A new-style class's special methods and descriptor protocol are looked up
through its MRO, and lookup caches are invalidated by any change to a class
in the MRO, including a classic class's dictionary. A method added to a
classic base after new-style subclass creation (`__len__`, `__hash__`,
`__repr__`, `__index__`, `__init__`, `__get__`, and others) therefore takes
effect immediately. CPython 2.7 fills such a subclass's slots at creation
and does not update them when the classic class changes; tinypy intentionally
uses dynamic lookup. Changes to a class added outside the bases by a custom
`mro()` are also immediately visible, whereas CPython 2.7's method cache may
return the old value. Lookup in a type with a classic base is cached just
as in any type without a custom `mro()`, so repeated lookups do not repeat
key comparisons in MRO dictionaries; CPython 2.7 does not cache such types.

`hash()` of nested tuples, including tuple subclass instances that retain
its `__hash__`, and bound methods is computed without native recursion at
any nesting depth; like `instancemethod_hash`, a bound method hashes its
receiver first, then its function. Other values whose hash hashes further
values, such as built-in methods with a receiver and types with a hash
slot, including native types, check the native stack budget
(`max_stack_bytes`) and raise RuntimeError when it is exhausted, without
consuming recursion levels, including when the host computes a hash outside
any call; CPython exhausts the C stack in this case.

As in CPython, classic class attribute lookup through bases, resolution of
its `__getattr__`, `__setattr__`, and `__delattr__` hooks, and
`PyClass_IsSubclass` (in `isinstance`, `issubclass`, `except`, and
`tinypy_class_is_subclass`) do not consume recursion levels, so key
comparisons in class dictionaries run at the caller's depth. They are
limited only by the native stack budget: subclass checking traverses the
last base in a loop and recurses only into the others. A hierarchy deeper
than the budget raises RuntimeError during lookup, subclass checking
(in `except`, this is an unraisable report and a non-match, like a
`PyErr_GivenExceptionMatches` error), and class creation or assignment of
`__bases__` and `__dict__` that resolves hooks; the assigned value remains
in place.

Runtime `intern()`, compiler names, and marshal's interned strings share a
VM-local table without owning references. A string is removed from the
table when its reference count reaches zero, before deferred release;
the table periodically clears tombstones and shrinks its capacity.
Single-character strings and other constant values remain retained by the
VM's own owned references.

Ordinary and checked byte-string constructors reuse an already interned
string by bytes and length and return an owning reference; a new string
created on a miss is not automatically added to the intern table. The empty
string and one-character strings retain separate VM constant caches.
Lookup does not retain deleted strings, accounts for embedded NUL, and does
not reuse objects during shutdown. Long strings exceeding the table's
entry-length limit do not require hash computation during lookup.

Constant protocol, metadata, keyword-argument, encoding, and compiler
service-object names are created once in a shared VM registry. Lookup by
bytes and length reuses the corresponding preset; the internal name
factory returns an owning reference. The registry defines fields,
initialization, interning flags, and shutdown roots, so strings from
different VMs are never mixed. The names `<genexpr>`, `<setcomp>`, and
`<dictcomp>` remain non-interned, preserving byte-for-byte marshal parity
with CPython 2.7.18.

The fixed-name registry is defined directly in the source. AST identifiers,
including names generated by the meta frontend, enter the shared intern
table at creation; code object names and eligible string constants are
interned according to Python 2 rules. Ordinary runtime string data is not
added without an explicit `intern()`. Separate non-interned compiler labels
are not used as AST identifier objects even when their text matches.

`tinypy_type_get_attr` and `tinypy_type_set_attr` accept bytes and a length;
the `*_attr_key` variants accept an existing borrowed str/unicode key from
the same VM. Setters preserve identical native method descriptor
registration, including owner and descriptor kind.
`tinypy_native_function_new_key` retains an existing byte-string name with
an owning reference and obtains the VM from it. These key APIs do not
create strings, look up names by bytes, or add keys to the intern table.

`tinypy_module_new_key`, `tinypy_type_new_key`, and
`tinypy_native_type_new_key` accept a borrowed byte-string name, obtain the
VM from it, and retain that exact object with an owning reference.
`tinypy_module_add_value_key` and `tinypy_module_get_value_key` accept a
borrowed str/unicode key from the same VM; the getter returns a borrowed
value or NULL. Registering a native function preserves its module metadata
under the same rules as the byte-based API.
`tinypy_instance_get_attr_key` returns a borrowed value directly from the
instance or type dictionary, without descriptor binding.
`tinypy_instance_set_attr_key` writes directly to the instance dictionary,
without invoking a user-defined `__setattr__` or descriptor setter.
`tinypy_object_*_attr_value` provides Python attribute semantics. Like
CPython's built-in types, VM-created types, including exceptions, reject
attribute assignment and deletion through this path; internal attribute
installation and native host types are unaffected. The key's full length
accounts for embedded NUL; the existing byte APIs remain compatible adapters.
`tinypy_import_module_key` accepts a borrowed byte-string name; qualified
name handling and the host resolver use its byte representation.

All internal operations on module, attribute, special-method, and keyword
argument names use value keys. Fixed tables contain borrowed VM keys or
offsets of those fields for static callback data. AST name comparisons in
the meta/preprocessor also use existing interned keys. A source check
prohibits internal calls to byte-name APIs and creation of fixed keys
through the byte factory. Bytes remain at public/host boundaries, in
dynamic import path handling, parser tokens, and diagnostic text.
The initial bootstrap of built-in types retains C names until the string
type and VM presets are created; these names do not create Python strings.

Internal registration of native functions, methods, classmethods,
staticmethods, and properties uses shared `tinypy_internal_*_add_*` helpers
with an existing borrowed byte-string key from the same VM. They preserve
descriptor kind, owner, module metadata, and finalizer; temporary references
are released inside the helper. Names from the shared registry are passed
directly from the VM.
Hot builtins are registered through `tinypy_internal_type_add_items_method`
and `tinypy_internal_native_items_function_new` using the internal items
convention: a callback receives self (NULL for a function), an array of
positional arguments, and kwargs without an argument tuple. The caller
checks the declared arity and rejects keywords with the same diagnostics as
the PyArg parsers, except under `TINYPY_ARITY_STYLE_UNCHECKED`, where the
callback parses arguments itself. Public `tinypy_native_function_new`
callbacks continue to receive a tuple.
The host uses the same convention through `tinypy_native_function_new_items`:
the caller checks `minimum <= count <= maximum` and rejects keywords with
`PyArg_ParseTuple` diagnostics; for `tinypy_native_function_new_items_keywords`,
the callback receives kwargs and validates all arguments itself. user_data
is read through `tinypy_native_function_user_data(function)`. A fresh
function held only by the caller's reference and not published by any
module becomes a method descriptor when installed on a type through
`tinypy_type_set_attr`: when called through an instance, a bound method,
or a type with a checked receiver, its callback receives that receiver as
self, and a method call invokes the descriptor without a bound method.
A native function retained elsewhere, such as builtin `len` or a module
function, is stored unchanged, as with CPython's `PyObject_SetAttrString`:
it remains an ordinary function for all owners and binds no receiver.
A function installed as `__new__` is also stored unchanged: like the static
method that `type_new` creates from a Python `__new__`, it does not bind a
receiver, and a class call passes the class as its first argument. In other
cases, such as a property getter, self is NULL and all arguments are in
items. A callback needing an argument tuple obtains it through
`tinypy_native_arguments_acquire`: self, when non-NULL, followed by items,
as in the tuple convention; `tinypy_native_arguments_release` returns an
unretained tuple to the VM cache for the next call.
All fixed names in production C code, including module-local methods,
exceptions, codec aliases, and `__future__`, are created from the registry
at VM initialization. These string fields have the `internal_` prefix,
such as `internal_special_length_key` and `internal_func_code_key`. A source
guard prohibits internal production code from accessing the lazy C-literal
key factory.
`TINYPY_INTERNAL_STRING(vm, "literal")` remains available for extension/test
literals: its length is computed from the literal, including embedded NUL,
and the VM expression is evaluated once. The macro returns a borrowed
interned string retained by the VM until shutdown; it needs no DECREF.
Use `TINYPY_RET` for an owning reference. Outside the registry, these
literals are retained in a lazy VM dictionary. Ordinary runtime strings
check the shared intern table, but a miss adds them neither to that table
nor to the internal C-literal dictionary. A matching literal does not
change a non-interned compiler preset: a separate string is used for it.

`TINYPY_NAME_EQ(name, preset)` compares pointers first, then bytes and
length for different objects. The fallback preserves Unicode and str
subclasses without invoking their user-defined `__eq__`. Different pointers
alone do not imply different strings. `func_code` and `__code__` are
different names for one Python 2 attribute; both aliases are checked
separately using existing VM fields.
Operator index tables and native wrapper slots contain offsets of existing
VM fields. `tinypy_internal_object_special_operator_key(vm, index)` returns
a borrowed preset without allocating or changing reference counts; index
must be less than `TINYPY_SPECIAL_OPERATOR_COUNT`. No separate string array
or additional owning operator references are created. AUTO wrapper
classification compares the full key with its preset, including raw/Unicode
fallback and embedded NUL.
Bound builtin slots have a separate VM type, `method-wrapper`, while
retaining the shared native-function payload and public value kind. Ordinary
bound C methods retain type `builtin_function_or_method`; metadata uses
standard member/getset descriptors, and their free list accounts for the
actual type owner. Bytearray iteration uses `bytearray_iterator`;
str/unicode subtype iteration retains the generic `__getitem__` and
`__len__` protocols.
Method-wrapper and method-descriptor preserve Python-visible cold/warm
metadata errors: the first safe attribute read marks the shared type as
ready; preceding set/delete operations do not cause this transition.
Eager VM names and namespace registration are unchanged. Numeric
real/imag/numerator/denominator fields have the corresponding C member/getset
descriptor kinds, including readonly and wrong-receiver diagnostics.
`object.__class__` is also a getset descriptor, while slice objects'
`start`, `stop`, and `step` are readonly member descriptors, so reading them
does not consume a recursion level, as in CPython.
Built-in codec names are compared with presets after
`encodings.search_function` normalization: ASCII letters are lowercased,
and each run of other characters except `.` between letters and digits
becomes one `_`; aliases from `encodings.aliases` are also checked with
`.` replaced by `_`. The `_codecs` search-function registry, cache, and
error handlers are not exposed as module attributes. Name checks for which
Python 2 considers only the prefix before NUL preserve that behavior.

Compiler literals, filenames, and non-interned marshal payloads bypass
intern-table lookup; during constant folding, an interned result is copied
before its serialization policy is changed.

`TINYPY_RET(value)` returns the same non-null object with an incremented
reference count, evaluating its argument exactly once. `TINYPY_RET_NONE`,
`TINYPY_RET_TRUE`, `TINYPY_RET_FALSE`, `TINYPY_RET_NOT_IMPLEMENTED`,
`TINYPY_RET_ELLIPSIS`, `TINYPY_RET_EMPTY_TUPLE`, `TINYPY_RET_EMPTY_STRING`,
and `TINYPY_RET_EMPTY_UNICODE` take a VM and return an owning reference to
the corresponding existing singleton without allocating.

A Python class retains the original `str` object used as its name,
including its subtype. A native type with a qualified C name returns the
part after the last dot as `__name__` and the prefix as `__module__`.
Builtin exceptions with a short C name use an explicitly registered module.
The instance descriptor `function.__module__` is not the module of the
`function` type itself. A Python class's module is read from its own namespace.

C boolean values use `tinypy_bool_t` and the named constants
`TINYPY_TRUE`/`TINYPY_FALSE`. A `!= TINYPY_FALSE` check preserves the
semantics of any nonzero value; numeric statuses and bitmasks remain numbers.
These constants differ from the Python singletons returned by
`TINYPY_RET_TRUE` and `TINYPY_RET_FALSE`.

Built-in attribute getters select a function from immutable tables by
object type and `attribute_id`. Every byte string contains a borrowed
pointer to immutable `tinypy_internal_string_metadata_t`; for VM presets,
it points to static metadata shared by all VMs with a
`builtin_attribute_id`, and for ordinary strings it is NULL. A zero id
means no specialized getter. An ordinary string matching an interned
preset receives the preset itself and its metadata. Immutable subtype
copies and raw strings do not inherit metadata; changing a string's
contents during growth clears the pointer. Metadata contains no Python
values, retains no references, and is not part of the marshal payload.
Reading an id from a preset requires neither hashing nor name comparison.
For raw/Unicode keys, a preset is borrowed from the shared immutable cache
of internal VM names, and its id is then read from metadata. No separate
builtin-name table is created; the incoming key is neither replaced nor
retained nor added to the cache. Lookup compares full contents without
user-defined `__hash__`/`__eq__`; collisions and embedded NUL do not shorten
the compared span. Shared getters for `__class__`, `__call__`, `__dict__`,
function-code aliases, and the module dictionary retain their direct path.
Custom hook, descriptor, and instance dictionary precedence is preserved:
the `__class__` of an instance whose type has a `get_attribute` hook, a weak
proxy, or a host native type is handled by the hook itself.

The bundled Python-visible surface is intentionally limited to a memory-only
runtime:

- `__builtin__`, `sys`, `exceptions`, `__future__`, `_codecs`,
  `_functools`, `_weakref`, `_struct`, `_sre`, `array`, and `copy_reg` are
  built in;
- `_codecs` guarantees ASCII, Latin-1, UTF-8, and the `hex` transform codec;
  other encodings must be provided by a host search function;
- the native `copy_reg` and `hex_encode`/`hex_decode` functions, implemented
  in Python in CPython, bind positional and keyword arguments like
  `PyEval_EvalCodeEx` and report binding errors like Python functions;
  keyword keys are compared with parameter names by text without calling
  the `__eq__` of `str` and `unicode` subtypes. The `copy_reg` functions
  access their arguments through the same attribute, comparison, `int()`,
  and table protocols as `copy_reg.py`, without their own type checks
  (`_reduce_ex` checks `proto < 2` as an `assert`). Like `PyImport_Import`,
  `object.__reduce_ex__` imports `copy_reg` through `__import__` from the
  executing code's globals' `__builtins__`, read at call time (a dictionary
  item or an attribute of another object); without executing code, it uses
  the `__builtin__` module with globals `{'__builtins__': __builtin__}`;
- `_struct` guarantees the `d` format with repeat counts and byte-order
  prefixes, and `Struct`, `pack`, `unpack`, `pack_into`, and `unpack_from`;
  `_struct.error` is `ValueError`. `Struct` stores its compiled format in
  its native payload, exposes readonly `format`/`size` getsets, and supports
  weakrefs without an instance dictionary. A Unicode format is converted
  to ASCII; embedded NUL terminates the parsed format. Module calls use a
  bounded format cache of 100 entries; instance methods use their own
  compiled snapshot;
- `_sre` executes Python 2.7 regex programs; `_sre.compile` validates a
  program like Python 2.7's `_validate` and rejects any other program with
  `RuntimeError("invalid SRE code")`. Replacement template parsing and
  `Match.expand` use host-provided `re._subx` and `re._expand`: `sub` and
  `subn` pass every replacement except a callable or a string or buffer
  without a backslash to `re._subx`, insert a non-callable result as is,
  and delete the match for `None`;
  `re` is imported in the same way as `copy_reg` for `object.__reduce_ex__`.
  `groupindex` and `indexgroup` may be arbitrary objects: a group name is
  looked up by subscription on `groupindex`, `groupdict` enumerates names
  through its `keys()`, and `lastgroup` reads `indexgroup` as a sequence.
  A `str` subclass's `__len__` cannot exceed the string's actual length,
  and a subject shortened during `sub`, `findall`, `split`, `finditer`,
  or scanner execution ends the search at its current end; CPython 2.7
  reads beyond the buffer in both cases. A program with 100 groups may
  set marks 200 and 201, and `groupindex`'s `keys()` may return any iterable;
  CPython 2.7 writes such marks beyond its array and reads the `keys()`
  result as a list;
- `array` reproduces CPython 2.7.18's `array` module: the `array.array`
  type (also `ArrayType`) with typecodes `c b B u h H i I l L f d`, its
  constructor, methods, sequence, comparison, and iteration protocols,
  `__reduce__`, and CPython error messages. On all targets, item sizes
  match an LP64 CPython 2.7 build with UCS-4 unicode: `c`, `b`, and `B`
  occupy 1 byte; `h` and `H` occupy 2; `i`, `I`, and `f` occupy 4;
  `l`, `L`, and `d` occupy 8; and `u` occupies 4 (a code point, as in
  tinypy's unicode model). Thus `itemsize`, `tostring`/`fromstring`, and
  `buffer()` are identical on 32-bit and 64-bit hosts. Items use host byte
  order, as in CPython; `byteswap` rearranges their bytes. A `u` item above
  U+10FFFF, which `byteswap` or `fromstring` may write, cannot be represented
  in tinypy's unicode: reading it (`a[i]`, iteration, `tolist`, `tounicode`,
  `repr`, or comparison) raises `ValueError`, while its bytes remain
  accessible through `tostring` and `buffer()`; CPython 2.7 stores such
  an item as a character. The memory-only runtime has no file objects, so
  `fromfile`/`read` and `tofile`/`write` parse arguments like CPython and
  then always reject the file with `TypeError`. Array provides the old
  buffer protocol: `buffer(a)` reads the current items on every access,
  so resizing the array leaves no dangling pointers; `_struct`
  (`unpack`, `unpack_from`, and `pack_into` writes), `_codecs` decode
  functions, and `fromstring` accept an array, whereas character buffers,
  `memoryview`, and `bytearray` operations requiring the new buffer
  protocol do not, as in CPython. While an `_struct` call holds an array's
  items, resizing that array is rejected with `BufferError("cannot resize an array that
  is exporting buffers")`; CPython 2.7 writes and reads freed memory in
  this case. Conversion callbacks (`__int__`, `__float__`, `__index__`,
  `__eq__`) that resize the same array do not access beyond its items:
  assignment at an index the array no longer has raises `IndexError`,
  `fromlist` converts items before growing the array, and `fromstring`
  from a view of the array itself reads its items after resizing;
  CPython 2.7 accesses freed or uninitialized memory here, so results may
  differ. Like `ins1`, `append`, `insert`, `extend`, and construction from
  an iterable convert each item twice.
  Sizes computed from item counts are checked for overflow on 32-bit hosts
  as well, and item storage is allocated subject to `max_heap_bytes`,
  raising `MemoryError` on rejection. Like CPython's `arrayiterator`,
  tinypy's retains the array and sees appended items after exhaustion.
  `_sre` does not accept an array as a subject, and passes an array used
  as a replacement to `re._subx`. The module is created on first import;
  before that, like a CPython extension module, it is absent from
  `sys.modules`. The `array.array` type lives until VM destruction, and an
  import after removing the module from `sys.modules` copies the retained
  namespace, like `_PyImport_FindExtension`. Like VM bootstrap, the type
  and namespace are allocated without checking `max_heap_bytes`.
  `fromstring` rejects the array itself or its `buffer()` while the array
  has item storage: an array emptied by deletions retains that storage,
  whereas a new array or one after `*=` by a number no greater than zero
  does not, since this frees its items as in CPython. Extending an empty
  array with an empty array and deleting an empty extended slice allocate
  no storage; CPython 2.7 allocates it and then rejects such an array;
- filesystem-backed standard libraries, source-encoding discovery, process
  metadata, environment-dependent `sys` paths, and standard I/O input are
  not emulated. They are provided by the host or an imported memory artifact.

## 7. Bytecode runtime

The interpreter executes Python 2.7 opcodes and validates code objects before
execution. The verifier checks:

- instruction boundaries;
- argument width and `EXTENDED_ARG`;
- jump targets;
- block-stack transitions;
- value-stack depth: code neither reads nor pops below its block's stack
  level, to which an exception unwinds the stack;
- finally/with reason markers and continuation depth;
- name/constant/local/free-variable indices;
- configured instruction and stack limits.

A byte that is not an opcode is rejected only if control reaches it:
CPython 2.7's peephole optimizer leaves such a byte in dead code (§8), and
CPython executes the function.

Normal entry into cleanup requires a proven None constant; the verifier
obtains that property from the code object's constants. Code may modify or
consume values received by an except block, which clears the proof;
finally/with reasons are consumed only by `END_FINALLY` and `WITH_CLEANUP`,
and cleanup without a known marker is rejected. Like CPython's stack-depth
calculation, the verifier treats the reason for every finally branch as
having exception width (three values); entry reasons for a branch from the
same context are merged, so nested finally blocks do not multiply verifier
states. This is a structural CFG and stack check: arbitrary Python operand
types and the effects of user callbacks are checked by the runtime.

Shared validation for `code()`, the C API `tinypy_code_new`, and the marshal
loader additionally requires `co_nlocals == len(co_varnames)`, space for
arguments, and exact strings in name tuples: frame layout uses the first
value, while the verifier checks local operands against the second.
CPython accepts such code objects; tinypy rejects them, and
`tinypy_code_new` returns NULL with a pending `ValueError`.

Frame execution supports closures, generators, exception blocks,
comprehensions, `with`, imports, and code object tracing data.

After verification, the evaluator identifies method calls: `LOAD_ATTR`
whose receiver is not a global or name and whose value is used only as the
callable of a following `CALL_FUNCTION`. Every path from `LOAD_ATTR` not
interrupted by an exception reaches that call (yield only suspends it),
without reading the stack below the callable or leaving the expression;
jumps between them (conditional expressions, `and`/`or`, and list
comprehensions in arguments) remain between them, a backward jump returns
to the same stack depth, and no other jump enters that region. If such
pairs exist, the code object stores a bytecode copy with these pairs
replaced by internal evaluator opcodes, and its frames receive one stack
slot for each nested method call; `co_code`, `co_stacksize`, `f_lasti`,
marshal, and compiler output do not change. When an attribute resolves to
a function or method descriptor on the receiver's type, or, for a classic
instance whose dictionary provides no such attribute, to a function on its
class, the call receives the receiver as its first argument without a bound
method; otherwise an empty slot sits below the callable, and attribute
lookup and the call follow the normal path. `tinypy_frame_stack_depth`
includes these slots. Native type instances without a `get_attribute` hook
look up attributes and call methods in the same way as class instances.

A class may inherit multiple host native types created through
`tinypy_native_type_new` if their solid bases have matching payload geometry
(offset, size, alignment) and slot counts: the first base defines the
layout, and the others enter the MRO. Different geometry is rejected with
TypeError "multiple bases have incompatible native instance layouts".
Built-in types with native layouts, such as exception families, follow
CPython's solid-base rule and produce "multiple bases have instance lay-out
conflict".

A host native type calls only callbacks from its own spec: a slot without
a callback in the spec retains the behavior of the nearest base that is
not a host native type (`object` by default), even if a native base has a
callback, since that callback expects its own spec's user data.
`tinypy_native_type_update_spec` recomputes slots under the same rule and
restores generic behavior for a removed callback; existing Python subclasses
retain callbacks copied at creation, and host native subtypes retain their
own spec. A Python subclass's special methods remain the wrappers published
by the type, so a removed callback's name disappears from the subclass as
well, although its copied slot still calls that callback. Like
`add_operators` for a C type, a host native type publishes a slot wrapper
for each callback in its spec in its dictionary under the corresponding
special-method name: `__setattr__` and `__delattr__` for `set_attribute`,
`__lt__` through `__ge__` for `compare`, `__radd__` and `__iadd__` for
reflected and in-place callbacks, and `__delitem__` for `mapping_set`.
A wrapper whose name is shared by mapping and sequence callbacks calls
the mapping callback if present, otherwise the sequence callback. A name
defined by the type's namespace retains its value; `tinypy_type_set_attr`
replaces the wrapper, and `tinypy_native_type_update_spec` publishes wrappers
for added callbacks and removes wrappers for deleted callbacks without
touching other callback names or namespace values; reapplying the same spec
changes nothing. A wrapper calls the callback from the spec of the type
that published it, so a Python subclass can delegate to it through
`super()` or through the type itself. Like `update_one_slot`, operations
on an instance read a wrapper found under its own name as a slot of the
instance's type if that type calls the same callback with the same user
data: either the type itself or a Python subclass that copied its spec;
such a slot overrides nothing. A Python subclass whose copied spec defines
a callback for that slot retains it even after the base's spec update
replaces or removes the callback: the base wrapper is read as its slot,
and a copied `mapping_get` keeps instances as mappings even without a
wrapper, including for `%` and `eval`/`exec`. A host native base's wrapper
whose callback the type (or the host native type whose spec the Python
class copied) does not call is read as an attribute of that type's nearest
base that is not a host native type. Any other wrapper, under another name
or from a Python class's second host native base, is an ordinary method
that operations call by name, as in CPython. A spec with `mapping_get`
makes instances mappings, like `mp_subscript`, for `%` and `eval`/`exec`.
`get_attribute` and `set_attribute` act as the type's `tp_getattro` and
`tp_setattro`: `set_attribute` receives every attribute assignment before
type descriptors and every deletion with a NULL value, just as
`mapping_set` receives item deletions, while `object.__setattr__` and
`object.__delattr__` remain generic and do not call the callback. An
`object` attribute held in the dictionary of a Python subclass or its
Python base preceding the host native base in the MRO, such as
`__getattribute__ = object.__getattribute__`, overrides the slot of the
callback spec copied by that subclass, even after a spec update removes
the callback from the base: like `update_one_slot`, the slot becomes
`object`'s slot again, and with `__getattr__`, generic lookup runs before
the hook. Bases for `tinypy_native_type_new` retain the layout of `object`
or a host native type; a base with a built-in layout, such as `list` or an
exception, is rejected with TypeError "native type base '…' has an
incompatible instance layout". `__class__` and `__bases__` assignment does
not transfer payloads between types with different spec `construct`,
`finalize`, or user data, and produces "object layout differs". The
`construct` callback acts as the type's own `tp_new`: it receives the call
arguments, and `object.__new__` and `object.__init__` do not reject them.
A type with `sequence_get` but no `iter` iterates by index through
`sequence_get` until `IndexError`, even when `mapping_get` is present,
like a CPython type with `sq_item`; `in` without `contains` uses that
iteration, and `reversed()` accepts a type with `sequence_get` and
`sequence_length`. Likewise, `sequence_get` acts as `sq_item` for
`apply()`, and `sequence_length` acts as `sq_length` for `map()` and
negative bounds in a Python subclass's `__getslice__`, `__setslice__`,
and `__delslice__`.
A `hash` result of -1 is read as -2, as for `__hash__`; a callback reports
an error through `out_error` or solely through the VM error, and the slot
then forwards it through `out_error`. Like `PySequence_GetItem`, indexed
iteration calls `sequence_get` without `out_error` and reads the VM error:
`IndexError` or `StopIteration` end iteration without a diagnostic; any
other error is not mistaken for the end.
A call already dispatched to a callback that Python code removed through
`tinypy_native_type_update_spec` raises TypeError, while number and
comparison slots return `NotImplemented`. Bases for `tinypy_native_type_new`
belong to the same VM. Payload alignment may be up to
`alignof(max_align_t)` on any target: the VM's internal alignment is at
least that large even on i386 System V, where 64-bit structure fields are
aligned to 4 bytes. Method descriptors created in a heap type by
`tinypy_type_set_attr` refer weakly to the type, like its slots' member
descriptors: the type is freed with its last external reference, and a
descriptor that outlives it accepts no receiver.

`f_exc_type`, `f_exc_value`, and `f_exc_traceback` expose the caller
frame's saved state; the current handler is read through `sys.exc_info()`.
These getset descriptors permit assignment and deletion. `None` clears a
field; on return, saved state is transferred to the VM with ownership
preserved. A frame saves the caller's state on its first handled exception
and restores it on exit and on `yield`, like CPython 2.7's `set_exc_info`
and `reset_exc_info`, including after `sys.exc_clear()` in a called
function. `sys.exc_type`, `sys.exc_value`, and `sys.exc_traceback` mirror
this state; like `PySys_SetObject`, absent value and traceback remove the
names, `sys.exc_info()` returns `None` for them, and `sys.exc_clear()`
writes `None` to all three. Publication retains its own references, so a
`__del__` that calls `sys.exc_clear()` or writes the exiting frame's
`f_exc_*` fields during publication does not release the state being
published. `f_exc_value` and `f_exc_traceback` written without
`f_exc_type` are not saved state: as in CPython, they remain on the frame,
including across `yield`, until the frame is released or saves a handled
exception. Argumentless `raise` checks the handled exception like
`do_raise`: the type is `None` when none is present, and a traceback value
other than a traceback or `None` raises
`TypeError: raise: arg 3 must be a traceback or None`.
`f_locals`, `f_restricted`, and the generator name are readonly getsets
and raise `AttributeError` on assignment or deletion.

On frame exit, fast locals are released immediately even if the frame
remains reachable through a traceback: without cyclic GC, this breaks
frame -> locals -> frame cycles. A function frame also releases the mapping
built from them by `f_locals` or `locals()`; a generator frame does so when
the generator finishes. Anyone still holding the mapping receives it
unchanged, and a later `f_locals` read builds a new mapping from the cleared
locals. If the finalizer of a value being replaced by the `f_locals` read
itself finishes the generator, synchronization stops: names not yet
transferred retain their old values, and the frame keeps its mapping;
`tinypy_frame_locals` returns `NULL` if no one still retains the frame by
then. This intentionally differs from CPython.

Like `enum_next`, `enumerate` refills its result pair while it is the pair's
sole owner. An exhausted `enumerate` releases its pair even if a loop
variable still retains it: without cyclic GC, the last pair would otherwise
retain a cycle through one of its elements. This also intentionally differs
from CPython.

## 8. Compiler

Pipeline:

```text
source decoding
-> tokenizer
-> parser and CST
-> AST
-> future scan
-> optional build-constant preprocessing
-> optional metatemplate expansion
-> symbol table
-> basic blocks
-> bytecode generation
-> stack-depth calculation
-> peephole optimization
-> code object
```

The frontend is implemented inside `src/compiler` using tinypy's objects,
arena allocator, and diagnostics. The Python 2.7.18 compatibility algorithms
and their origins are documented in `LICENSES/README.md`; the PSF license
text is in `LICENSES/PSF-2.0.txt`. The tree contains no copy of CPython
sources, its compatibility headers, or a dependency on the CPython runtime.

The compiler accepts only sized memory buffers. The logical filename is
copied into the code object and diagnostics, but is never opened.

The source decoder supports:

- byte and Unicode source;
- a UTF-8 BOM;
- a PEP 263 cookie in the first two lines;
- ASCII, UTF-8, and Latin-1 aliases;
- CRLF and CR normalization;
- embedded NUL diagnostics;
- structured syntax, indentation, tab, and decoding errors.

In exec mode, source passed through the host API is file text, read like
CPython's file tokenizer: without a cookie or BOM, only ASCII bytes are
allowed (SyntaxError "Non-ASCII character '\xe9' in file ... but no encoding
declared", without text). A cookie specifying an unknown encoding or
contradicting the BOM is rejected on its own line ("encoding problem: ...").
End of file is read as another line with empty `text` (an error there is
"invalid syntax" on the line after the last, with its `offset` calculated
from the tokenizer's previous buffer: the last line read, with its first
byte replaced by a terminator). An open multiline token receives one more
newline before end of file unless the file ends in a lone `\r`
(`Py_UniversalNewlineFgets` reads ahead after it and encounters end of file).
The `TINYPY_COMPILE_FLAG_STRING_SOURCE` flag, eval and single modes, and
Python's `compile`, `exec`, and `eval` compile a string like CPython's string
tokenizer: bytes are accepted as is, end of string produces "unexpected EOF
while parsing", and a trailing CRLF in exec mode adds an empty line, like
`translate_newlines`. A string cookie is read only on a newline-terminated
line, as in `decode_str`: exec-mode strings receive a final newline; eval
and single strings do not.

Cookies are normalized like `get_normal_name`: spellings `utf-8`,
`latin-1`, `iso-8859-1`, `iso-latin-1`, and their suffixed forms take the
fast path; others are looked up in the codec registry according to
`encodings.aliases` rules and decode the source through the codec. A
string is decoded as a whole ("'utf8' codec can't decode byte ... in position
N", lineno 0), then re-encoded as UTF-8, like `translate_into_utf8`, which
joins a surrogate pair into one code point; a file is decoded line by line
after the cookie line, like a StreamReader.
File reading models StreamReader's `readline` as called by `decoding_fgets`:
72-byte reads, doubled up to 8000 until the line ends. Python 2's UTF-8
codec accepts surrogates; a decoding error belongs to the line the tokenizer
is reading when the read fails, with a position within that read's data;
a sequence truncated by end of file is discarded. Lines read through a
codec preserve lone `\r` (the tokenizer replaces only a final `\r\n` with
`\n`), join a surrogate pair into one code point like `PyUnicode_AsUTF8String`
in `fp_readl`, and an open token at the end of such a file receives no
additional newline. A SyntaxError's `text` and `offset` are restored to the
declared encoding like `PyTokenizer_RestoreEncoding` (the UTF-8 encoder
joins a surrogate pair into one code point); `text` covers the entire
logical line of a multiline token, and "expected an indented block" takes
precedence over "unexpected unindent", as in `err_input`.

Source and literal decoding errors are passed to the handler registered as
`strict` in the codec registry, as in CPython. The tokenizer reads text with
the returned replacements: a string is decoded as a whole; a file starts
from the read containing the first error, and the rest of the file after
that read is decoded in one call. A Unicode literal receives the replacement
for an invalid UTF-8 sequence as escapes, like `decode_unicode`, in full
regardless of length (CPython 2.7 writes replacement escapes longer than
the literal beyond the buffer sized for that literal), and a replacement
for an escape not handled by `unicodeescape` or `rawunicodeescape`, with
parsing continuing from the returned position. A byte literal from source
with an ASCII cookie is re-encoded by a strict ASCII encoder. In a literal,
only UnicodeError becomes SyntaxError "(unicode error) ..."; other handler
exceptions propagate. The tokenizer converts any decoding exception to a
SyntaxError with its text. If the exception's `str()` fails, the message
is "(unicode error) unknown error" or "unknown decode error", as in
`ast.c` and `err_input`.

Indentation consistency between tabs and spaces is not checked by default,
as in CPython without `-t`: tabs expand to multiples of eight.
`TINYPY_COMPILE_FLAG_TAB_WARNING` reproduces `-t` (one warning to
`sys.stderr` per file), and `TINYPY_COMPILE_FLAG_TAB_ERROR` reproduces
`-tt` (TabError).

Numeric parsing is locale-independent. An integer literal creates an
integer if it fits the signed 64-bit range, otherwise an arbitrary-precision
long. Float and complex preserve their binary values; a float literal of
any length is correctly rounded, with parsing considering at most 800
significant digits and a sticky digit, which is enough for exact double
rounding.

The compiler reproduces Python 2.7 closures, cells, class scopes, name
mangling, generators, comprehensions, future flags, code flags, constant
ordering, line tables, and nested code objects.

The peephole optimizer also reproduces CPython's rule replacing
`RETURN_VALUE LOAD_CONST None RETURN_VALUE` with one `RETURN_VALUE` by
checking only the byte four bytes after the first `RETURN_VALUE`: after
`END_FINALLY` and `POP_BLOCK` (for example, `return` at the end of a finally
block inside try/except), four `NOP` bytes end inside a jump, leaving its
high byte in dead code, as in CPython. If that byte takes an argument,
CPython reads the following code at a shifted offset, corrupts jumps, and
may crash; tinypy replaces both the `NOP` bytes and that byte, so its
bytecode here differs from CPython 2.7.18's invalid output.

Single-character and empty byte strings are shared interned VM singletons,
like `characters[]` in CPython. An escape-decoded single-character literal,
a single-character literal from source whose cookie without a normalized
name selects an ASCII or Latin-1 codec (`parsestr` re-encodes it through
that codec), and a single-character result of `%` formatting during constant
folding are created as separate non-interned objects, so marshal output
matches CPython 2.7.18 byte for byte and shared strings never change flags.

Optimization levels:

- `0`: assertions and docstrings are retained, `__debug__ == True`;
- `1`: assertions are removed;
- `2`: assertions and docstrings are removed.

Assigning to `__debug__` is a compile error.

## 9. Compiler API

Main functions:

```c
tinypy_value_t *tinypy_compile_source(...);
tinypy_value_t *tinypy_eval_source(...);
tinypy_value_t *tinypy_exec_source(...);
tinypy_value_t *tinypy_eval_code(...);
tinypy_value_t *tinypy_exec_code(...);
```

`tinypy_compile_options_t` specifies mode, future flags, `dont_inherit`,
optimize, feature flags, limits, an optional immutable build profile, and
the source flags `TINYPY_COMPILE_FLAG_STRING_SOURCE`,
`TINYPY_COMPILE_FLAG_TAB_WARNING`, and `TINYPY_COMPILE_FLAG_TAB_ERROR` (§8).

With `dont_inherit == 0`, explicit flags are merged with the current frame's
future flags. Imports compile source with `dont_inherit == 1`.

Options and limits are read within their `struct_size` (§3): fields added
beyond a smaller size receive defaults from `tinypy_compile_options_init`
and `tinypy_compile_limits_init`. The first `tinypy_compile_limits_t` layout
ended at `max_arena_bytes`; a different `abi_version` or a size smaller
than the first layout fails the call with `TINYPY_ERROR_TYPE`.

In exec mode, source passed through the host API is treated as file text
with a logical filename: a semantic SyntaxError from the AST, future
scanner, or symbol table receives its line without leading spaces in
`text`, like `PyErr_ProgramText`. The file line is reread with universal
newlines through a 1000-byte buffer, so only the final portion of a longer
line remains. SyntaxWarning is written to `sys.stderr` in one `write`
call, like `warnings.showwarning`, together with the file line stripped of
surrounding whitespace, as read by `linecache`: if `sys.stderr` is `None`
or `write` raises `IOError` or `UnicodeError`, the warning is lost; missing
`sys.stderr` or any other error fails compilation. A `-t` warning is written
like `PySys_WriteStderr`: to host stderr if `sys.stderr` is missing or
writing fails. Strings passed to `compile`, `exec`, and `eval`, host source
with `TINYPY_COMPILE_FLAG_STRING_SOURCE`, and eval and single modes are not
files, and `text` for such errors is `None`. Unicode source for `compile`,
`exec`, and `eval` is encoded as UTF-8 like `PyUnicode_AsUTF8String`, which
joins a surrogate pair into one code point. A SyntaxError with a location
is constructed from `(msg, (filename, lineno, offset, text))`, like
`err_input` and `compiler_error`, so its `message` is empty; a SyntaxError
raised with only a message receives its location through attributes, like
`PyErr_SyntaxLocation`, and retains its `message`.

Compiler limits cover:

- source bytes;
- tokens;
- CST and AST nodes;
- nesting: parser-stack depth and AST statement/expression depth, including
  elif chains and with items, each of which nests the next statement, and
  flat chains of attribute, call, and subscript trailers. The symbol table,
  code generator, and canonical source renderer (§11) traverse flat binary
  operator chains without recursion, so those chains count only with
  preprocessor or meta features;
- symbols;
- basic blocks;
- instructions;
- constants and constant bytes;
- compiler arena bytes;
- preprocessor operations, value nodes, and bytes;
- template expansions, depth, and generated AST nodes;
- expanded source bytes and source-map entries.

Temporary compiler allocations live in a call-local arena and are released
together on success or error.

## 10. Build profiles

An immutable build profile stores typed constants and a deterministic
digest. Supported values:

- `None`, bool, integer, long, and float;
- byte string and Unicode;
- an immutable tuple of supported values.

Reserved build-constant format:

```text
^__[A-Z][A-Z0-9_]*__$
```

The profile fully copies input through the host allocator; an allocation
rejected by the allocator ends `tinypy_build_profile_create` with
`TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED`, releasing all allocated storage.
Input order affects neither canonical ordering nor the digest. The optimize
level is part of the profile.

With `TINYPY_COMPILE_FEATURE_PREPROCESSOR` enabled, every bare-name read
matching the reserved format is replaced by literal AST before the symbol
table. Attributes such as `object.__STATE__` are not build constants.
A missing constant or any attempt to bind a reserved name is a compile
error.

`__NDEBUG__` is always created by the profile itself: optimize `0` produces
`False`, while optimize `1` and `2` produce `True`. This constant cannot be
provided explicitly.

The pure evaluator supports literals and literal containers, boolean,
unary, arithmetic, and bitwise operations, comparisons, membership, and
identity with `None`/bool. A fully decidable `if` is replaced by the chosen
suite before symbol analysis. Calls, attributes, subscripts, lambdas, and
comprehensions keep a condition at runtime even though build constants
inside it have already been replaced. A condition whose evaluation raises
an exception (`1 // 0` or an unhashable dict key) also remains at runtime:
the exception is raised when and if the condition executes; only
`MemoryError` causes a compile error.

Build constants are also substituted inside assignment targets: augmented
assignment, `for`, `with`, `except`, and comprehension targets. A suite
completely removed by preprocessing is replaced with `pass` at its first
statement's position, and an emptied `else` disappears entirely, keeping
the line table monotonic. Evaluator intermediate results are bounded before
computation: integer results of `**`, `<<`, and `*` cannot exceed 65536
bits, and strings and sequences cannot exceed `max_preprocessor_bytes`;
exceeding a limit is a compile error. Without a build profile, the
preprocessor defines no constants.

The preprocessor also replaces the immutable builtin `__debug__` with a bool
literal derived from the compile optimize level. Conditions such as
`if __debug__ is True and runtime_flag is True:` lose their body at optimize
`1` and `2`; an ordinary `else` is retained. Boolean conditions can fold a
decisive constant prefix, preserving short-circuit order. For example,
`__debug__ and callback()` skips the callback in optimized code, whereas
`callback() and __debug__` still calls it.

Hosts can supply `TINYPY_BUILD_VALUE_DEBUG_FLAG` entries in the same profile
to mark runtime boolean names such as `_DEVELOPMENT`. These entries have no
value fields and cannot occur inside tuples. Unlike literal build constants,
their names accept ASCII Python identifiers, excluding keywords, `None`,
`True`, `False`, `__debug__`, and the synthesized `__NDEBUG__`.

With `TINYPY_COMPILE_FEATURE_PREPROCESSOR` enabled:

- Optimize `0` preserves reads and bindings of these flags at runtime. A flag
  can be either `True` or `False` and can change between executions.
- Optimize `1` and `2` remove the test and body of an `if` when its test
  contains a bare-name read of a registered flag. Its `elif` and `else`
  alternatives are retained and preprocessed normally. Any calls in the
  removed test are also dropped. This applies to negation and `or` as well;
  it is a lexical removal rule, rather than substitution with `False`.
- Reads outside `if` tests remain runtime reads at every optimize level.
  Attributes such as `object._DEVELOPMENT` and strings do not match. Matching
  precedes name mangling and symbol analysis, so local names with the same
  spelling also match. Removed bindings and yields contribute no symbols or
  generator flag to their enclosing function.

An `elif` is a nested `if` in the AST: if only its test uses a debug flag,
that `elif`'s test and body disappear while earlier clauses and following
alternatives remain. The flag list is copied into the immutable compile
environment and included in the profile digest, so dynamic `compile`, `eval`,
and `exec` inherit the same rules. With preprocessing disabled, the markers
have no effect on source compilation. Preprocessor ABI `3` retains the
alternatives that ABI `2` removed.

The host can attach that environment to marshal-loaded code and all nested
code constants with `tinypy_code_set_compile_options`. The function copies
the profile for subsequent dynamic compilation; it leaves existing bytecode
unchanged. Invalid settings or allocation failure preserve the previous
environment. Static debug blocks in serialized code must already have been
removed when that code was compiled.

For example, an embedding host can register `_DEVELOPMENT` when constructing
its profile (and register additional flags with more entries in the array):

```c
tinypy_build_constant_t flags[1] = {0};
flags[0].abi_version = TINYPY_COMPILER_ABI_VERSION;
flags[0].struct_size = (uint32_t)sizeof(flags[0]);
flags[0].name = "_DEVELOPMENT";
flags[0].name_size = sizeof("_DEVELOPMENT") - 1U;
tinypy_build_value_init(&flags[0].value, TINYPY_BUILD_VALUE_DEBUG_FLAG);

tinypy_build_profile_t *profile = NULL;
/* Check the result before using profile. */
tinypy_build_profile_result_e status = tinypy_build_profile_create(
    allocator, optimize_level, flags, 1U, NULL, &profile, NULL);

tinypy_compile_options_t options;
tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
options.optimize_level = optimize_level;
options.feature_flags |= TINYPY_COMPILE_FEATURE_PREPROCESSOR;
options.build_profile = profile;
/* Pass options to tinypy_compile_source, then destroy the host profile
   when it is no longer needed; compiled code owns its own copy. */
```

## 11. Metatemplates

With `TINYPY_COMPILE_FEATURE_META`, `@meta` is an explicit compiler-only
syntax prefix. No import is required, and no builtin value or runtime module
is installed. Basic form:

```python
@meta.template
def ObjectTemplate(TypeName):
    ClassName = @meta.concat('Mixin', TypeName)

    @meta.emit(name=ClassName)
    class Generated(object):
        pass

MixinItem = @meta.expand(ObjectTemplate, 'Item')
```

The template marker has only the explicit form `@meta.template`; bare
`@meta` remains an ordinary Python decorator. The name `meta`, including
function, class, parameter, variable and import bindings, has no special
meaning without the explicit dotted prefix. Ordinary `meta.expand(...)`
is an ordinary runtime call. There are no import aliases or binding rules
for generation operations.

The frontend recognizes the token sequence `@`, `meta`, `.`, member with
META enabled. Strings and comments are never rewritten. Reading these
tokens ahead of the parser does not move a syntax error: it is reported at
the token where the parser stops, as without META. Declaration
operations (`template`, `emit`, `rename`) retain their decorator shape in
the CST; expression operations such as `expand`, `range`, and `concat`
are lowered as expressions. The AST uses an inaccessible compiler marker
for both forms. The meta pass consumes these markers before symbol analysis
and bytecode generation. Ordinary decorators on emitted declarations are
preserved and are applied once at runtime to the generated declaration.
No unconsumed compiler marker can enter the generated bytecode or canonical
expanded source.

`@meta.expand` accepts a template and literal arguments, either as a
standalone statement or as the entire value of an assignment. Assignment
receives the generated declaration, or a tuple when the template emits
multiple declarations. Expansion also supports defaults and keywords,
staging assignments/`if`/`for`, `@meta.range`, `@meta.concat`, multiple
`@meta.emit` declarations, `@meta.rename`, generated declaration names,
`@meta.name`, `@meta.getattr`, `@meta.setattr`, `@meta.delattr`, and
`@meta.current_class`. The generated class name is used for private-name
mangling and `super`.

Expansion executes no Python bytecode, imports, I/O, or user runtime
functions. After the pass, any unconsumed `@meta` operation is a compile
error. Staged tuple and list values are substituted into generated code as
displays, rather than constants. A generated name cannot be a keyword;
a bound name (`@meta.emit`, declaration renaming, or `@meta.setattr`) also
cannot be `None` or `__debug__`.
The meta evaluator honors `from __future__ import division` and the same
result limits as the build preprocessor. META ABI 2 requires the explicit
prefix at every generation operation; sources using the previous implicit
`meta` namespace must add `@` at those operation sites.

`tinypy_preprocess_source` runs the same preprocessing pipeline and returns
an owned result containing canonical valid Python 2 source, source-map
entries, and the map's SHA-256 digest. Each generated declaration associates
the generated position, template position, expansion position, and semantic
symbol. Canonical source compiles to the same program. An operand is
parenthesized only when Python 2 operator precedence and associativity
require it, and an else containing a single if is written as elif, so long
operator and elif chains do not hit parser or indentation limits.
Negative numeric literals and numeric operands of attribute, subscript,
and arithmetic unary operations are parenthesized; byte strings under
`unicode_literals` receive the `b` prefix; a one-dimensional extended slice
retains its comma (`k[1:2,]`); and a complex literal is written as an
imaginary literal or its sum or difference with the real part, preserving
signed zeros, infinities, and nan without the name `complex`.

## 12. Compile environment

Every code object graph produced from source shares a reference-counted
immutable compile environment: feature flags, optimize level, and an
optional deep copy of the build profile. The original host profile may
therefore be destroyed immediately after compilation. The environment and
profile copy live in the VM heap and count toward `max_heap_bytes`;
rejection by the host or budget ends compilation or import with `MemoryError`.

Python-visible `compile`, `eval`, and string-based `exec` inherit the
current frame's environment. `dont_inherit` disables only future-flag
inheritance, not the build profile, meta/preprocessor flags, or optimize
level. A C API call without a current frame uses explicitly supplied
options; imports use the module artifact's fields. Marshal code receives
its environment from the size-aware artifact descriptor because marshal
v2 does not serialize it.

## 13. Imports and host callbacks

The core does not construct filesystem paths. The resolver receives a
canonical module request and returns one of these memory artifacts:

- a source buffer;
- a marshal-v2 code buffer;
- a native module descriptor.

An artifact contains a logical filename, canonical name, package metadata,
and release callback; a package artifact's `package_token` is the package
directory path and the sole element of its `__path__`; without it,
`__path__` is empty. `sys.modules` supports packages, circular imports, and
rollback on resolution, compilation, or execution errors; `MemoryError`
while writing module metadata (`__name__`, `__file__`, `__path__`) or
registering the module in `sys.modules` also rolls back the import.
Like `add_submodule`, a loaded submodule is bound to its parent: a module
receives it in its namespace, bypassing `__setattr__`; another object from
`sys.modules` receives it as an attribute. A binding error, whether an
`__setattr__` exception or `MemoryError`, ends the import, and the submodule
remains in `sys.modules`, as in CPython.
If a loader replaces the registered object during `reload`, that object
becomes the result; the original module retains its namespace. The
replacement need not be a module. Ordinary reloading of a source artifact
executes in the existing namespace and, like `PyImport_ExecCodeModuleEx`,
preserves the module's own `__builtins__`. Calling `reload` on a module
already being reloaded returns that module, like `modules_reloading`;
unlike CPython, a nested reload of another module does not clear this marker.

A module absent from `sys.modules` is searched for in `find_module` order:
finders from `sys.meta_path`, the host finder (`tinypy_vm_set_module_finder`),
modules created by the VM itself, and the resolver. A non-list
`sys.meta_path` produces `RuntimeError`, as in CPython. Like `find_module`,
import reads the length of `sys.meta_path` once: a finder appended during
the search is not queried, and a finder that shortens the list ends the
import with `IndexError` from `PyList_GetItem`. The resolver request passes
the submodule package's borrowed `__path__`, whatever its value, as
`package_path`, or NULL for a top-level module. The host looks up a
top-level module wherever it stores modules (the CLI uses `sys.path` as
left by the program); in this case finders receive path `None`, as in
CPython. A resolver that has no module returns NULL; an exception it raises
before returning ends the import, except for `ImportError`, which means
the module is absent, as with finders (this is how the CLI reports
`RuntimeError` for a non-list `sys.path` or `__path__`). An artifact
returned by the resolver with an exception raised is not loaded: import
ends with that exception. Like `_PyImport_LoadDynamicModule`, which checks
`PyErr_Occurred()`, a native initializer ends the import with its raised
exception even when it returns true; a failed import's module is removed
from `sys.modules`. `sys.path_hooks` and `sys.path_importer_cache` do not
exist: search-path entries belong to the host, the core does not read
them, and a hook installed from Python would not be called.

Both kinds of finder follow PEP 302: `find_module(name, path)` returns a
loader or `None`, in which case the next source is queried. As in
`import_submodule`, `ImportError` from `find_module` during import means
the module is absent and the search has ended: an implicit relative import
falls back to the absolute name, a missing from-list name is skipped, and
otherwise the result is `ImportError: No module named X`; `reload` reports
the original error unchanged. A finder is retained only during its
`find_module`, so one removed from `sys.meta_path` or the host is released
before its loader's `load_module`. The module returned by
`load_module(name)` is the import result. Unlike CPython, which leaves
`sys.modules` to the loader, import registers the returned module if the
loader registered none, and removes the entry of a loader that failed or
returned `None`; `None` is reported as `No module named X`, including in
`reload` (CPython returns `None`). `reload` returns the loader's module and
leaves `sys.modules` as the loader left it.

Relative import continues from the package in `sys.modules`, like
`get_parent` and `load_next`: its ancestors are not reimported even if
they are absent from `sys.modules` or hold `None` there. For `load_next`,
`None` in place of the package itself means a package without `__path__`:
its submodules are searched for as top-level modules (finders receive
path `None`, and the resolver receives NULL `package_path`) and registered
under their full names, and an implicit import does not fall back to an
absolute name. Unlike CPython's `is_builtin`, VM modules are not found
by the last name component in this case.

Like `_PyImport_FixupExtension`, the VM saves its modules' namespace names
and values at creation: importing such a module after its removal from
`sys.modules` creates a new module with those names, while `reload`
restores their saved values without removing added names; the resolver is
not called for these modules. As in CPython, `sys.modules`, `sys.meta_path`,
and `sys.exc_type` are not among the saved names. Saved values live until
`tinypy_vm_destroy`, like extension-module copies until `_PyImport_Fini`;
consequently, the tables of `copy_reg`, which is written in Python in
CPython, and their registered objects survive host namespace cleanup as
well. The `copy_reg` functions read globals from the module created by
the VM; the VM retains that module until `tinypy_vm_destroy`, so these
functions still work after it is removed from `sys.modules`, including in
a new module created by import (in CPython, a released module's globals
become `None`).
`__main__` appears in `sys.builtin_module_names` but cannot be recreated:
`ImportError: Cannot re-init internal module __main__`.

Output streams, warnings, diagnostics, and formatted tracebacks are sent
to host callbacks. Callback input is valid only for the duration of the
call unless explicitly stated otherwise. A host finishing a top-level run
or reporting an uncaught exception first calls `tinypy_output_flush_line`:
like `Py_FlushLine`, it appends to `sys.stdout` the newline deferred by a
print statement with a trailing comma, ignores write errors, and preserves
the raised exception.

The evaluator calls `poll_interrupt` on frame entry and before every
backward jump: `JUMP_ABSOLUTE`, `CONTINUE_LOOP`, and conditional jumps.
Every loop, including one in loaded bytecode, passes through such a jump,
so the host can interrupt it: a true result from `poll_interrupt` raises
`KeyboardInterrupt` (`TINYPY_ERROR_INTERRUPT`). Like `SIGCHECK` in `x_mul`,
`x_divrem`, and `long_format`, the quadratic long-integer kernels
(multiplication, division and remainder, modular `pow`, and conversion to
decimal text) poll the host after every 65536 word steps, so even a single
long operation can be interrupted. These kernels do not poll the host
during compilation: constant folding interrupted by the host would discard
the interrupt together with its error.

The debugger (`TINYPY_DEBUGGER`, Debug builds only, `debugger.h`) receives
the events of the trace function installed by `PyEval_SetTrace` in CPython.
Like `maybe_call_line_trace`, LINE marks the first instruction of a line
according to `co_lnotab` and backward jumps: each iteration of a one-line
loop receives its own LINE, but a line entered in its middle by a jump
(for example, `END_FINALLY` after a non-matching `except`) does not; each
frame execution, including generator resumption, starts with an empty line
window. Like `call_exc_trace`, EXCEPTION arrives after the frame has been
added to the traceback and passes the type, value in its raised form
(a `sys.exit` argument is not an instance), and traceback; the event's
`exception_type` field is present when covered by `struct_size`. The
callback executes with the pending exception set aside and cannot raise
its own: any exception left by its code is discarded. An interrupt
reported by `poll_interrupt` while the callback runs interrupts its code
and is raised again at the next poll after the callback in the same
execution, so the program does not lose it (CPython's trace function would
propagate `KeyboardInterrupt`); if the outer execution frame finishes
first or `tinypy_debugger_set` removes the debugger, the interrupt is
discarded. Like `PyFrame_FastToLocals`, `tinypy_debugger_frame_get`,
`_set`, and `_delete` find a closure-captured local, whether an argument
or a sublist-parameter name (`def f((a, b))`), in its cell; like
`PyFrame_LocalsToFast`, set and delete update both that cell and the name's
fast slot. Free variables of a frame without `CO_OPTIMIZED` (a class
body) are not included in its locals. Like `PyFrame_LocalsToFast(frame, 0)`,
`tinypy_debugger_frame_sync` copies locals materialized by
`tinypy_frame_locals` into fast locals and cells.

Reporting an uncaught exception is the host's responsibility: it reads the
raised exception, its type, and traceback through the public API and writes
the report to the `sys.stderr` object. Like `PyErr_SetObject`, `sys.exit`
leaves SystemExit unnormalized: the raised value is its argument (`None`
without an argument), not an instance, until `except` or `with` normalizes
it, so the host matches the raised type and reads the code like
`handle_system_exit`. An argument that is a SystemExit instance or subclass
instance also remains the value with raised type SystemExit until
normalization takes the instance's class, like `PyErr_NormalizeException`.
Likewise, after `raise A, value`, the raised type remains `A` even when
calling `A` returned a subclass instance. `tinypy_vm_normalize_error`
normalizes the raised exception like `PyErr_NormalizeException`: a value
that is not an instance of the raised class becomes its call arguments,
while a subclass instance supplies its own class as the raised type. If
the call fails, the function returns false and the exception it raised
remains pending, with the original traceback if it has none of its own;
like `PyErr_NormalizeException`, that exception is normalized in turn, one
recursion level deeper each time, up to the recursion limit. Beyond that,
as with `PyExc_RecursionErrorInst`, the pending exception becomes
RuntimeError "maximum recursion depth exceeded" with the same traceback.
Exceptions the runtime raises as a class and arguments, like
`PyErr_SetString`, `PyErr_SetObject`, `PyErr_Format`, and
`PyErr_SetFromErrno` (KeyError, IndexError, ZeroDivisionError, NameError,
AttributeError, OverflowError from float exponentiation, ReferenceError
from a dead proxy, RuntimeError from the recursion guard, and other VM errors), remain unnormalized
until the first handler: normalization at the recursion limit replaces
them, so `except` in the deepest frame sees RuntimeError, as in CPython.
An exception set aside and restored by the runtime while other code runs
is considered normalized. Like `PyErr_PrintEx`, the host normalizes an
uncaught exception after the SystemExit check and before `sys.excepthook`
and reporting.
Like `PyErr_GivenExceptionMatches`, `tinypy_exception_matches` matches an
exception instance's class, matches a value that is not an exception class
by identity, and raises no exception: it reports a metaclass
`__subclasscheck__` error as unraisable. Thus an unnormalized raised
SystemExit value does not match SystemExit. Matching, including in
`except` in the deepest frame, consumes no recursion levels: a tuple of
candidates is checked item by item, and nested tuples are limited only by
the native stack budget; nesting beyond that budget, where CPython recurses
without a limit, is reported as unraisable and does not match.
A host finishing a program like `Py_Finalize` calls `sys.exitfunc` before
`tinypy_vm_destroy`, then replaces module namespace values with `None`
in `PyImport_Cleanup` order so that finalizers of still-live objects run;
values surviving this cleanup remain reachable from the VM because,
without cyclic GC, releasing the namespace would otherwise leave a cycle
unreachable. The CLI (`cli/README.md`) implements both contracts.

## 14. Errors

`tinypy_error_t` is an optional owned structured error. It stores copies
of the message, logical filename, source line, line number, and column offset.

For a raised Python exception, the error message is that exception's
`str()` up to the first NUL byte; an unnormalized value, such as a
`sys.exit` code, is read as the `str()` of the instance created from it
by normalization (`""` for `sys.exit()`). Like CPython's traceback printer,
tinypy computes it on the first `tinypy_error_message` read, rather than
when the exception is raised: until then, the error holds a reference to
the exception, and an exception handled by Python code is never formatted.
The error's kind is likewise computed on the first `tinypy_error_kind`
read from the class with which the exception was raised.
`tinypy_vm_destroy` first computes the message and kind of all unread
errors, so an error remains usable after VM destruction; until then, it
counts toward the VM's heap budget (section 5). Code executed by a read
(`str()` or releasing a class) may release the error itself: in that case
it is freed after the read, and `tinypy_error_message` returns an empty
message.

Recoverable categories include:

- Python semantic errors;
- syntax, indentation, tab, and decoding errors;
- malformed bytecode, marshal, and artifacts;
- configured resource limits;
- ABI and profile mismatch;
- host module resolution failure.

An error also sets the correct Python exception state in the VM.
`tinypy_vm_raise_error` raises the kind's built-in class with the message
as its sole argument; `TINYPY_ERROR_UNICODE_DECODE` and
`TINYPY_ERROR_UNICODE_ENCODE` raise UnicodeError because UnicodeDecodeError
and UnicodeEncodeError take five codec-error fields, supplied by
`tinypy_exception_new`.

Only top-level C API entry points clear a pending exception on entry:
`tinypy_compile_source`, `tinypy_preprocess_source`, `tinypy_eval_source`,
`tinypy_exec_source`, `tinypy_eval_code`, and `tinypy_exec_code`. Like
CPython's C API, other functions expect no pending exception: a successful
call leaves any pending exception in place, and a failed call replaces it
with its own. `tinypy_object_get_attr`, `tinypy_object_get_attr_value`,
and `tinypy_object_has_attr*` look up an attribute with the exception set
aside, so `__getattr__` is called, a missing attribute raises
`AttributeError`, and `has_attr` does not clear it; `tinypy_equal` and
`tinypy_hash` do not read it. Error messages display a non-string attribute
key passed by the host to value-based variants through its repr.

When binding additional `**kwargs`, a `__hash__` or key-comparison error
propagates to the caller immediately; the function body does not run.
CPython 2.7.18 ignores a dict-insertion error in this branch and may return
a result with a pending C exception. Tinypy does not reproduce this invalid
state. This exception to oracle comparison is covered by a separate runtime
regression in every build profile.

## 13. Marshal and artifacts

The marshal reader and writer operate on raw Python 2.7 marshal-v2 objects.
Direct code dumping supports a size query and a caller-owned output buffer.
The writer preserves string interning/reference order and nested code
objects. The direct dump's interned-string table is allocated from the VM:
rejection by the host or budget ends the dump with
`TINYPY_MARSHAL_BYTE_LIMIT` and a pending `MemoryError`, and writes never
exceed the buffer's capacity.

The artifact header contains ABI versions, optimize level, profile digest,
source hash, future flags, payload size, and checksum. The loader never
silently accepts an unknown version or incompatible payload.

`tinypy_marshal_load_code_v2` is fully fallible: the parser and materialized
values allocate VM memory subject to `max_heap_bytes`. An allocation
rejected by the host or budget ends loading with
`TINYPY_MARSHAL_BYTE_LIMIT` and a pending `MemoryError`, releasing everything
constructed so far. Like CPython's `r_object`, a constant dict with an
unhashable key ends loading with `TINYPY_MARSHAL_INVALID_GRAPH` and a
pending `TypeError`; other marshal errors raise no exception. Importing a
marshal artifact reports such a pending exception, and reports other errors
as `ImportError`.

## 14. Validation

Required gates:

- C11 builds with warnings as errors;
- C and C++ public-header compilation;
- unit tests for the object model, bytecode runtime, compiler, marshal, and artifacts;
- source modes `exec`, `eval`, `single`, and dynamic compilation;
- imports, packages, circular imports, and failed-import rollback;
- compiler limit boundary tests;
- allocator accounting after success and error;
- independent-VM parallel compilation;
- reentrant compilation from a native callback;
- bounded malformed-input tests and fuzz targets;
- ASan and UBSan;
- a symbol audit of direct allocator, I/O, environment, process, locale, and thread APIs;
- a namespace audit prohibiting legacy and foreign exported symbols.

After `tinypy_vm_destroy`, allocator accounting must return to zero when
the host ownership contract is respected.
