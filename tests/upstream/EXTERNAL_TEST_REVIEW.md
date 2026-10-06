# External test scenario review — 2026-10-07

This review uses external test scenarios as coverage leads. The resulting
Python 2.7 tests are independently written in this project, use our assertion
adapter and contain explicit behavioral expectations checked on CPython 2.7.18.
External test implementations and test frameworks are not vendored.

## Reviewed inventories

- [python-spec-test-suite](https://github.com/soniccyclops-bot-collab/python-spec-test-suite/tree/2e2a450050fa2c15d946e4cbb42d9ad309627827/tests/conformance),
  commit `2e2a450050fa2c15d946e4cbb42d9ad309627827`: 54 Python files under
  `tests/conformance`. Static inventory finds 1,611 named test functions,
  including version examples; this is an inventory count, not a validated
  execution count. Many cases check AST shape without executing the program.
- [RustPython extra_tests](https://github.com/RustPython/RustPython/tree/f2f8f95a808b9d96faf4456e6b40330f20f8b94b/extra_tests),
  commit `f2f8f95a808b9d96faf4456e6b40330f20f8b94b`: inventoried the complete
  `extra_tests` tree and selected 156 top-level, non-stdlib snippet files for
  source review. These contain both top-level assertions and named functions;
  function counts cannot describe their coverage. Library, platform and
  benchmark groups were left out of the downloaded snippet subset.

The upstream suites were not executed wholesale: they target Python 3 and
depend on APIs/frameworks outside our Python 2.7 corpus. Concrete candidates
were checked against existing coverage and rewritten as behavioral tests.

## Added coverage

| Project module | Cases | Behavior checked | Upstream coverage leads |
| --- | ---: | --- | --- |
| `local.test_evaluation_semantics` | 25 | Receiver/argument/RHS/target order; single evaluation of augmented targets and chained-comparison middle operands; result identity and truth callback counts; unpacking failure state; comprehension evaluation/scope; decorator order and class replacement; local binding; finally on return/break/continue; context entry/binding failures; generator send/throw/close | Spec sections 4, 6.10–6.16, 7.2, 7.6–7.10, 8.4–8.9; RustPython `syntax_assignment`, `syntax_comprehension`, `syntax_decorator`, `syntax_generator`, `syntax_short_circuit_bool`, `syntax_try`, `syntax_with` |
| `local.test_container_semantics` | 31 | Sequence-protocol termination/errors; membership dispatch and consumption; callable-iterator exhaustion; next defaults; live list iteration; element identity under repetition; self/subtype extension; incremental extension and reinitialization; constructor keywords; atomic slice replacement; extended-slice mismatch; zero/large slice indices; dict missing/update/iteration; streaming set difference-update; set-subclass repr; property accessor copies | Spec sections 3/3.3 and 7.2; RustPython `protocol_iterable`, `protocol_iternext`, `operator_membership`, `protocol_index_argument`, `builtin_list`, `builtin_slice`, `builtin_dict`, `builtin_set`, `builtin_property` |

The cases check values, identities, exact callback journals, exception types
and post-failure state. They use no AST, pytest or third-party library. Each
case runs independently; the coordinator requires zero outstanding tinypy
allocator bytes/allocations. All 56 are ordinary cases in every build profile.

## Python 2 expectations

The reference run prevents importing Python 3 assumptions:

- List-comprehension targets leak into the surrounding scope in Python 2;
  set/dict comprehensions and generator expressions have separate scopes.
- Python 2 dict displays evaluate each value before its key.
- An exception target remains bound after its handler.
- Truth callbacks use `__nonzero__`, iterator advancement uses `next`.
- `list.index` bounds accept `__index__`; `list.insert` and `list.pop` reject
  objects that supply only that method.
- Exact lists can extend themselves from their original storage. A list
  subclass follows its overridden iterator, including on self-extension.
- Slice construction permits a zero step; applying or normalizing it raises
  `ValueError`. One upstream snippet catches every exception around an
  intentional failing assertion, so it does not actually test this distinction.

## Selection boundaries

| Source category | Decision |
| --- | --- |
| Runtime and evaluation scenarios available in Python 2.7 | Added the cases above; kept stronger existing numeric/dispatch/MRO/length-hint/sort/format/import regressions |
| AST-only checks | Replaced selected themes with runtime callback journals; did not add an AST dependency |
| async/await, nonlocal, walrus, match, f-strings, yield-from, annotation/type statements, extended unpacking and modern signatures | Outside the Python 2.7 language contract |
| Python 3 dict ordering/views/union, text/bytes distinctions and generator-return values | Kept Python 2 behavior; did not translate incompatible expectations mechanically |
| Stdlib, OS, sockets, threads, ctypes, subprocesses, file I/O, frozen/JIT/RustPython CLI internals | Require external APIs or a different runtime contract; not added to this portable corpus |
| GC/lifetime tests with ownership cycles | Existing Debug detector adaptations remain the applicable coverage; no new cyclic-GC claim or skip was added |
| Existing numeric, descriptor, binding, sort, import, Unicode and iterator cases | Avoided copying duplicate fixtures; selected protocol interactions and failure-state checks instead |

## Confirmed fix and validation

The differential cases exposed atomic behavior where Python 2.7 mutates
incrementally. `list.extend` and `+=` now share an incremental implementation.
List, set and bytearray reinitialization clears the destination before reading
the source and keeps items produced before a callback failure. General
`set.difference_update` removes items as they are yielded. Constructor keyword
parsing now accepts `sequence`, `source`, `encoding` and `errors` where Python
2.7 does, and set/frozenset subclass repr uses an overridden iterator. Exact
container fast paths remain checked; slice assignment still collects its
replacement before changing the target.

The complete corpus now has 681 discovered / 670 selected / 11 excluded
originals. Debug and ASan/UBSan pass 670 cases; Release and LTO pass 666 with
the four pre-existing detector adaptations skipped. CTest passes 114/114 in
all four profiles; tool/API/coordinator tests pass 45/45. Python-visible
callbacks, failures and partial-update semantics remain active in Release.
The two new sources also produce byte-identical marshal-v2 output against
CPython 2.7.18 at optimize levels 0, 1 and 2 (six compilations).
