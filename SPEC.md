# TinyPy: спецификация runtime и compiler

## 1. Назначение

TinyPy — встраиваемая реализация Python 2.7 для выполнения доверенного и
ограниченного кода из memory buffers. Runtime, compiler и форматы данных
реализованы на C99 и не требуют установленного Python.

Core не обращается напрямую к:

- filesystem и стандартным потокам;
- environment и process API;
- locale;
- sockets;
- thread API и TLS;
- системному allocator.

Память, modules, output, diagnostics и interrupt polling предоставляет host
через C callbacks.

## 2. Границы реализации

Поддерживаются:

- Python 2.7 source semantics;
- Python 2.7 bytecode и code objects;
- marshal v2;
- режимы компиляции `exec`, `eval` и `single`;
- динамические `compile`, `eval` и строковый `exec`;
- host-resolved source и bytecode modules;
- независимые VM без общей runtime-блокировки.

Не поддерживаются:

- Python 3;
- CPython binary extension ABI;
- загрузка shared libraries из core;
- Python-visible `_ast` и `compile(ast, ...)`;
- одновременное выполнение одной VM несколькими потоками;
- автоматический cyclic GC.

## 3. C ABI

Публичный namespace:

```text
tinypy_          functions, types and values
TINYPY_          constants and enum values
tinypy_internal_ cross-unit implementation helpers
__tinypy_        private implementation functions and tables
```

Правила именования:

- структуры, union и scalar typedef заканчиваются на `_t`;
- enum typedef заканчивается на `_e`;
- declarations в публичных headers записываются в одну строку;
- `tinypy.h` агрегирует весь C API;
- `tinypy.hpp` является единственным C++ proxy header;
- публичные C headers не содержат `extern "C"` и `__cplusplus`.

ABI-extensible структуры начинаются с:

```c
uint32_t abi_version;
uint32_t struct_size;
```

Меньший известный `struct_size` означает отсутствие добавленных в конец полей.
Required pointers, ownership, правильный direct-accessor type и индекс являются
C contract и проверяются assertions в debug build.

## 4. VM и параллельность

`tinypy_vm_t` владеет всем mutable state:

- builtins и modules;
- interned values и cached constants;
- frames и exception state;
- compiler context;
- import state;
- recursion и instruction counters;
- allocator и host callbacks.

Process-wide mutable globals, GIL и TLS отсутствуют. Неизменяемые opcode,
grammar, Unicode и parser tables могут разделяться всеми VM.

Одна VM имеет одного owner thread. Синхронный reentrant вход из native callback
разрешён. Разные VM могут выполняться и компилировать одновременно.

## 5. Память и lifetime

Каждый runtime object начинается с универсального header:

```c
struct tinypy_value_t {
    tinypy_ref_t ref;
    tinypy_type_t *type;
};
```

Variable-size objects добавляют signed size и type-specific storage. Общий
payload union и allocation prefix отсутствуют.

Все allocations проходят через `tinypy_allocator_t` с размером, alignment,
allocation tag и user data. Корректный allocation request обязан вернуть
non-NULL; exhaustion является contract violation.

Lifetime определяется reference counting. Ациклические объекты уничтожаются
немедленно. Host обязан освободить owned references и разорвать owning cycles
перед `tinypy_vm_destroy`.

VM хранит собственные постоянные значения:

- `None`, `True`, `False`;
- integers от `-1023` до `1024`;
- positive float zero;
- пустую byte string;
- пустой tuple.

Они имеют обычный refcount и базовую owned reference самой VM; специального
immortal refcount sentinel нет.

## 6. Object model

Runtime реализует:

- `None`, bool, integer, long, float и complex;
- byte `str` и `unicode`;
- tuple, list, dict, set, frozenset, bytearray и buffer;
- slices и sequence/mapping protocols;
- functions, bound methods, closures и cells;
- iterators, comprehensions и generators;
- exceptions, frames и tracebacks;
- old-style и new-style classes;
- `type`, metaclasses, C3 MRO и `super`;
- descriptors, properties, class/static methods и `__slots__`;
- weak references и explicit finalization behavior.

Type slots возвращают прямой semantic result. Неверный slot input является
debug contract. Python exceptions используются только для настоящих runtime
ошибок.

## 7. Bytecode runtime

Interpreter выполняет Python 2.7 opcodes и проверяет code objects до запуска.
Verifier контролирует:

- instruction boundaries;
- argument width и `EXTENDED_ARG`;
- jump targets;
- block-stack transitions;
- value-stack depth;
- indices names/constants/locals/free variables;
- configured instruction и stack limits.

Frame execution поддерживает closures, generators, exception blocks,
comprehensions, `with`, imports и tracing data code object.

## 8. Compiler

Pipeline:

```text
source decoding
-> tokenizer
-> parser and CST
-> AST
-> future scan
-> symbol table
-> basic blocks
-> bytecode generation
-> stack-depth calculation
-> peephole optimization
-> code object
```

Frontend реализован внутри `src/compiler` на объектах, arena allocator и
diagnostics TinyPy. Алгоритмы совместимости Python 2.7.18 и их происхождение
зафиксированы в `LICENSES/README.md`; текст PSF license находится в
`LICENSES/PSF-2.0.txt`. В дереве нет копии исходников CPython, его compatibility
headers или зависимости от CPython runtime.

Compiler принимает только sized memory buffers. Logical filename копируется в
code object и diagnostics, но никогда не открывается.

Source decoder поддерживает:

- byte и Unicode source;
- UTF-8 BOM;
- PEP 263 cookie в первых двух строках;
- ASCII, UTF-8 и Latin-1 aliases;
- CRLF и CR normalization;
- embedded NUL diagnostics;
- structured syntax, indentation, tab и decoding errors.

Numeric parsing locale-independent. Integer literal создаёт integer при
попадании в signed 64-bit range, иначе arbitrary-precision long. Float и
complex сохраняют binary value.

Compiler воспроизводит closures, cells, class scopes, name mangling, generators,
comprehensions, future flags, code flags, constants ordering, line table и
nested code objects Python 2.7.

Optimization levels:

- `0`: assertions и docstrings сохраняются, `__debug__ == True`;
- `1`: assertions удаляются;
- `2`: assertions и docstrings удаляются.

Присваивание `__debug__` является compile error.

## 9. Compiler API

Основные функции:

```c
tinypy_value_t *tinypy_compile_source(...);
tinypy_value_t *tinypy_eval_source(...);
tinypy_value_t *tinypy_exec_source(...);
tinypy_value_t *tinypy_eval_code(...);
tinypy_value_t *tinypy_exec_code(...);
```

`tinypy_compile_options_t` задаёт mode, future flags, `dont_inherit`, optimize,
feature flags, limits и optional immutable build profile.

При `dont_inherit == 0` явно переданные flags объединяются с future flags
текущего frame. Imports компилируют source с `dont_inherit == 1`.

Compiler limits охватывают:

- source bytes;
- tokens;
- CST и AST nodes;
- nesting;
- symbols;
- basic blocks;
- instructions;
- constants и constant bytes;
- compiler arena bytes.

Временные compiler allocations живут в call-local arena и освобождаются целиком
при success или error.

## 10. Build profiles

Immutable build profile хранит typed constants и deterministic digest.
Поддерживаемые значения:

- `None`, bool, integer, long и float;
- byte string и Unicode;
- immutable tuple из поддерживаемых значений.

Зарезервированный формат build constant:

```text
^__[A-Z][A-Z0-9_]*__$
```

Profile полностью копирует входные данные через host allocator. Input order не
влияет на canonical ordering и digest. Optimize level входит в profile.

## 11. Imports и host callbacks

Core не строит filesystem paths. Resolver получает canonical module request и
возвращает один из memory artifacts:

- source buffer;
- marshal-v2 code buffer;
- native module descriptor.

Artifact содержит logical filename, canonical name, package metadata и release
callback. `sys.modules` поддерживает packages, circular imports и rollback при
ошибке resolution, compilation или execution.

Output streams, warnings, diagnostics и formatted tracebacks направляются host
callbacks. Callback input действителен только на время вызова, если явно не
указано иное.

## 12. Errors

`tinypy_error_t` — optional owned structured error. Он хранит копии message,
logical filename, source line, line number и column offset.

Recoverable categories включают:

- Python semantic errors;
- syntax, indentation, tab и decoding errors;
- malformed bytecode, marshal и artifacts;
- configured resource limits;
- ABI и profile mismatch;
- host module resolution failure.

Error одновременно устанавливает корректное Python exception state в VM.

## 13. Marshal и artifacts

Marshal reader и writer работают с raw Python 2.7 marshal-v2 objects. Direct
code dump поддерживает size query и caller-owned output buffer. Writer
сохраняет string interning/reference order и nested code objects.

Artifact header содержит ABI versions, optimize level, profile digest, source
hash, future flags, payload size и checksum. Loader никогда не принимает
неизвестную версию или incompatible payload молча.

## 14. Проверка

Обязательные gates:

- C99 build с warnings-as-errors;
- C и C++ public-header compilation;
- unit tests object model, bytecode runtime, compiler, marshal и artifacts;
- source modes `exec`, `eval`, `single` и dynamic compilation;
- imports, packages, circular imports и failed-import rollback;
- compiler limit boundary tests;
- allocator accounting после success и error;
- independent-VM parallel compilation;
- reentrant compilation из native callback;
- bounded malformed-input tests и fuzz targets;
- ASan и UBSan;
- symbol audit прямых allocator, I/O, environment, process, locale и thread API;
- namespace audit, запрещающий legacy и foreign exported symbols.

После `tinypy_vm_destroy` allocator accounting должен вернуться к нулю при
соблюдении host ownership contract.
