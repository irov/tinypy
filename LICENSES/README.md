# Third-party notices

TinyPy's Python 2.7-compatible tokenizer, grammar, parser, CST-to-AST
conversion, future scanner, symbol analysis, bytecode generation, assembler,
peephole optimization, AST tables and Unicode name data were adapted from the
Python 2.7.18 source tree.

Upstream snapshot: CPython commit
`8d21aa21f2cbc6d50aab3f420bb23be1d081dac4`.

The adapted implementation now lives in TinyPy's own `src/compiler` namespace
and uses TinyPy objects, allocation, errors and bytecode definitions. It does
not provide or depend on the CPython C ABI. This is a derivative port, not a
clean-room reimplementation, so the original attribution is intentionally
retained.

The applicable Python Software Foundation license is reproduced in
`PSF-2.0.txt` in this directory.
