# Third-party notices

tinypy's Python 2.7-compatible tokenizer, grammar, parser, CST-to-AST
conversion, future scanner, symbol analysis, bytecode generation, assembler,
peephole optimization, AST tables, Unicode name data and small-object pool
allocator were adapted from the Python 2.7.18 source tree.

Upstream snapshot: CPython commit
`8d21aa21f2cbc6d50aab3f420bb23be1d081dac4`.

The adapted compiler implementation lives in tinypy's own `src/compiler`
namespace and uses tinypy objects, allocation, errors and bytecode definitions.
The pool allocator is VM-local and obtains arenas only through the host
allocator callbacks. Neither component provides or depends on the CPython C
ABI. These are derivative ports, not clean-room reimplementations, so the
original attribution is intentionally retained.

The applicable Python Software Foundation license is reproduced in
`PSF-2.0.txt` in this directory.
