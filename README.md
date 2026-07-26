# tinypy

tinypy is a host-embedded Python 2.7 runtime and compiler written in C99.
It executes source or bytecode entirely from memory and exposes a versioned C
ABI with the `tinypy_` prefix.

The runtime has no direct dependency on filesystem, stdio, environment,
process, locale or thread APIs. Dynamic memory is provided by the host
allocator. Module resolution, output, diagnostics and interruption are also
delegated through host callbacks.

Each `tinypy_vm_t` owns all mutable runtime and compiler state. Independent VMs
may run concurrently without a process-wide lock. A single VM has one owner
thread and supports synchronous reentrant calls.

## Runtime

The current implementation provides:

- `None`, bool, integer, arbitrary-precision long, float and complex values;
- byte strings, Unicode strings, tuples, lists, dictionaries, sets,
  frozensets, byte arrays, buffers and slices;
- functions, closures, methods, generators, iterators and exceptions;
- old-style and new-style classes, metaclasses, C3 MRO, descriptors,
  properties, `__slots__` and `super`;
- frames, tracebacks and Python 2.7 bytecode execution;
- memory-only imports with packages, circular imports and failed-import
  rollback;
- VM-local cached constants, reference-counted lifetime and a VM-local
  arena/pool allocator for small objects;
- a memory-only CPython 2.7 marshal-v2 reader and writer;
- an artifact container with ABI/profile metadata and SHA-256 integrity.

tinypy deliberately has no cyclic collector. Embedders must release owned
values and break owning cycles before destroying a VM.

## Compiler

The embedded compiler accepts sized memory buffers in `exec`, `eval` and
`single` modes. It contains a Python 2.7.18 tokenizer, parser, CST-to-AST
conversion, future scanner, symbol table, bytecode generator, assembler,
stack-depth calculation and peephole optimizer.

Source decoding supports UTF-8 BOM, PEP 263 cookies, ASCII, UTF-8, Latin-1,
CRLF/CR normalization and structured syntax diagnostics. Compiler limits cover
source bytes, tokens, syntax nodes, nesting, symbols, blocks, instructions,
constants and arena memory.

Trusted source can opt into two compiler-only features. The build
preprocessor replaces reserved `__UPPERCASE__` names with immutable typed
profile constants and removes fully decidable `if` branches before symbol
analysis. The `meta` builtin expands valid Python 2 declarations such as
`@meta.template`, `@meta.emit(...)` and `meta.expand(...)` into ordinary classes
and functions. Bare `@meta` is not a template marker. Neither profile constants
nor `meta` exist in runtime globals.

Every compiled code graph owns an immutable compile environment containing its
feature flags, optimize level and an optional deep-copied build profile. String
`exec`, `eval` and Python-visible `compile` inherit that environment from the
current frame; `dont_inherit` continues to control future flags only. The host
can also request canonical expanded Python source plus a deterministic source
map through `tinypy_preprocess_source`.

The generated code objects and marshal-v2 payloads follow Python 2.7 semantics.
The compiler never opens the logical filename supplied for diagnostics.

The frontend is implemented directly on tinypy values, compiler arenas and
diagnostics under `src/compiler/`. It has no CPython compatibility headers,
runtime objects or linked dependency. The Python 2.7.18 algorithms from which
parts of the frontend were adapted remain covered by the PSF license; exact
provenance is recorded in `LICENSES/README.md`.

## C API

Include the complete C API with:

```c
#include <tinypy/tinypy.h>
```

The only C++ proxy is:

```cpp
#include <tinypy/tinypy.hpp>
```

Public types end in `_t`, enums end in `_e`, functions use `tinypy_`, constants
use `TINYPY_`, and private implementation symbols use `__tinypy_` or
`tinypy_internal_`.

Invalid pointers, ownership violations and wrong direct-accessor types are
optional internal contracts. Python semantic failures, malformed external
data, configured limits and ABI mismatches remain recoverable.

The optional [CLI library](cli/README.md) is a separate target inside this
project. It is disabled by default and is never linked into the embedding
`tinypy` target.

## Build and test

```sh
cmake -S . -B build/default -DBUILD_TESTING=ON
cmake --build build/default -j
ctest --test-dir build/default --output-on-failure

python3 -m unittest discover -s tests/tools -p 'test_*.py' -v
python3 tests/artifact/run_tests.py --sanitize
python3 tests/marshal/run_tests.py --sanitize
python3 tools/audit_core_symbols.py build/default/libtinypy.a
```

Internal `TINYPY_ASSERT` contracts and cycle diagnostics are independent,
opt-in build features. Both are disabled by default, including Debug builds:

```sh
cmake -S . -B build/asserts -DTINYPY_ENABLE_ASSERTS=ON
cmake -S . -B build/cycles \
    -DCMAKE_BUILD_TYPE=Debug \
    -DTINYPY_ENABLE_CYCLE_DIAGNOSTICS=ON
```

The symbol audit rejects direct allocator, I/O, environment, process, locale
and thread dependencies as well as symbols outside the tinypy namespace.

Exact compiler parity can be checked against an external Python 2.7.18
executable without adding it to the repository:

```sh
cmake -S . -B build/cli -DTINYPY_BUILD_CLI=ON
cmake --build build/cli -j

python3 tests/compiler/run_differential.py \
    --compiler build/cli/cli/tinypy_compile \
    --reference /path/to/python2.7 \
    --logical-root /path/to/sources \
    --source-root /path/to/sources \
    --expected-count NUMBER
```

The harness compiles every source at optimize levels 0, 1 and 2, requires
byte-identical marshal-v2 output, and reports the first differing field of a
nested code object when bytes diverge. Source roots and the expected count are
caller-owned inputs; no external corpus or interpreter is stored in tinypy.

## Repository layout

- `include/tinypy/` — public C ABI and the single C++ proxy;
- `src/core/` — values, objects, types and runtime protocols;
- `src/runtime/` — frame execution, builtins and imports;
- `src/compiler/` — native memory-only frontend, preprocessing, metatemplates, code generation and compiler integration;
- `src/bytecode/` — opcode metadata and bytecode verification;
- `src/marshal/` — marshal-v2 reader and writer;
- `src/artifact/` — versioned code artifact container;
- `cli/` — optional CLI library and thin executable entrypoints;
- `tests/` — standalone runtime, compiler, format and fuzz tests;
- `LICENSES/` — third-party attribution and license texts.

The detailed implementation contract is in [SPEC.md](SPEC.md).
